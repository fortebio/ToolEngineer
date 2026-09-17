/**
 * display.c — single unified Display module cho VIMATE (xiaozhi pattern).
 *
 * Architecture:
 *   - HW init (SPI + esp_lcd_panel + esp_lvgl_port) tách bạch khỏi UI.
 *   - vimate_display_t struct holds ALL widget pointers + state.
 *   - display_setup_ui() tạo TẤT CẢ widgets ON lv_screen_active() 1 lần.
 *   - display_task (UI core, high app priority) drain sched_q, execute callbacks.
 *   - Public setters push schedule items → drain task gọi LVGL setters.
 *
 * Tại sao bỏ lv_async_call?
 *   - lv_async_call enqueue vào LVGL internal queue. Khi LVGL task đang
 *     ở giữa lv_display_flush (DMA chờ ack), nó ko process async queue.
 *     Caller (WS task) gọi lv_async_call sẽ corrupt queue nếu LVGL task
 *     đang trong hot path. → Dùng FreeRTOS xQueue + display task riêng,
 *     drain task chỉ wrap LVGL lock và setter, không race với render.
 *
 * Tại sao bỏ lv_layer_top?
 *   - layer_top z-order conflict với screen widgets. Cần lv_obj_move_foreground
 *     phức tạp. → Dùng widget HIDDEN flag + lv_screen_active duy nhất.
 *
 * Tại sao lv_image_set_scale thay vì inner_align STRETCH?
 *   - STRETCH với LV_COLOR_FORMAT_RGB565A8 dispatch sang transform_rgb565a8
 *     path → crash khi w/h scale > 2x do internal buffer overflow.
 *     set_scale chỉ pixel multiply integer (256 = 1x) → dùng nearest
 *     neighbor, không buffer alloc trung gian → an toàn.
 */
#include "display.h"
#include "vimate.h"
#include "boards/board.h"
#include "emo/vimate_emotions.h"
#include "fonts/lv_font_vimate.h"
#include "icons_home.h"
#include "ui_theme.h"
#include "ui_image.h"
#include "store/emotion_sync.h"
#include "gif/lvgl_gif.h"
#include "face.h"
#include "esp_random.h"
#include "core/task_profile.h"
#include "core/ble_wifi_prov.h"
#include "audio/audio_pipeline.h"
#include <sys/stat.h>
#include <dirent.h>

#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "driver/i2c_master.h"
#if BOARD_LCD_USE_ILI9341
#include "esp_lcd_ili9341.h"
#endif
#if defined(BOARD_LCD_USE_ST7796S) && BOARD_LCD_USE_ST7796S
#include "esp_lcd_st7796.h"
#endif
#if BOARD_LCD_USE_MIPI_DSI
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_st7102.h"
/* Chống xé hình cho panel DPI (video mode): driver DPI cấp 2 frame buffer,
 * LVGL vẽ THẲNG vào fb (direct mode), esp_lvgl_port đổi fb đúng ranh giới khung
 * qua callback on_refresh_done (ISR cuối khung của DMA). KHÔNG cần tín hiệu TE
 * (0x35) — TE chỉ dành cho panel command mode; ghi chú cũ ở đây nói ngược lại
 * là sai. Lợi thêm 768KB PSRAM vì không còn 2 buffer LVGL riêng + bỏ memcpy
 * 768KB/khung từ buffer LVGL sang fb. Board muốn tắt: BOARD_LCD_DSI_AVOID_TEARING 0. */
#ifndef BOARD_LCD_DSI_AVOID_TEARING
#define BOARD_LCD_DSI_AVOID_TEARING 1
#endif
/* Xoay màn ngang trên panel DPI dọc (board header: BOARD_LCD_ROTATION 90/270).
 * LVGL vẽ LOGICAL 800×480 (direct mode, 2 buffer PSRAM của port), flush cb của
 * ta xoay từng vùng bẩn bằng PPA (Pixel Processing Accelerator của P4) vào fb DPI
 * đang ẨN, rồi đổi khung đúng vsync như đường avoid_tearing. Không dùng
 * lv_display_set_rotation/sw_rotate của port: LVGL 9.5 không tự xoay trong
 * direct mode (chỉ có matrix rotation, mà SW renderer không hỗ trợ), còn port chỉ
 * xoay ở partial mode → mất chống xé hình. Chi tiết README-P4 §6.6. */
#ifndef BOARD_LCD_ROTATION
#define BOARD_LCD_ROTATION 0
#endif
#if BOARD_LCD_ROTATION == 90 || BOARD_LCD_ROTATION == 270
#define DSI_ROTATE 1
#include "driver/ppa.h"
#include "freertos/semphr.h"
#elif BOARD_LCD_ROTATION == 0
#define DSI_ROTATE 0
#else
#error "BOARD_LCD_ROTATION: DSI chi ho tro 0 / 90 / 270"
#endif
/* Bảng init ST7102 — copy NGUYÊN VĂN từ demo chính chủ của board
 * (`ESP-IDF 5/P4-IDF_ST7102-MIPI_ESP-LVGL-PORT_V9/main/main.c`). BẮT BUỘC:
 * init mặc định trong component KHÔNG khớp panel 4.3" này (gamma/power/GIP
 * riêng của nhà sản xuất). 0x11 sleep-out chờ 600ms, 0x29 display-on chờ 120ms. */
static const st7102_lcd_init_cmd_t s_st7102_init_cmds[] = {
    {0x99, (uint8_t []){0x71,0x02,0xa2}, 3, 0},
    {0x99, (uint8_t []){0x71,0x02,0xa3}, 3, 0},
    {0x99, (uint8_t []){0x71,0x02,0xa4}, 3, 0},
    {0xB0, (uint8_t []){0x22,0x57,0x1E,0x61,0x2F,0x57,0x61}, 7, 0},
    {0xB7, (uint8_t []){0x64,0x64}, 2, 0},
    {0xBF, (uint8_t []){0xB4,0xB4}, 2, 0},
    {0xC8, (uint8_t []){0x00,0x00,0x13,0x24,0x44,0x00,0x74,0x03,0xB8,0x04,
                        0x11,0x16,0x08,0x86,0x04,0x21,0xD3,0x02,0x10,0x0F,
                        0x22,0x4D,0x0E,0x90,0x09,0x32,0xF0,0x0B,0x40,0x0E,
                        0xF3,0x7D,0x0E,0xA9,0xBF,0x03,0xC4}, 37, 0},
    {0xC9, (uint8_t []){0x00,0x00,0x13,0x24,0x44,0x00,0x74,0x03,0xB8,0x04,
                        0x11,0x16,0x08,0x86,0x04,0x21,0xD3,0x02,0x10,0x0F,
                        0x22,0x4D,0x0E,0x90,0x09,0x32,0xF0,0x0B,0x40,0x0E,
                        0xF3,0x7D,0x0E,0xA9,0xBF,0x03,0xC4}, 37, 0},
    {0xD7, (uint8_t []){0x10,0x0C,0x36,0x19,0x90,0x90}, 6, 0},
    {0xA3, (uint8_t []){0x51,0x03,0x80,0xCF,0x44,0x00,0x00,0x00,0x00,0x04,
                        0x78,0x78,0x00,0x1A,0x00,0x45,0x05,0x00,0x00,0x00,
                        0x00,0x46,0x00,0x00,0x02,0x20,0x52,0x00,0x05,0x00,
                        0x00,0xFF}, 32, 0},
    {0xA6, (uint8_t []){0x02,0x00,0x24,0x55,0x35,0x00,0x38,0x00,0x78,0x78,
                        0x00,0x24,0x55,0x36,0x00,0x37,0x00,0x78,0x78,0x02,
                        0xAC,0x51,0x3A,0x00,0x00,0x00,0x78,0x78,0x03,0xAC,
                        0x21,0x00,0x04,0x00,0x00,0x78,0x78,0x3e,0x00,0x06,
                        0x00,0x00,0x00,0x00}, 44, 0},
    {0xA7, (uint8_t []){0x19,0x19,0x00,0x64,0x40,0x07,0x16,0x40,0x00,0x04,
                        0x03,0x78,0x78,0x00,0x64,0x40,0x25,0x34,0x00,0x00,
                        0x02,0x01,0x78,0x78,0x00,0x64,0x40,0x4B,0x5A,0x00,
                        0x00,0x02,0x01,0x78,0x78,0x00,0x24,0x40,0x69,0x78,
                        0x00,0x00,0x00,0x00,0x78,0x78,0x00,0x44}, 48, 0},
    {0xAC, (uint8_t []){0x08,0x0A,0x11,0x00,0x13,0x03,0x1B,0x18,0x06,0x1A,
                        0x19,0x1B,0x1B,0x1B,0x18,0x1B,0x09,0x0B,0x10,0x02,
                        0x12,0x01,0x1B,0x18,0x06,0x1A,0x19,0x1B,0x1B,0x1B,
                        0x18,0x1B,0xFF,0x67,0xFF,0x67,0x00}, 37, 0},
    {0xAD, (uint8_t []){0xCC,0x40,0x46,0x11,0x04,0x78,0x78}, 7, 0},
    {0xE8, (uint8_t []){0x30,0x07,0x00,0x94,0x94,0x9C,0x00,0xE2,0x04,0x00,
                        0x00,0x00,0x00,0xEF}, 14, 0},
    {0xE7, (uint8_t []){0x8B,0x3C,0x00,0x0C,0xF0,0x5D,0x00,0x5D,0x00,0x5D,
                        0x00,0x5D,0x00,0xFF,0x00,0x08,0x7B,0x00,0x00,0xC8,
                        0x6A,0x5A,0x08,0x1A,0x3C,0x00,0x81,0x01,0xCC,0x01,
                        0x7F,0xF0,0x22}, 33, 0},
    {0x11, (uint8_t []){0x00}, 0, 600},
    {0x29, (uint8_t []){0x00}, 0, 120},
};
#endif /* BOARD_LCD_USE_MIPI_DSI */
#if defined(BOARD_LCD_USE_ST77922) && BOARD_LCD_USE_ST77922
#include "esp_lcd_st77922.h"
/* Init cmds ST77922 — copy CHÍNH CHỦ từ demo ESP-IDF của board (3.5inch_ESP32-S3
 * 资料包 / bsp_display.c). BẮT BUỘC: init mặc định của component KHÔNG khớp panel này. */
static const st77922_lcd_init_cmd_t s_st77922_init_cmds[] = {
    {0xF1, (uint8_t []){0x00}, 1, 0},
    {0x60, (uint8_t []){0x00, 0x00, 0x00}, 3, 0},
    {0x65, (uint8_t []){0x80}, 1, 0},
    {0x79, (uint8_t []){0x06}, 1, 0},
    {0x7B, (uint8_t []){0x00, 0x08, 0x08}, 3, 0},
    {0x80, (uint8_t []){0x55, 0x62, 0x2F, 0x17, 0xF0, 0x52, 0x70, 0xD2, 0x52, 0x62, 0xEA}, 11, 0},
    {0x81, (uint8_t []){0x26, 0x52, 0x72, 0x27}, 4, 0},
    {0x84, (uint8_t []){0x92, 0x25}, 2, 0},
    {0x87, (uint8_t []){0x10, 0x10, 0x58, 0x00, 0x02, 0x3A}, 6, 0},
    {0x88, (uint8_t []){0x00, 0x00, 0x2C, 0x10, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x06}, 15, 0},
    {0x89, (uint8_t []){0x00, 0x00, 0x00}, 3, 0},
    {0x8A, (uint8_t []){0x13, 0x00, 0x2C, 0x00, 0x00, 0x2C, 0x10, 0x10, 0x00, 0x3E, 0x19}, 11, 0},
    {0x8B, (uint8_t []){0x15, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x97, 0x8E}, 9, 0},
    {0x8C, (uint8_t []){0x1D, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x50, 0x0F, 0x01, 0xC5, 0x12, 0x09}, 13, 0},
    {0x8D, (uint8_t []){0x0C}, 1, 0},
    {0x8E, (uint8_t []){0x33, 0x01, 0x0C, 0x13, 0x01, 0x01}, 6, 0},
    {0xB3, (uint8_t []){0x00, 0x30}, 2, 0},
    {0xF1, (uint8_t []){0x00}, 1, 0},
    {0x71, (uint8_t []){0xD0}, 1, 0},
    {0x66, (uint8_t []){0x02, 0x3F}, 2,  0},
    {0xBE, (uint8_t []){0x26, 0x00, 0x9D}, 3, 0},
    {0x70, (uint8_t []){0x01, 0xA0, 0x11, 0x40, 0xE0, 0x00, 0x11, 0x69, 0x11, 0x00, 0x00, 0x1A}, 12, 0},
    {0x90, (uint8_t []){0x04, 0x04, 0x55, 0x74, 0x00, 0x40, 0x43, 0x27, 0x27}, 9, 0},
    {0x91, (uint8_t []){0x04, 0x04, 0x55, 0x75, 0x00, 0x40, 0x42, 0x27, 0x27}, 9, 0},
    {0x92, (uint8_t []){0x04, 0x44, 0x55, 0xC0, 0x06, 0x00, 0x07, 0x05, 0x90, 0x27}, 10, 0},
    {0x93, (uint8_t []){0x04, 0x43, 0x11, 0x00, 0x00, 0x00, 0x00, 0x05, 0x90, 0x27}, 10, 0},
    {0x94, (uint8_t []){0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 6, 0},
    {0x95, (uint8_t []){0x96, 0x16, 0x00, 0x00, 0xFF}, 5, 0},
    {0x96, (uint8_t []){0x44, 0x53, 0x03, 0x12, 0x23, 0x24, 0x06, 0x05, 0x94, 0x27, 0x00, 0x44}, 12, 0},
    {0x97, (uint8_t []){0x44, 0x53, 0x47, 0x56, 0x20, 0x20, 0x02, 0x01, 0x94, 0x27, 0x00, 0x44}, 12, 0},
    {0xBA, (uint8_t []){0x55, 0x94, 0x2D, 0x94, 0x27}, 5, 0},
    {0x9A, (uint8_t []){0x40, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {0x9B, (uint8_t []){0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {0x9C, (uint8_t []){0x5C, 0x12, 0x00, 0x00, 0x10, 0x12, 0x00, 0x00, 0x10, 0x02, 0x00, 0x00, 0x00}, 13, 0},
    {0x9D, (uint8_t []){0x8A, 0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01}, 8, 0},
    {0x9E, (uint8_t []){0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01}, 7, 0},
    {0xB4, (uint8_t []){0x1D, 0x1C, 0x1E, 0x0B, 0x14, 0x02, 0x13, 0x09, 0x1E, 0x00, 0x1E, 0x10}, 12, 0},
    {0xB5, (uint8_t []){0x1D, 0x1C, 0x1E, 0x0A, 0x15, 0x03, 0x11, 0x08, 0x1E, 0x01, 0x1E, 0x12}, 12, 0},
    {0xB6, (uint8_t []){0x77, 0x77, 0x00, 0x0A, 0xFF, 0x0A, 0xFF}, 7, 0},
    {0x86, (uint8_t []){0xCD, 0x04, 0xB1, 0x02, 0x58, 0x12, 0x58, 0x0C, 0x13, 0x01, 0xA5, 0x00, 0xA5, 0xA5}, 14, 0},
    {0xB7, (uint8_t []){0x07, 0x0A, 0x0E, 0x06, 0x05, 0x03, 0x2B, 0x03, 0x03, 0x42, 0x07, 0x10, 0x10, 0x2E, 0x3F, 0x0D}, 16, 0},
    {0xB8, (uint8_t []){0x07, 0x0A, 0x0D, 0x05, 0x05, 0x02, 0x2B, 0x02, 0x03, 0x42, 0x06, 0x10, 0x0F, 0x2E, 0x3F, 0x0D}, 16, 0},
    {0xB9, (uint8_t []){0x23, 0x23}, 2, 0},
    {0xBF, (uint8_t []){0x10, 0x14, 0x14, 0x0B, 0x0B, 0x0B}, 6, 0},
    {0xF2, (uint8_t []){0x00}, 1, 0},
    {0x73, (uint8_t []){0x04, 0xDA, 0x12, 0x54, 0x47}, 5, 0},
    {0x77, (uint8_t []){0x6B, 0x5B, 0xFD, 0xC3, 0xC5}, 5, 0},
    {0x7A, (uint8_t []){0x15, 0x27}, 2, 0},
    {0x7B, (uint8_t []){0x04, 0x57}, 2, 0},
    {0x7E, (uint8_t []){0x01, 0x0E}, 2, 0},
    {0xBF, (uint8_t []){0x36}, 1, 0},
    {0xE3, (uint8_t []){0x40, 0x40}, 2, 0},
    {0xF0, (uint8_t []){0x00}, 1, 0},
    {0xD0, (uint8_t []){0x00}, 1, 0},
    {0x2A, (uint8_t []){0x00, 0x00, 0x01, 0x3F}, 4, 0},
    {0x2B, (uint8_t []){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x21, (uint8_t []){0x00}, 0, 0},
    {0x11, (uint8_t []){0x00}, 0, 120},
    {0x29, (uint8_t []){0x00}, 0, 0},
    {0x2C, (uint8_t []){0x00}, 0, 0},
    {0x3A, (uint8_t []){0x01}, 1, 0},
    {0x36, (uint8_t []){0x00}, 1, 0},
    {0x35, (uint8_t []){0x01}, 1, 20},
};
#endif
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_cache.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#ifndef BOARD_LCD_USE_I80
#define BOARD_LCD_USE_I80 0
#endif

#if BOARD_LCD_USE_MIPI_DSI && DSI_ROTATE
/* ===== Xoay landscape → fb DPI dọc bằng PPA (xem ghi chú ở đầu file) =====
 * Toạ độ: logical (lx,ly) ∈ 800×480; native panel (px,py) ∈ 480×800.
 *   ROTATION 270 (nội dung quay theo kim đồng hồ): (px,py) = (479-ly, lx)
 *   ROTATION  90 (ngược kim đồng hồ)              : (px,py) = (ly, 799-lx)
 * PPA quay NGƯỢC kim đồng hồ (driver/ppa.h), nên 270 ↔ PPA_ANGLE_270, 90 ↔ 90.
 *
 * Đồng bộ 2 fb: fb ẩn đang giữ khung N-2, LVGL buffer đã là khung N (LVGL tự sync
 * 2 buffer của nó bằng refr_sync_areas). Xoay HỢP của vùng bẩn khung N-1 và N là
 * đủ để fb ẩn thành khung N — đúng cách LVGL làm với sync_areas. Hai khung đầu và
 * khi danh sách tràn thì xoay cả màn. */
#define DSI_ROT_MAX_AREAS 32          /* = LV_INV_BUF_SIZE mặc định */
typedef struct { lv_area_t a[DSI_ROT_MAX_AREAS]; int n; bool overflow; } dsi_rot_areas_t;
static struct {
    ppa_client_handle_t ppa;          /* NULL → xoay bằng CPU (chậm, chỉ dự phòng) */
    esp_lcd_panel_handle_t panel;
    uint8_t *fb[2];
    size_t fb_size;
    int front;                        /* fb đang hiển thị (cur_fb_index của driver) */
    SemaphoreHandle_t vsync;
    dsi_rot_areas_t cur, prev;
    int full_pending;                 /* số khung tới phải xoay cả màn */
    uint32_t stat_full, stat_area;    /* đếm để log/diag */
    int64_t stat_full_us_max;
    bool vsync_timeout_logged;
} s_rot;

static bool IRAM_ATTR dsi_rot_vsync_cb(esp_lcd_panel_handle_t panel,
                                       esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    (void)panel; (void)edata; (void)user_ctx;
    BaseType_t yield = pdFALSE;
    if (s_rot.vsync) xSemaphoreGiveFromISR(s_rot.vsync, &yield);
    return yield == pdTRUE;
}

static void dsi_rot_areas_add(dsi_rot_areas_t *l, const lv_area_t *a)
{
    if (l->n >= DSI_ROT_MAX_AREAS) { l->overflow = true; return; }
    l->a[l->n++] = *a;
}

/* Xoay khối (bx,by,bw,bh) của ảnh nguồn src_pic (pic_w×pic_h, RGB565, stride =
 * pic_w) đặt tại vị trí LOGICAL (lx,ly) trên màn, vào fb đích (native dọc). */
static esp_err_t dsi_rot_put(const uint16_t *src_pic, int pic_w, int pic_h,
                             int bx, int by, int bw, int bh, int lx, int ly, uint8_t *dst_fb)
{
    if (bw <= 0 || bh <= 0) return ESP_OK;
#if BOARD_LCD_ROTATION == 270
    const int ox = BOARD_LCD_V_RES - ly - bh;   /* 480 - ly - bh */
    const int oy = lx;
    const ppa_srm_rotation_angle_t ang = PPA_SRM_ROTATION_ANGLE_270;
#else
    const int ox = ly;
    const int oy = BOARD_LCD_H_RES - lx - bw;   /* 800 - lx - bw */
    const ppa_srm_rotation_angle_t ang = PPA_SRM_ROTATION_ANGLE_90;
#endif
    if (s_rot.ppa) {
        ppa_srm_oper_config_t cfg = {
            .in = {
                .buffer = src_pic,
                .pic_w = pic_w, .pic_h = pic_h,
                .block_w = bw, .block_h = bh,
                .block_offset_x = bx, .block_offset_y = by,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            },
            .out = {
                .buffer = dst_fb, .buffer_size = s_rot.fb_size,
                .pic_w = BOARD_LCD_NATIVE_W, .pic_h = BOARD_LCD_NATIVE_H,
                .block_offset_x = ox, .block_offset_y = oy,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            },
            .rotation_angle = ang,
            .scale_x = 1.0f, .scale_y = 1.0f,
            .mode = PPA_TRANS_MODE_BLOCKING,
        };
        esp_err_t err = ppa_do_scale_rotate_mirror(s_rot.ppa, &cfg);
        if (err == ESP_OK) return ESP_OK;
        ESP_LOGW(TAG_UI, "PPA rotate (%d,%d %dx%d) loi %s -> CPU", lx, ly, bw, bh, esp_err_to_name(err));
    }
    /* Dự phòng CPU: chậm (cả màn cỡ vài chục ms) nhưng đúng. */
    uint16_t *d16 = (uint16_t *)dst_fb;
    for (int j = 0; j < bh; j++) {
        const uint16_t *row = src_pic + (size_t)(by + j) * pic_w + bx;
        const int yy = ly + j;
        for (int i = 0; i < bw; i++) {
            const int xx = lx + i;
#if BOARD_LCD_ROTATION == 270
            const int px = BOARD_LCD_V_RES - 1 - yy, py = xx;
#else
            const int px = yy, py = BOARD_LCD_H_RES - 1 - xx;
#endif
            d16[(size_t)py * BOARD_LCD_NATIVE_W + px] = row[i];
        }
    }
    /* Khối xoay chiếm các hàng native [oy, oy+bw) — write-back cho DMA của DPI. */
    const size_t stride = (size_t)BOARD_LCD_NATIVE_W * 2;
    esp_cache_msync(dst_fb + (size_t)oy * stride, (size_t)bw * stride,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    return ESP_OK;
}

static bool dsi_rot_area_inside(const lv_area_t *a, const dsi_rot_areas_t *l)
{
    for (int i = 0; i < l->n; i++) {
        const lv_area_t *b = &l->a[i];
        if (a->x1 >= b->x1 && a->y1 >= b->y1 && a->x2 <= b->x2 && a->y2 <= b->y2) return true;
    }
    return false;
}

static void dsi_rot_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    /* direct mode: px_map = ĐẦU buffer logical, area = vùng bẩn. */
    dsi_rot_areas_add(&s_rot.cur, area);
    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }
    const int back = 1 - s_rot.front;
    uint8_t *fb = s_rot.fb[back];
    if (s_rot.full_pending > 0 || s_rot.cur.overflow || s_rot.prev.overflow) {
        const int64_t t0 = esp_timer_get_time();
        dsi_rot_put((const uint16_t *)px_map, BOARD_LCD_H_RES, BOARD_LCD_V_RES,
                    0, 0, BOARD_LCD_H_RES, BOARD_LCD_V_RES, 0, 0, fb);
        const int64_t dt = esp_timer_get_time() - t0;
        if (dt > s_rot.stat_full_us_max) s_rot.stat_full_us_max = dt;
        if (s_rot.stat_full < 3) {
            ESP_LOGI(TAG_UI, "DSI rotate: ca man %dx%d qua %s mat %lld us",
                     BOARD_LCD_H_RES, BOARD_LCD_V_RES, s_rot.ppa ? "PPA" : "CPU", (long long)dt);
        }
        s_rot.stat_full++;
        if (s_rot.full_pending > 0) s_rot.full_pending--;
    } else {
        for (int i = 0; i < s_rot.cur.n; i++) {
            const lv_area_t *a = &s_rot.cur.a[i];
            dsi_rot_put((const uint16_t *)px_map, BOARD_LCD_H_RES, BOARD_LCD_V_RES,
                        a->x1, a->y1, lv_area_get_width(a), lv_area_get_height(a), a->x1, a->y1, fb);
            s_rot.stat_area++;
        }
        for (int i = 0; i < s_rot.prev.n; i++) {
            const lv_area_t *a = &s_rot.prev.a[i];
            if (dsi_rot_area_inside(a, &s_rot.cur)) continue;   /* đã xoay ở trên */
            dsi_rot_put((const uint16_t *)px_map, BOARD_LCD_H_RES, BOARD_LCD_V_RES,
                        a->x1, a->y1, lv_area_get_width(a), lv_area_get_height(a), a->x1, a->y1, fb);
            s_rot.stat_area++;
        }
    }
    /* Con trỏ nằm trong fb của driver → driver chỉ msync + đổi cur_fb_index; ISR
     * cuối khung mới chuyển DMA sang fb này (đúng ranh giới khung, không xé). */
    esp_lcd_panel_draw_bitmap(s_rot.panel, 0, 0, BOARD_LCD_NATIVE_W, BOARD_LCD_NATIVE_H, fb);
    s_rot.front = back;
    /* Chờ ISR cuối khung đã đổi DMA — trước đó fb cũ vẫn đang được quét, chưa
     * được ghi đè ở khung sau. Timeout để LVGL không treo nếu DSI chết. */
    xSemaphoreTake(s_rot.vsync, 0);
    if (xSemaphoreTake(s_rot.vsync, pdMS_TO_TICKS(100)) != pdTRUE && !s_rot.vsync_timeout_logged) {
        ESP_LOGW(TAG_UI, "DSI rotate: khong thay vsync trong 100ms (DPI ngung quet?)");
        s_rot.vsync_timeout_logged = true;
    }
    s_rot.prev = s_rot.cur;
    s_rot.cur.n = 0;
    s_rot.cur.overflow = false;
    lv_display_flush_ready(disp);
}

/* Gọi SAU lvgl_port_add_disp_dsi(), trong khi vẫn giữ lvgl_port_lock. */
static esp_err_t dsi_rot_attach(esp_lcd_panel_handle_t panel, lv_display_t *disp)
{
    memset(&s_rot, 0, sizeof(s_rot));
    s_rot.panel = panel;
    s_rot.fb_size = (size_t)BOARD_LCD_NATIVE_W * BOARD_LCD_NATIVE_H * 2;
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(panel, 2, (void **)&s_rot.fb[0], (void **)&s_rot.fb[1]),
                        TAG_UI, "DSI rotate: lay 2 fb loi (num_fbs phai = 2)");
    s_rot.vsync = xSemaphoreCreateBinary();
    if (!s_rot.vsync) return ESP_ERR_NO_MEM;
    s_rot.full_pending = 2;   /* 2 fb đều trống lúc đầu */

    ppa_client_config_t pcfg = { .oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1 };
    esp_err_t err = ppa_register_client(&pcfg, &s_rot.ppa);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_UI, "DSI rotate: PPA khong kha dung (%s) -> xoay bang CPU", esp_err_to_name(err));
        s_rot.ppa = NULL;
    }
    /* Đè callback của port: port đăng ký on_color_trans_done → lv_disp_flush_ready
     * quá sớm; ta chỉ cần ISR cuối khung. */
    const esp_lcd_dpi_panel_event_callbacks_t cbs = { .on_refresh_done = dsi_rot_vsync_cb };
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(panel, &cbs, NULL),
                        TAG_UI, "DSI rotate: dang ky vsync cb loi");
    lv_display_set_flush_cb(disp, dsi_rot_flush_cb);
    ESP_LOGI(TAG_UI, "DSI rotate: logical %dx%d -> panel %dx%d, goc %d, %s, 2 fb + vsync",
             BOARD_LCD_H_RES, BOARD_LCD_V_RES, BOARD_LCD_NATIVE_W, BOARD_LCD_NATIVE_H,
             BOARD_LCD_ROTATION, s_rot.ppa ? "PPA" : "CPU");
    return ESP_OK;
}

/* Vẽ thẳng (MP4): xoay vào CẢ HAI fb — fb đang quét có thể xé một khung video,
 * chấp nhận được; quan trọng là fb ẩn cũng có hình để lần đổi khung sau không mất. */
static esp_err_t dsi_rot_blit(int x, int y, int w, int h, const void *rgb565)
{
    const int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    const int x1 = (x + w > BOARD_LCD_H_RES) ? BOARD_LCD_H_RES : x + w;
    const int y1 = (y + h > BOARD_LCD_V_RES) ? BOARD_LCD_V_RES : y + h;
    if (x1 <= x0 || y1 <= y0) return ESP_OK;
    for (int i = 0; i < 2; i++) {
        esp_err_t r = dsi_rot_put((const uint16_t *)rgb565, w, h,
                                  x0 - x, y0 - y, x1 - x0, y1 - y0, x0, y0, s_rot.fb[i]);
        if (r != ESP_OK) return r;
    }
    return ESP_OK;
}
#endif /* BOARD_LCD_USE_MIPI_DSI && DSI_ROTATE */
#ifndef BOARD_LCD_I80_FREQ_HZ
#define BOARD_LCD_I80_FREQ_HZ BOARD_LCD_SPI_FREQ_HZ
#endif
#ifndef BOARD_LCD_SPI_MODE
#define BOARD_LCD_SPI_MODE 0
#endif

#if defined(BOARD_LCD_USE_ST77922) && BOARD_LCD_USE_ST77922
/* ===== LANDSCAPE flush cho ST77922 (công thức demo chính chủ) =====
 * GPSPI trên S3 KHÔNG DMA được từ PSRAM: spi_master setup_priv_desc() copy tạm
 * mỗi chunk vào RAM nội (heap alloc từng transaction) → NO_MEM khi heap sụt +
 * chunk fail giữa frame = sọc/rác. Demo hãng né bằng cách xoay từng DẢI vào
 * buffer RAM NỘI DMA rồi mới draw. Ta làm y vậy: LVGL render LOGICAL landscape
 * 480×320 (full_refresh, buffer PSRAM — CPU đọc, không DMA); flush cb xoay 90°
 * + swap byte từng dải ST77922_STRIP_ROWS hàng native vào strip nội, draw ĐỒNG BỘ.
 * Map A: native(nx,ny) = logical(lx=ny, ly=NATIVE_W-1-nx). Nếu màn lộn đầu →
 * đổi map (lx=NATIVE_H-1-ny, ly=nx) + đảo BOARD_TOUCH_MIRROR_* board header. */
#include "freertos/semphr.h"
#define ST77922_NATIVE_W   BOARD_LCD_V_RES    /* 320 — panel dọc */
#define ST77922_NATIVE_H   BOARD_LCD_H_RES    /* 480 */
#define ST77922_STRIP_ROWS 24                 /* 24 hàng × 320 × 2B = 15.36KB nội */
static esp_lcd_panel_handle_t s_st77922_panel;
static uint16_t *s_st77922_strip;             /* RAM nội DMA, cấp 1 lần */
static SemaphoreHandle_t s_st77922_flush_sem;

static bool st77922_strip_done_cb(esp_lcd_panel_io_handle_t io,
                                  esp_lcd_panel_io_event_data_t *edata,
                                  void *user_ctx) {
    (void)io; (void)edata; (void)user_ctx;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_st77922_flush_sem, &hp);
    return hp == pdTRUE;
}

static void st77922_landscape_flush_cb(lv_display_t *disp, const lv_area_t *area,
                                       uint8_t *px_map) {
    const uint16_t *src = (const uint16_t *)px_map;  /* logical, stride = bề rộng area */
    const int lw = (int)(area->x2 - area->x1 + 1);   /* full_refresh → 480 */
    for (int ny0 = 0; ny0 < ST77922_NATIVE_H; ny0 += ST77922_STRIP_ROWS) {
        int rows = ST77922_NATIVE_H - ny0;
        if (rows > ST77922_STRIP_ROWS) rows = ST77922_STRIP_ROWS;
        /* Đọc src TUẦN TỰ theo hàng logical (prefetch PSRAM tốt); ghi cột vào
         * strip nội (write nội rẻ). dst(nx=NATIVE_W-1-ly, r=ny-ny0). */
        for (int ly = 0; ly < ST77922_NATIVE_W; ly++) {
            const uint16_t *s = src + (size_t)ly * lw + ny0;
            uint16_t *dcol = s_st77922_strip + (ST77922_NATIVE_W - 1 - ly);
            for (int r = 0; r < rows; r++) {
                uint16_t v = s[r];
                dcol[(size_t)r * ST77922_NATIVE_W] = (uint16_t)((v >> 8) | (v << 8));
            }
        }
        if (esp_lcd_panel_draw_bitmap(s_st77922_panel, 0, ny0, ST77922_NATIVE_W,
                                      ny0 + rows, s_st77922_strip) == ESP_OK) {
            xSemaphoreTake(s_st77922_flush_sem, pdMS_TO_TICKS(1000));
        }
    }
    lv_display_flush_ready(disp);
}
#endif /* BOARD_LCD_USE_ST77922 landscape flush */
#ifndef BOARD_LCD_USE_ST7796S
#define BOARD_LCD_USE_ST7796S 0
#endif
#ifndef BOARD_LCD_POWER_CTRL_GPIO
#define BOARD_LCD_POWER_CTRL_GPIO GPIO_NUM_NC
#endif
#ifndef BOARD_LCD_POWER_CTRL_ENABLED
#define BOARD_LCD_POWER_CTRL_ENABLED 0
#endif
#ifndef BOARD_LCD_EXTRA_BL_GPIO
#define BOARD_LCD_EXTRA_BL_GPIO GPIO_NUM_NC
#endif
#ifndef BOARD_LCD_EXTRA_BL_ENABLED
#define BOARD_LCD_EXTRA_BL_ENABLED 1
#endif
#ifndef BOARD_LCD_POWER_ON_LEVEL
#define BOARD_LCD_POWER_ON_LEVEL 1
#endif
#ifndef BOARD_LCD_DIAG_POWER_BL
#define BOARD_LCD_DIAG_POWER_BL 0
#endif
#ifndef BOARD_LCD_BOOT_COLOR_HOLD_MS
#define BOARD_LCD_BOOT_COLOR_HOLD_MS 250
#endif
#ifndef BOARD_LCD_IO_SWAP_COLOR_BYTES
#define BOARD_LCD_IO_SWAP_COLOR_BYTES 0
#endif
#ifndef BOARD_AUDIO_TCA9555_I2C_ADDR
#define BOARD_AUDIO_TCA9555_I2C_ADDR 0
#endif
#ifndef BOARD_LCD_TCA9555_KICK
#define BOARD_LCD_TCA9555_KICK 1
#endif
#ifndef BOARD_UI_SOFTWARE_SHADOWS
#define BOARD_UI_SOFTWARE_SHADOWS 1
#endif

#if BOARD_UI_SOFTWARE_SHADOWS
#define UI_SET_SHADOW_WIDTH(obj, value, selector) \
    lv_obj_set_style_shadow_width((obj), (value), (selector))
#define UI_SET_SHADOW_OFS_Y(obj, value, selector) \
    lv_obj_set_style_shadow_ofs_y((obj), (value), (selector))
#define UI_SET_SHADOW_OPA(obj, value, selector) \
    lv_obj_set_style_shadow_opa((obj), (value), (selector))
#define UI_SET_SHADOW_COLOR(obj, value, selector) \
    lv_obj_set_style_shadow_color((obj), (value), (selector))
#else
#define UI_SET_SHADOW_WIDTH(obj, value, selector) \
    lv_obj_set_style_shadow_width((obj), 0, (selector))
#define UI_SET_SHADOW_OFS_Y(obj, value, selector) ((void)0)
#define UI_SET_SHADOW_OPA(obj, value, selector) ((void)0)
#define UI_SET_SHADOW_COLOR(obj, value, selector) ((void)0)
#endif

/* ===== Display state singleton ===== */
#define DISPLAY_SCHED_Q_LEN     16
/* 10KB — apply_emotion chain (lvgl_gif_create → gd_open_gif_data → LZW init +
 * gd_render_frame palette lookup + canvas write) tốn ~6-7KB peak stack khi
 * decoding GIF 128×128. 6KB cũ cận kề overflow → corrupt IDLE TCB → crash
 * xTaskIncrementTick. */
#define DISPLAY_TASK_STACK      VIMATE_TASK_STACK_DISPLAY
/* Pri 6 (giảm từ 10) — đủ cao hơn WiFi default Core 0 nhưng không chặn
 * IDLE0 housekeeping. Display chỉ chạy khi có queue item nên ko cần pri cao. */
#define DISPLAY_TASK_PRI        VIMATE_TASK_PRIO_DISPLAY
#define DISPLAY_TASK_CORE       VIMATE_TASK_CORE_UI
#define PREVIEW_HIDE_MS         0          /* 0 = lesson image stays until replaced/hidden */
#define REWARD_HIDE_MS          5000       /* 13/09: 3000 → 5000, trẻ 6–8 tuổi đọc kịp "+3 sao · Tốt lắm!" (MASTER.md §7) */

#define CHAT_AUTO_HIDE_MS       8000       /* tránh text STT/TTS cũ nằm mãi trên LCD */
#define POPUP_AUTO_HIDE_MS      5000       /* xác nhận cấu hình chỉ hiện ngắn rồi về idle */
#define EMOJI_PNG_SIZE          128       /* built-in PNG fallback dimension (GIF: show_gif_locked tự đọc header.w) */
#if defined(BOARD_LCD_USE_ST7796S) && BOARD_LCD_USE_ST7796S
#define EMOJI_TARGET_PX         128       /* VIMATE edu: Chuppy GIF rendered 1:1 to avoid alpha-scale tearing */
#define EMOJI_WIDGET_W          160
#define EMOJI_WIDGET_H          160
#elif defined(BOARD_LCD_USE_ST77922) && BOARD_LCD_USE_ST77922
/* Mặt = 3/4 CẠNH NHỎ (~240px) căn giữa, chừa lề → không phóng to/cắt mép.
 * Chỉnh tỉ lệ 3/4 nếu muốn to/nhỏ hơn. */
#define EMOJI_TARGET_PX         ((BOARD_LCD_H_RES < BOARD_LCD_V_RES ? BOARD_LCD_H_RES : BOARD_LCD_V_RES) * 3 / 4)
#define EMOJI_WIDGET_W          BOARD_LCD_H_RES
#define EMOJI_WIDGET_H          BOARD_LCD_V_RES
#elif defined(BOARD_LCD_USE_MIPI_DSI) && BOARD_LCD_USE_MIPI_DSI
/* P4 4.3": mặt căn giữa, kích thước theo board header (BOARD_EMOJI_TARGET_PX), mặc
 * định = CẠNH NHỎ. Nhánh #else bên dưới lấy cạnh LỚN → tràn widget, cắt mép (ảnh chụp
 * 11/09/2026). GIF Noto 160 px: 480 = ×3 (nguyên), 400 = ×2,5 (nearest-neighbour,
 * hàng pixel xen 2/3 — với emoji gradient không thấy). */
#ifdef BOARD_EMOJI_TARGET_PX
#define EMOJI_TARGET_PX         BOARD_EMOJI_TARGET_PX
#else
#define EMOJI_TARGET_PX         (BOARD_LCD_H_RES < BOARD_LCD_V_RES ? BOARD_LCD_H_RES : BOARD_LCD_V_RES)
#endif
#define EMOJI_WIDGET_W          BOARD_LCD_H_RES
#define EMOJI_WIDGET_H          BOARD_LCD_V_RES
#else
#define EMOJI_TARGET_PX         (BOARD_LCD_H_RES > BOARD_LCD_V_RES ? BOARD_LCD_H_RES : BOARD_LCD_V_RES)
#define EMOJI_WIDGET_W          BOARD_LCD_H_RES
#define EMOJI_WIDGET_H          BOARD_LCD_V_RES
#endif
#define EMOJI_WIDGET_ALIGN      LV_ALIGN_CENTER
#define EMOJI_WIDGET_X          0
#define EMOJI_WIDGET_Y          0
/* lv_image_set_scale: 256=1x. 256 * 380 / 128 = 760 → crop tràn viền. */
#define EMOJI_SCALE_FACTOR      (256 * EMOJI_TARGET_PX / EMOJI_PNG_SIZE)
#ifndef BOARD_EMOJI_GIF_RUNTIME_ENABLE
#if defined(BOARD_LCD_USE_ST77922) && BOARD_LCD_USE_ST77922
#define BOARD_EMOJI_GIF_RUNTIME_ENABLE 0
#else
#define BOARD_EMOJI_GIF_RUNTIME_ENABLE 1
#endif
#endif
#define EMOJI_GIF_RUNTIME_ENABLE BOARD_EMOJI_GIF_RUNTIME_ENABLE
/* LCD DMA draw buffer nằm trong internal DRAM. ST7796S SPI 480x320 dễ nghẽn
 * nếu flush vùng lớn từ PSRAM, nên chia nhỏ để panel không kẹt wait flush. */
#if defined(BOARD_LCD_USE_ST7796S) && BOARD_LCD_USE_ST7796S
/* Keep ST7796S flush chunks small and internal-DMA backed. Larger PSRAM DMA strips
 * can wedge LVGL in wait_for_flushing after SPI transmit failure when internal heap
 * is tight after WiFi/WS startup. */
#ifdef BOARD_LCD_DRAW_BUF_LINES
#define LCD_DRAW_BUF_LINES      BOARD_LCD_DRAW_BUF_LINES
#else
#define LCD_DRAW_BUF_LINES      20
#endif
#else
#define LCD_DRAW_BUF_LINES      10
#endif

#define VIMATE_HOME_MAX 8   /* số ô tối đa trên lưới home/menu (Pha C) */
#define VIMATE_SS_MAX   20  /* số ảnh tối đa trong slideshow màn chờ */

typedef struct {
    void (*cb)(void *);
    void *arg;
} display_sched_item_t;

typedef struct {
    /* Lifecycle */
    bool setup_ui_called;

    /* Top: state label */
    lv_obj_t *status_label;
    /* Voice turn indicator: stays visible above Agent content and bottom bar. */
    lv_obj_t *voice_state_panel;
    lv_obj_t *voice_state_dot;
    lv_obj_t *voice_state_label;
    /* Center: emoji image full màn (built-in PNG 128, scaled to 240) */
    lv_obj_t *emoji_image;
    /* Center: emoji GIF widget (lv_image driven by lvgl_gif_t controller) —
     * KHÔNG dùng lv_gif vì frame timer của nó cần LVGL pri đủ cao; controller
     * riêng dùng lv_timer tick, set_src ARGB8888 canvas → ổn định hơn. */
    lv_obj_t *emoji_gif;
    /* Bottom: chat/STT message */
    lv_obj_t *chat_label;
    /* Overlay: preview lesson image (RGB565, hidden default) */
    lv_obj_t *preview_image;
    /* Overlay: title/body popup (activation, error) */
    lv_obj_t *popup_panel;
    lv_obj_t *popup_title;
    lv_obj_t *popup_brand;
    lv_obj_t *popup_body;
    lv_obj_t *popup_code;        /* activation code lớn */
    lv_obj_t *popup_qr;          /* QR mã kích hoạt */
    /* Overlay: reward stars */
    lv_obj_t *reward_panel;
    lv_obj_t *reward_star_img[5]; /* icon_star recolor: đầy/rỗng (thay chuỗi "* ." cũ) */
    lv_obj_t *reward_msg;

    /* EDU home grid (Pha C) — lưới icon kiểu iOS 26 (touch đã sống) */
    lv_obj_t *home_panel;
    lv_obj_t *home_cards[VIMATE_HOME_MAX];        /* cell container (hit region) */
    lv_obj_t *home_card_tiles[VIMATE_HOME_MAX];   /* squircle icon (đổi màu theo id) */
    lv_obj_t *home_card_icons[VIMATE_HOME_MAX];   /* label glyph (LV_SYMBOL) */
    lv_obj_t *home_card_titles[VIMATE_HOME_MAX];  /* label chữ dưới icon */
    lv_obj_t *home_pill_lbl;                      /* header trái: lời chào */
    lv_obj_t *home_clock_lbl;                     /* header giữa: giờ HH:MM (SNTP) */
    lv_obj_t *home_star_lbl;                      /* header phải: badge N sao */
    lv_obj_t *home_bar_btns[5];                   /* bottom bar 5 app cố định (mockup) */
    char      home_card_ids[VIMATE_HOME_MAX][24]; /* id gửi home_select */
    int       home_card_count;

    /* Home khóa học: một bìa lớn + tên đầy đủ. Chỉ decode một cover tại một
     * thời điểm; chuỗi nằm ở PSRAM và được thay nguyên lô theo payload Home. */
    lv_obj_t *home_course_panel;
    lv_obj_t *home_course_cover_frame;
    lv_obj_t *home_course_cover_image;
    lv_obj_t *home_course_placeholder;
    char      home_cover_applied_url[384];   /* URL bìa đang hiện trong widget ("" = ẩn) */
    lv_obj_t *home_course_title;
    lv_obj_t *home_course_subtitle;
    lv_obj_t *home_course_page;
    lv_obj_t *home_course_prev;
    lv_obj_t *home_course_next;
    char     *home_course_ids[VIMATE_HOME_MAX];
    char     *home_course_titles[VIMATE_HOME_MAX];
    char     *home_course_subtitles[VIMATE_HOME_MAX];
    char     *home_course_cover_urls[VIMATE_HOME_MAX];
    int       home_course_count;
    int       home_course_index;
    bool      home_course_mode;

    /* EDU quiz — màn trắc nghiệm 4 nút chạm (mockup màn 4) */
    lv_obj_t *quiz_panel;
    lv_obj_t *quiz_q_lbl;                         /* câu hỏi */
    lv_obj_t *quiz_prog_lbl;                      /* "N/M" góc trên */
    lv_obj_t *quiz_btns[4];                        /* 4 nút đáp án (vùng chạm) */
    lv_obj_t *quiz_btn_lbls[4];
    int       quiz_btn_count;

    /* EDU thống kê (mockup màn 10) */
    lv_obj_t *stats_panel;
    lv_obj_t *stats_val[4];                        /* 4 số: phút/bài/ngày/sao */
    /* EDU uống nước (mockup màn 9) — 2 nút chạm */
    lv_obj_t *water_panel;
    lv_obj_t *water_ml_lbl;
    lv_obj_t *water_bar;                           /* thanh tiến độ ml */
    lv_obj_t *water_btns[3];                        /* Đã uống / Để sau / Tắt nhắc */
    /* EDU lộ trình học (mockup màn 6) — list bài + sao/khóa */
    lv_obj_t *progress_panel;
    lv_obj_t *progress_title;
    lv_obj_t *progress_pct_lbl;
    lv_obj_t *progress_bar;                         /* thanh % (fill) */
    lv_obj_t *progress_rows[6];
    lv_obj_t *progress_name[6];
    lv_obj_t *progress_stat[6];                     /* sao hoặc "khóa" */
    /* Bottom bar GLOBAL + nút Home tròn giữa — hiện trên mọi màn menu/tiện ích. */
    lv_obj_t *bottom_bar;
    lv_obj_t *nav_home_btn;
    /* Đồng hồ màn chờ: nền đen, giờ/thứ/ngày màu trắng. */
    lv_obj_t *idle_clock_panel;
    lv_obj_t *idle_clock_time;
    lv_obj_t *idle_clock_weekday;
    lv_obj_t *idle_clock_date;
    volatile bool idle_clock_active;

    /* EDU timetable — bảng thời khóa biểu 3.5" (T2-T6, Sáng/Chiều) */
    lv_obj_t *timetable_panel;
    lv_obj_t *timetable_class_label;
    lv_obj_t *timetable_cells[2][DISPLAY_TIMETABLE_DAYS];
    lv_obj_t *timetable_detail_panel;
    lv_obj_t *timetable_detail_title;
    lv_obj_t *timetable_detail_morning;
    lv_obj_t *timetable_detail_afternoon;
    lv_obj_t *timetable_detail_body;
    lv_obj_t *timetable_detail_hint;
    char      timetable_morning_text[DISPLAY_TIMETABLE_DAYS][160];
    char      timetable_afternoon_text[DISPLAY_TIMETABLE_DAYS][160];
    char     *timetable_morning_detail[DISPLAY_TIMETABLE_DAYS];
    char     *timetable_afternoon_detail[DISPLAY_TIMETABLE_DAYS];
    int       timetable_day_hit_x[DISPLAY_TIMETABLE_DAYS];
    int       timetable_day_hit_w[DISPLAY_TIMETABLE_DAYS];
    int       timetable_day_hit_y;
    int       timetable_day_hit_h;
    int       timetable_selected_day;
    int       timetable_detail_offset;
    int       timetable_detail_total_lines;

	/* Alarm screen (Hẹn giờ/Uống nước) — màn nhắc dễ thương */
	lv_obj_t            *alarm_panel;
	lv_obj_t            *alarm_card;
	lv_obj_t            *alarm_icon;
	lv_obj_t            *alarm_text;
	esp_timer_handle_t   alarm_hide_timer;

	/* Countdown screen — hiển thị MM:SS khi phụ huynh/bé bấm đếm ngược */
	lv_obj_t            *countdown_panel;
	lv_obj_t            *countdown_card;
	lv_obj_t            *countdown_title;
	lv_obj_t            *countdown_time;
	esp_timer_handle_t   countdown_timer;
	int64_t              countdown_end_us;
	bool                 countdown_active;

	/* Clock screen (app Đồng hồ) — full màn giờ + ngày, chạm để về Home */
    lv_obj_t            *clock_panel;
    lv_obj_t            *clock_time;
    lv_obj_t            *clock_date;
    lv_obj_t            *clock_sun;        /* trang trí (chỉ theme digital) */
    lv_obj_t            *clock_cloud;      /* trang trí (chỉ theme digital) */
    lv_obj_t            *clock_scrim;      /* nền mờ sau chữ (chỉ theme wallpaper) */
    lv_obj_t            *clock_hint;       /* "Chạm để về Home" — đổi màu theo theme */
    esp_timer_handle_t   clock_timer;     /* tick 1s khi clock hiện */
    int64_t              clock_base_local; /* epoch UTC + lệch múi giờ (giây) */
    int64_t              clock_base_us;    /* mốc esp_timer lúc nhận lệnh */
    bool                 clock_wallpaper;  /* true = nền là ảnh (preview_image) */

    /* Slideshow màn chờ (ảnh gia đình + giờ đè lên) */
    bool                 ss_enabled;       /* cha mẹ bật slideshow */
    bool                 ss_show_clock;    /* hiện giờ/ngày đè lên ảnh */
    bool                 ss_active;        /* slideshow ĐANG chạy */
    int                  ss_interval_sec;  /* mỗi ảnh hiện bao lâu */
    int                  ss_idle_after_sec;/* rảnh bao lâu thì vào slideshow */
    int                  ss_count;         /* số ảnh */
    int                  ss_idx;           /* ảnh đang chiếu */
    char                *ss_urls[VIMATE_SS_MAX]; /* URL ảnh (strdup) */
    int64_t              ss_clock_local;   /* epoch+lệch giờ cho overlay */
    esp_timer_handle_t   ss_timer;         /* đổi ảnh mỗi interval */

    /* State */
    esp_lcd_panel_handle_t panel;
    int lcd_width;
    int lcd_height;
    esp_timer_handle_t preview_hide_timer;
    esp_timer_handle_t reward_hide_timer;
    esp_timer_handle_t chat_hide_timer;
    esp_timer_handle_t popup_hide_timer;
    esp_timer_handle_t sleep_timer;
    int backlight_percent;
    int sleep_timeout_sec;
    int64_t last_activity_us;
    bool backlight_sleeping;
    bool hw_ready;
    vimate_emotion_t current_emotion;
    esp_err_t init_error;
    char init_status[64];
    uint32_t chat_seq;
    uint32_t popup_seq;

    /* Task */
    QueueHandle_t sched_q;
    TaskHandle_t  task;
} vimate_display_t;

static vimate_display_t s_display = {0};
static void screens_dismiss_locked(void);   /* định nghĩa cạnh overlays_dismiss_locked */
static lv_display_t   *s_lvgl_display = NULL;
static void display_flush_count_cb(lv_event_t *e);
static void display_record_status(esp_err_t err, const char *stage) {
    s_display.init_error = err;
    if (err == ESP_OK) {
        strlcpy(s_display.init_status, stage && stage[0] ? stage : "ok",
                sizeof(s_display.init_status));
    } else {
        snprintf(s_display.init_status, sizeof(s_display.init_status), "%s:%s",
                 stage && stage[0] ? stage : "display", esp_err_to_name(err));
    }
}

/* ===== Forward decls ===== */
static void display_task(void *arg);
static bool display_schedule(void (*cb)(void *), void *arg);
static void on_preview_hide_timer(void *arg);
static void on_reward_hide_timer(void *arg);
static void on_chat_hide_timer(void *arg);
static void on_popup_hide_timer(void *arg);
static void on_alarm_hide_timer(void *arg);
static void on_countdown_timer(void *arg);
static void on_clock_timer(void *arg);
static void on_ss_timer(void *arg);
static void on_sleep_timer(void *arg);
static void apply_ss_idle_tick(void *arg);
static void slideshow_stop(void);
static void display_apply_backlight_hw(int percent);
static void display_apply_lcd_power_hw(bool on);
static void display_note_activity(void);
static void apply_idle_clock_hide(void *arg);
#if BOARD_FACE_GIF_FULLSCREEN
/* Mặt robot toàn màn — định nghĩa ở khối sau find_embedded_gif. */
static void face_init_locked(void);
static void face_on_state_locked(vimate_dev_state_t s);
static void face_on_emotion_locked(vimate_emotion_t e);
static void face_set_overlay_locked(face_id_t id);
static void face_set_visible_locked(bool visible);
#endif

/* ===== Schedule queue ===== */
/* Trả false nếu KHÔNG enqueue được (chưa init / queue đầy) → caller có cấp phát
 * heap cho arg phải tự free để khỏi rò (callback sẽ không bao giờ chạy). */
static bool display_schedule(void (*cb)(void *), void *arg) {
    if (!s_display.sched_q) {
        ESP_LOGW(TAG_UI, "display_schedule called before init");
        return false;
    }
    display_sched_item_t item = { .cb = cb, .arg = arg };
    if (xQueueSend(s_display.sched_q, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG_UI, "display sched_q full — dropping callback");
        return false;
    }
    return true;
}

static void display_task(void *arg) {
    (void)arg;
    display_sched_item_t item;
    ESP_LOGI(TAG_UI, "display task running on core %d pri %d",
             xPortGetCoreID(), uxTaskPriorityGet(NULL));
    while (1) {
        if (xQueueReceive(s_display.sched_q, &item, portMAX_DELAY) == pdTRUE) {
            if (item.cb) item.cb(item.arg);
        }
    }
}

static void display_apply_backlight_hw(int percent) {
#if BOARD_LCD_HAS_BACKLIGHT
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t duty = (1023 * percent) / 100;
#if !BOARD_LCD_BL_ON_LEVEL
    duty = 1023 - duty;
#endif
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
#else
    (void)percent;
#endif
}

static bool display_gpio_is_valid(gpio_num_t gpio) {
    return gpio >= 0 && gpio < GPIO_NUM_MAX;
}

static void display_apply_lcd_power_hw(bool on) {
#if BOARD_LCD_POWER_CTRL_ENABLED
    if (!display_gpio_is_valid(BOARD_LCD_POWER_CTRL_GPIO)) {
        (void)on;
        return;
    }
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << (unsigned)BOARD_LCD_POWER_CTRL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    const int level = on ? BOARD_LCD_POWER_ON_LEVEL : !BOARD_LCD_POWER_ON_LEVEL;
    ESP_ERROR_CHECK(gpio_set_level(BOARD_LCD_POWER_CTRL_GPIO, level));
    ESP_LOGI(TAG_UI, "LCD power GPIO%d=%d on_level=%d", BOARD_LCD_POWER_CTRL_GPIO,
             level, BOARD_LCD_POWER_ON_LEVEL);
#else
    (void)on;
#endif
}

static void display_apply_extra_backlight_hw(bool on) {
#if BOARD_LCD_HAS_BACKLIGHT && BOARD_LCD_EXTRA_BL_ENABLED
    if (!display_gpio_is_valid(BOARD_LCD_EXTRA_BL_GPIO)) {
        (void)on;
        return;
    }
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << (unsigned)BOARD_LCD_EXTRA_BL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    const int level = on ? BOARD_LCD_BL_ON_LEVEL : !BOARD_LCD_BL_ON_LEVEL;
    ESP_ERROR_CHECK(gpio_set_level(BOARD_LCD_EXTRA_BL_GPIO, level));
    ESP_LOGI(TAG_UI, "LCD extra backlight GPIO%d=%d", BOARD_LCD_EXTRA_BL_GPIO, level);
#else
    (void)on;
#endif
}

static esp_err_t display_i2c_get_or_create(i2c_port_num_t port,
                                           gpio_num_t sda,
                                           gpio_num_t scl,
                                           i2c_master_bus_handle_t *out_bus) {
    if (!out_bus) return ESP_ERR_INVALID_ARG;

    esp_err_t err = i2c_master_get_bus_handle(port, out_bus);
    if (err == ESP_OK && *out_bus) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = port,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&bus_cfg, out_bus);
    if (err == ESP_ERR_INVALID_STATE) {
        err = i2c_master_get_bus_handle(port, out_bus);
    }
    return err;
}

static esp_err_t display_i2c_write_reg(i2c_master_dev_handle_t dev,
                                       uint8_t reg,
                                       uint8_t val) {
    uint8_t data[] = {reg, val};
    return i2c_master_transmit(dev, data, sizeof(data), pdMS_TO_TICKS(100));
}

static esp_err_t display_i2c_read_reg(i2c_master_dev_handle_t dev,
                                      uint8_t reg,
                                      uint8_t *val) {
    if (!val) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(dev, &reg, 1, val, 1, pdMS_TO_TICKS(100));
}

static void display_genu_tca9555_kick(void) {
#if BOARD_AUDIO_TCA9555_I2C_ADDR && BOARD_LCD_TCA9555_KICK
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = display_i2c_get_or_create(BOARD_AUDIO_CODEC_I2C_NUM,
                                              BOARD_AUDIO_CODEC_I2C_SDA,
                                              BOARD_AUDIO_CODEC_I2C_SCL,
                                              &bus);
    if (err != ESP_OK || !bus) {
        ESP_LOGW(TAG_UI, "GENU TCA9555 skipped: I2C bus init failed: %s",
                 esp_err_to_name(err));
        return;
    }

    uint8_t addr = BOARD_AUDIO_TCA9555_I2C_ADDR;
    err = i2c_master_probe(bus, addr, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        bool found = false;
        for (uint8_t probe = 0x20; probe <= 0x27; ++probe) {
            if (i2c_master_probe(bus, probe, pdMS_TO_TICKS(50)) == ESP_OK) {
                addr = probe;
                found = true;
                break;
            }
        }
        if (!found) {
            ESP_LOGW(TAG_UI, "GENU TCA9555 not found on I2C%d SDA%d SCL%d",
                     BOARD_AUDIO_CODEC_I2C_NUM, BOARD_AUDIO_CODEC_I2C_SDA,
                     BOARD_AUDIO_CODEC_I2C_SCL);
            return;
        }
    }

    i2c_master_dev_handle_t dev = NULL;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (err != ESP_OK || !dev) {
        ESP_LOGW(TAG_UI, "GENU TCA9555 device add failed at 0x%02x: %s",
                 addr, esp_err_to_name(err));
        return;
    }

    uint8_t out0 = 0x00;
    uint8_t out1 = 0x00;
    uint8_t cfg0 = 0xff;
    uint8_t cfg1 = 0xff;
    (void)display_i2c_read_reg(dev, 0x02, &out0); /* output port 0 */
    (void)display_i2c_read_reg(dev, 0x03, &out1); /* output port 1 */
    (void)display_i2c_read_reg(dev, 0x06, &cfg0); /* config port 0 */
    (void)display_i2c_read_reg(dev, 0x07, &cfg1); /* config port 1 */

    /* Waveshare ESP32-S3-AUDIO-Board demo initializes every TCA9555 line as
     * output, then uses EXIO0 for LCD reset, EXIO1 for touch reset and EXIO8
     * for speaker PA. Match that sequence; unknown high outputs are the
     * TCA9555 power-on default on this carrier. */
    cfg0 = 0x00;
    cfg1 = 0x00;
    ESP_ERROR_CHECK_WITHOUT_ABORT(display_i2c_write_reg(dev, 0x06, cfg0));
    ESP_ERROR_CHECK_WITHOUT_ABORT(display_i2c_write_reg(dev, 0x07, cfg1));

    out0 |= (uint8_t)((1U << 0) | (1U << 1));
    out1 |= (uint8_t)(1U << 0);
    ESP_ERROR_CHECK_WITHOUT_ABORT(display_i2c_write_reg(dev, 0x02, out0));
    ESP_ERROR_CHECK_WITHOUT_ABORT(display_i2c_write_reg(dev, 0x03, out1));
    vTaskDelay(pdMS_TO_TICKS(30));

    out0 &= (uint8_t)~((1U << 0) | (1U << 1));
    ESP_ERROR_CHECK_WITHOUT_ABORT(display_i2c_write_reg(dev, 0x02, out0));
    vTaskDelay(pdMS_TO_TICKS(30));

    out0 |= (uint8_t)((1U << 0) | (1U << 1));
    ESP_ERROR_CHECK_WITHOUT_ABORT(display_i2c_write_reg(dev, 0x02, out0));
    vTaskDelay(pdMS_TO_TICKS(30));

    ESP_LOGI(TAG_UI, "GENU TCA9555 found at 0x%02x; kick out0=0x%02x out1=0x%02x cfg0=0x%02x cfg1=0x%02x",
             addr, out0, out1, cfg0, cfg1);
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_bus_rm_device(dev));
    /* Display only needs this bus for a short TCA9555 reset pulse. Release it
     * so the ES8311 audio path can create a fresh shared codec/touch bus. */
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_del_master_bus(bus));
#endif
}

static void display_diag_power_backlight_hw(void) {
#if BOARD_LCD_DIAG_POWER_BL && BOARD_LCD_HAS_BACKLIGHT && BOARD_LCD_POWER_CTRL_ENABLED
    if (!display_gpio_is_valid(BOARD_LCD_POWER_CTRL_GPIO) ||
        !display_gpio_is_valid(BOARD_LCD_PIN_BL)) {
        ESP_LOGW(TAG_UI, "Skip LCD power/backlight diag: power_gpio=%d bl_gpio=%d",
                 BOARD_LCD_POWER_CTRL_GPIO, BOARD_LCD_PIN_BL);
        return;
    }
    gpio_config_t pwr_cfg = {
        .pin_bit_mask = 1ULL << (unsigned)BOARD_LCD_POWER_CTRL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << (unsigned)BOARD_LCD_PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&pwr_cfg));
    ESP_ERROR_CHECK(gpio_config(&bl_cfg));

    const int pwr_on = BOARD_LCD_POWER_ON_LEVEL;
    const int pwr_off = !BOARD_LCD_POWER_ON_LEVEL;
    const int bl_on = BOARD_LCD_BL_ON_LEVEL;
    const int bl_off = !BOARD_LCD_BL_ON_LEVEL;

    ESP_LOGI(TAG_UI, "LCD diag step 1: power=%d backlight=%d", pwr_on, bl_on);
    gpio_set_level(BOARD_LCD_POWER_CTRL_GPIO, pwr_on);
    gpio_set_level(BOARD_LCD_PIN_BL, bl_on);
    display_apply_extra_backlight_hw(true);
    vTaskDelay(pdMS_TO_TICKS(1200));

    ESP_LOGI(TAG_UI, "LCD diag step 2: power=%d backlight=%d", pwr_on, bl_off);
    gpio_set_level(BOARD_LCD_POWER_CTRL_GPIO, pwr_on);
    gpio_set_level(BOARD_LCD_PIN_BL, bl_off);
    vTaskDelay(pdMS_TO_TICKS(1200));

    ESP_LOGI(TAG_UI, "LCD diag step 3: power=%d backlight=%d", pwr_off, bl_on);
    gpio_set_level(BOARD_LCD_POWER_CTRL_GPIO, pwr_off);
    gpio_set_level(BOARD_LCD_PIN_BL, bl_on);
    vTaskDelay(pdMS_TO_TICKS(1200));

    ESP_LOGI(TAG_UI, "LCD diag step 4: power=%d backlight=%d", pwr_off, bl_off);
    gpio_set_level(BOARD_LCD_POWER_CTRL_GPIO, pwr_off);
    gpio_set_level(BOARD_LCD_PIN_BL, bl_off);
    vTaskDelay(pdMS_TO_TICKS(1200));

    ESP_LOGI(TAG_UI, "LCD diag final: power=%d backlight=%d", pwr_on, bl_on);
    gpio_set_level(BOARD_LCD_POWER_CTRL_GPIO, pwr_on);
    gpio_set_level(BOARD_LCD_PIN_BL, bl_on);
    display_apply_extra_backlight_hw(true);
    vTaskDelay(pdMS_TO_TICKS(200));
#endif
}

static void display_boot_color_fill(esp_lcd_panel_handle_t panel, uint16_t color) {
    const int lines = LCD_DRAW_BUF_LINES;
    const size_t pixels = BOARD_LCD_H_RES * lines;
    uint16_t *buf = heap_caps_malloc(pixels * sizeof(uint16_t),
                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGW(TAG_UI, "boot color fill skipped: no DMA buffer");
        return;
    }
    for (size_t i = 0; i < pixels; ++i) {
        buf[i] = color;
    }
    for (int y = 0; y < BOARD_LCD_V_RES; y += lines) {
        int y_end = y + lines;
        if (y_end > BOARD_LCD_V_RES) y_end = BOARD_LCD_V_RES;
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, BOARD_LCD_H_RES, y_end, buf));
    }
    free(buf);
}

static void display_boot_color_step(esp_lcd_panel_handle_t panel,
                                    const char *name,
                                    uint16_t color) {
    ESP_LOGI(TAG_UI, "Boot LCD color: %s", name);
    display_boot_color_fill(panel, color);
    vTaskDelay(pdMS_TO_TICKS(BOARD_LCD_BOOT_COLOR_HOLD_MS));
}

#if BOARD_LCD_USE_ST7796S
static const st7796_lcd_init_cmd_t s_st7796_lcd_init_cmds[] = {
    {0x3A, (uint8_t []){0x05}, 1, 0},
    {0xF0, (uint8_t []){0xC3}, 1, 0},
    {0xF0, (uint8_t []){0x96}, 1, 0},
    {0xB4, (uint8_t []){0x01}, 1, 0},
    {0xB7, (uint8_t []){0xC6}, 1, 0},
    {0xC0, (uint8_t []){0x80, 0x45}, 2, 0},
    {0xC1, (uint8_t []){0x13}, 1, 0},
    {0xC2, (uint8_t []){0xA7}, 1, 0},
    {0xC5, (uint8_t []){0x0A}, 1, 0},
    {0xE8, (uint8_t []){0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 8, 0},
    {0xE0, (uint8_t []){0xD0, 0x08, 0x0F, 0x06, 0x06, 0x33, 0x30,
                         0x33, 0x47, 0x17, 0x13, 0x13, 0x2B, 0x31}, 14, 0},
    {0xE1, (uint8_t []){0xD0, 0x0A, 0x11, 0x0B, 0x09, 0x07, 0x2F,
                         0x33, 0x47, 0x38, 0x15, 0x16, 0x2C, 0x32}, 14, 0},
    {0xF0, (uint8_t []){0x3C}, 1, 0},
    {0xF0, (uint8_t []){0x69}, 1, 0},
    {0x11, NULL, 0, 120},
    {0x21, NULL, 0, 0},
    {0x29, NULL, 0, 120},
};

static esp_err_t st7796s_tx(esp_lcd_panel_io_handle_t io, int cmd,
                            const uint8_t *data, size_t len) {
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, cmd, data, len),
                        TAG_UI, "ST7796S cmd 0x%02x failed", cmd);
    return ESP_OK;
}

static esp_err_t st7796s_init_panel_io(esp_lcd_panel_io_handle_t io) {
    ESP_LOGI(TAG_UI, "Init ST7796S SPI command sequence");
    ESP_RETURN_ON_ERROR(st7796s_tx(io, LCD_CMD_SWRESET, NULL, 0), TAG_UI,
                        "ST7796S SWRESET failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_RETURN_ON_ERROR(st7796s_tx(io, LCD_CMD_SLPOUT, NULL, 0), TAG_UI,
                        "ST7796S SLPOUT failed");
    vTaskDelay(pdMS_TO_TICKS(120));

    uint8_t data;
    data = 0x55; /* RGB565 */
    ESP_RETURN_ON_ERROR(st7796s_tx(io, LCD_CMD_COLMOD, &data, 1), TAG_UI,
                        "ST7796S COLMOD failed");

    const uint8_t f0_c3[] = {0xC3};
    ESP_RETURN_ON_ERROR(st7796s_tx(io, 0xF0, f0_c3, sizeof(f0_c3)), TAG_UI,
                        "ST7796S command unlock C3 failed");
    const uint8_t f0_96[] = {0x96};
    ESP_RETURN_ON_ERROR(st7796s_tx(io, 0xF0, f0_96, sizeof(f0_96)), TAG_UI,
                        "ST7796S command unlock 96 failed");

    data = BOARD_LCD_USE_BGR ? LCD_CMD_BGR_BIT : 0x00;
    ESP_RETURN_ON_ERROR(st7796s_tx(io, LCD_CMD_MADCTL, &data, 1), TAG_UI,
                        "ST7796S MADCTL failed");

    const uint8_t b4[] = {0x01};
    ESP_RETURN_ON_ERROR(st7796s_tx(io, 0xB4, b4, sizeof(b4)), TAG_UI,
                        "ST7796S inversion control failed");
    const uint8_t b7[] = {0xC6};
    ESP_RETURN_ON_ERROR(st7796s_tx(io, 0xB7, b7, sizeof(b7)), TAG_UI,
                        "ST7796S entry mode failed");
    const uint8_t c0[] = {0x80, 0x45};
    ESP_RETURN_ON_ERROR(st7796s_tx(io, 0xC0, c0, sizeof(c0)), TAG_UI,
                        "ST7796S power control 1 failed");
    const uint8_t c1[] = {0x13};
    ESP_RETURN_ON_ERROR(st7796s_tx(io, 0xC1, c1, sizeof(c1)), TAG_UI,
                        "ST7796S power control 2 failed");
    const uint8_t c2[] = {0xA7};
    ESP_RETURN_ON_ERROR(st7796s_tx(io, 0xC2, c2, sizeof(c2)), TAG_UI,
                        "ST7796S power control 3 failed");
    const uint8_t c5[] = {0x0A};
    ESP_RETURN_ON_ERROR(st7796s_tx(io, 0xC5, c5, sizeof(c5)), TAG_UI,
                        "ST7796S VCOM failed");
    const uint8_t e8[] = {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33};
    ESP_RETURN_ON_ERROR(st7796s_tx(io, 0xE8, e8, sizeof(e8)), TAG_UI,
                        "ST7796S display output control failed");
    const uint8_t e0[] = {
        0xD0, 0x08, 0x0F, 0x06, 0x06, 0x33, 0x30,
        0x33, 0x47, 0x17, 0x13, 0x13, 0x2B, 0x31,
    };
    ESP_RETURN_ON_ERROR(st7796s_tx(io, 0xE0, e0, sizeof(e0)), TAG_UI,
                        "ST7796S positive gamma failed");
    const uint8_t e1[] = {
        0xD0, 0x0A, 0x11, 0x0B, 0x09, 0x07, 0x2F,
        0x33, 0x47, 0x38, 0x15, 0x16, 0x2C, 0x32,
    };
    ESP_RETURN_ON_ERROR(st7796s_tx(io, 0xE1, e1, sizeof(e1)), TAG_UI,
                        "ST7796S negative gamma failed");
    const uint8_t f0_3c[] = {0x3C};
    ESP_RETURN_ON_ERROR(st7796s_tx(io, 0xF0, f0_3c, sizeof(f0_3c)), TAG_UI,
                        "ST7796S command lock 3C failed");
    const uint8_t f0_69[] = {0x69};
    ESP_RETURN_ON_ERROR(st7796s_tx(io, 0xF0, f0_69, sizeof(f0_69)), TAG_UI,
                        "ST7796S command lock 69 failed");
    vTaskDelay(pdMS_TO_TICKS(120));

#if BOARD_LCD_INVERT_COLOR
    ESP_RETURN_ON_ERROR(st7796s_tx(io, LCD_CMD_INVON, NULL, 0), TAG_UI,
                        "ST7796S INVON failed");
#endif

    ESP_RETURN_ON_ERROR(st7796s_tx(io, LCD_CMD_NORON, NULL, 0), TAG_UI,
                        "ST7796S NORON failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(st7796s_tx(io, LCD_CMD_DISPON, NULL, 0), TAG_UI,
                        "ST7796S DISPON failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

static esp_err_t st7796s_raw_fill(esp_lcd_panel_io_handle_t io, uint16_t color) {
    uint8_t col[] = {0x00, 0x00, (BOARD_LCD_NATIVE_W - 1) >> 8, (BOARD_LCD_NATIVE_W - 1) & 0xff};
    uint8_t row[] = {0x00, 0x00, (BOARD_LCD_NATIVE_H - 1) >> 8, (BOARD_LCD_NATIVE_H - 1) & 0xff};
    ESP_RETURN_ON_ERROR(st7796s_tx(io, LCD_CMD_CASET, col, sizeof(col)), TAG_UI,
                        "ST7796S raw CASET failed");
    ESP_RETURN_ON_ERROR(st7796s_tx(io, LCD_CMD_RASET, row, sizeof(row)), TAG_UI,
                        "ST7796S raw RASET failed");
    const size_t chunk_pixels = 320 * 8;
    uint16_t *buf = heap_caps_malloc(chunk_pixels * sizeof(uint16_t),
                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < chunk_pixels; ++i) {
        buf[i] = color;
    }
    size_t remaining = (size_t)BOARD_LCD_NATIVE_W * BOARD_LCD_NATIVE_H;
    while (remaining > 0) {
        size_t n = remaining > chunk_pixels ? chunk_pixels : remaining;
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(io, LCD_CMD_RAMWR, buf, n * sizeof(uint16_t)),
                            TAG_UI, "ST7796S raw RAMWR failed");
        remaining -= n;
    }
    free(buf);
    return ESP_OK;
}
#endif

static bool idle_clock_hide_locked(void) {
    bool visible = s_display.idle_clock_panel &&
                   !lv_obj_has_flag(s_display.idle_clock_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_display.idle_clock_panel) {
        lv_obj_add_flag(s_display.idle_clock_panel, LV_OBJ_FLAG_HIDDEN);
    }
    s_display.idle_clock_active = false;
#if BOARD_FACE_GIF_FULLSCREEN
    if (visible) face_set_visible_locked(true);
#endif
    return visible;
}

static void apply_idle_clock_hide(void *arg) {
    (void)arg;
    display_lock();
    bool visible = idle_clock_hide_locked();
    display_unlock();
    if (visible) {
        ESP_LOGI(TAG_UI, "idle clock exit: activity");
    }
}

static void display_note_activity(void) {
    s_display.last_activity_us = esp_timer_get_time();
    if (s_display.idle_clock_active) {
        s_display.idle_clock_active = false;
        if (s_display.sched_q) {
            display_schedule(apply_idle_clock_hide, NULL);
        }
    }
    if (s_display.backlight_sleeping) {
        s_display.backlight_sleeping = false;
        int pct = s_display.backlight_percent > 0 ? s_display.backlight_percent : 100;
        display_apply_backlight_hw(pct);
    }
}

void display_note_user_activity(void) {
    display_note_activity();
}

bool display_idle_clock_visible(void) {
    return s_display.idle_clock_active;
}

/* "Đang trong phiên AI" (chat/bài học). Mặc định FALSE → chạm màn KHÔNG gọi AI.
 * Server bật (command:lesson khi vào chat/agent), tắt khi về home/idle. touch đọc
 * để chỉ mở lượt nghe khi thực sự đang trong AI. */
static volatile bool s_ai_active = false;
static volatile bool s_agent_active = false;
void display_set_ai_active(bool on) {
    s_ai_active = on;
    if (!on) s_agent_active = false;
}
bool display_ai_active(void) { return s_ai_active; }
void display_set_agent_active(bool on) {
    s_agent_active = on;
    if (on) s_ai_active = true;
}
bool display_agent_active(void) { return s_agent_active; }

static void on_sleep_timer(void *arg) {
    (void)arg;
    int64_t idle_us = esp_timer_get_time() - s_display.last_activity_us;
    /* Idle đủ lâu → thoát phiên AI: chạm sau đó chỉ đánh thức về Home, không gọi
     * AI (vá "chạm màn chờ vẫn gọi AI"). */
    int idle_ai = s_display.ss_idle_after_sec > 0 ? s_display.ss_idle_after_sec : 60;
    if (s_ai_active && idle_us >= (int64_t)idle_ai * 1000000LL) {
        s_ai_active = false;
        s_agent_active = false;
        ESP_LOGI(TAG_UI, "Idle %llds → thoát phiên AI (tap sẽ về Home)", idle_us / 1000000);
    }
    /* Màn chờ slideshow: gate sơ bộ (đọc bool đơn giản) rồi DỜI sang display_task
     * để đọc LVGL (home_visible) + ss_urls AN TOÀN (không đọc LVGL từ timer task). */
    if (s_display.ss_enabled && s_display.ss_count > 0 && !s_display.ss_active) {
        display_schedule(apply_ss_idle_tick, NULL);
    }
    int timeout = s_display.sleep_timeout_sec;
    if (timeout <= 0 || s_display.backlight_sleeping) return;
    if (idle_us >= (int64_t)timeout * 1000000LL) {
        s_display.backlight_sleeping = true;
        display_apply_backlight_hw(0);
    }
}


/* ===== HW bring-up — EDU SPI/QSPI panels (ILI9341 / ST7796S / ST77922) ===== */
esp_err_t display_init(void) {
    s_display.hw_ready = false;
    display_record_status(ESP_ERR_INVALID_STATE, "starting");
    display_genu_tca9555_kick();
    display_diag_power_backlight_hw();
    display_apply_lcd_power_hw(true);
    display_apply_extra_backlight_hw(true);

#if BOARD_LCD_HAS_BACKLIGHT
    /* Backlight PWM */
    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = BOARD_LCD_BL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));
    ledc_channel_config_t ccfg = {
        .gpio_num = BOARD_LCD_PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ccfg));
#endif

#if BOARD_LCD_USE_MIPI_DSI
    /* ===== ST7102 480×800 MIPI-DSI (board ESP32-P4 4.3") ===== */
    /* Reset CỨNG panel TRƯỚC khi mở DSI. LCD_RST trên board chỉ có RC (R70 10K
     * + C95 1uF) nên panel CHỈ tự reset lúc cấp nguồn — reset MCU (nạp firmware,
     * watchdog, nút RST1) KHÔNG reset panel, nó giữ nguyên trạng thái của lần
     * chạy trước. Dính thật 11/09/2026: panel kẹt sau một loạt nạp lại, lệnh
     * DSI đầu tiên của driver không được trả lời, main task quay vô hạn trong
     * HAL (task_wdt IDLE0) — kể cả bin cũ đã chạy tốt hôm trước. Xung thấp
     * 10ms, chờ 120ms theo datasheet ST7102 rồi mới nói chuyện. Driver còn
     * reset thêm lần nữa trong esp_lcd_panel_reset() — vô hại. */
#if BOARD_LCD_PIN_RST >= 0
    {
        gpio_config_t rst_io = {
            .pin_bit_mask = 1ULL << (unsigned)BOARD_LCD_PIN_RST,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        gpio_config(&rst_io);
        gpio_set_level(BOARD_LCD_PIN_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(BOARD_LCD_PIN_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(BOARD_LCD_PIN_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
        ESP_LOGI(TAG_UI, "LCD hard reset GPIO%d truoc khi mo DSI", BOARD_LCD_PIN_RST);
    }
#endif
    /* VDD_MIPI_DPHY 2.5V lấy từ LDO nội — PHẢI acquire TRƯỚC khi tạo DSI bus,
     * nếu không PHY còn ở trạng thái "no power" và esp_lcd_new_dsi_bus() fail. */
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = BOARD_LCD_DSI_PHY_LDO_CHAN,
        .voltage_mv = BOARD_LCD_DSI_PHY_LDO_MV,
    };
    /* Boot-safety như các board SPI: KHÔNG abort khi LCD lỗi — trả lỗi để
     * app_main boot tiếp không màn, giữ WS/OTA cứu thiết bị từ xa. */
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi_phy),
                        TAG_UI, "MIPI DPHY LDO acquire lỗi — bỏ display, vẫn boot");

    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t dsi_bus_cfg = {
        .bus_id = BOARD_LCD_DSI_BUS_ID,
        .num_data_lanes = BOARD_LCD_DSI_LANES,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = BOARD_LCD_DSI_LANE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&dsi_bus_cfg, &mipi_dsi_bus),
                        TAG_UI, "esp_lcd_new_dsi_bus lỗi");

    /* Lệnh/tham số panel đi qua DBI; pixel đi qua DPI (video mode). */
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = ST7102_MIPI_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_cfg, &io),
                        TAG_UI, "esp_lcd_new_panel_io_dbi lỗi");

    esp_lcd_dpi_panel_config_t dpi_cfg =
        ST7102_MIPI_480_800_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    /* 2 fb trong PSRAM (2 x 768KB) để lvgl_port đổi khung không xé; macro của
     * component mặc định 1. */
    dpi_cfg.num_fbs = (BOARD_LCD_DSI_AVOID_TEARING || DSI_ROTATE) ? 2 : 1;
    st7102_vendor_config_t st7102_vendor = {
        .init_cmds = s_st7102_init_cmds,
        .init_cmds_size = sizeof(s_st7102_init_cmds) / sizeof(s_st7102_init_cmds[0]),
        .flags.use_mipi_interface = 1,
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_cfg,
        },
    };
    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = BOARD_LCD_PIN_RST,
#if BOARD_LCD_USE_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = BOARD_LCD_BITS_PER_PIXEL,
        .vendor_config = &st7102_vendor,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7102(io, &pcfg, &panel), TAG_UI, "st7102 panel lỗi");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG_UI, "st7102 reset lỗi");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG_UI, "st7102 init lỗi");
#if BOARD_LCD_INVERT_COLOR
    esp_lcd_panel_invert_color(panel, true);
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG_UI, "st7102 disp on lỗi");
    display_apply_backlight_hw(100);   /* no-op khi board không có chân BL */
    ESP_LOGI(TAG_UI, "Init MIPI-DSI ST7102 panel %dx%d (%d lane @ %d Mbps), logical %dx%d",
             BOARD_LCD_NATIVE_W, BOARD_LCD_NATIVE_H,
             BOARD_LCD_DSI_LANES, BOARD_LCD_DSI_LANE_MBPS,
             BOARD_LCD_H_RES, BOARD_LCD_V_RES);
#elif BOARD_LCD_USE_I80
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t buscfg = {
        .dc_gpio_num = BOARD_LCD_PIN_DC,
        .wr_gpio_num = BOARD_LCD_PIN_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            BOARD_LCD_PIN_DATA0,
            BOARD_LCD_PIN_DATA1,
            BOARD_LCD_PIN_DATA2,
            BOARD_LCD_PIN_DATA3,
            BOARD_LCD_PIN_DATA4,
            BOARD_LCD_PIN_DATA5,
            BOARD_LCD_PIN_DATA6,
            BOARD_LCD_PIN_DATA7,
        },
        .bus_width = 8,
        .max_transfer_bytes = BOARD_LCD_H_RES * LCD_DRAW_BUF_LINES * 2,
        .psram_trans_align = 64,
        .sram_trans_align = 4,
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&buscfg, &i80_bus));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num = BOARD_LCD_PIN_CS,
        .pclk_hz = BOARD_LCD_I80_FREQ_HZ,
        .trans_queue_depth = 10,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .flags = {
            .swap_color_bytes = BOARD_LCD_IO_SWAP_COLOR_BYTES,
        },
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_cfg, &io));

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = BOARD_LCD_PIN_RST,
#if BOARD_LCD_USE_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &pcfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
#if BOARD_LCD_INVERT_COLOR
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, BOARD_LCD_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, BOARD_LCD_MIRROR_X, BOARD_LCD_MIRROR_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
	    display_apply_backlight_hw(100);
	    ESP_LOGI(TAG_UI, "Boot LCD direct fill: white/red/green/blue");
	    display_boot_color_step(panel, "white", 0xffff);
	    display_boot_color_step(panel, "red", 0xf800);
	    display_boot_color_step(panel, "green", 0x07e0);
	    display_boot_color_step(panel, "blue", 0x001f);
    ESP_LOGI(TAG_UI, "Init i80 LCD %dx%d pclk=%dHz", BOARD_LCD_H_RES,
             BOARD_LCD_V_RES, BOARD_LCD_I80_FREQ_HZ);
#elif BOARD_LCD_USE_ST77922
    /* ===== ST77922 320×480 QSPI (LCDwiki 3.5inch ESP32-S3 Display) ===== */
    spi_bus_config_t buscfg = ST77922_PANEL_BUS_QSPI_CONFIG(
        BOARD_LCD_PIN_PCLK, BOARD_LCD_PIN_D0, BOARD_LCD_PIN_D1,
        BOARD_LCD_PIN_D2, BOARD_LCD_PIN_D3,
        /* 32KB = trần HW 1 transaction SPI DMA (SPI_LL_DMA_MAX_BIT_LEN 2^18 bit).
         * Đặt đúng trần → esp_lcd chẻ full-frame 307KB thành ~10 chunk hợp lệ
         * (CS giữ nguyên giữa chunk — SPI_TRANS_CS_KEEP_ACTIVE). */
        32768);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG_UI, "qspi bus init lỗi — bỏ display, vẫn boot");
    esp_lcd_panel_io_spi_config_t io_cfg = ST77922_PANEL_IO_QSPI_CONFIG(BOARD_LCD_PIN_CS, NULL, NULL);
    io_cfg.pclk_hz = BOARD_LCD_SPI_FREQ_HZ;   /* demo chạy 80MHz */
    io_cfg.trans_queue_depth = 6;
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)BOARD_LCD_HOST, &io_cfg, &io),
        TAG_UI, "st77922 panel io lỗi");
    st77922_vendor_config_t st77922_vendor = {
        .init_cmds = s_st77922_init_cmds,
        .init_cmds_size = sizeof(s_st77922_init_cmds) / sizeof(s_st77922_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = BOARD_LCD_PIN_RST,
#if BOARD_LCD_USE_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
        .vendor_config = &st77922_vendor,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st77922(io, &pcfg, &panel), TAG_UI, "st77922 panel lỗi");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG_UI, "st77922 reset lỗi");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG_UI, "st77922 init lỗi");
#if BOARD_LCD_INVERT_COLOR
    esp_lcd_panel_invert_color(panel, true);
#endif
    esp_lcd_panel_swap_xy(panel, BOARD_LCD_SWAP_XY);
    esp_lcd_panel_mirror(panel, BOARD_LCD_MIRROR_X, BOARD_LCD_MIRROR_Y);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
#else
    /* SPI bus */
    spi_bus_config_t buscfg = {
        .sclk_io_num = BOARD_LCD_PIN_SCLK,
        .mosi_io_num = BOARD_LCD_PIN_MOSI,
        .miso_io_num = BOARD_LCD_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOARD_LCD_H_RES * LCD_DRAW_BUF_LINES * 2,
    };
    /* Boot-safety: KHÔNG abort (boot-loop) khi LCD/SPI lỗi — trả lỗi để app_main
     * bỏ qua display nhưng VẪN boot, giữ WiFi/WS/OTA để cứu thiết bị từ xa. */
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG_UI, "spi_bus_initialize lỗi — bỏ display, vẫn boot");

    /* Panel IO */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = BOARD_LCD_PIN_DC,
        .cs_gpio_num = BOARD_LCD_PIN_CS,
        .pclk_hz = BOARD_LCD_SPI_FREQ_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = BOARD_LCD_SPI_MODE,
        .trans_queue_depth = 6,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)BOARD_LCD_HOST, &io_cfg, &io),
        TAG_UI, "esp_lcd_new_panel_io_spi lỗi");

    /* LCD panel */
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = BOARD_LCD_PIN_RST,
#if BOARD_LCD_USE_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
    };
#if BOARD_LCD_USE_ST7796S
    st7796_vendor_config_t st7796_vendor_config = {
        .init_cmds = s_st7796_lcd_init_cmds,
        .init_cmds_size = sizeof(s_st7796_lcd_init_cmds) / sizeof(s_st7796_lcd_init_cmds[0]),
    };
    pcfg.vendor_config = &st7796_vendor_config;
    ESP_LOGI(TAG_UI, "Install native ST7796 SPI panel driver");
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7796(io, &pcfg, &panel), TAG_UI, "st7796 panel lỗi");
#elif BOARD_LCD_USE_ILI9341
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &pcfg, &panel));
#else
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &pcfg, &panel));
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG_UI, "panel reset lỗi");
#if BOARD_LCD_USE_ST7796S
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG_UI, "panel init lỗi");
    display_apply_backlight_hw(100);
#else
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG_UI, "panel init lỗi");
#endif
#if BOARD_LCD_INVERT_COLOR
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
#endif
#if BOARD_LCD_USE_ST7796S
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, BOARD_LCD_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, BOARD_LCD_MIRROR_X, BOARD_LCD_MIRROR_Y));
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
#if BOARD_LCD_USE_ST7796S
	    /* ponytail: bỏ boot-fill chẩn đoán (white/red/green/blue). Nó dùng
	     * ESP_ERROR_CHECK(draw_bitmap) → 1 lỗi draw / brownout lúc boot = panic →
	     * reboot loop kẹt ở màu green, lại CHẶN đường degrade mềm (app_main:828
	     * display_init lỗi → "boot tiếp KHÔNG màn") nên boot không bao giờ tới
	     * enter_provisioning_mode (BLE+AP) để cấu hình lại WiFi. Panel vẫn init
	     * bình thường; LVGL vẽ UI ngay sau. Chỉ bật backlight để LVGL hiện. */
	    display_apply_backlight_hw(100);
#endif
#endif

    /* LVGL render task = task DUY NHẤT rasterize + flush + chạy timer GIF.
     * Đặt trên Core UI (0) tách HẲN khỏi Core IO (1) — nơi audio (pri 7) +
     * WiFi(23)/lwIP(18) chạy — để flush/GIF không bị preempt giữa khung
     * (gốc gây giật + kéo dài cửa sổ xé hình). Theo task_profile.h: DISPLAY
     * pri 6 trên CORE_UI; cùng core/pri với display_task (chỉ marshal mutation). */
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 10240;
    lvgl_cfg.task_priority = DISPLAY_TASK_PRI;   /* 6 — trên touch/btn/main (5), dưới audio (7) */
    lvgl_cfg.task_affinity = DISPLAY_TASK_CORE;  /* 0 — Core UI, rời Core IO/audio/WiFi */
    esp_err_t lvgl_err = lvgl_port_init(&lvgl_cfg);
    if (lvgl_err != ESP_OK) {
        display_record_status(lvgl_err, "lvgl_port_init");
        ESP_RETURN_ON_ERROR(lvgl_err, TAG_UI, "lvgl_port_init lỗi");
    }

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size =
#if BOARD_LCD_USE_MIPI_DSI
            /* DPI video mode: port doi buffer thang vao frame buffer cua panel,
             * buffer phai phu TOAN man (480*800*2 = 768KB, nam PSRAM). */
            BOARD_LCD_H_RES * BOARD_LCD_V_RES,
#elif defined(BOARD_LCD_USE_ST77922) && BOARD_LCD_USE_ST77922
            /* full_refresh cần buffer full-screen: panel ST77922 QSPI hỏng khi flush
             * vùng partial nhỏ (số đếm cập nhật → sọc xanh); luôn vẽ full màn như demo. */
            BOARD_LCD_H_RES * BOARD_LCD_V_RES,
#else
            BOARD_LCD_H_RES * LCD_DRAW_BUF_LINES,
#endif
        .double_buffer =
#if BOARD_LCD_USE_MIPI_DSI
            true,
#elif (defined(BOARD_LCD_USE_ST7796S) && BOARD_LCD_USE_ST7796S) || \
    (defined(BOARD_LCD_USE_ST77922) && BOARD_LCD_USE_ST77922)
            /* ST77922: 1 buffer + rotate-buffer là đủ (full_refresh); 2 buffer dễ
             * đua ghi đè rotate-buffer khi DMA đang đọc + tốn 307KB PSRAM. */
            false,
#else
            true,
#endif
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .monochrome = false,
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .rotation = {
            .swap_xy = BOARD_LCD_SWAP_XY,
            .mirror_x = BOARD_LCD_MIRROR_X,
            .mirror_y = BOARD_LCD_MIRROR_Y,
        },
        .flags = {
#if BOARD_LCD_USE_MIPI_DSI
            /* Panel DPI tu DMA tu frame buffer cua no; buffer LVGL chi la RAM
             * thuong nen de PSRAM (768KB x2 khong vua DRAM noi). Khi chong xe
             * hinh, port BO QUA hai co nay va dung thang 2 fb cua driver DPI. */
            .buff_dma = false,
            .buff_spiram = true,
            /* direct mode: LVGL chi ve vung ban, tu dong bo vung ban cua khung
             * truoc sang fb kia (refr_sync_areas). full_refresh se rasterize ca
             * 480x800 moi khung — dat hon nhieu. */
            .direct_mode = (BOARD_LCD_DSI_AVOID_TEARING || DSI_ROTATE) ? true : false,
#else
            .buff_dma = true,
#if BOARD_LCD_USE_I80
            /* i80 LCD_CAM flush ổn định nhất với internal DMA RAM (buffer nhỏ). */
            .buff_spiram = false,
#else
            .buff_spiram =
#if defined(BOARD_LCD_USE_ST7796S) && BOARD_LCD_USE_ST7796S
                false,
#else
                true,
#endif
#endif
#endif /* BOARD_LCD_USE_MIPI_DSI */
#if BOARD_LCD_SWAP_BYTES
            .swap_bytes = true,
#endif
#if defined(BOARD_LCD_USE_ST77922) && BOARD_LCD_USE_ST77922
            /* Luôn vẽ lại TOÀN màn — panel này hỏng partial-flush vùng nhỏ (số đếm/
             * phụ đề → glitch xanh). Demo chính chủ cũng full_refresh=1. */
            .full_refresh = true,
#endif
        },
    };
#if defined(BOARD_LCD_USE_ST77922) && BOARD_LCD_USE_ST77922
    /* LANDSCAPE: chuẩn bị strip buffer TRƯỚC, rồi GIỮ LOCK (recursive) xuyên suốt
     * add_disp → override flush, để taskLVGL không kịp flush bằng cb mặc định của
     * port (cb đó draw thẳng từ PSRAM → NO_MEM → treo wait_for_flushing). */
    s_st77922_panel = panel;
    s_st77922_flush_sem = xSemaphoreCreateBinary();
    s_st77922_strip = heap_caps_aligned_alloc(
        64, ST77922_NATIVE_W * ST77922_STRIP_ROWS * 2,
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_st77922_strip || !s_st77922_flush_sem) {
        ESP_LOGE(TAG_UI, "st77922 strip buffer alloc fail");
        display_record_status(ESP_ERR_NO_MEM, "st77922_strip");
        return ESP_ERR_NO_MEM;
    }
    lvgl_port_lock(0);
#endif
#if BOARD_LCD_USE_MIPI_DSI
    /* avoid_tearing: port lay 2 fb qua esp_lcd_dpi_panel_get_frame_buffer(),
     * dang ky on_refresh_done thay cho on_color_trans_done, va flush cuoi cung
     * cho toi khi DMA doi sang fb moi. Can num_fbs = 2 o dpi_cfg (da dat tren).
     * Khi XOAY: avoid_tearing=false de port cap 2 buffer LVGL logical rieng (khong
     * dung fb), viec doi khung + vsync do dsi_rot_flush_cb lam. */
    const lvgl_port_display_dsi_cfg_t dsi_disp_cfg = {
        .flags = { .avoid_tearing = (BOARD_LCD_DSI_AVOID_TEARING && !DSI_ROTATE) ? true : false },
    };
#if DSI_ROTATE
    /* Giu lock xuyen add_disp -> doi flush cb (nhu ST77922): flush mac dinh cua
     * port o che do nay se take semaphore NULL -> crash neu task LVGL kip chay. */
    lvgl_port_lock(0);
#endif
    s_lvgl_display = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_disp_cfg);
#if DSI_ROTATE
    if (s_lvgl_display) {
        esp_err_t rot_err = dsi_rot_attach(panel, s_lvgl_display);
        if (rot_err != ESP_OK) {
            lvgl_port_unlock();
            display_record_status(rot_err, "dsi_rotate");
            return rot_err;
        }
    }
    lvgl_port_unlock();
#endif
#else
    s_lvgl_display = lvgl_port_add_disp(&disp_cfg);
#endif
    if (!s_lvgl_display) {
#if defined(BOARD_LCD_USE_ST77922) && BOARD_LCD_USE_ST77922
        lvgl_port_unlock();
#endif
        ESP_LOGE(TAG_UI, "LVGL display add failed");
        display_record_status(ESP_FAIL, "lvgl_add_display");
        return ESP_FAIL;
    }
    lv_display_add_event_cb(s_lvgl_display, display_flush_count_cb, LV_EVENT_FLUSH_FINISH, NULL);
#if BOARD_LCD_USE_MIPI_DSI
    ESP_LOGI(TAG_UI, "DSI: avoid_tearing=%d num_fbs=%d direct_mode=%d rotation=%d",
             (int)(BOARD_LCD_DSI_AVOID_TEARING || DSI_ROTATE), (int)dpi_cfg.num_fbs,
             (int)(BOARD_LCD_DSI_AVOID_TEARING || DSI_ROTATE), (int)BOARD_LCD_ROTATION);
#endif
#if defined(BOARD_LCD_USE_ST77922) && BOARD_LCD_USE_ST77922
    lv_display_set_flush_cb(s_lvgl_display, st77922_landscape_flush_cb);
    /* Đăng ký io done cb SAU add_disp — add_disp tự đăng ký cb của port, đăng ký
     * trước sẽ bị ĐÈ → semaphore không ai give → mỗi strip chờ timeout 1s. */
    const esp_lcd_panel_io_callbacks_t st_cbs = {
        .on_color_trans_done = st77922_strip_done_cb,
    };
    esp_err_t st_cb_err = esp_lcd_panel_io_register_event_callbacks(io, &st_cbs, NULL);
    lvgl_port_unlock();
    if (st_cb_err != ESP_OK) {
        ESP_LOGE(TAG_UI, "st77922 io cb lỗi: %s", esp_err_to_name(st_cb_err));
        display_record_status(st_cb_err, "st77922_io_cb");
        return st_cb_err;
    }
#endif

    s_display.panel = panel;
    s_display.lcd_width = BOARD_LCD_H_RES;
    s_display.lcd_height = BOARD_LCD_V_RES;
    s_display.backlight_percent = 100;
    s_display.sleep_timeout_sec = 0;
    s_display.last_activity_us = esp_timer_get_time();
    s_display.backlight_sleeping = false;
    s_display.hw_ready = true;
    display_record_status(ESP_OK, "ok");
    display_set_backlight(100);

    /* Schedule queue + drain task. KHỞI TẠO TRƯỚC setup_ui để API
     * có thể queue-up cuộc gọi từ rất sớm (sẽ drain sau setup_ui). */
    s_display.sched_q = xQueueCreate(DISPLAY_SCHED_Q_LEN, sizeof(display_sched_item_t));
    configASSERT(s_display.sched_q);
    BaseType_t r = xTaskCreatePinnedToCore(display_task, "vimate_disp",
        DISPLAY_TASK_STACK, NULL, DISPLAY_TASK_PRI, &s_display.task, DISPLAY_TASK_CORE);
    configASSERT(r == pdPASS);

    ESP_LOGI(TAG_UI, "Display HW ready %dx%d (BL=100%%)",
             BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return ESP_OK;
}

void display_set_backlight(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_display.backlight_percent = percent;
    s_display.backlight_sleeping = false;
    s_display.last_activity_us = esp_timer_get_time();
    display_apply_backlight_hw(percent);
}

void display_set_sleep_timeout(int seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds > 7200) seconds = 7200;
    s_display.sleep_timeout_sec = seconds;
    s_display.last_activity_us = esp_timer_get_time();
    if (seconds == 0 && s_display.backlight_sleeping) {
        s_display.backlight_sleeping = false;
        display_apply_backlight_hw(s_display.backlight_percent);
    }
}

esp_lcd_panel_handle_t display_lcd_panel_handle(void) {
    return s_display.panel;
}

int display_lcd_width(void) {
    return s_display.lcd_width;
}

int display_lcd_height(void) {
    return s_display.lcd_height;
}

bool display_hw_ready(void) {
    return s_display.hw_ready;
}

bool display_hdmi_ready(void) {
    return false;
}

const char *display_runtime_status(void) {
    return s_display.init_status[0] ? s_display.init_status : "not_started";
}

void display_lock(void)   { lvgl_port_lock(0); }
void display_unlock(void) { lvgl_port_unlock(); }

static volatile uint32_t s_flush_count = 0;
static void display_flush_count_cb(lv_event_t *e) { (void)e; s_flush_count++; }
uint32_t display_flush_count(void) { return s_flush_count; }

bool display_rotate_stats(uint32_t *full_frames, uint32_t *area_blits, uint32_t *full_us_max)
{
#if BOARD_LCD_USE_MIPI_DSI && DSI_ROTATE
    if (full_frames) *full_frames = s_rot.stat_full;
    if (area_blits) *area_blits = s_rot.stat_area;
    if (full_us_max) *full_us_max = (uint32_t)s_rot.stat_full_us_max;
    return true;
#else
    (void)full_frames; (void)area_blits; (void)full_us_max;
    return false;
#endif
}

esp_err_t display_panel_blit(int x, int y, int w, int h, const void *rgb565)
{
    if (!s_display.panel || !rgb565 || w <= 0 || h <= 0) return ESP_ERR_INVALID_ARG;
#if BOARD_LCD_USE_MIPI_DSI && DSI_ROTATE
    return dsi_rot_blit(x, y, w, h, rgb565);
#elif BOARD_LCD_USE_MIPI_DSI
    if (BOARD_LCD_DSI_AVOID_TEARING) {
        void *fb[2] = {0};
        esp_err_t err = esp_lcd_dpi_panel_get_frame_buffer(s_display.panel, 2, &fb[0], &fb[1]);
        if (err != ESP_OK) return err;
        /* cắt vào màn */
        const int x0 = x < 0 ? 0 : x;
        const int y0 = y < 0 ? 0 : y;
        const int x1 = (x + w > BOARD_LCD_H_RES) ? BOARD_LCD_H_RES : x + w;
        const int y1 = (y + h > BOARD_LCD_V_RES) ? BOARD_LCD_V_RES : y + h;
        if (x1 <= x0 || y1 <= y0) return ESP_OK;
        const size_t bpp = 2;
        const size_t src_stride = (size_t)w * bpp;
        const size_t dst_stride = (size_t)BOARD_LCD_H_RES * bpp;
        const size_t row_bytes = (size_t)(x1 - x0) * bpp;
        for (int i = 0; i < 2; i++) {
            uint8_t *dst = (uint8_t *)fb[i] + (size_t)y0 * dst_stride + (size_t)x0 * bpp;
            const uint8_t *src = (const uint8_t *)rgb565
                               + (size_t)(y0 - y) * src_stride + (size_t)(x0 - x) * bpp;
            for (int r = y0; r < y1; r++) {
                memcpy(dst, src, row_bytes);
                dst += dst_stride;
                src += src_stride;
            }
            /* DMA của DPI đọc PSRAM không qua cache CPU -> phải write-back, giống
             * chính driver làm trong dpi_panel_draw_bitmap(). */
            esp_cache_msync((uint8_t *)fb[i] + (size_t)y0 * dst_stride,
                            (size_t)(y1 - y0) * dst_stride,
                            ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        }
        return ESP_OK;
    }
#endif
    return esp_lcd_panel_draw_bitmap(s_display.panel, x, y, x + w, y + h, rgb565);
}

/* ===== State label text mapping ===== */
static const char *state_text(vimate_dev_state_t s) {
    switch (s) {
        case DEV_STATE_BOOT:               return "Khởi động...";
        case DEV_STATE_WIFI_PROVISIONING:  return "Cài đặt WiFi";
        case DEV_STATE_WIFI_CONNECTING:    return "Đang kết nối WiFi";
        case DEV_STATE_OTA_CHECKING:       return "Kiểm tra cập nhật";
        case DEV_STATE_NOT_ACTIVATED:      return "Chưa kích hoạt";
        case DEV_STATE_NO_PLAN:            return "Chưa có gói";
        case DEV_STATE_READY:              return "Sẵn sàng";
        case DEV_STATE_LISTENING:          return "Đang nghe...";
        case DEV_STATE_THINKING:           return "Đang nghĩ...";
        case DEV_STATE_SPEAKING:           return "Đang nói...";
        case DEV_STATE_ERROR:              return "Đang kết nối lại";
        default:                           return "";
    }
}
static uint32_t state_color(vimate_dev_state_t s) {
    switch (s) {
        case DEV_STATE_LISTENING: return 0x16a34a;
        case DEV_STATE_SPEAKING:  return 0x2563eb;
        case DEV_STATE_ERROR:     return 0xdc2626;
        case DEV_STATE_READY:     return 0x7c3aed;
        default:
            return 0x1f2937;
    }
}


/* ========== EDU home grid (Pha C) — Home iOS 26 "Liquid Glass" ========== */
#define HOME_ICON_SZ   72   /* cạnh squircle icon */
#define HOME_CELL_W    108  /* bề rộng 1 ô (icon + nhãn) */
#define HOME_CELL_H    104  /* chiều cao 1 ô */
#define HOME_COURSE_CONTENT_W (BOARD_LCD_H_RES * 3 / 4)
#define HOME_COURSE_CONTENT_H (BOARD_LCD_V_RES * 3 / 5)
#define HOME_COURSE_CONTENT_X ((BOARD_LCD_H_RES - HOME_COURSE_CONTENT_W) / 2)
#define HOME_COURSE_CONTENT_Y 6
#define HOME_COURSE_COLUMN_GAP 8
#define HOME_COURSE_COVER_W    (HOME_COURSE_CONTENT_W * 43 / 100)
#define HOME_COURSE_INFO_W     (HOME_COURSE_CONTENT_W - HOME_COURSE_COVER_W - HOME_COURSE_COLUMN_GAP)

/* Số cột responsive theo bề ngang màn (3 / 4 / 6). */
static int home_cols(void) {
    if (BOARD_LCD_H_RES >= 600) return 6;
    if (BOARD_LCD_H_RES >= 440) return 4;
    return 3;
}

static void home_make_blob(lv_obj_t *parent, int sz, uint32_t color,
                           lv_align_t al, int x, int y) {
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_set_size(b, sz, sz);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_30, 0); /* mờ → cảm giác glass */
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(b, al, x, y);
}

/* Giờ HH:MM cho header home. Nguồn ưu tiên = server_time (OTA đẩy, khớp múi giờ
 * VN qua clock_base_local — như màn Đồng hồ); dự phòng SNTP (app_main). Trả false
 * nếu chưa có nguồn nào (giữ "--:--"). Tránh hiện giờ 1970 sai. */
static bool home_now_hhmm(char *buf, size_t n) {
    time_t local;
    if (s_display.clock_base_local > 0) {
        int64_t elapsed = (esp_timer_get_time() - s_display.clock_base_us) / 1000000;
        local = (time_t)(s_display.clock_base_local + elapsed);
    } else {
        time_t now = time(NULL);
        if (now < 1735689600) return false;   /* SNTP chưa sync */
        local = now + 7 * 3600;                 /* ponytail: VN UTC+7 cố định (no DST) */
    }
    struct tm tmv;
    gmtime_r(&local, &tmv);
    snprintf(buf, n, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    return true;
}
/* OTA đẩy server_time → set mốc giờ (dùng chung với màn Đồng hồ). Gọi từ OTA task. */
void display_set_server_time(int64_t epoch_ms, int tz_offset_min) {
    if (epoch_ms < 1735689600000LL) return;   /* < 2025 → bỏ (giá trị rác) */
    display_lock();
    s_display.clock_base_local = epoch_ms / 1000 + (int64_t)tz_offset_min * 60;
    s_display.clock_base_us = esp_timer_get_time();
    display_unlock();
}
static void home_clock_render(void) {
    if (!s_display.home_clock_lbl) return;
    char hhmm[8];
    if (home_now_hhmm(hhmm, sizeof(hhmm)))
        lv_label_set_text(s_display.home_clock_lbl, hhmm);
}
static void home_clock_timer_cb(lv_timer_t *t) {
    (void)t;
    if (s_display.home_panel &&
        !lv_obj_has_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN))
        home_clock_render();
}

/* Giờ local (tm) cho đồng hồ chờ — cùng nguồn với header (server_time/SNTP). */
static bool home_now_tm(struct tm *out) {
    time_t local;
    if (s_display.clock_base_local > 0) {
        int64_t elapsed = (esp_timer_get_time() - s_display.clock_base_us) / 1000000;
        local = (time_t)(s_display.clock_base_local + elapsed);
    } else {
        time_t now = time(NULL);
        if (now < 1735689600) return false;
        local = now + 7 * 3600; /* ponytail: VN UTC+7 */
    }
    gmtime_r(&local, out);
    return true;
}
/* Đồng hồ màn chờ chạy độc lập với slideshow. Home được giữ nguyên phía dưới để
 * lần chạm đầu chỉ đánh thức, không phải tải lại dữ liệu và không hit xuyên. */
static bool device_rest_scene(void) {
    bool popup_visible = s_display.popup_panel &&
                         !lv_obj_has_flag(s_display.popup_panel, LV_OBJ_FLAG_HIDDEN);
    int idle_after = s_display.ss_idle_after_sec > 0 ? s_display.ss_idle_after_sec : 60;
    int64_t idle_us = esp_timer_get_time() - s_display.last_activity_us;
    bool home_visible = display_home_visible();
    bool slideshow_will_run = home_visible && s_display.ss_enabled &&
                              s_display.ss_count > 0;
    return g_vimate_server.activated &&
           g_vimate_server.device_token[0] != '\0' &&
           idle_us >= (int64_t)idle_after * 1000000LL &&
           !popup_visible &&
           !display_ai_active() && !s_display.ss_active && !slideshow_will_run &&
           !display_stats_visible() && !display_progress_visible() &&
           !display_water_visible() && !display_timetable_visible() &&
           !display_countdown_visible() && !display_clock_visible() &&
           !display_quiz_visible() && !display_alarm_visible();
}
static void idle_clock_render(void) {
    struct tm tmv;
    if (!home_now_tm(&tmv)) return;
    lv_label_set_text_fmt(s_display.idle_clock_time, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    static const char *const wd[7] = {"Chủ nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư",
                                      "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};
    int w = tmv.tm_wday; if (w < 0 || w > 6) w = 0;
    int yr = tmv.tm_year + 1900; if (yr < 2000 || yr > 2099) yr = 2000;
    lv_label_set_text(s_display.idle_clock_weekday, wd[w]);
    lv_label_set_text_fmt(s_display.idle_clock_date, "%02d tháng %02d năm %04d",
                          tmv.tm_mday, tmv.tm_mon + 1, yr);
}
/* Giữ API cũ để tương thích server cũ; màn chờ mới luôn đen/trắng. */
void display_set_clock_bg(bool on) { (void)on; }
static void idle_clock_tick_cb(lv_timer_t *t) {
    (void)t;
    if (!s_display.idle_clock_panel) return;
    if (device_rest_scene()) {
        bool entering = !s_display.idle_clock_active;
        if (s_display.emoji_image) lv_obj_add_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
        if (s_display.emoji_gif) lv_obj_add_flag(s_display.emoji_gif, LV_OBJ_FLAG_HIDDEN);
#if BOARD_FACE_GIF_FULLSCREEN
        face_set_visible_locked(false);   /* dừng decode khi đồng hồ che mặt */
#endif
        idle_clock_render();
        s_display.idle_clock_active = true;
        if (s_display.bottom_bar) lv_obj_add_flag(s_display.bottom_bar, LV_OBJ_FLAG_HIDDEN);
        if (s_display.nav_home_btn) lv_obj_add_flag(s_display.nav_home_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_display.idle_clock_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_display.idle_clock_panel);
        if (entering) {
            int64_t idle_s = (esp_timer_get_time() - s_display.last_activity_us) / 1000000LL;
            ESP_LOGI(TAG_UI, "idle clock enter: idle=%llds home=%d",
                     (long long)idle_s, display_home_visible());
        }
    } else {
        idle_clock_hide_locked();
    }
}
static void idle_clock_create(lv_obj_t *screen) {
    lv_obj_t *p = lv_obj_create(screen);
    s_display.idle_clock_panel = p;
    lv_obj_set_size(p, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_align(p, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_color(p, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    s_display.idle_clock_time = lv_label_create(p);
    lv_label_set_text(s_display.idle_clock_time, "--:--");
    lv_obj_set_style_text_font(s_display.idle_clock_time, &lv_font_vimate_48, 0);
    lv_obj_set_style_text_color(s_display.idle_clock_time, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_display.idle_clock_time, LV_ALIGN_CENTER, 0, -62);
    s_display.idle_clock_weekday = lv_label_create(p);
    lv_label_set_text(s_display.idle_clock_weekday, "");
    lv_obj_set_style_text_font(s_display.idle_clock_weekday, &lv_font_vimate_24, 0);
    lv_obj_set_style_text_color(s_display.idle_clock_weekday, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_display.idle_clock_weekday, LV_ALIGN_CENTER, 0, 10);
    s_display.idle_clock_date = lv_label_create(p);
    lv_label_set_text(s_display.idle_clock_date, "");
    lv_obj_set_style_text_font(s_display.idle_clock_date, &lv_font_vimate_24, 0);
    lv_obj_set_style_text_color(s_display.idle_clock_date, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_display.idle_clock_date, LV_ALIGN_CENTER, 0, 52);
    lv_timer_create(idle_clock_tick_cb, 1000, NULL); /* cập nhật + arbitrate rest/talking */
}

/* Nút "Trang chủ" nổi ở đáy — hiện trên MỌI màn con (Lộ trình/Thống kê/Lịch/
 * Đếm ngược/Đồng hồ). Poll theo màn đang hiện (đỡ phải sửa từng apply_*). */
/* Bottom bar (mockup màn 1): 4 app cố định. id gửi home_select khi chạm.
 * (Cài đặt BỎ — chỉnh trong app phụ huynh, trẻ không tự sửa.) */
#define HOME_BAR_N 4
#define HOME_BAR_H          64   /* 13/09: 54 → 64 (ô 62 px ≈ 7,3 mm + slop 10 px) */
#define HOME_BAR_RESERVED_H (HOME_BAR_H + 14)
#define AGENT_CHAT_BOTTOM_GAP 6
static const char *const kHomeBarIds[HOME_BAR_N] = {
    "app_schedule", "app_timer", "app_water", "app_agents"};

static void nav_home_tick_cb(lv_timer_t *t) {
    (void)t;
    if (!s_display.nav_home_btn) return;
    /* Bottom bar + nút Home GIỮA — hiện trên menu/tiện ích và suốt
     * phiên Agent. Bài học/quiz vẫn toàn màn để không che nội dung. */
    /* Popup/thông báo (display_set_message: nhắc, âm lượng, "Con vừa nói"...) +
     * màn Nhắc (alarm) là MODAL → ẩn bar để không đè text (user báo 05/07). */
    bool popup = s_display.popup_panel &&
                 !lv_obj_has_flag(s_display.popup_panel, LV_OBJ_FLAG_HIDDEN);
    bool idle_clock = s_display.idle_clock_panel &&
                      !lv_obj_has_flag(s_display.idle_clock_panel, LV_OBJ_FLAG_HIDDEN);
    bool agent = display_agent_active();
    bool need = !idle_clock && !display_alarm_visible() &&
                (agent || (!popup &&
                 (display_home_visible() || display_progress_visible() ||
                  display_stats_visible() || display_countdown_visible() ||
                  display_clock_visible() || display_water_visible())));
    if (need) {
        if (s_display.bottom_bar) {
            lv_obj_clear_flag(s_display.bottom_bar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_display.bottom_bar);
        }
        lv_obj_clear_flag(s_display.nav_home_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_display.nav_home_btn);
    } else {
        if (s_display.bottom_bar) lv_obj_add_flag(s_display.bottom_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_display.nav_home_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_display.voice_state_panel &&
        !lv_obj_has_flag(s_display.voice_state_panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_move_foreground(s_display.voice_state_panel);
    }
}
/* Bottom bar GLOBAL (con của screen): 4 app + chừa ô giữa cho nút Home tròn. */
static void bottombar_create(lv_obj_t *screen) {
    static const char *const kBarSym[HOME_BAR_N] = {
        LV_SYMBOL_LIST, LV_SYMBOL_BELL, LV_SYMBOL_TINT, LV_SYMBOL_CALL};
    static const char *const kBarLbl[HOME_BAR_N] = {
        "Lịch học", "Hẹn giờ", "Uống nước", "AI Agent"};
    lv_obj_t *bar = lv_obj_create(screen);
    s_display.bottom_bar = bar;
    lv_obj_set_size(bar, BOARD_LCD_H_RES - 16, HOME_BAR_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_radius(bar, 22, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_90, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    UI_SET_SHADOW_WIDTH(bar, 12, 0);
    UI_SET_SHADOW_OFS_Y(bar, -2, 0);
    UI_SET_SHADOW_OPA(bar, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    /* 5 slot: 4 app ở slot 0,1,3,4; slot 2 (GIỮA) chừa cho nút Home tròn. */
    int bw = (BOARD_LCD_H_RES - 16) / 5;
    for (int i = 0; i < HOME_BAR_N; i++) {
        int slot = (i < 2) ? i : i + 1;
        lv_obj_t *cell = lv_obj_create(bar);
        lv_obj_set_size(cell, bw, HOME_BAR_H - 2);
        lv_obj_align(cell, LV_ALIGN_LEFT_MID, slot * bw, 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_set_style_radius(cell, 18, 0);
        /* PRESSED: nền xanh nhạt hiện ngay khi ngón chạm (display_touch_feedback). */
        lv_obj_set_style_bg_color(cell, lv_color_hex(UI_CLR_PRIMARY_SOFT), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *ic = lv_label_create(cell);
        lv_label_set_text(ic, kBarSym[i]);
        lv_obj_set_style_text_color(ic, lv_color_hex(UI_CLR_PRIMARY), 0);
        lv_obj_set_style_text_font(ic, UI_FONT_SYMBOL, 0);
        lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 7);
        /* 13/09: nhãn 14 → 18 px (1,6 → 2,1 mm) — tối thiểu cho chữ có thể chạm
         * (MASTER.md §3). Ô cao 62: icon 7..25, nhãn 18 px ở 35..56 → còn cách 10 px. */
        lv_obj_t *tx = lv_label_create(cell);
        lv_label_set_text(tx, kBarLbl[i]);
        lv_obj_set_style_text_color(tx, lv_color_hex(UI_CLR_PRIMARY_DARK), 0);
        lv_obj_set_style_text_font(tx, UI_FONT_LABEL, 0);
        lv_obj_align(tx, LV_ALIGN_BOTTOM_MID, 0, -6);
        s_display.home_bar_btns[i] = cell;
    }
}
static void nav_home_create(lv_obj_t *screen) {
    /* Nút Home tròn ở CHÍNH GIỮA đáy (khớp ô giữa bottom bar), nhô nhẹ (FAB). */
    lv_obj_t *b = lv_obj_create(screen);
    s_display.nav_home_btn = b;
    /* 68 px ≈ 8 mm (+10 px slop hit-test mỗi cạnh ≈ 10,4 mm) — trước 58 px = 6,8 mm,
     * dưới chuẩn 48 dp. */
    lv_obj_set_size(b, 68, 68);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    /* 13/09: nền 22c55e → 15803d: icon trắng trên 22c55e chỉ 2,3:1, trên 15803d
     * 5,0:1 (MASTER.md §2.3). Nhấn → tối thêm một nấc. */
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_CLR_SUCCESS), 0);
    lv_obj_set_style_bg_grad_color(b, lv_color_hex(UI_CLR_SUCCESS_PRESSED), 0);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_CLR_SUCCESS_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_color(b, lv_color_hex(UI_CLR_SUCCESS_DEEP), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 3, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(UI_CLR_ON_PRIMARY), 0);
    UI_SET_SHADOW_WIDTH(b, 12, 0);
    UI_SET_SHADOW_OPA(b, LV_OPA_40, 0);
    UI_SET_SHADOW_COLOR(b, lv_color_hex(UI_CLR_SUCCESS), 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    /* Chỉ icon nhà (bỏ chữ "Home" — bottom bar đã rõ; đỡ đè nội dung). */
    lv_obj_t *ic = lv_label_create(b);
    lv_label_set_text(ic, LV_SYMBOL_HOME);
    lv_obj_set_style_text_color(ic, lv_color_hex(UI_CLR_ON_PRIMARY), 0);
    lv_obj_set_style_text_font(ic, UI_FONT_SYMBOL, 0);
    lv_obj_center(ic);
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(nav_home_tick_cb, 300, NULL);
}
/* ========== Hit-test chạm ==========
 * Màn 4.3" 800×480 ≈ 217 ppi → 1 mm ≈ 8,5 px. Mục tiêu chạm chuẩn 48 dp (Android)
 * ≈ 9 mm ≈ 77 px; trẻ em cần rộng hơn. Nút vẽ nhỏ hơn thế thì hit-test nới thêm
 * TOUCH_HIT_SLOP mỗi cạnh — ngón đè mép vẫn trúng, không cần vẽ nút to xấu.
 * Chỉ nới khi các mục tiêu không kề sát (bar/quiz/water có gap ≥ 12 px). */
#define TOUCH_HIT_SLOP 10
static bool hit_in(lv_obj_t *o, int x, int y, int slop) {
    if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    return x >= a.x1 - slop && x <= a.x2 + slop && y >= a.y1 - slop && y <= a.y2 + slop;
}

bool display_nav_home_hit(int x, int y) {
    display_lock();
    bool hit = hit_in(s_display.nav_home_btn, x, y, TOUCH_HIT_SLOP);
    display_unlock();
    return hit;
}

/* Nút/ô có thể bấm dưới (x,y) — cùng thứ tự ưu tiên với touch.c (nav Home → bar →
 * quiz → water → course nav → ô Home). Trả NULL nếu chạm vào chỗ không bấm được. */
static lv_obj_t *pressable_at(int x, int y) {
    if (hit_in(s_display.nav_home_btn, x, y, TOUCH_HIT_SLOP)) return s_display.nav_home_btn;
    if (s_display.bottom_bar && !lv_obj_has_flag(s_display.bottom_bar, LV_OBJ_FLAG_HIDDEN)) {
        for (int i = 0; i < HOME_BAR_N; i++)
            if (hit_in(s_display.home_bar_btns[i], x, y, TOUCH_HIT_SLOP)) return s_display.home_bar_btns[i];
    }
    if (s_display.quiz_panel && !lv_obj_has_flag(s_display.quiz_panel, LV_OBJ_FLAG_HIDDEN)) {
        for (int i = 0; i < s_display.quiz_btn_count && i < 4; i++)
            if (hit_in(s_display.quiz_btns[i], x, y, TOUCH_HIT_SLOP)) return s_display.quiz_btns[i];
    }
    if (s_display.water_panel && !lv_obj_has_flag(s_display.water_panel, LV_OBJ_FLAG_HIDDEN)) {
        for (int i = 0; i < 3; i++)
            if (hit_in(s_display.water_btns[i], x, y, TOUCH_HIT_SLOP)) return s_display.water_btns[i];
    }
    if (s_display.home_panel && !lv_obj_has_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN)) {
        if (s_display.home_course_mode) {
            if (hit_in(s_display.home_course_prev, x, y, TOUCH_HIT_SLOP)) return s_display.home_course_prev;
            if (hit_in(s_display.home_course_next, x, y, TOUCH_HIT_SLOP)) return s_display.home_course_next;
            if (hit_in(s_display.home_course_panel, x, y, 0)) return s_display.home_course_panel;
        } else {
            for (int i = 0; i < s_display.home_card_count && i < VIMATE_HOME_MAX; i++)
                if (hit_in(s_display.home_cards[i], x, y, 0)) return s_display.home_cards[i];
        }
    }
    return NULL;
}

static lv_obj_t *s_pressed_obj;
/* Chạy trong display task (đã có lock). arg: bit31 = pressed, [30:16] = x, [15:0] = y. */
static void apply_touch_feedback(void *arg) {
    uint32_t v = (uint32_t)(uintptr_t)arg;
    bool pressed = (v >> 31) & 1;
    int x = (int)((v >> 16) & 0x7fff), y = (int)(v & 0xffff);
    display_lock();
    if (s_pressed_obj && lv_obj_is_valid(s_pressed_obj)) {
        lv_obj_remove_state(s_pressed_obj, LV_STATE_PRESSED);
    }
    s_pressed_obj = NULL;
    if (pressed) {
        lv_obj_t *o = pressable_at(x, y);
        if (o) {
            lv_obj_add_state(o, LV_STATE_PRESSED);
            s_pressed_obj = o;
        }
    }
    display_unlock();
}

bool display_touch_target_at(int x, int y) {
    if (!s_display.setup_ui_called) return false;
    display_lock();
    bool hit = pressable_at(x, y) != NULL;
    display_unlock();
    return hit;
}

/* KHÔNG lấy lock LVGL trong touch task: lúc emoji đang render, taskLVGL giữ lock hàng
 * chục ms mỗi khung → touch task đứng chờ, mất mẫu (log 13/09: chạm 200 ms chỉ 1 mẫu).
 * Đẩy qua hàng đợi display; display task áp khi tới lượt (vài ms sau, mắt không thấy). */
void display_touch_feedback(int x, int y, bool pressed) {
    if (!s_display.setup_ui_called || !s_display.sched_q) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    uint32_t v = ((uint32_t)(pressed ? 1u : 0u) << 31) | ((uint32_t)(x & 0x7fff) << 16) | (uint32_t)(y & 0xffff);
    display_schedule(apply_touch_feedback, (void *)(uintptr_t)v);
}

static void home_create_ui(lv_obj_t *screen) {
    static const uint32_t kHomeColors[VIMATE_HOME_MAX] = {
        0x6366f1 /* indigo */, 0x22c55e /* green */,
        0xf59e0b /* amber */,  0xec4899 /* pink */,
        0x0ea5e9 /* sky */,    0xf43f5e /* rose */,
        0x14b8a6 /* teal */,   0x8b5cf6 /* violet */,
    };

    s_display.home_panel = lv_obj_create(screen);
    lv_obj_set_size(s_display.home_panel, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_align(s_display.home_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    /* Nền "liquid glass": gradient rất sáng, dịu. */
    lv_obj_set_style_bg_color(s_display.home_panel, lv_color_hex(0xe8eefc), 0);
    lv_obj_set_style_bg_grad_color(s_display.home_panel, lv_color_hex(0xfdfdff), 0);
    lv_obj_set_style_bg_grad_dir(s_display.home_panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_display.home_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_display.home_panel, 0, 0);
    lv_obj_set_style_pad_all(s_display.home_panel, 0, 0);
    lv_obj_clear_flag(s_display.home_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);

    /* Vệt sáng trang trí (depth kính). */
    home_make_blob(s_display.home_panel, 240, 0x93c5fd, LV_ALIGN_TOP_LEFT, -70, -80);
    home_make_blob(s_display.home_panel, 200, 0xc4b5fd, LV_ALIGN_BOTTOM_RIGHT, 60, 70);

    /* Header top-bar (mockup màn 1): chào bé (trái) · giờ (giữa) · sao (phải). */
    /* -- Pill CHÀO (trái), kính mờ. */
    lv_obj_t *pill = lv_obj_create(s_display.home_panel);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, 34);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_70, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_pad_hor(pill, 16, 0);
    lv_obj_set_style_pad_ver(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(pill, LV_ALIGN_TOP_LEFT, 10, 8);
    lv_obj_t *pill_t = lv_label_create(pill);
    lv_label_set_text(pill_t, VIMATE_BRAND_NAME);
    lv_obj_set_style_text_color(pill_t, lv_color_hex(0x1e3a8a), 0);
    lv_obj_set_style_text_font(pill_t, &lv_font_vimate_18, 0);
    lv_obj_center(pill_t);
    s_display.home_pill_lbl = pill_t;

    /* -- Pill GIỜ HH:MM (giữa). */
    lv_obj_t *clk = lv_obj_create(s_display.home_panel);
    lv_obj_set_size(clk, LV_SIZE_CONTENT, 34);
    lv_obj_set_style_radius(clk, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(clk, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(clk, LV_OPA_60, 0);
    lv_obj_set_style_border_width(clk, 0, 0);
    lv_obj_set_style_pad_hor(clk, 14, 0);
    lv_obj_set_style_pad_ver(clk, 0, 0);
    lv_obj_clear_flag(clk, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(clk, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *clk_t = lv_label_create(clk);
    lv_label_set_text(clk_t, "--:--");
    lv_obj_set_style_text_color(clk_t, lv_color_hex(0x334155), 0);
    lv_obj_set_style_text_font(clk_t, &lv_font_vimate_18, 0);
    lv_obj_center(clk_t);
    s_display.home_clock_lbl = clk_t;

    /* -- Badge SAO (phải), nền vàng gợi ngôi sao (font không có glyph ⭐). */
    lv_obj_t *sb = lv_obj_create(s_display.home_panel);
    lv_obj_set_size(sb, LV_SIZE_CONTENT, 34);
    lv_obj_set_style_radius(sb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sb, lv_color_hex(0xfacc15), 0);
    lv_obj_set_style_bg_grad_color(sb, lv_color_hex(0xf59e0b), 0);
    lv_obj_set_style_bg_grad_dir(sb, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(sb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sb, 0, 0);
    UI_SET_SHADOW_WIDTH(sb, 8, 0);
    UI_SET_SHADOW_OPA(sb, LV_OPA_30, 0);
    UI_SET_SHADOW_COLOR(sb, lv_color_hex(0xf59e0b), 0);
    lv_obj_set_style_pad_hor(sb, 14, 0);
    lv_obj_set_style_pad_ver(sb, 0, 0);
    lv_obj_clear_flag(sb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(sb, LV_ALIGN_TOP_RIGHT, -10, 8);
    lv_obj_t *sb_t = lv_label_create(sb);
    lv_label_set_text(sb_t, "0 sao");
    lv_obj_set_style_text_color(sb_t, lv_color_hex(0x7c2d12), 0);
    lv_obj_set_style_text_font(sb_t, &lv_font_vimate_18, 0);
    lv_obj_center(sb_t);
    s_display.home_star_lbl = sb_t;

    /* Khóa học dành cho trẻ: mỗi lần chỉ hiện một bìa thật, tên đầy đủ và vùng
     * chạm lớn. Cách này dễ phân biệt hơn lưới nhiều icon giống nhau, đồng thời
     * không cần giữ nhiều framebuffer cover trong PSRAM. */
    lv_obj_t *course = lv_obj_create(s_display.home_panel);
    s_display.home_course_panel = course;
    lv_obj_set_size(course, BOARD_LCD_H_RES, BOARD_LCD_V_RES - 112);
    lv_obj_align(course, LV_ALIGN_TOP_LEFT, 0, 42);
    lv_obj_set_style_bg_opa(course, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(course, 0, 0);
    lv_obj_set_style_pad_all(course, 0, 0);
    lv_obj_clear_flag(course, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(course, LV_OBJ_FLAG_HIDDEN);

    /* Hai cột: bìa dọc bên trái, thông tin bên phải. Bố cục này tận dụng chiều
     * cao của bìa sách và không để khoảng trắng ngang vô ích. */
    lv_obj_t *cover_frame = lv_obj_create(course);
    s_display.home_course_cover_frame = cover_frame;
    lv_obj_set_size(cover_frame, HOME_COURSE_COVER_W, HOME_COURSE_CONTENT_H);
    lv_obj_align(cover_frame, LV_ALIGN_TOP_LEFT,
                 HOME_COURSE_CONTENT_X, HOME_COURSE_CONTENT_Y);
    lv_obj_set_style_radius(cover_frame, 8, 0);
    lv_obj_set_style_bg_color(cover_frame, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_bg_opa(cover_frame, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cover_frame, 2, 0);
    lv_obj_set_style_border_color(cover_frame, lv_color_hex(0xffffff), 0);
    UI_SET_SHADOW_WIDTH(cover_frame, 10, 0);
    UI_SET_SHADOW_OPA(cover_frame, LV_OPA_20, 0);
    lv_obj_set_style_clip_corner(cover_frame, true, 0);
    lv_obj_set_style_pad_all(cover_frame, 0, 0);
    lv_obj_clear_flag(cover_frame, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cover_image = lv_image_create(cover_frame);
    s_display.home_course_cover_image = cover_image;
    lv_obj_set_size(cover_image, HOME_COURSE_COVER_W, HOME_COURSE_CONTENT_H);
    lv_image_set_inner_align(cover_image, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_center(cover_image);
    lv_obj_clear_flag(cover_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(cover_image, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *placeholder = lv_image_create(cover_frame);
    s_display.home_course_placeholder = placeholder;
    lv_image_set_src(placeholder, &icon_hoc);
    lv_obj_center(placeholder);

    lv_obj_t *info = lv_obj_create(course);
    lv_obj_set_size(info, HOME_COURSE_INFO_W, HOME_COURSE_CONTENT_H);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT,
                 HOME_COURSE_CONTENT_X + HOME_COURSE_COVER_W + HOME_COURSE_COLUMN_GAP,
                 HOME_COURSE_CONTENT_Y);
    lv_obj_set_style_radius(info, 8, 0);
    lv_obj_set_style_bg_color(info, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(info, LV_OPA_90, 0);
    lv_obj_set_style_border_width(info, 2, 0);
    lv_obj_set_style_border_color(info, lv_color_hex(0xffffff), 0);
    UI_SET_SHADOW_WIDTH(info, 12, 0);
    UI_SET_SHADOW_OPA(info, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(info, 0, 0);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);

    s_display.home_course_title = lv_label_create(info);
    lv_label_set_text(s_display.home_course_title, "");
    lv_label_set_long_mode(s_display.home_course_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(s_display.home_course_title, HOME_COURSE_INFO_W - 28, 120);
    lv_obj_set_style_text_font(s_display.home_course_title, &lv_font_vimate_24, 0);
    lv_obj_set_style_text_color(s_display.home_course_title, lv_color_hex(0x111827), 0);
    lv_obj_set_style_text_align(s_display.home_course_title, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(s_display.home_course_title, LV_ALIGN_TOP_LEFT, 14, 14);

    s_display.home_course_subtitle = lv_label_create(info);
    lv_label_set_text(s_display.home_course_subtitle, "");
    lv_label_set_long_mode(s_display.home_course_subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(s_display.home_course_subtitle, HOME_COURSE_INFO_W - 82, 42);
    lv_obj_set_style_text_font(s_display.home_course_subtitle, &lv_font_vimate_14, 0);
    lv_obj_set_style_text_color(s_display.home_course_subtitle, lv_color_hex(0x475569), 0);
    lv_obj_align(s_display.home_course_subtitle, LV_ALIGN_BOTTOM_LEFT, 14, -10);

    s_display.home_course_page = lv_label_create(info);
    lv_label_set_text(s_display.home_course_page, "1 / 1");
    lv_obj_set_style_text_font(s_display.home_course_page, &lv_font_vimate_18, 0);
    lv_obj_set_style_text_color(s_display.home_course_page, lv_color_hex(0x4338ca), 0);
    lv_obj_align(s_display.home_course_page, LV_ALIGN_BOTTOM_RIGHT, -14, -10);

    lv_obj_t *navs[2] = {0};
    const char *nav_symbols[2] = {LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *nav = lv_obj_create(course);
        navs[i] = nav;
        lv_obj_set_size(nav, 64, 64);   /* 13/09: 48 (5,6 mm) → 64 (7,5 mm) + slop */
        lv_obj_align(nav, i == 0 ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID,
                     i == 0 ? 6 : -6, 0);
        lv_obj_set_style_radius(nav, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(nav, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_bg_opa(nav, LV_OPA_90, 0);
        lv_obj_set_style_bg_color(nav, lv_color_hex(0xe0e7ff), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(nav, 2, 0);
        lv_obj_set_style_border_color(nav, lv_color_hex(0x4f46e5), 0);
        UI_SET_SHADOW_WIDTH(nav, 10, 0);
        UI_SET_SHADOW_OPA(nav, LV_OPA_30, 0);
        lv_obj_set_style_pad_all(nav, 0, 0);
        lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *sym = lv_label_create(nav);
        lv_label_set_text(sym, nav_symbols[i]);
        lv_obj_set_style_text_font(sym, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(sym, lv_color_hex(0x4338ca), 0);
        lv_obj_center(sym);
    }
    s_display.home_course_prev = navs[0];
    s_display.home_course_next = navs[1];

    /* Ô icon: container trong suốt (vùng chạm) = squircle + nhãn DƯỚI. */
    for (int i = 0; i < VIMATE_HOME_MAX; i++) {
        lv_obj_t *cell = lv_obj_create(s_display.home_panel);
        lv_obj_set_size(cell, HOME_CELL_W, HOME_CELL_H);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_set_style_radius(cell, 20, 0);
        lv_obj_set_style_bg_color(cell, lv_color_hex(0xe2e8f0), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(cell, LV_OPA_60, LV_STATE_PRESSED);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);

        /* Squircle icon — gradient màu→sáng, viền trắng mờ, đổ bóng (đa lớp). */
        lv_obj_t *tile = lv_obj_create(cell);
        lv_obj_set_size(tile, HOME_ICON_SZ, HOME_ICON_SZ);
        lv_obj_align(tile, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_radius(tile, 22, 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(kHomeColors[i]), 0);
        lv_obj_set_style_bg_grad_color(tile, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_main_stop(tile, 40, 0);
        lv_obj_set_style_bg_grad_stop(tile, 255, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tile, 2, 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_border_opa(tile, LV_OPA_50, 0);
        UI_SET_SHADOW_WIDTH(tile, 14, 0);
        UI_SET_SHADOW_OFS_Y(tile, 6, 0);
        UI_SET_SHADOW_OPA(tile, LV_OPA_30, 0);
        UI_SET_SHADOW_COLOR(tile, lv_color_hex(kHomeColors[i]), 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *icon = lv_image_create(tile);
        lv_image_set_src(icon, &icon_hoc); /* mặc định; đổi theo id ở apply_home */
        lv_obj_center(icon);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);

        /* Nhãn DƯỚI icon (tiếng Việt → font vimate). */
        lv_obj_t *lbl = lv_label_create(cell);
        lv_label_set_text(lbl, "");
        /* Xuống dòng (wrap) — màn đủ lớn: tên khóa dài hiện 2-3 dòng cho đọc trọn;
         * tên app ngắn vẫn 1 dòng. */
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl, HOME_CELL_W);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x1f2937), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_vimate_18, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, HOME_ICON_SZ + 4);

        s_display.home_cards[i] = cell;
        s_display.home_card_tiles[i] = tile;
        s_display.home_card_icons[i] = icon;
        s_display.home_card_titles[i] = lbl;
    }

    /* Bottom bar giờ là GLOBAL (bottombar_create) — hiện trên mọi màn menu/tiện
     * ích, không chỉ Home. Xem nav/bottombar helpers. */
    s_display.home_card_count = 0;
    /* Cập nhật giờ header mỗi 20s khi home hiện (SNTP đã sync lúc boot). */
    lv_timer_create(home_clock_timer_cb, 20000, NULL);
}

/* ========== EDU quiz — màn trắc nghiệm 4 nút chạm (mockup màn 4) ========== */
static void quiz_create_ui(lv_obj_t *screen) {
    s_display.quiz_panel = lv_obj_create(screen);
    lv_obj_set_size(s_display.quiz_panel, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_align(s_display.quiz_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_display.quiz_panel, lv_color_hex(0xeef2ff), 0);
    lv_obj_set_style_bg_grad_color(s_display.quiz_panel, lv_color_hex(0xfdfdff), 0);
    lv_obj_set_style_bg_grad_dir(s_display.quiz_panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_display.quiz_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_display.quiz_panel, 0, 0);
    lv_obj_set_style_pad_all(s_display.quiz_panel, 0, 0);
    lv_obj_clear_flag(s_display.quiz_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_display.quiz_panel, LV_OBJ_FLAG_HIDDEN);

    /* Tiến độ "N/M" góc trên trái + 3 tim đỏ góc trên phải. */
    s_display.quiz_prog_lbl = lv_label_create(s_display.quiz_panel);
    lv_label_set_text(s_display.quiz_prog_lbl, "");
    lv_obj_set_style_text_font(s_display.quiz_prog_lbl, &lv_font_vimate_18, 0);
    lv_obj_set_style_text_color(s_display.quiz_prog_lbl, lv_color_hex(0x6366f1), 0);
    lv_obj_align(s_display.quiz_prog_lbl, LV_ALIGN_TOP_LEFT, 16, 12);
    for (int h = 0; h < 3; h++) {
        lv_obj_t *heart = lv_obj_create(s_display.quiz_panel);
        lv_obj_set_size(heart, 16, 16);
        lv_obj_set_style_radius(heart, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(heart, lv_color_hex(0xef4444), 0);
        lv_obj_set_style_border_width(heart, 0, 0);
        lv_obj_clear_flag(heart, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(heart, LV_ALIGN_TOP_RIGHT, -16 - h * 22, 12);
    }

    /* Câu hỏi (giữa trên, wrap). */
    s_display.quiz_q_lbl = lv_label_create(s_display.quiz_panel);
    lv_label_set_long_mode(s_display.quiz_q_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_display.quiz_q_lbl, BOARD_LCD_H_RES - 48);
    lv_label_set_text(s_display.quiz_q_lbl, "");
    lv_obj_set_style_text_font(s_display.quiz_q_lbl, &lv_font_vimate_24, 0);
    lv_obj_set_style_text_color(s_display.quiz_q_lbl, lv_color_hex(0x1e293b), 0);
    lv_obj_set_style_text_align(s_display.quiz_q_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_display.quiz_q_lbl, LV_ALIGN_TOP_MID, 0, 44);

    /* 4 nút đáp án 2×2 dưới câu hỏi. */
    /* Nút đáp án 72 px cao ≈ 8,5 mm (trước 54 = 6,4 mm); gap 14 px > 8 dp. */
    const int bw = (BOARD_LCD_H_RES - 48) / 2, bh = 72;
    const int gx = 16, gy = 14, top = 150;
    for (int i = 0; i < 4; i++) {
        int col = i % 2, row = i / 2;
        int x = 16 + col * (bw + gx);
        int y = top + row * (bh + gy);
        lv_obj_t *btn = lv_obj_create(s_display.quiz_panel);
        lv_obj_set_size(btn, bw, bh);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, x, y);
        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xc7d2fe), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xe0e7ff), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x6366f1), LV_STATE_PRESSED);
        UI_SET_SHADOW_WIDTH(btn, 8, 0);
        UI_SET_SHADOW_OPA(btn, LV_OPA_20, 0);
        lv_obj_set_style_pad_all(btn, 4, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, bw - 12);
        lv_obj_set_style_text_font(lbl, &lv_font_vimate_18, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x1e293b), 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
        s_display.quiz_btns[i] = btn;
        s_display.quiz_btn_lbls[i] = lbl;
    }
    s_display.quiz_btn_count = 0;
}

typedef struct {
    char *question;
    char *opts[4];
    int   n;
    int   step;
    int   total;
} sched_quiz_t;

static void apply_quiz(void *arg) {
    sched_quiz_t *p = (sched_quiz_t *)arg;
    display_lock();
    if (s_display.quiz_panel) {
        int n = p->n > 4 ? 4 : p->n;
        lv_label_set_text(s_display.quiz_q_lbl, p->question ? p->question : "");
        if (p->total > 0) {
            lv_label_set_text_fmt(s_display.quiz_prog_lbl, "%d/%d", p->step, p->total);
        } else {
            lv_label_set_text(s_display.quiz_prog_lbl, "");
        }
        for (int i = 0; i < 4; i++) {
            if (!s_display.quiz_btns[i]) continue;
            if (i < n) {
                lv_label_set_text(s_display.quiz_btn_lbls[i], p->opts[i] ? p->opts[i] : "");
                lv_obj_clear_flag(s_display.quiz_btns[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_display.quiz_btns[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        s_display.quiz_btn_count = n;
        if (s_display.emoji_image) lv_obj_add_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
        if (s_display.preview_image) lv_obj_add_flag(s_display.preview_image, LV_OBJ_FLAG_HIDDEN);
        if (s_display.home_panel) lv_obj_add_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_display.quiz_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_display.quiz_panel);
    }
    display_unlock();
    free(p->question);
    for (int i = 0; i < p->n && i < 4; i++) free(p->opts[i]);
    free(p);
}

void display_show_quiz(const char *question, const char *const *opts, int n,
                       int step, int total) {
    if (!s_display.sched_q) return;
    if (n < 0) n = 0;
    if (n > 4) n = 4;
    sched_quiz_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->n = n; p->step = step; p->total = total;
    p->question = (question && question[0]) ? strdup(question) : NULL;
    for (int i = 0; i < n; i++) p->opts[i] = (opts && opts[i]) ? strdup(opts[i]) : NULL;
    if (!display_schedule(apply_quiz, p)) {
        free(p->question);
        for (int i = 0; i < n; i++) free(p->opts[i]);
        free(p);
    }
}

static void apply_hide_quiz(void *arg) {
    (void)arg;
    display_lock();
    if (s_display.quiz_panel) lv_obj_add_flag(s_display.quiz_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_display.emoji_image) lv_obj_clear_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
    display_unlock();
}

void display_hide_quiz(void) {
    if (!s_display.sched_q) return;
    display_schedule(apply_hide_quiz, NULL);
}

bool display_quiz_visible(void) {
    display_lock();
    bool visible = s_display.quiz_panel &&
                   !lv_obj_has_flag(s_display.quiz_panel, LV_OBJ_FLAG_HIDDEN);
    display_unlock();
    return visible;
}

/* Trả index nút quiz (0-3) tại (x,y) khi quiz hiện; -1 nếu trượt/không hiện. */
int display_quiz_hit_index(int x, int y) {
    int hit = -1;
    display_lock();
    bool visible = s_display.quiz_panel &&
                   !lv_obj_has_flag(s_display.quiz_panel, LV_OBJ_FLAG_HIDDEN);
    if (visible) {
        for (int i = 0; i < s_display.quiz_btn_count && i < 4; i++) {
            if (hit_in(s_display.quiz_btns[i], x, y, TOUCH_HIT_SLOP)) {
                hit = i;
                break;
            }
        }
    }
    display_unlock();
    return hit;
}

/* ========== EDU thống kê (mockup màn 10) — 4 số + tiêu đề ========== */
static void stats_create_ui(lv_obj_t *screen) {
    s_display.stats_panel = lv_obj_create(screen);
    lv_obj_set_size(s_display.stats_panel, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_align(s_display.stats_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_display.stats_panel, lv_color_hex(0xeff3ff), 0);
    lv_obj_set_style_bg_grad_color(s_display.stats_panel, lv_color_hex(0xfdfdff), 0);
    lv_obj_set_style_bg_grad_dir(s_display.stats_panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_display.stats_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_display.stats_panel, 0, 0);
    lv_obj_clear_flag(s_display.stats_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_display.stats_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(s_display.stats_panel);
    lv_label_set_text(title, "Thống kê tuần này");
    lv_obj_set_style_text_font(title, &lv_font_vimate_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x1e3a8a), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    const char *labels[4] = {"phút học", "bài học", "ngày học", "ngôi sao"};
    const uint32_t cols[4] = {0x5b54e8, 0x22c55e, 0xf59e0b, 0xec4899};
    const int cw = (BOARD_LCD_H_RES - 48) / 2, ch = 96, gx = 16, gy = 8, top = 50;
    for (int i = 0; i < 4; i++) {
        int c = i % 2, r = i / 2;
        lv_obj_t *card = lv_obj_create(s_display.stats_panel);
        lv_obj_set_size(card, cw, ch);
        lv_obj_align(card, LV_ALIGN_TOP_LEFT, 16 + c * (cw + gx), top + r * (ch + gy));
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_border_width(card, 0, 0);
        UI_SET_SHADOW_WIDTH(card, 8, 0);
        UI_SET_SHADOW_OPA(card, LV_OPA_20, 0);
        lv_obj_set_style_pad_all(card, 6, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        /* Flex column căn giữa: số trên, nhãn dưới — LVGL tự xếp, KHÔNG đè
         * (không phụ thuộc line-height font 48 vốn cao do dấu tiếng Việt). */
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(card, 2, 0);
        lv_obj_t *val = lv_label_create(card);
        lv_label_set_text(val, "0");
        lv_obj_set_style_text_font(val, &lv_font_vimate_48, 0);
        lv_obj_set_style_text_color(val, lv_color_hex(cols[i]), 0);
        lv_obj_t *lb = lv_label_create(card);
        lv_label_set_text(lb, labels[i]);
        lv_obj_set_style_text_font(lb, &lv_font_vimate_18, 0);
        lv_obj_set_style_text_color(lb, lv_color_hex(0x6b7280), 0);
        s_display.stats_val[i] = val;
    }
}

typedef struct { int v[4]; } sched_stats_t;
static void apply_stats(void *arg) {
    sched_stats_t *p = (sched_stats_t *)arg;
    display_lock();
    if (s_display.stats_panel) {
        for (int i = 0; i < 4; i++) {
            if (s_display.stats_val[i]) lv_label_set_text_fmt(s_display.stats_val[i], "%d", p->v[i]);
        }
        screens_dismiss_locked();
        if (s_display.emoji_image) lv_obj_add_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
        if (s_display.home_panel) lv_obj_add_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_display.stats_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_display.stats_panel);
    }
    display_unlock();
    free(p);
}
void display_show_stats(int minutes, int lessons, int days, int stars) {
    if (!s_display.sched_q) return;
    sched_stats_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->v[0] = minutes; p->v[1] = lessons; p->v[2] = days; p->v[3] = stars;
    if (!display_schedule(apply_stats, p)) free(p);
}
static void apply_hide_stats(void *arg) {
    (void)arg;
    display_lock();
    if (s_display.stats_panel) lv_obj_add_flag(s_display.stats_panel, LV_OBJ_FLAG_HIDDEN);
    display_unlock();
}
void display_hide_stats(void) {
    if (!s_display.sched_q) return;
    display_schedule(apply_hide_stats, NULL);
}

/* ========== EDU uống nước (mockup màn 9) — ml + 2 nút chạm ========== */
static void water_create_ui(lv_obj_t *screen) {
    s_display.water_panel = lv_obj_create(screen);
    lv_obj_set_size(s_display.water_panel, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_align(s_display.water_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_display.water_panel, lv_color_hex(0xe0f2fe), 0);
    lv_obj_set_style_bg_grad_color(s_display.water_panel, lv_color_hex(0xfdfdff), 0);
    lv_obj_set_style_bg_grad_dir(s_display.water_panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_display.water_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_display.water_panel, 0, 0);
    lv_obj_clear_flag(s_display.water_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_display.water_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(s_display.water_panel);
    lv_label_set_text(title, "Uống nước nào!");
    lv_obj_set_style_text_font(title, &lv_font_vimate_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0369a1), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    s_display.water_ml_lbl = lv_label_create(s_display.water_panel);
    lv_label_set_text(s_display.water_ml_lbl, "0 / 2000 ml");
    lv_obj_set_style_text_font(s_display.water_ml_lbl, &lv_font_vimate_24, 0);
    lv_obj_set_style_text_color(s_display.water_ml_lbl, lv_color_hex(0x0ea5e9), 0);
    lv_obj_align(s_display.water_ml_lbl, LV_ALIGN_TOP_MID, 0, 66);

    /* Thanh tiến độ ml. */
    lv_obj_t *track = lv_obj_create(s_display.water_panel);
    lv_obj_set_size(track, BOARD_LCD_H_RES - 80, 18);
    lv_obj_align(track, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(0xbae6fd), 0);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_set_style_pad_all(track, 0, 0);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    s_display.water_bar = lv_obj_create(track);
    lv_obj_set_size(s_display.water_bar, 0, 18);
    lv_obj_align(s_display.water_bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(s_display.water_bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_display.water_bar, lv_color_hex(0x0ea5e9), 0);
    lv_obj_set_style_border_width(s_display.water_bar, 0, 0);

    /* 3 nút: Đã uống (log+tắt nhắc) · Để sau (1 lần) · Tắt nhắc (xoá hẳn). */
    const char *bl[3] = {"Đã uống", "Để sau", "Tắt nhắc"};
    const uint32_t bc[3] = {0x0ea5e9, 0xe2e8f0, 0xfecaca};
    const uint32_t tc[3] = {0xffffff, 0x334155, 0x991b1b};
    const int bw = (BOARD_LCD_H_RES - 64) / 3;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_obj_create(s_display.water_panel);
        lv_obj_set_size(btn, bw, 64);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 16 + i * (bw + 16),
                     -(HOME_BAR_RESERVED_H + 4)); /* chừa bar */
        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(bc[i]), 0);
        lv_obj_set_style_bg_color(btn, lv_color_darken(lv_color_hex(bc[i]), LV_OPA_30),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 0, 0);
        UI_SET_SHADOW_WIDTH(btn, 6, 0);
        UI_SET_SHADOW_OPA(btn, LV_OPA_20, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lb = lv_label_create(btn);
        lv_label_set_text(lb, bl[i]);
        lv_obj_set_style_text_font(lb, &lv_font_vimate_18, 0);
        lv_obj_set_style_text_color(lb, lv_color_hex(tc[i]), 0);
        lv_obj_center(lb);
        s_display.water_btns[i] = btn;
    }
}
typedef struct { int today, goal; } sched_water_t;  /* dùng cho apply_water */
static void apply_water(void *arg) {
    sched_water_t *p = (sched_water_t *)arg;
    display_lock();
    if (s_display.water_panel) {
        lv_label_set_text_fmt(s_display.water_ml_lbl, "%d / %d ml", p->today, p->goal);
        int pct = p->goal > 0 ? (p->today * 100 / p->goal) : 0;
        if (pct > 100) pct = 100;
        int w = (BOARD_LCD_H_RES - 80) * pct / 100;
        lv_obj_set_width(s_display.water_bar, w);
        screens_dismiss_locked();
        if (s_display.emoji_image) lv_obj_add_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
        if (s_display.home_panel) lv_obj_add_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_display.water_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_display.water_panel);
    }
    display_unlock();
    free(p);
}
void display_show_water(int todayMl, int goalMl) {
    if (!s_display.sched_q) return;
    sched_water_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->today = todayMl; p->goal = goalMl > 0 ? goalMl : 2000;
    if (!display_schedule(apply_water, p)) free(p);
}
static void apply_hide_water(void *arg) {
    (void)arg;
    display_lock();
    if (s_display.water_panel) lv_obj_add_flag(s_display.water_panel, LV_OBJ_FLAG_HIDDEN);
    display_unlock();
}
void display_hide_water(void) {
    if (!s_display.sched_q) return;
    display_schedule(apply_hide_water, NULL);
}
bool display_water_visible(void) {
    display_lock();
    bool visible = s_display.water_panel &&
                   !lv_obj_has_flag(s_display.water_panel, LV_OBJ_FLAG_HIDDEN);
    display_unlock();
    return visible;
}
/* Trả 0=Đã uống, 1=Để sau, 2=Tắt nhắc, -1=trượt. */
int display_water_hit_index(int x, int y) {
    int hit = -1;
    display_lock();
    bool visible = s_display.water_panel &&
                   !lv_obj_has_flag(s_display.water_panel, LV_OBJ_FLAG_HIDDEN);
    if (visible) {
        for (int i = 0; i < 3; i++) {
            if (hit_in(s_display.water_btns[i], x, y, TOUCH_HIT_SLOP)) {
                hit = i;
                break;
            }
        }
    }
    display_unlock();
    return hit;
}
bool display_stats_visible(void) {
    display_lock();
    bool visible = s_display.stats_panel &&
                   !lv_obj_has_flag(s_display.stats_panel, LV_OBJ_FLAG_HIDDEN);
    display_unlock();
    return visible;
}

/* ========== EDU lộ trình học (mockup màn 6) — % + list bài sao/khóa ========== */
#define PROGRESS_ROWS 6
static void progress_create_ui(lv_obj_t *screen) {
    s_display.progress_panel = lv_obj_create(screen);
    lv_obj_set_size(s_display.progress_panel, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_align(s_display.progress_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_display.progress_panel, lv_color_hex(0xeef2ff), 0);
    lv_obj_set_style_bg_grad_color(s_display.progress_panel, lv_color_hex(0xfdfdff), 0);
    lv_obj_set_style_bg_grad_dir(s_display.progress_panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_display.progress_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_display.progress_panel, 0, 0);
    lv_obj_set_style_pad_all(s_display.progress_panel, 0, 0);
    lv_obj_clear_flag(s_display.progress_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_display.progress_panel, LV_OBJ_FLAG_HIDDEN);

    s_display.progress_title = lv_label_create(s_display.progress_panel);
    lv_label_set_text(s_display.progress_title, "Lộ trình học");
    lv_obj_set_style_text_font(s_display.progress_title, &lv_font_vimate_24, 0);
    lv_obj_set_style_text_color(s_display.progress_title, lv_color_hex(0x1e3a8a), 0);
    lv_obj_align(s_display.progress_title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *sub = lv_label_create(s_display.progress_panel);
    lv_label_set_text(sub, "Tiến độ của bé");
    lv_obj_set_style_text_font(sub, &lv_font_vimate_18, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x6b7280), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 16, 48);

    s_display.progress_pct_lbl = lv_label_create(s_display.progress_panel);
    lv_label_set_text(s_display.progress_pct_lbl, "0%");
    lv_obj_set_style_text_font(s_display.progress_pct_lbl, &lv_font_vimate_18, 0);
    lv_obj_set_style_text_color(s_display.progress_pct_lbl, lv_color_hex(0x5b54e8), 0);
    lv_obj_align(s_display.progress_pct_lbl, LV_ALIGN_TOP_RIGHT, -16, 48);

    lv_obj_t *track = lv_obj_create(s_display.progress_panel);
    lv_obj_set_size(track, BOARD_LCD_H_RES - 32, 14);
    lv_obj_align(track, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(0xdbe2f9), 0);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_set_style_pad_all(track, 0, 0);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    s_display.progress_bar = lv_obj_create(track);
    lv_obj_set_size(s_display.progress_bar, 0, 14);
    lv_obj_align(s_display.progress_bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(s_display.progress_bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_display.progress_bar, lv_color_hex(0x22c55e), 0);
    lv_obj_set_style_border_width(s_display.progress_bar, 0, 0);

    /* Rows list bài. Chừa đáy cho nút Home tròn (top ~254). */
    const int rh = 24, rtop = 90, rgap = 3;
    for (int i = 0; i < PROGRESS_ROWS; i++) {
        lv_obj_t *row = lv_obj_create(s_display.progress_panel);
        lv_obj_set_size(row, BOARD_LCD_H_RES - 32, rh);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 16, rtop + i * (rh + rgap));
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        UI_SET_SHADOW_WIDTH(row, 4, 0);
        UI_SET_SHADOW_OPA(row, LV_OPA_10, 0);
        lv_obj_set_style_pad_hor(row, 12, 0);
        lv_obj_set_style_pad_ver(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *nm = lv_label_create(row);
        lv_label_set_text(nm, "");
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        lv_obj_set_width(nm, BOARD_LCD_H_RES - 32 - 24 - 76);
        lv_obj_set_style_text_font(nm, &lv_font_vimate_18, 0);
        lv_obj_set_style_text_color(nm, lv_color_hex(0x1f2937), 0);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t *stt = lv_label_create(row);
        lv_label_set_text(stt, "");
        lv_obj_set_style_text_font(stt, &lv_font_vimate_18, 0);
        lv_obj_set_style_text_color(stt, lv_color_hex(0xf59e0b), 0);
        lv_obj_align(stt, LV_ALIGN_RIGHT_MID, 0, 0);
        s_display.progress_rows[i] = row;
        s_display.progress_name[i] = nm;
        s_display.progress_stat[i] = stt;
    }
}
typedef struct {
    char *title;
    int   percent, n;
    char *names[PROGRESS_ROWS];
    int   stars[PROGRESS_ROWS];
    bool  locked[PROGRESS_ROWS];
} sched_progress_t;
static void apply_progress(void *arg) {
    sched_progress_t *p = (sched_progress_t *)arg;
    display_lock();
    if (s_display.progress_panel) {
        if (p->title) lv_label_set_text(s_display.progress_title, p->title);
        lv_label_set_text_fmt(s_display.progress_pct_lbl, "%d%%", p->percent);
        int w = (BOARD_LCD_H_RES - 32) * (p->percent > 100 ? 100 : p->percent) / 100;
        lv_obj_set_width(s_display.progress_bar, w);
        for (int i = 0; i < PROGRESS_ROWS; i++) {
            if (i < p->n) {
                lv_label_set_text(s_display.progress_name[i], p->names[i] ? p->names[i] : "");
                if (p->locked[i]) {
                    /* 13/09: 9ca3af (2,5:1) → FG_MUTED (~7:1 trên eef2ff). Vẫn "mờ" hơn
                     * hàng mở (1f2937) và có chữ "khóa" — không chỉ dựa vào màu. */
                    lv_label_set_text(s_display.progress_stat[i], "khóa");
                    lv_obj_set_style_text_color(s_display.progress_stat[i], lv_color_hex(UI_CLR_FG_MUTED), 0);
                    lv_obj_set_style_text_color(s_display.progress_name[i], lv_color_hex(UI_CLR_FG_MUTED), 0);
                } else {
                    /* Sao: '*' vàng cho số sao (font không có ⭐ glyph). 0 sao = "..." */
                    char sbuf[8] = {0};
                    int ns = p->stars[i]; if (ns < 0) ns = 0; if (ns > 3) ns = 3;
                    if (ns == 0) { strcpy(sbuf, "..."); }
                    else { for (int k = 0; k < ns; k++) strcat(sbuf, "*"); }
                    lv_label_set_text(s_display.progress_stat[i], sbuf);
                    lv_obj_set_style_text_color(s_display.progress_stat[i],
                        ns == 0 ? lv_color_hex(0xcbd5e1) : lv_color_hex(0xf59e0b), 0);
                    lv_obj_set_style_text_color(s_display.progress_name[i], lv_color_hex(0x1f2937), 0);
                }
                lv_obj_clear_flag(s_display.progress_rows[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_display.progress_rows[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        screens_dismiss_locked();
        if (s_display.emoji_image) lv_obj_add_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
        if (s_display.home_panel) lv_obj_add_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_display.progress_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_display.progress_panel);
    }
    display_unlock();
    for (int i = 0; i < p->n; i++) free(p->names[i]);
    free(p->title);
    free(p);
}
void display_show_progress(const char *title, int percent,
                           const char *const *names, const int *stars,
                           const bool *locked, int n) {
    if (!s_display.sched_q) return;
    if (n > PROGRESS_ROWS) n = PROGRESS_ROWS;
    sched_progress_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->title = title ? strdup(title) : NULL;
    p->percent = percent;
    p->n = n;
    for (int i = 0; i < n; i++) {
        p->names[i] = (names && names[i]) ? strdup(names[i]) : NULL;
        p->stars[i] = stars ? stars[i] : 0;
        p->locked[i] = locked ? locked[i] : false;
    }
    if (!display_schedule(apply_progress, p)) {
        for (int i = 0; i < n; i++) free(p->names[i]);
        free(p->title);
        free(p);
    }
}
static void apply_hide_progress(void *arg) {
    (void)arg;
    display_lock();
    if (s_display.progress_panel) lv_obj_add_flag(s_display.progress_panel, LV_OBJ_FLAG_HIDDEN);
    display_unlock();
}
void display_hide_progress(void) {
    if (!s_display.sched_q) return;
    display_schedule(apply_hide_progress, NULL);
}
bool display_progress_visible(void) {
    display_lock();
    bool visible = s_display.progress_panel &&
                   !lv_obj_has_flag(s_display.progress_panel, LV_OBJ_FLAG_HIDDEN);
    display_unlock();
    return visible;
}

/* ========== EDU timetable — bảng lịch học cho LCD 3.5" 480x320 ========== */
static lv_obj_t *timetable_make_text(lv_obj_t *parent, const char *text,
                                     const lv_font_t *font, uint32_t color,
                                     int w, lv_text_align_t align) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    if (w > 0) lv_obj_set_width(label, w);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}

static void timetable_make_cloud(lv_obj_t *parent, lv_align_t align, int x, int y) {
    static const int sizes[3] = {32, 42, 28};
    static const int ox[3] = {0, 24, 52};
    static const int oy[3] = {10, 0, 12};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = lv_obj_create(parent);
        lv_obj_set_size(c, sizes[i], sizes[i]);
        lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(c, align, x + ox[i], y + oy[i]);
    }
}

static const char *timetable_day_name(int day) {
    static const char *names[DISPLAY_TIMETABLE_DAYS] = {
        "Thứ 2", "Thứ 3", "Thứ 4", "Thứ 5", "Thứ 6",
    };
    if (day < 1 || day > DISPLAY_TIMETABLE_DAYS) return "Thời khóa biểu";
    return names[day - 1];
}

static lv_obj_t *timetable_make_detail_body(lv_obj_t *parent) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 356, 156);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xe0f2fe), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *body = timetable_make_text(card, "", &lv_font_vimate_14,
                                         0x075985, 326, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_line_space(body, -4, 0);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 15, 10);
    return body;
}

static void timetable_create_ui(lv_obj_t *screen) {
    static const uint32_t day_colors[DISPLAY_TIMETABLE_DAYS] = {
        0x20a7db, 0x4caf50, 0xf7df2e, 0xe9792c, 0xe63946,
    };
    static const char *day_full[DISPLAY_TIMETABLE_DAYS] = {
        "Thứ 2", "Thứ 3", "Thứ 4", "Thứ 5", "Thứ 6",
    };
    static const char *day_short[DISPLAY_TIMETABLE_DAYS] = {
        "T2", "T3", "T4", "T5", "T6",
    };

    s_display.timetable_panel = lv_obj_create(screen);
    lv_obj_set_size(s_display.timetable_panel, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_align(s_display.timetable_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_display.timetable_panel, lv_color_hex(0x89cbe3), 0);
    lv_obj_set_style_bg_opa(s_display.timetable_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_display.timetable_panel, 0, 0);
    lv_obj_set_style_pad_all(s_display.timetable_panel, 0, 0);
    lv_obj_clear_flag(s_display.timetable_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_display.timetable_panel, LV_OBJ_FLAG_HIDDEN);

    timetable_make_cloud(s_display.timetable_panel, LV_ALIGN_TOP_LEFT, 12, -4);
    timetable_make_cloud(s_display.timetable_panel, LV_ALIGN_BOTTOM_LEFT, 12, -42);
    timetable_make_cloud(s_display.timetable_panel, LV_ALIGN_TOP_RIGHT, -92, -2);

    lv_obj_t *accent = lv_obj_create(s_display.timetable_panel);
    lv_obj_set_size(accent, 64, 18);
    lv_obj_set_style_radius(accent, 9, 0);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0xf87171), 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_align(accent, LV_ALIGN_TOP_RIGHT, -34, 30);
    lv_obj_set_style_transform_rotation(accent, 250, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = timetable_make_text(s_display.timetable_panel, "THỜI KHÓA BIỂU",
                                          &lv_font_vimate_18, 0x0ea5e9,
                                          BOARD_LCD_H_RES - 170,
                                          LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_letter_space(title, 0, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    const int sheet_x = BOARD_LCD_H_RES >= 440 ? 12 : 8;
    const int sheet_y = BOARD_LCD_V_RES >= 300 ? 44 : 38;
    const int sheet_w = BOARD_LCD_H_RES - sheet_x * 2;
    const int sheet_h = BOARD_LCD_V_RES - sheet_y - (BOARD_LCD_V_RES >= 300 ? 14 : 8);
    lv_obj_t *sheet = lv_obj_create(s_display.timetable_panel);
    lv_obj_set_size(sheet, sheet_w, sheet_h);
    lv_obj_align(sheet, LV_ALIGN_TOP_LEFT, sheet_x, sheet_y);
    lv_obj_set_style_radius(sheet, BOARD_LCD_H_RES >= 440 ? 22 : 14, 0);
    lv_obj_set_style_bg_color(sheet, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sheet, 0, 0);
    lv_obj_set_style_pad_all(sheet, 0, 0);
    lv_obj_clear_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);

    s_display.timetable_class_label = timetable_make_text(
        sheet, "Lớp: ...", &lv_font_vimate_14, 0x0ea5e9, 116, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(s_display.timetable_class_label, LV_ALIGN_TOP_RIGHT, -14, 6);

    const int inner = BOARD_LCD_H_RES >= 440 ? 10 : 8;
    const int row_label_w = BOARD_LCD_H_RES >= 440 ? 54 : 42;
    const int gap = BOARD_LCD_H_RES >= 440 ? 4 : 3;
    const int table_x = inner;
    const int day_x0 = table_x + row_label_w + gap;
    const int col_w = (sheet_w - inner * 2 - row_label_w - gap -
                       gap * (DISPLAY_TIMETABLE_DAYS - 1)) /
                      DISPLAY_TIMETABLE_DAYS;
    const int header_y = BOARD_LCD_V_RES >= 300 ? 34 : 28;
    const int header_h = BOARD_LCD_V_RES >= 300 ? 26 : 22;
    const int row_gap = BOARD_LCD_V_RES >= 300 ? 6 : 4;
    const int hint_h = BOARD_LCD_V_RES >= 300 ? 22 : 0; /* 13/09: hint 14 → 18 px (cao ~21) */
    const int row_y0 = header_y + header_h + row_gap;
    int row_h = (sheet_h - row_y0 - row_gap - hint_h - 12) / 2;
    if (row_h < 52) row_h = 52;
    s_display.timetable_day_hit_y = sheet_y + header_y;
    s_display.timetable_day_hit_h = header_h + row_gap * 2 + row_h * 2;
    s_display.timetable_selected_day = 0;

    for (int i = 0; i < DISPLAY_TIMETABLE_DAYS; i++) {
        s_display.timetable_day_hit_x[i] = sheet_x + day_x0 + i * (col_w + gap);
        s_display.timetable_day_hit_w[i] = col_w;
        lv_obj_t *pill = lv_obj_create(sheet);
        lv_obj_set_size(pill, col_w, header_h);
        lv_obj_align(pill, LV_ALIGN_TOP_LEFT, day_x0 + i * (col_w + gap), header_y);
        lv_obj_set_style_radius(pill, 8, 0);
        lv_obj_set_style_bg_color(pill, lv_color_hex(day_colors[i]), 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(pill, 0, 0);
        lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lbl = timetable_make_text(
            pill, BOARD_LCD_H_RES >= 440 ? day_full[i] : day_short[i],
            &lv_font_vimate_14, 0xffffff, col_w - 4, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_style_text_line_space(lbl, -3, 0);
        lv_obj_center(lbl);
    }

    static const char *row_names[2] = {"Sáng", "Chiều"};
    for (int row = 0; row < 2; row++) {
        int y = row_y0 + row * (row_h + row_gap);
        lv_obj_t *row_tag = lv_obj_create(sheet);
        lv_obj_set_size(row_tag, row_label_w, row_h);
        lv_obj_align(row_tag, LV_ALIGN_TOP_LEFT, table_x, y);
        lv_obj_set_style_radius(row_tag, 7, 0);
        lv_obj_set_style_bg_color(row_tag, lv_color_hex(0x8bd0e8), 0);
        lv_obj_set_style_bg_opa(row_tag, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row_tag, 0, 0);
        lv_obj_clear_flag(row_tag, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *row_lbl = timetable_make_text(row_tag, row_names[row],
                                                &lv_font_vimate_14, 0xffffff,
                                                row_label_w - 4,
                                                LV_TEXT_ALIGN_CENTER);
        lv_obj_set_style_text_line_space(row_lbl, -3, 0);
        lv_obj_center(row_lbl);

        for (int day = 0; day < DISPLAY_TIMETABLE_DAYS; day++) {
            lv_obj_t *cell = lv_obj_create(sheet);
            lv_obj_set_size(cell, col_w, row_h);
            lv_obj_align(cell, LV_ALIGN_TOP_LEFT, day_x0 + day * (col_w + gap), y);
            lv_obj_set_style_radius(cell, 5, 0);
            lv_obj_set_style_bg_color(cell, lv_color_hex(0xe4f8fb), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(cell, lv_color_hex(0xffffff), 0);
            lv_obj_set_style_border_width(cell, 2, 0);
            lv_obj_set_style_pad_all(cell, 3, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *cell_lbl = timetable_make_text(cell, "",
                                                     &lv_font_vimate_14,
                                                     0x075985,
                                                     col_w - 6,
                                                     LV_TEXT_ALIGN_CENTER);
            lv_obj_set_style_text_line_space(cell_lbl, -4, 0);
            lv_obj_center(cell_lbl);
            s_display.timetable_cells[row][day] = cell_lbl;
        }
    }

    if (BOARD_LCD_V_RES >= 300) {
        /* 13/09: 7aa7cc (2,6:1) → FG_MUTED (7,6:1), 14 → 18 px (MASTER.md §2.3, §3). */
        lv_obj_t *hint = timetable_make_text(sheet, "Chạm để về Home",
                                             UI_FONT_LABEL, UI_CLR_FG_MUTED,
                                             sheet_w - 32, LV_TEXT_ALIGN_CENTER);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
    }

    s_display.timetable_detail_panel = lv_obj_create(s_display.timetable_panel);
    lv_obj_set_size(s_display.timetable_detail_panel, 404, 244);
    lv_obj_align(s_display.timetable_detail_panel, LV_ALIGN_CENTER, 0, 12);
    lv_obj_set_style_radius(s_display.timetable_detail_panel, 16, 0);
    lv_obj_set_style_bg_color(s_display.timetable_detail_panel, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(s_display.timetable_detail_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_display.timetable_detail_panel, 2, 0);
    lv_obj_set_style_border_color(s_display.timetable_detail_panel, lv_color_hex(0x7dd3fc), 0);
    UI_SET_SHADOW_WIDTH(s_display.timetable_detail_panel, 18, 0);
    UI_SET_SHADOW_OPA(s_display.timetable_detail_panel, LV_OPA_30, 0);
    UI_SET_SHADOW_COLOR(s_display.timetable_detail_panel, lv_color_hex(0x0284c7), 0);
    lv_obj_set_style_pad_all(s_display.timetable_detail_panel, 0, 0);
    lv_obj_clear_flag(s_display.timetable_detail_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_display.timetable_detail_panel, LV_OBJ_FLAG_HIDDEN);

    s_display.timetable_detail_title = timetable_make_text(
        s_display.timetable_detail_panel, "Thứ 2", &lv_font_vimate_18,
        0x0284c7, 320, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(s_display.timetable_detail_title, LV_ALIGN_TOP_MID, 0, 12);

    s_display.timetable_detail_body = timetable_make_detail_body(
        s_display.timetable_detail_panel);

    /* 13/09: 7aa7cc → FG_MUTED, 14 → 18 px. Card chi tiết kết thúc y=204, hint ~215..236. */
    s_display.timetable_detail_hint = timetable_make_text(
        s_display.timetable_detail_panel, "Chạm để quay lại bảng",
        UI_FONT_LABEL, UI_CLR_FG_MUTED, 320, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(s_display.timetable_detail_hint, LV_ALIGN_BOTTOM_MID, 0, -8);
}

/* ========== Alarm screen (Hẹn giờ/Uống nước) — màn nhắc dễ thương ========== */
static void alarm_create_ui(lv_obj_t *screen) {
    s_display.alarm_panel = lv_obj_create(screen);
    lv_obj_set_size(s_display.alarm_panel, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_align(s_display.alarm_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_display.alarm_panel, lv_color_hex(0xffe9c7), 0);
    lv_obj_set_style_bg_grad_color(s_display.alarm_panel, lv_color_hex(0xfff7ed), 0);
    lv_obj_set_style_bg_grad_dir(s_display.alarm_panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_display.alarm_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_display.alarm_panel, 0, 0);
    lv_obj_set_style_pad_all(s_display.alarm_panel, 0, 0);
    lv_obj_clear_flag(s_display.alarm_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_display.alarm_panel, LV_OBJ_FLAG_HIDDEN);

    /* Thẻ trắng bo tròn + đổ bóng (nổi) */
    s_display.alarm_card = lv_obj_create(s_display.alarm_panel);
    lv_obj_set_size(s_display.alarm_card, 380, 250);
    lv_obj_center(s_display.alarm_card);
    lv_obj_set_style_radius(s_display.alarm_card, 32, 0);
    lv_obj_set_style_bg_color(s_display.alarm_card, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(s_display.alarm_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_display.alarm_card, 0, 0);
    UI_SET_SHADOW_WIDTH(s_display.alarm_card, 24, 0);
    UI_SET_SHADOW_OFS_Y(s_display.alarm_card, 8, 0);
    UI_SET_SHADOW_OPA(s_display.alarm_card, LV_OPA_30, 0);
    UI_SET_SHADOW_COLOR(s_display.alarm_card, lv_color_hex(0xb45309), 0);
    lv_obj_clear_flag(s_display.alarm_card, LV_OBJ_FLAG_SCROLLABLE);

    s_display.alarm_icon = lv_image_create(s_display.alarm_card);
    lv_image_set_src(s_display.alarm_icon, &icon_alarm_timer);
    lv_obj_align(s_display.alarm_icon, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_clear_flag(s_display.alarm_icon, LV_OBJ_FLAG_CLICKABLE);

    s_display.alarm_text = lv_label_create(s_display.alarm_card);
    lv_label_set_text(s_display.alarm_text, "");
    lv_label_set_long_mode(s_display.alarm_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_display.alarm_text, 340);
    lv_obj_set_style_text_color(s_display.alarm_text, lv_color_hex(0x7c2d12), 0);
    lv_obj_set_style_text_font(s_display.alarm_text, &lv_font_vimate_24, 0);
    lv_obj_set_style_text_align(s_display.alarm_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_display.alarm_text, LV_ALIGN_TOP_MID, 0, 130);

    lv_obj_t *hint = lv_label_create(s_display.alarm_card);
    lv_label_set_text(hint, "Chạm để tắt");
    /* 13/09: 9ca3af trên trắng chỉ 2,5:1 → FG_MUTED 7,6:1 (MASTER.md §2.3). */
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_CLR_FG_MUTED), 0);
    lv_obj_set_style_text_font(hint, UI_FONT_LABEL, 0);
	lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);
}

/* ========== Countdown screen — đếm ngược realtime MM:SS ========== */
static void countdown_create_ui(lv_obj_t *screen) {
	s_display.countdown_panel = lv_obj_create(screen);
	lv_obj_set_size(s_display.countdown_panel, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
	lv_obj_align(s_display.countdown_panel, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_set_style_bg_color(s_display.countdown_panel, lv_color_hex(0xdbeafe), 0);
	lv_obj_set_style_bg_grad_color(s_display.countdown_panel, lv_color_hex(0xfdf2f8), 0);
	lv_obj_set_style_bg_grad_dir(s_display.countdown_panel, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(s_display.countdown_panel, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(s_display.countdown_panel, 0, 0);
	lv_obj_set_style_pad_all(s_display.countdown_panel, 0, 0);
	lv_obj_clear_flag(s_display.countdown_panel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(s_display.countdown_panel, LV_OBJ_FLAG_HIDDEN);

	int card_w = BOARD_LCD_H_RES - 36;
	if (card_w > 420) card_w = 420;
	int card_h = BOARD_LCD_V_RES - 82;
	if (card_h > 238) card_h = 238;
	if (card_h < 150) card_h = 150;
	s_display.countdown_card = lv_obj_create(s_display.countdown_panel);
	lv_obj_set_size(s_display.countdown_card, card_w, card_h);
	lv_obj_align(s_display.countdown_card, LV_ALIGN_TOP_MID, 0, 10);
	lv_obj_set_style_radius(s_display.countdown_card, 30, 0);
	lv_obj_set_style_bg_color(s_display.countdown_card, lv_color_hex(0xffffff), 0);
	lv_obj_set_style_bg_opa(s_display.countdown_card, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(s_display.countdown_card, 0, 0);
	UI_SET_SHADOW_WIDTH(s_display.countdown_card, 22, 0);
	UI_SET_SHADOW_OFS_Y(s_display.countdown_card, 8, 0);
	UI_SET_SHADOW_OPA(s_display.countdown_card, LV_OPA_30, 0);
	UI_SET_SHADOW_COLOR(s_display.countdown_card, lv_color_hex(0x2563eb), 0);
	lv_obj_clear_flag(s_display.countdown_card, LV_OBJ_FLAG_SCROLLABLE);

	s_display.countdown_title = lv_label_create(s_display.countdown_card);
	lv_label_set_text(s_display.countdown_title, "Đếm ngược");
	lv_label_set_long_mode(s_display.countdown_title, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(s_display.countdown_title, card_w - 34);
	lv_obj_set_style_text_color(s_display.countdown_title, lv_color_hex(0x1e3a8a), 0);
	lv_obj_set_style_text_font(s_display.countdown_title, &lv_font_vimate_24, 0);
	lv_obj_set_style_text_align(s_display.countdown_title, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_align(s_display.countdown_title, LV_ALIGN_TOP_MID, 0, 14);

	s_display.countdown_time = lv_label_create(s_display.countdown_card);
	lv_label_set_text(s_display.countdown_time, "00:00");
	lv_obj_set_style_text_color(s_display.countdown_time, lv_color_hex(0x0f172a), 0);
	lv_obj_set_style_text_font(s_display.countdown_time, &lv_font_countdown_72, 0);
	lv_obj_set_style_text_align(s_display.countdown_time, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_align(s_display.countdown_time, LV_ALIGN_CENTER, 0, 18);
}

/* ========== Clock screen (app Đồng hồ) — nền cute + giờ to + ngày ========== */
static void clock_create_ui(lv_obj_t *screen) {
    s_display.clock_panel = lv_obj_create(screen);
    lv_obj_set_size(s_display.clock_panel, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_align(s_display.clock_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    /* Nền trời dịu: xanh nhạt → trắng. */
    lv_obj_set_style_bg_color(s_display.clock_panel, lv_color_hex(0xbfe3ff), 0);
    lv_obj_set_style_bg_grad_color(s_display.clock_panel, lv_color_hex(0xeaf6ff), 0);
    lv_obj_set_style_bg_grad_dir(s_display.clock_panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_display.clock_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_display.clock_panel, 0, 0);
    lv_obj_set_style_pad_all(s_display.clock_panel, 0, 0);
    lv_obj_clear_flag(s_display.clock_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_display.clock_panel, LV_OBJ_FLAG_HIDDEN);

    /* Scrim mờ tối sau chữ (chỉ hiện theme wallpaper, để chữ trắng đọc rõ trên
     * ảnh bất kỳ). Tạo SỚM → nằm dưới chữ. Ẩn mặc định. */
    s_display.clock_scrim = lv_obj_create(s_display.clock_panel);
    lv_obj_set_size(s_display.clock_scrim, 300, 150);
    lv_obj_align(s_display.clock_scrim, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_style_radius(s_display.clock_scrim, 28, 0);
    lv_obj_set_style_bg_color(s_display.clock_scrim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_display.clock_scrim, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_display.clock_scrim, 0, 0);
    lv_obj_clear_flag(s_display.clock_scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_display.clock_scrim, LV_OBJ_FLAG_HIDDEN);

    /* Mặt trời cute góc trên-phải. */
    s_display.clock_sun = lv_obj_create(s_display.clock_panel);
    lv_obj_set_size(s_display.clock_sun, 64, 64);
    lv_obj_align(s_display.clock_sun, LV_ALIGN_TOP_RIGHT, -26, 24);
    lv_obj_set_style_radius(s_display.clock_sun, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_display.clock_sun, lv_color_hex(0xffd54f), 0);
    lv_obj_set_style_bg_grad_color(s_display.clock_sun, lv_color_hex(0xffb300), 0);
    lv_obj_set_style_bg_grad_dir(s_display.clock_sun, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_display.clock_sun, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_display.clock_sun, 0, 0);
    lv_obj_clear_flag(s_display.clock_sun, LV_OBJ_FLAG_SCROLLABLE);

    /* Mây trắng cute góc trên-trái. */
    s_display.clock_cloud = lv_obj_create(s_display.clock_panel);
    lv_obj_set_size(s_display.clock_cloud, 96, 40);
    lv_obj_align(s_display.clock_cloud, LV_ALIGN_TOP_LEFT, 24, 40);
    lv_obj_set_style_radius(s_display.clock_cloud, 20, 0);
    lv_obj_set_style_bg_color(s_display.clock_cloud, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(s_display.clock_cloud, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_display.clock_cloud, 0, 0);
    lv_obj_clear_flag(s_display.clock_cloud, LV_OBJ_FLAG_SCROLLABLE);

    /* Giờ to (HH:MM) — font lớn nhất. */
    s_display.clock_time = lv_label_create(s_display.clock_panel);
    lv_label_set_text(s_display.clock_time, "--:--");
    lv_obj_set_style_text_color(s_display.clock_time, lv_color_hex(0x1e3a5f), 0);
    lv_obj_set_style_text_font(s_display.clock_time, &lv_font_vimate_48, 0);
    lv_obj_align(s_display.clock_time, LV_ALIGN_CENTER, 0, -18);

    /* Ngày (Thứ ..., dd/mm). */
    s_display.clock_date = lv_label_create(s_display.clock_panel);
    lv_label_set_text(s_display.clock_date, "");
    lv_obj_set_style_text_color(s_display.clock_date, lv_color_hex(0x3b6ea5), 0);
    lv_obj_set_style_text_font(s_display.clock_date, &lv_font_vimate_24, 0);
    lv_obj_set_style_text_align(s_display.clock_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_display.clock_date, LV_ALIGN_CENTER, 0, 40);

    /* 13/09: 7aa7cc cố định (1,9:1 trên nền sáng, 3,6:1 trên slate tối) → lưu lại,
     * apply_clock() đặt màu theo theme (xám sáng trên tối, trắng trên ảnh). */
    lv_obj_t *hint = lv_label_create(s_display.clock_panel);
    s_display.clock_hint = hint;
    lv_label_set_text(hint, "Chạm để về Home");
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_CLR_FG_MUTED), 0);
    lv_obj_set_style_text_font(hint, UI_FONT_LABEL, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -66); /* chừa bar */
}

/* ========== Icon sóng WiFi — góc trên phải ==========
 * 4 vạch cao dần + số dBm (font 14). Ngưỡng theo thông lệ: ≥−60 tốt (4 vạch, xanh),
 * ≥−67 khá (3), ≥−75 yếu (2, cam), còn lại 1 vạch đỏ; mất kết nối → xám + "--".
 * Không đụng layout cũ: nằm trên cùng lớp, không bắt chạm. */
static lv_obj_t *s_wifi_panel, *s_wifi_bar[4], *s_wifi_label;
#define WIFI_ICON_BAR_W   6
#define WIFI_ICON_BAR_GAP 2
#define WIFI_ICON_H       24
#define WIFI_PANEL_W      (4 * (WIFI_ICON_BAR_W + WIFI_ICON_BAR_GAP) + 44)
#define WIFI_PANEL_X      (-8)   /* lề phải */
#define WIFI_PANEL_Y      6

static void wifi_icon_create(lv_obj_t *screen) {
    s_wifi_panel = lv_obj_create(screen);
    lv_obj_set_size(s_wifi_panel, WIFI_PANEL_W, WIFI_ICON_H + 4);
    lv_obj_align(s_wifi_panel, LV_ALIGN_TOP_RIGHT, WIFI_PANEL_X, WIFI_PANEL_Y);
    lv_obj_set_style_bg_opa(s_wifi_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_wifi_panel, 0, 0);
    lv_obj_set_style_pad_all(s_wifi_panel, 0, 0);
    lv_obj_clear_flag(s_wifi_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = lv_obj_create(s_wifi_panel);
        int h = 8 + i * 5;                            /* 8, 13, 18, 23 */
        lv_obj_set_size(b, WIFI_ICON_BAR_W, h);
        lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, i * (WIFI_ICON_BAR_W + WIFI_ICON_BAR_GAP), -2);
        lv_obj_set_style_radius(b, 1, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0xcbd5e1), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        s_wifi_bar[i] = b;
    }
    s_wifi_label = lv_label_create(s_wifi_panel);
    lv_label_set_text(s_wifi_label, "--");
    lv_obj_set_style_text_font(s_wifi_label, &lv_font_vimate_14, 0);
    lv_obj_set_style_text_color(s_wifi_label, lv_color_hex(0x64748b), 0);
    lv_obj_align(s_wifi_label, LV_ALIGN_BOTTOM_LEFT, 4 * (WIFI_ICON_BAR_W + WIFI_ICON_BAR_GAP) + 4, -1);
}

/* arg: rssi (âm) hoặc WIFI_RSSI_NONE khi mất kết nối — gói trong con trỏ, không malloc. */
#define WIFI_RSSI_NONE 0x7fff
static void apply_wifi_rssi(void *arg) {
    int v = (int)(intptr_t)arg;
    if (!s_wifi_panel) return;
    display_lock();
    int bars; uint32_t col;
    if (v == WIFI_RSSI_NONE) {
        bars = 0; col = 0xcbd5e1;
        lv_label_set_text(s_wifi_label, "--");
    } else {
        if (v >= -60) { bars = 4; col = 0x16a34a; }
        else if (v >= -67) { bars = 3; col = 0x16a34a; }
        else if (v >= -75) { bars = 2; col = 0xd97706; }
        else { bars = 1; col = 0xdc2626; }
        lv_label_set_text_fmt(s_wifi_label, "%d", v);
    }
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(s_wifi_bar[i], lv_color_hex(i < bars ? col : 0xcbd5e1), 0);
    }
    lv_obj_set_style_text_color(s_wifi_label, lv_color_hex(v == WIFI_RSSI_NONE ? 0x64748b : col), 0);
    lv_obj_move_foreground(s_wifi_panel);
    display_unlock();
}

void display_set_wifi_rssi(int rssi_dbm, bool connected) {
    if (!s_display.sched_q) return;
    intptr_t v = connected ? rssi_dbm : WIFI_RSSI_NONE;
    display_schedule(apply_wifi_rssi, (void *)v);
}

/* ========== Icon Bluetooth — bên trái icon WiFi ==========
 * Chữ rune Bluetooth vẽ bằng một lv_line 6 điểm (một nét liền: chéo xuống, lên
 * thân, chéo xuống) — không cần font biểu tượng. Hai chấm hai bên = đã nối (quy ước
 * Android). Màu: xám khi tắt/không có radio (P4 không có BT → luôn xám), xanh khi
 * đang quảng bá chờ app, xanh đậm + chấm khi điện thoại đã nối. Không nhấp nháy:
 * icon nằm trên vùng LVGL xoay bằng PPA, mỗi lần đổi là một vùng bẩn. */
#define BT_ICON_W   12
#define BT_ICON_H   22
#define BT_DOT      3
static lv_obj_t *s_bt_panel, *s_bt_line, *s_bt_dot[2];
static lv_point_precise_t s_bt_pts[6];

static void bt_icon_create(lv_obj_t *screen) {
    if (!s_wifi_panel) return;
    s_bt_panel = lv_obj_create(screen);
    const int bt_w = BT_ICON_W + 2 * (BT_DOT + 2);
    lv_obj_set_size(s_bt_panel, bt_w, WIFI_ICON_H + 4);
    /* Đặt theo toạ độ tuyệt đối (không align_to) để không phụ thuộc layout của panel
     * WiFi lúc vừa tạo: sát bên trái panel WiFi, cách 4 px, cùng hàng. */
    lv_obj_align(s_bt_panel, LV_ALIGN_TOP_RIGHT, WIFI_PANEL_X - WIFI_PANEL_W - 4, WIFI_PANEL_Y);
    lv_obj_set_style_bg_opa(s_bt_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_bt_panel, 0, 0);
    lv_obj_set_style_pad_all(s_bt_panel, 0, 0);
    lv_obj_clear_flag(s_bt_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Rune: A(0,¼h) → B(w,¾h) → C(w/2,h) → D(w/2,0) → E(w,¼h) → F(0,¾h) */
    const lv_value_precise_t w = BT_ICON_W - 1, h = BT_ICON_H - 1;
    s_bt_pts[0].x = 0;     s_bt_pts[0].y = h / 4;
    s_bt_pts[1].x = w;     s_bt_pts[1].y = h * 3 / 4;
    s_bt_pts[2].x = w / 2; s_bt_pts[2].y = h;
    s_bt_pts[3].x = w / 2; s_bt_pts[3].y = 0;
    s_bt_pts[4].x = w;     s_bt_pts[4].y = h / 4;
    s_bt_pts[5].x = 0;     s_bt_pts[5].y = h * 3 / 4;
    s_bt_line = lv_line_create(s_bt_panel);
    lv_line_set_points(s_bt_line, s_bt_pts, 6);
    lv_obj_set_size(s_bt_line, BT_ICON_W, BT_ICON_H);
    lv_obj_align(s_bt_line, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_line_width(s_bt_line, 2, 0);
    lv_obj_set_style_line_rounded(s_bt_line, true, 0);
    lv_obj_set_style_line_color(s_bt_line, lv_color_hex(0xcbd5e1), 0);

    for (int i = 0; i < 2; i++) {
        lv_obj_t *d = lv_obj_create(s_bt_panel);
        lv_obj_set_size(d, BT_DOT, BT_DOT);
        lv_obj_align(d, LV_ALIGN_CENTER, i == 0 ? -(BT_ICON_W / 2 + BT_DOT) : (BT_ICON_W / 2 + BT_DOT), 0);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(0x1d4ed8), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
        s_bt_dot[i] = d;
    }
}

static void bt_icon_apply_locked(int st) {
    if (!s_bt_panel) return;
    uint32_t col = 0xcbd5e1;                       /* OFF / UNAVAILABLE */
    if (st == BLE_PROV_STATE_ADVERTISING) col = 0x2563eb;
    else if (st == BLE_PROV_STATE_CONNECTED) col = 0x1d4ed8;
    lv_obj_set_style_line_color(s_bt_line, lv_color_hex(col), 0);
    for (int i = 0; i < 2; i++) {
        if (st == BLE_PROV_STATE_CONNECTED) lv_obj_clear_flag(s_bt_dot[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_bt_dot[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_move_foreground(s_bt_panel);
}

static void apply_ble_state(void *arg) {
    display_lock();
    bt_icon_apply_locked((int)(intptr_t)arg);
    display_unlock();
}

void display_set_ble_state(int state) {
    if (!s_display.sched_q) return;
    display_schedule(apply_ble_state, (void *)(intptr_t)state);
}

/* ========== setup_ui — create ALL widgets ON lv_screen_active() ========== */
esp_err_t display_setup_ui(void) {
    if (s_display.setup_ui_called) {
        ESP_LOGW(TAG_UI, "display_setup_ui called twice — skipping");
        return ESP_OK;
    }
    display_lock();

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xfaf6ed), 0);
    lv_obj_set_style_text_font(screen, &lv_font_vimate_18, 0);

    /* --- Status label (bottom) --- */
    s_display.status_label = lv_label_create(screen);
    lv_label_set_text(s_display.status_label, state_text(DEV_STATE_BOOT));
    lv_obj_set_style_text_color(s_display.status_label,
        lv_color_hex(state_color(DEV_STATE_BOOT)), 0);
    lv_obj_set_style_text_font(s_display.status_label, &lv_font_vimate_18, 0);
    /* Hạ status xuống SÁT ĐÁY; chat overlay nằm ngay trên nó (xem chat_label). */
    lv_obj_align(s_display.status_label, LV_ALIGN_BOTTOM_MID, 0, -4);

    /* Voice indicator riêng: status label sát đáy bị bottom bar Agent che. */
    s_display.voice_state_panel = lv_obj_create(screen);
    lv_obj_set_size(s_display.voice_state_panel, 226, 48);
    lv_obj_align(s_display.voice_state_panel, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_radius(s_display.voice_state_panel, 8, 0);
    lv_obj_set_style_bg_color(s_display.voice_state_panel, lv_color_hex(0xecfdf5), 0);
    lv_obj_set_style_bg_opa(s_display.voice_state_panel, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_display.voice_state_panel, 2, 0);
    lv_obj_set_style_border_color(s_display.voice_state_panel, lv_color_hex(0x22c55e), 0);
    lv_obj_set_style_pad_all(s_display.voice_state_panel, 0, 0);
    lv_obj_clear_flag(s_display.voice_state_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_display.voice_state_panel, LV_OBJ_FLAG_HIDDEN);

    s_display.voice_state_dot = lv_obj_create(s_display.voice_state_panel);
    lv_obj_set_size(s_display.voice_state_dot, 18, 18);
    lv_obj_align(s_display.voice_state_dot, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_set_style_radius(s_display.voice_state_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_display.voice_state_dot, lv_color_hex(0x16a34a), 0);
    lv_obj_set_style_bg_opa(s_display.voice_state_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_display.voice_state_dot, 0, 0);
    lv_obj_clear_flag(s_display.voice_state_dot, LV_OBJ_FLAG_SCROLLABLE);

    s_display.voice_state_label = lv_label_create(s_display.voice_state_panel);
    lv_label_set_text(s_display.voice_state_label, "MIC  Đang nghe...");
    lv_obj_set_style_text_font(s_display.voice_state_label, &lv_font_vimate_18, 0);
    lv_obj_set_style_text_color(s_display.voice_state_label, lv_color_hex(0x166534), 0);
    lv_obj_align(s_display.voice_state_label, LV_ALIGN_LEFT_MID, 46, 0);

    /* --- Emoji image (center) ---
     * Built-in PNG 128×128. set_scale overscan → render ~380×380, crop
     * bởi màn 320×240 để không còn viền trống quanh avatar.
     * KHÔNG dùng inner_align STRETCH (transform_rgb565a8 crash path). */
    s_display.emoji_image = lv_image_create(screen);
    lv_image_set_scale(s_display.emoji_image, EMOJI_SCALE_FACTOR);
    lv_image_set_antialias(s_display.emoji_image, false);
    lv_image_set_inner_align(s_display.emoji_image, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_size(s_display.emoji_image, EMOJI_WIDGET_W, EMOJI_WIDGET_H);
    lv_obj_align(s_display.emoji_image, EMOJI_WIDGET_ALIGN, EMOJI_WIDGET_X, EMOJI_WIDGET_Y);
    lv_obj_clear_flag(s_display.emoji_image, LV_OBJ_FLAG_CLICKABLE);
    /* Render default neutral ngay từ setup — không cần đợi server. */
    lv_image_set_src(s_display.emoji_image, vimate_emotion_image(EMOTION_NEUTRAL));

    /* --- Emoji GIF widget (cùng vị trí, hidden) ---
     * lv_image widget thường — KHÔNG dùng lv_gif. lvgl_gif_t controller giữ
     * canvas ARGB8888 và drive frames qua lv_timer; frame_cb sẽ set_src
     * pointer canvas lên widget này khi 1 frame mới render xong. Scale
     * EMOJI_SCALE_FACTOR pixel-multiply trên ARGB8888 → an toàn, không
     * trigger transform_rgb565a8 crash path. */
    s_display.emoji_gif = lv_image_create(screen);
    lv_image_set_scale(s_display.emoji_gif, EMOJI_SCALE_FACTOR);
    lv_image_set_antialias(s_display.emoji_gif, false);
    lv_image_set_inner_align(s_display.emoji_gif, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_size(s_display.emoji_gif, EMOJI_WIDGET_W, EMOJI_WIDGET_H);
    lv_obj_align(s_display.emoji_gif, EMOJI_WIDGET_ALIGN, EMOJI_WIDGET_X, EMOJI_WIDGET_Y);
    lv_obj_clear_flag(s_display.emoji_gif, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_display.emoji_gif, LV_OBJ_FLAG_HIDDEN);
#if BOARD_FACE_GIF_FULLSCREEN
    /* Mặt robot toàn màn: widget phủ cả màn, nền đen (GIF 800×480 sau nhân đôi phủ
     * kín; board khác cỡ thì phần dư cũng đen), ảnh RGB565 blit 1:1 không scale.
     * PNG 128 px chỉ còn là dự phòng khi GIF lỗi. */
    lv_obj_set_size(s_display.emoji_gif, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_align(s_display.emoji_gif, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_image_set_scale(s_display.emoji_gif, 256);
    lv_obj_set_style_bg_color(s_display.emoji_gif, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_display.emoji_gif, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
#endif

    /* --- Chat label (bottom overlay) ---
     * Chỉ hiện khi có STT/feedback. Container được resize theo text trong
     * apply_chat(), tránh một khối nền cố định che ảnh bài học.
     * BOTTOM_MID: nằm sát đáy và GROW LÊN TRÊN khi text dài → không đè status. */
    s_display.chat_label = lv_obj_create(screen);
    lv_obj_set_size(s_display.chat_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_display.chat_label, LV_ALIGN_BOTTOM_MID, 0, -36);
    lv_obj_set_style_bg_color(s_display.chat_label, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(s_display.chat_label, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_display.chat_label, 0, 0);
    lv_obj_set_style_pad_hor(s_display.chat_label, 12, 0);
    lv_obj_set_style_pad_ver(s_display.chat_label, 8, 0);
    lv_obj_set_style_radius(s_display.chat_label, 12, 0);
    /* Scroll dọc, ẩn scrollbar (clean look). */
    lv_obj_set_scroll_dir(s_display.chat_label, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_display.chat_label, LV_SCROLLBAR_MODE_OFF);
    /* Label thực bên trong container — grow theo content. */
    lv_obj_t *txt = lv_label_create(s_display.chat_label);
    lv_label_set_text(txt, "");
    lv_label_set_long_mode(txt, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(txt, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(txt, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(txt, &lv_font_vimate_18, 0);
    lv_obj_set_style_text_align(txt, LV_TEXT_ALIGN_CENTER, 0);
    /* Save inner label reference qua user_data của container. */
    lv_obj_set_user_data(s_display.chat_label, txt);

    /* --- Preview image (lesson) — persistent widget, HIDDEN default ---
     * EDU dùng CONTAIN để giữ đúng tỷ lệ ảnh bài học; server/app nên upload
     * đúng 480x320 để không phải scale nhiều trên thiết bị. */
    s_display.preview_image = lv_image_create(screen);
    lv_obj_set_size(s_display.preview_image, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_image_set_inner_align(s_display.preview_image, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_align(s_display.preview_image, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_display.preview_image, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_display.preview_image, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_display.preview_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_display.preview_image, LV_OBJ_FLAG_HIDDEN);

    /* --- Popup (activation / error / message) — HIDDEN default --- */
    s_display.popup_panel = lv_obj_create(screen);
    lv_obj_set_size(s_display.popup_panel, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_align(s_display.popup_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_display.popup_panel, lv_color_hex(0xede9fe), 0);
    lv_obj_set_style_bg_opa(s_display.popup_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_display.popup_panel, 0, 0);
    lv_obj_set_style_pad_all(s_display.popup_panel, 8, 0);
    lv_obj_clear_flag(s_display.popup_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_display.popup_panel, LV_OBJ_FLAG_HIDDEN);

    s_display.popup_title = lv_label_create(s_display.popup_panel);
    lv_label_set_text(s_display.popup_title, "");
    lv_obj_set_style_text_color(s_display.popup_title, lv_color_hex(0xffd166), 0);
    lv_obj_set_style_text_font(s_display.popup_title, &lv_font_vimate_24, 0);
    lv_obj_align(s_display.popup_title, LV_ALIGN_TOP_MID, 0, 6);

    s_display.popup_brand = lv_label_create(s_display.popup_panel);
    lv_label_set_text(s_display.popup_brand, VIMATE_BRAND_NAME);
    lv_obj_set_style_text_font(s_display.popup_brand, &lv_font_vimate_48, 0);
    lv_obj_align(s_display.popup_brand, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_text_color(s_display.popup_brand, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_text_align(s_display.popup_brand, LV_TEXT_ALIGN_CENTER, 0);

    /* Mã kích hoạt (SỐ) + QR là CORE — mọi board hiển thị để bind tài khoản.
     * Layout co theo kích thước màn (BOARD_LCD_V_RES): màn nhỏ (320x240)
     * số+QR gọn; màn lớn (3.5" 480) số+QR to. App "Quét QR" đọc QR (encode
     * raw mã; app _activationCodeFromQr nhận raw) hoặc nhập tay SỐ.
     * LV_USE_QRCODE bật ở base sdkconfig.defaults nên mọi build có. */
    s_display.popup_code = lv_label_create(s_display.popup_panel);
    lv_label_set_text(s_display.popup_code, "");
#if BOARD_LCD_V_RES <= 320
    lv_obj_set_style_text_font(s_display.popup_code, &lv_font_vimate_24, 0);
    lv_obj_set_style_text_letter_space(s_display.popup_code, 3, 0);
    lv_obj_align(s_display.popup_code, LV_ALIGN_CENTER, 0, -68);
#else
    lv_obj_set_style_text_font(s_display.popup_code, &lv_font_vimate_48, 0);
    lv_obj_set_style_text_letter_space(s_display.popup_code, 8, 0);
    lv_obj_align(s_display.popup_code, LV_ALIGN_CENTER, 0, -150);
#endif
    lv_obj_set_style_text_color(s_display.popup_code, lv_color_hex(0x1f2937), 0);
    lv_obj_add_flag(s_display.popup_code, LV_OBJ_FLAG_HIDDEN);

    s_display.popup_qr = lv_qrcode_create(s_display.popup_panel);
#if BOARD_LCD_V_RES <= 320
    lv_qrcode_set_size(s_display.popup_qr, 108);
    lv_obj_align(s_display.popup_qr, LV_ALIGN_CENTER, 0, 34);
#else
    lv_qrcode_set_size(s_display.popup_qr, 240);
    lv_obj_align(s_display.popup_qr, LV_ALIGN_CENTER, 0, 70);
#endif
    lv_qrcode_set_dark_color(s_display.popup_qr, lv_color_hex(0x111827));
    lv_qrcode_set_light_color(s_display.popup_qr, lv_color_hex(0xffffff));
    lv_obj_set_style_border_color(s_display.popup_qr, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(s_display.popup_qr, 8, 0);
    lv_obj_add_flag(s_display.popup_qr, LV_OBJ_FLAG_HIDDEN);

    s_display.popup_body = lv_label_create(s_display.popup_panel);
    lv_label_set_long_mode(s_display.popup_body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_display.popup_body, "");
    lv_obj_set_width(s_display.popup_body, BOARD_LCD_H_RES - 32);
    lv_obj_set_style_text_color(s_display.popup_body, lv_color_hex(0x374151), 0);
    lv_obj_set_style_text_font(s_display.popup_body, &lv_font_vimate_18, 0);
    lv_obj_set_style_text_align(s_display.popup_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_display.popup_body, LV_ALIGN_BOTTOM_MID, 0, -10);

    /* --- Reward overlay --- */
    s_display.reward_panel = lv_obj_create(screen);
    lv_obj_set_size(s_display.reward_panel, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_align(s_display.reward_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_display.reward_panel, lv_color_hex(0x000000), 0);
#if BOARD_FACE_GIF_FULLSCREEN
    /* Mặt robot: pháo hoa sym_celebrate chạy dưới lớp sao → làm mờ nhẹ thôi. */
    lv_obj_set_style_bg_opa(s_display.reward_panel, LV_OPA_30, 0);
#else
    lv_obj_set_style_bg_opa(s_display.reward_panel, LV_OPA_70, 0);
#endif
    lv_obj_set_style_border_width(s_display.reward_panel, 0, 0);
    lv_obj_clear_flag(s_display.reward_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_display.reward_panel, LV_OBJ_FLAG_HIDDEN);

    /* 13/09: 5 icon sao (icon_star trắng + recolor) thay chuỗi "* . " 48 px — trẻ
     * nhận ra hình sao, không phải dấu hoa thị (MASTER.md §6.4). Hàng 5×44 + gap 8,
     * căn giữa, cách nhau 52 px. Màu đặt lại trong apply_reward(). */
    for (int i = 0; i < 5; i++) {
        lv_obj_t *im = lv_image_create(s_display.reward_panel);
        lv_image_set_src(im, &icon_star);
        lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
        lv_obj_set_style_image_recolor(im, lv_color_hex(UI_CLR_STAR_EMPTY), 0);
        lv_obj_align(im, LV_ALIGN_CENTER, (i - 2) * (44 + UI_SP_2), -24);
        lv_obj_clear_flag(im, LV_OBJ_FLAG_CLICKABLE);
        s_display.reward_star_img[i] = im;
    }

    s_display.reward_msg = lv_label_create(s_display.reward_panel);
    lv_label_set_text(s_display.reward_msg, "");
    lv_obj_set_style_text_color(s_display.reward_msg, lv_color_hex(UI_CLR_ON_PRIMARY), 0);
    lv_obj_set_style_text_font(s_display.reward_msg, UI_FONT_BODY, 0);
    lv_obj_align(s_display.reward_msg, LV_ALIGN_CENTER, 0, 40);

	home_create_ui(screen); /* Pha C: lưới home (edu); ẩn mặc định */
	quiz_create_ui(screen); /* EDU: màn trắc nghiệm 4 nút; ẩn mặc định */
	stats_create_ui(screen); /* EDU: màn thống kê tuần; ẩn mặc định */
	water_create_ui(screen); /* EDU: màn uống nước 2 nút; ẩn mặc định */
	progress_create_ui(screen); /* EDU: màn lộ trình học; ẩn mặc định */
	timetable_create_ui(screen); /* EDU: bảng thời khóa biểu 3.5"; ẩn mặc định */
	alarm_create_ui(screen); /* màn nhắc Hẹn giờ/Uống nước; ẩn mặc định */
	countdown_create_ui(screen); /* app Đếm ngược; ẩn mặc định */
	clock_create_ui(screen); /* app Đồng hồ; ẩn mặc định */
	bottombar_create(screen); /* bottom bar GLOBAL (mọi màn menu/tiện ích); ẩn mặc định */
	nav_home_create(screen); /* nút Home tròn giữa; ẩn mặc định */
	idle_clock_create(screen); /* đồng hồ màn chờ (rest) thay mặt cười; ẩn mặc định */

    /* --- esp_timer hide handlers --- */
    const esp_timer_create_args_t prev_args = {
        .callback = on_preview_hide_timer,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "vimate_prev_hide",
    };
    ESP_ERROR_CHECK(esp_timer_create(&prev_args, &s_display.preview_hide_timer));
    const esp_timer_create_args_t rew_args = {
        .callback = on_reward_hide_timer,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "vimate_rew_hide",
    };
    ESP_ERROR_CHECK(esp_timer_create(&rew_args, &s_display.reward_hide_timer));
    const esp_timer_create_args_t chat_args = {
        .callback = on_chat_hide_timer,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "vimate_chat_hide",
    };
    ESP_ERROR_CHECK(esp_timer_create(&chat_args, &s_display.chat_hide_timer));
    const esp_timer_create_args_t popup_args = {
        .callback = on_popup_hide_timer,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "vimate_popup_hide",
    };
    ESP_ERROR_CHECK(esp_timer_create(&popup_args, &s_display.popup_hide_timer));
    const esp_timer_create_args_t sleep_args = {
        .callback = on_sleep_timer,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "vimate_lcd_sleep",
    };
    ESP_ERROR_CHECK(esp_timer_create(&sleep_args, &s_display.sleep_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_display.sleep_timer, 5 * 1000 * 1000ULL));
    const esp_timer_create_args_t alarm_args = {
        .callback = on_alarm_hide_timer,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "vimate_alarm_hide",
	};
	ESP_ERROR_CHECK(esp_timer_create(&alarm_args, &s_display.alarm_hide_timer));
	const esp_timer_create_args_t countdown_args = {
		.callback = on_countdown_timer,
		.arg = NULL,
		.dispatch_method = ESP_TIMER_TASK,
		.name = "vimate_countdown",
	};
	ESP_ERROR_CHECK(esp_timer_create(&countdown_args, &s_display.countdown_timer));
	const esp_timer_create_args_t clock_args = {
        .callback = on_clock_timer,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "vimate_clock_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&clock_args, &s_display.clock_timer));
    const esp_timer_create_args_t ss_args = {
        .callback = on_ss_timer,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "vimate_slideshow",
    };
    ESP_ERROR_CHECK(esp_timer_create(&ss_args, &s_display.ss_timer));

    wifi_icon_create(screen);   /* góc trên phải, luôn trên cùng */
    bt_icon_create(screen);     /* bên trái icon WiFi; trạng thái ban đầu từ core */
    bt_icon_apply_locked(ble_wifi_prov_state());   /* setup_ui đang giữ display_lock */

#if BOARD_FACE_GIF_FULLSCREEN
    face_init_locked();   /* mount emo_spiffs, boot_up, lv_timer idle, preload cache */
#endif
    s_display.setup_ui_called = true;
    display_unlock();
    ESP_LOGI(TAG_UI, "display_setup_ui done");
    return ESP_OK;
}

/* ===========================================================
 *  Schedule callbacks (run in display_task)
 *  Mỗi callback wrap LVGL ops trong lock — defense in depth.
 *  display_task (Core 0 / pri 6) và LVGL render task (giờ cũng Core 0 / pri 6)
 *  serialize trên cùng 1 lvgl_port mutex.
 * =========================================================== */
typedef struct { vimate_dev_state_t s; } sched_state_t;
/* Ẩn overlay đồng hồ/nhắc + DỪNG timer của chúng khi 1 màn nội dung khác chiếm
 * chỗ (emotion/chat/preview/popup/home/alarm). Tránh clock_panel (đã move_foreground)
 * che màn mới + tránh clock_timer tick mồ côi. GỌI KHI ĐANG GIỮ display_lock(). */
static void overlays_dismiss_locked(void) {
    if (s_display.clock_panel &&
        !lv_obj_has_flag(s_display.clock_panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(s_display.clock_panel, LV_OBJ_FLAG_HIDDEN);
        if (s_display.clock_timer) esp_timer_stop(s_display.clock_timer);
        /* Nếu đang dùng ảnh nền → gỡ luôn ảnh (preview_image). */
        if (s_display.clock_wallpaper && s_display.preview_image) {
            lv_obj_add_flag(s_display.preview_image, LV_OBJ_FLAG_HIDDEN);
        }
        s_display.clock_wallpaper = false;
    }
	if (s_display.alarm_panel &&
	    !lv_obj_has_flag(s_display.alarm_panel, LV_OBJ_FLAG_HIDDEN)) {
		lv_obj_add_flag(s_display.alarm_panel, LV_OBJ_FLAG_HIDDEN);
		if (s_display.alarm_hide_timer) esp_timer_stop(s_display.alarm_hide_timer);
	}
	if (s_display.countdown_panel &&
	    !lv_obj_has_flag(s_display.countdown_panel, LV_OBJ_FLAG_HIDDEN)) {
		lv_obj_add_flag(s_display.countdown_panel, LV_OBJ_FLAG_HIDDEN);
		if (s_display.countdown_timer) esp_timer_stop(s_display.countdown_timer);
		s_display.countdown_active = false;
	}
    if (s_display.timetable_panel &&
        !lv_obj_has_flag(s_display.timetable_panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(s_display.timetable_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Màn APP toàn màn (Home/Lịch/Nước/Thống kê/Lộ trình/Đồng hồ/Đếm ngược) thay nhau:
 * màn mới tới thì ẩn TẤT CẢ màn cũ ngay trong cùng một lần lock — touch.c không
 * ẩn trước nữa (13/09/2026: ẩn trước → màn trống 150–400 ms chờ server, người dùng
 * thấy "load lại cả màn"). Không dùng cho emotion/chat/popup: quiz/nước phải sống
 * qua đổi cảm xúc. */
static void screens_dismiss_locked(void) {
    overlays_dismiss_locked();
    if (s_display.stats_panel)    lv_obj_add_flag(s_display.stats_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_display.water_panel)    lv_obj_add_flag(s_display.water_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_display.progress_panel) lv_obj_add_flag(s_display.progress_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_display.quiz_panel)     lv_obj_add_flag(s_display.quiz_panel, LV_OBJ_FLAG_HIDDEN);
}

/* Dừng DỨT KHOÁT slideshow màn chờ (cờ + timer) khi 1 màn nội dung mới
 * (emotion/chat/popup/home) chiếm chỗ — nếu không, tick ss_timer kế tiếp sẽ
 * bật ảnh gia đình đè lại lên nội dung sau ~1 interval. GỌI KHI ĐANG GIỮ
 * display_lock(). KHÔNG gọi từ chính path slideshow (apply_clock/apply_preview
 * khi clock_wallpaper) để slideshow không tự tắt ngay khi vừa bật. */
static void slideshow_interrupt_locked(void) {
    if (s_display.ss_active) {
        if (s_display.ss_timer) esp_timer_stop(s_display.ss_timer);
        s_display.ss_active = false;
    }
}

static void apply_state(void *arg) {
    sched_state_t *p = (sched_state_t *)arg;
    if (!s_display.setup_ui_called || !s_display.status_label) goto done;
    display_note_activity();
    display_lock();
#if BOARD_FACE_GIF_FULLSCREEN
    face_on_state_locked(p->s);
#endif
    lv_label_set_text(s_display.status_label, state_text(p->s));
    lv_obj_set_style_text_color(s_display.status_label,
        lv_color_hex(state_color(p->s)), 0);
    if (s_display.voice_state_panel && s_display.voice_state_label &&
        s_display.voice_state_dot) {
        bool show = p->s == DEV_STATE_LISTENING || p->s == DEV_STATE_THINKING;
        if (p->s == DEV_STATE_LISTENING) {
            lv_label_set_text(s_display.voice_state_label, "MIC  Đang nghe...");
            lv_obj_set_style_bg_color(s_display.voice_state_panel,
                                      lv_color_hex(0xecfdf5), 0);
            lv_obj_set_style_border_color(s_display.voice_state_panel,
                                          lv_color_hex(0x22c55e), 0);
            lv_obj_set_style_bg_color(s_display.voice_state_dot,
                                      lv_color_hex(0x16a34a), 0);
            lv_obj_set_style_text_color(s_display.voice_state_label,
                                        lv_color_hex(0x166534), 0);
        } else if (p->s == DEV_STATE_THINKING) {
            lv_label_set_text(s_display.voice_state_label, "Đang xử lý...");
            lv_obj_set_style_bg_color(s_display.voice_state_panel,
                                      lv_color_hex(0xfffbeb), 0);
            lv_obj_set_style_border_color(s_display.voice_state_panel,
                                          lv_color_hex(0xf59e0b), 0);
            lv_obj_set_style_bg_color(s_display.voice_state_dot,
                                      lv_color_hex(0xf59e0b), 0);
            lv_obj_set_style_text_color(s_display.voice_state_label,
                                        lv_color_hex(0x92400e), 0);
        }
        if (show) {
            lv_obj_clear_flag(s_display.voice_state_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_display.voice_state_panel);
        } else {
            lv_obj_add_flag(s_display.voice_state_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    display_unlock();
done:
    free(p);
}

typedef struct { vimate_emotion_t e; } sched_emotion_t;

#if !BOARD_FACE_GIF_FULLSCREEN
/* Embedded GIFs — declared by EMBED_FILES CMake macro.
 * Memory-mapped flash, zero copy needed. lv_gif_set_src(widget, &dsc) drive
 * frames đều khi dsc.data trỏ vào flash region.
 * P4 mặt toàn màn: không embed (main/CMakeLists.txt), bảng này không biên dịch. */
#define EMBED_GIF(name) \
    extern const uint8_t _binary_##name##_gif_start[] asm("_binary_" #name "_gif_start"); \
    extern const uint8_t _binary_##name##_gif_end[]   asm("_binary_" #name "_gif_end")
EMBED_GIF(neutral); EMBED_GIF(happy); EMBED_GIF(sad); EMBED_GIF(angry);
EMBED_GIF(confused); EMBED_GIF(surprised); EMBED_GIF(sleepy);
EMBED_GIF(embarrassed); EMBED_GIF(thinking); EMBED_GIF(relaxed);
EMBED_GIF(funny); EMBED_GIF(delicious); EMBED_GIF(loving);

typedef struct { const char *key; const uint8_t *start; const uint8_t *end; } gif_entry_t;
static const gif_entry_t s_embedded_gifs[] = {
    {"neutral",     _binary_neutral_gif_start,     _binary_neutral_gif_end},
    {"happy",       _binary_happy_gif_start,       _binary_happy_gif_end},
    {"sad",         _binary_sad_gif_start,         _binary_sad_gif_end},
    {"angry",       _binary_angry_gif_start,       _binary_angry_gif_end},
    {"confused",    _binary_confused_gif_start,    _binary_confused_gif_end},
    {"surprised",   _binary_surprised_gif_start,   _binary_surprised_gif_end},
    {"sleepy",      _binary_sleepy_gif_start,      _binary_sleepy_gif_end},
    {"embarrassed", _binary_embarrassed_gif_start, _binary_embarrassed_gif_end},
    {"thinking",    _binary_thinking_gif_start,    _binary_thinking_gif_end},
    {"relaxed",     _binary_relaxed_gif_start,     _binary_relaxed_gif_end},
    {"funny",       _binary_funny_gif_start,       _binary_funny_gif_end},
    {"delicious",   _binary_delicious_gif_start,   _binary_delicious_gif_end},
    {"loving",      _binary_loving_gif_start,      _binary_loving_gif_end},
};
#define EMBEDDED_GIF_COUNT (sizeof(s_embedded_gifs)/sizeof(s_embedded_gifs[0]))
#endif /* !BOARD_FACE_GIF_FULLSCREEN */

#if !BOARD_FACE_GIF_FULLSCREEN
/* Static dsc — chỉ trỏ pointer, không alloc buffer. */
static lv_image_dsc_t s_gif_dsc = {0};
static uint8_t       *s_custom_gif_data = NULL;
static size_t         s_custom_gif_size = 0;
static time_t         s_custom_gif_mtime = 0;
#endif

/* Cache GIF controller theo key — destroy + create lại khi đổi emotion.
 * Tránh leak: 1 emotion active → 1 controller alive. */
static lvgl_gif_t *s_gif_controller     = NULL;
static char        s_gif_controller_key[32] = {0};

/* Frame callback chạy trong LVGL task (lv_timer context).
 * lvgl_gif_t publish frame qua double-buffer ổn định, nên set_src sang dsc mới
 * an toàn, không còn LVGL đọc canvas đang bị decode frame kế tiếp. */
static void on_gif_frame(void *user_data) {
    lvgl_gif_t *ctrl = (lvgl_gif_t *)user_data;
    if (!ctrl || !s_display.emoji_gif) return;
    const lv_image_dsc_t *dsc = lvgl_gif_image_dsc(ctrl);
    if (!dsc) return;
#if BOARD_FACE_GIF_FULLSCREEN
    /* Mặt toàn màn: buffer RGB565 duy nhất, src đã gắn lúc show → chỉ đánh dấu hộp
     * bẩn của khung (mắt chớp ≈ 570×250 px thay vì 800×480: LVGL blit + PPA xoay
     * đúng phần đó). set_src lại = invalidate cả widget = vẽ + xoay cả màn mỗi khung. */
    lv_area_t dirty;
    if (lvgl_gif_last_dirty(ctrl, &dirty)) {
        lv_area_t coords;
        lv_obj_get_coords(s_display.emoji_gif, &coords);
        int32_t ox = coords.x1 + (lv_area_get_width(&coords) - (int32_t)dsc->header.w) / 2;
        int32_t oy = coords.y1 + (lv_area_get_height(&coords) - (int32_t)dsc->header.h) / 2;
        lv_area_move(&dirty, ox, oy);
        lv_obj_invalidate_area(s_display.emoji_gif, &dirty);
        return;
    }
#endif
    lv_image_set_src(s_display.emoji_gif, dsc);
}

static bool str_ends_with(const char *s, const char *suffix) {
    if (!s || !suffix) return false;
    size_t sl = strlen(s);
    size_t pl = strlen(suffix);
    return sl >= pl && strcasecmp(s + sl - pl, suffix) == 0;
}

static void destroy_gif_controller_locked(void) {
    if (!s_gif_controller) return;
#if BOARD_FACE_GIF_FULLSCREEN
    {   /* số đo mỗi clip (README-P4 §6.5b): khung đã vẽ, µs decode+vẽ, px hộp bẩn */
        uint32_t fr = 0, us = 0, px = 0;
        lvgl_gif_take_stats(s_gif_controller, &fr, &us, &px);
        if (fr) {
            ESP_LOGI(TAG_UI, "face: %s xong frames=%u decode=%u us/khung dirty=%u px/khung",
                     s_gif_controller_key, (unsigned)fr, (unsigned)(us / fr), (unsigned)(px / fr));
        }
    }
#endif
    lvgl_gif_destroy(s_gif_controller);
    s_gif_controller = NULL;
    s_gif_controller_key[0] = '\0';
}

/* Cache GIF cảm xúc trong PSRAM: mỗi lượt hội thoại đổi mặt 3–4 lần, mỗi lần đọc lại
 * 25–32 KB từ SPIFFS (log 13/09 §9.6). 13 mặt ≈ 400 KB PSRAM (còn 23 MB). Khoá theo
 * key + size + mtime nên pack đồng bộ từ server thay file vẫn nạp lại đúng. */
#if BOARD_FACE_GIF_FULLSCREEN
#define GIF_CACHE_N 40   /* 32 mặt robot (2,9 MB PSRAM khi nạp hết) + dư */
#else
#define GIF_CACHE_N 24
#endif
typedef struct { char key[24]; uint8_t *data; size_t size; time_t mtime; } gif_cache_t;
static gif_cache_t s_gif_cache[GIF_CACHE_N];
#if !BOARD_FACE_GIF_FULLSCREEN
static bool s_custom_gif_cached = false;   /* s_custom_gif_data thuộc cache → không free */
#endif

static gif_cache_t *gif_cache_slot(const char *key, bool take_free) {
    gif_cache_t *free_slot = NULL;
    for (int i = 0; i < GIF_CACHE_N; i++) {
        if (s_gif_cache[i].data && strcmp(s_gif_cache[i].key, key) == 0) return &s_gif_cache[i];
        if (!s_gif_cache[i].data && !free_slot) free_slot = &s_gif_cache[i];
    }
    return take_free ? free_slot : NULL;
}

#if !BOARD_FACE_GIF_FULLSCREEN
static void release_custom_gif_data(void) {
    if (s_custom_gif_data && !s_custom_gif_cached) {
        free(s_custom_gif_data);
    }
    s_custom_gif_data = NULL;
    s_custom_gif_cached = false;
    s_custom_gif_size = 0;
    s_custom_gif_mtime = 0;
}

static bool custom_gif_path_for_key(const char *key, char *out, size_t out_len,
                                    size_t *out_size, time_t *out_mtime) {
    const char *path = emotion_sync_path(key);
    if (!path || !str_ends_with(path, ".gif")) return false;
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0) return false;
    if ((size_t)st.st_size > (460 * 1024)) {
        ESP_LOGW(TAG_UI, "custom GIF too large: %s (%u bytes)", key, (unsigned)st.st_size);
        return false;
    }
    snprintf(out, out_len, "%s", path);
    if (out_size) *out_size = (size_t)st.st_size;
    if (out_mtime) *out_mtime = st.st_mtime;
    return true;
}

static bool load_custom_gif_from_file(const char *key, const char *path, size_t size, time_t mtime) {
    gif_cache_t *ce = gif_cache_slot(key, false);
    if (ce && ce->size == size && ce->mtime == mtime) {
        s_custom_gif_data = ce->data;
        s_custom_gif_size = size;
        s_custom_gif_mtime = mtime;
        s_custom_gif_cached = true;
        memset(&s_gif_dsc, 0, sizeof(s_gif_dsc));
        s_gif_dsc.data = s_custom_gif_data;
        s_gif_dsc.data_size = s_custom_gif_size;
        ESP_LOGD(TAG_UI, "custom GIF cache: %s (%u bytes)", key, (unsigned)size);
        return true;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG_UI, "open custom GIF fail: %s", path);
        return false;
    }
    uint8_t *buf = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = (uint8_t *)malloc(size);
    }
    if (!buf) {
        fclose(f);
        ESP_LOGW(TAG_UI, "alloc custom GIF fail: %s (%u bytes)", key, (unsigned)size);
        return false;
    }
    size_t n = fread(buf, 1, size, f);
    fclose(f);
    if (n != size) {
        free(buf);
        ESP_LOGW(TAG_UI, "read custom GIF short: %s (%u/%u)", key, (unsigned)n, (unsigned)size);
        return false;
    }
    s_custom_gif_data = buf;
    s_custom_gif_size = size;
    s_custom_gif_mtime = mtime;
    s_custom_gif_cached = false;
    /* Cất vào cache (thay bản cũ nếu file đổi). Hết chỗ → dùng như trước, free khi đổi mặt. */
    if (!ce) ce = gif_cache_slot(key, true);
    if (ce) {
        if (ce->data && ce->data != buf) free(ce->data);
        strlcpy(ce->key, key, sizeof(ce->key));
        ce->data = buf; ce->size = size; ce->mtime = mtime;
        s_custom_gif_cached = true;
    }
    memset(&s_gif_dsc, 0, sizeof(s_gif_dsc));
    s_gif_dsc.data = s_custom_gif_data;
    s_gif_dsc.data_size = s_custom_gif_size;
    ESP_LOGI(TAG_UI, "custom GIF loaded: %s (%u bytes)%s", key, (unsigned)size,
             s_custom_gif_cached ? " -> cache PSRAM" : "");
    return true;
}

static bool show_gif_locked(const char *controller_key) {
    bool same_key = (s_gif_controller != NULL) &&
                    (strcmp(s_gif_controller_key, controller_key) == 0);
    if (!same_key) {
        destroy_gif_controller_locked();
        s_gif_controller = lvgl_gif_create(&s_gif_dsc);
        if (s_gif_controller) {
            strncpy(s_gif_controller_key, controller_key, sizeof(s_gif_controller_key) - 1);
            s_gif_controller_key[sizeof(s_gif_controller_key) - 1] = '\0';
            lvgl_gif_set_frame_cb(s_gif_controller, on_gif_frame, s_gif_controller);
        }
    }
    if (!s_gif_controller || !lvgl_gif_is_loaded(s_gif_controller)) {
        return false;
    }
    const lv_image_dsc_t *frame_dsc = lvgl_gif_image_dsc(s_gif_controller);
    if (frame_dsc) {
        int32_t src_w = frame_dsc->header.w > 0 ? frame_dsc->header.w : EMOJI_PNG_SIZE;
        lv_image_set_scale(s_display.emoji_gif, 256 * EMOJI_TARGET_PX / src_w);
        lv_image_set_src(s_display.emoji_gif, frame_dsc);
    }
    lv_obj_clear_flag(s_display.emoji_gif, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
    lvgl_gif_start(s_gif_controller);
    return true;
}

static bool find_embedded_gif(const char *key) {
    for (size_t i = 0; i < EMBEDDED_GIF_COUNT; i++) {
        if (strcmp(s_embedded_gifs[i].key, key) == 0) {
            size_t sz = s_embedded_gifs[i].end - s_embedded_gifs[i].start;
            memset(&s_gif_dsc, 0, sizeof(s_gif_dsc));
            s_gif_dsc.data      = s_embedded_gifs[i].start;
            s_gif_dsc.data_size = sz;
            ESP_LOGI(TAG_UI, "embedded GIF found: %s (%u bytes) ptr=%p",
                     key, (unsigned)sz, s_embedded_gifs[i].start);
            return true;
        }
    }
    ESP_LOGW(TAG_UI, "no embedded GIF for %s", key);
    return false;
}
#endif /* !BOARD_FACE_GIF_FULLSCREEN */

#if BOARD_FACE_GIF_FULLSCREEN
/* ===================== Mặt robot toàn màn (P4 4.3", 15/09/2026) =====================
 *
 * 32 clip GIF 400×240 nền đen (spiffs_face_image/, flash vào emo_spiffs) đọc từ
 * /spiffs_emo/<key>.gif vào cache PSRAM (bảng s_gif_cache dùng chung) rồi giao cho
 * lvgl_gif ở chế độ opaque ×2: một buffer RGB565 800×480 (768 KB PSRAM), mỗi khung
 * chỉ chuyển + invalidate hộp bẩn. Bảng clip, map cảm xúc/trạng thái ở face.c.
 *
 * Ba lớp chọn mặt (face_resolve): phản ứng một lượt (reaction) > biểu tượng tính năng
 * (overlay, giữ tới khi bỏ) > mặt nền theo trạng thái (base). Clip ONCE chạy hết →
 * lvgl_gif báo done_cb (LVGL task) → apply_face_done (display task) → bỏ reaction →
 * chọn lại. Idle: lv_timer 1 s chen idle_* ngẫu nhiên khi READY rảnh; cũng dùng để
 * tạm dừng decode khi có màn khác (Home/bài học/popup…) che mặt.
 */
typedef enum { FACE_RK_NONE = 0, FACE_RK_ENTRY, FACE_RK_EMOTION, FACE_RK_IDLE } face_rk_t;

typedef struct {
    int         state;            /* vimate_dev_state_t hiện tại; -1 lúc chưa có */
    face_id_t   base;             /* mặt nền theo state */
    face_id_t   current;          /* clip đang gắn vào controller */
    face_id_t   reaction;         /* clip một lượt đang chạy */
    face_rk_t   reaction_kind;    /* ai đặt reaction: vào trạng thái / cảm xúc / idle */
    face_id_t   overlay;          /* biểu tượng tính năng (sym_*) giữ tới khi bỏ */
    bool        hidden;           /* đồng hồ màn chờ đã ẩn widget → controller pause */
    bool        covered;          /* màn khác che mặt (tick 1 s) → controller pause */
    lv_timer_t *idle_timer;
    int64_t     next_variation_us;/* mốc chen clip idle kế */
    int         preload_idx;
    size_t      preload_bytes;
    esp_timer_handle_t preload_timer;   /* nghỉ giữa hai bước preload */
    uint32_t    tick_n;           /* đếm tick 1 s để log thống kê mỗi 10 s */
} face_ctx_t;
static face_ctx_t s_face = { .state = -1 };
static lv_image_dsc_t s_face_dsc = {0};   /* trỏ vào cache PSRAM, không alloc */

static void apply_face_done(void *arg);
static void apply_face_idle(void *arg);
static void apply_face_preload(void *arg);

/* Đọc /spiffs_emo/<key>.gif vào PSRAM một lần, giữ trong s_gif_cache (không thu hồi:
 * bộ 32 file cố định ≈ 2,9 MB, PSRAM còn > 20 MB). Trả NULL nếu thiếu file / hết slot.
 * Không rơi về RAM nội: 30–145 KB/file sẽ giết heap nội 53 KB. */
static const uint8_t *face_cache_get(face_id_t id, size_t *out_size) {
    const char *key = face_key(id);
    if (!key) return NULL;
    gif_cache_t *ce = gif_cache_slot(key, false);
    if (ce) {
        if (out_size) *out_size = ce->size;
        return ce->data;
    }
    const char *path = emotion_sync_path(key);   /* "/spiffs_emo/<key>.gif" nếu có */
    struct stat st;
    if (!path || !str_ends_with(path, ".gif") || stat(path, &st) != 0 || st.st_size <= 0) {
        ESP_LOGW(TAG_UI, "face: thieu file %s.gif trong emo_spiffs", key);
        return NULL;
    }
    if ((size_t)st.st_size > (460 * 1024)) {
        ESP_LOGW(TAG_UI, "face: %s qua co (%u B)", key, (unsigned)st.st_size);
        return NULL;
    }
    ce = gif_cache_slot(key, true);
    if (!ce) {
        ESP_LOGW(TAG_UI, "face: het slot cache (%d) cho %s", GIF_CACHE_N, key);
        return NULL;
    }
    int64_t t0 = esp_timer_get_time();
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG_UI, "face: mo %s that bai", path);
        return NULL;
    }
    uint8_t *buf = (uint8_t *)heap_caps_malloc((size_t)st.st_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        fclose(f);
        ESP_LOGE(TAG_UI, "face: PSRAM het cho %s (%u B)", key, (unsigned)st.st_size);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);
    if (n != (size_t)st.st_size) {
        heap_caps_free(buf);
        ESP_LOGW(TAG_UI, "face: doc %s thieu (%u/%u)", key, (unsigned)n, (unsigned)st.st_size);
        return NULL;
    }
    strlcpy(ce->key, key, sizeof(ce->key));
    ce->data = buf;
    ce->size = (size_t)st.st_size;
    ce->mtime = st.st_mtime;
    s_face.preload_bytes += ce->size;
    ESP_LOGI(TAG_UI, "face: nap %s %u B tu SPIFFS %lld ms -> cache PSRAM", key,
             (unsigned)ce->size, (long long)((esp_timer_get_time() - t0) / 1000));
    if (out_size) *out_size = ce->size;
    return buf;
}

static void face_gif_done_cb(void *user) {
    (void)user;
    /* LVGL task → sang display task (có lock) để đổi clip; kèm id để bỏ qua nếu clip
     * đã bị thay trong lúc chờ hàng đợi. */
    display_schedule(apply_face_done, (void *)(intptr_t)s_face.current);
}

static const char *face_mode_str(face_id_t id) {
    switch (face_play_mode(id)) {
        case FACE_PLAY_ONCE:      return "once";
        case FACE_PLAY_ONCE_HOLD: return "once+hold";
        default:                  return "loop";
    }
}

/* Gắn clip vào controller và chạy. Cùng clip đang chạy → chạy lại từ đầu. */
static bool face_play_locked(face_id_t id) {
    if (id == FACE_NONE || !s_display.emoji_gif) return false;
    const char *key = face_key(id);
    bool loop = face_play_mode(id) == FACE_PLAY_LOOP;
    bool can_run = !s_face.hidden && !s_face.covered;
    if (s_face.current == id && s_gif_controller) {
        lvgl_gif_stop(s_gif_controller);            /* rewind + publish khung 1 */
        lvgl_gif_set_loop(s_gif_controller, loop);
        if (can_run) lvgl_gif_start(s_gif_controller);
        lv_obj_invalidate(s_display.emoji_gif);
        return true;
    }
    size_t size = 0;
    const uint8_t *data = face_cache_get(id, &size);
    if (!data || size < 10) return false;
    destroy_gif_controller_locked();
    memset(&s_face_dsc, 0, sizeof(s_face_dsc));
    s_face_dsc.data      = data;
    s_face_dsc.data_size = size;
    /* Hệ số nhân nguyên lớn nhất còn lọt màn logical: 400×240 ×2 = 800×480. */
    uint32_t gw = data[6] | ((uint32_t)data[7] << 8);
    uint32_t gh = data[8] | ((uint32_t)data[9] << 8);
    uint8_t scale = BOARD_FACE_GIF_SCALE;
    while (scale > 1 && (gw * scale > (uint32_t)BOARD_LCD_H_RES || gh * scale > (uint32_t)BOARD_LCD_V_RES)) {
        scale--;
    }
    lvgl_gif_opts_t opts = {
        .scale = scale,
        .opaque = true,
        .min_frame_ms = BOARD_FACE_GIF_MIN_FRAME_MS,
    };
    s_gif_controller = lvgl_gif_create_ex(&s_face_dsc, &opts);
    if (!s_gif_controller) {
        ESP_LOGE(TAG_UI, "face: decode %s that bai", key);
        return false;
    }
    strlcpy(s_gif_controller_key, key, sizeof(s_gif_controller_key));
    lvgl_gif_set_frame_cb(s_gif_controller, on_gif_frame, s_gif_controller);
    lvgl_gif_set_done_cb(s_gif_controller, face_gif_done_cb, NULL);
    lvgl_gif_set_loop(s_gif_controller, loop);
    const lv_image_dsc_t *frame = lvgl_gif_image_dsc(s_gif_controller);
    if (frame) lv_image_set_src(s_display.emoji_gif, frame);   /* gắn 1 lần, sau chỉ invalidate hộp bẩn */
    if (s_display.emoji_image) lv_obj_add_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
    if (!s_face.hidden) lv_obj_clear_flag(s_display.emoji_gif, LV_OBJ_FLAG_HIDDEN);
    if (can_run) lvgl_gif_start(s_gif_controller);
    s_face.current = id;
    ESP_LOGI(TAG_UI, "face: %s (%s, %ux%u x%u)", key, face_mode_str(id),
             (unsigned)gw, (unsigned)gh, (unsigned)scale);
    return true;
}

static face_id_t face_resolve(void) {
    if (s_face.reaction != FACE_NONE) return s_face.reaction;
    if (s_face.overlay != FACE_NONE) return s_face.overlay;
    return s_face.base != FACE_NONE ? s_face.base : FACE_IDLE_NORMAL;
}

/* Phát đúng clip theo reaction > overlay > base; clip lỗi thì bỏ lớp đó và thử lớp dưới. */
static void face_apply_locked(void) {
    if (!s_display.emoji_gif) return;
    for (int guard = 0; guard < 3; guard++) {
        face_id_t want = face_resolve();
        if (want == s_face.current && s_gif_controller) return;
        if (face_play_locked(want)) return;
        if (s_face.reaction == want) { s_face.reaction = FACE_NONE; s_face.reaction_kind = FACE_RK_NONE; }
        else if (s_face.overlay == want) s_face.overlay = FACE_NONE;
        else { s_face.base = FACE_NONE; }
        if (want == FACE_IDLE_NORMAL) break;
    }
    /* Không nạp được cả mặt nền: rơi về PNG 128 px như đường S3 để màn không trống. */
    ESP_LOGW(TAG_UI, "face: khong nap duoc clip nao -> PNG du phong");
    destroy_gif_controller_locked();
    s_face.current = FACE_NONE;
    lv_obj_add_flag(s_display.emoji_gif, LV_OBJ_FLAG_HIDDEN);
    if (s_display.emoji_image) {
        lv_image_set_src(s_display.emoji_image, vimate_emotion_image(EMOTION_NEUTRAL));
        lv_obj_clear_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
    }
}

static void face_arm_idle_variation(void) {
    s_face.next_variation_us = esp_timer_get_time() +
        (int64_t)BOARD_FACE_IDLE_VARIATION_MIN_S * 1000000LL;
}

static void face_on_state_locked(vimate_dev_state_t s) {
    if (!s_display.emoji_gif) return;
    if (s_face.state == (int)s) return;
    int prev = s_face.state;
    s_face.state = (int)s;
    s_face.base = face_for_state(s);
    face_arm_idle_variation();
    if (s_face.reaction_kind == FACE_RK_IDLE) {          /* clip idle nhường mọi thay đổi */
        s_face.reaction = FACE_NONE; s_face.reaction_kind = FACE_RK_NONE;
    }
    if (face_state_is_conversation(s) || s == DEV_STATE_ERROR) {
        s_face.overlay = FACE_NONE;                      /* biểu tượng nhường lượt hội thoại */
    }
    if (s == DEV_STATE_LISTENING || s == DEV_STATE_ERROR) {
        s_face.reaction = FACE_NONE; s_face.reaction_kind = FACE_RK_NONE;   /* phản hồi tức thì */
    }
    face_id_t entry = face_entry_clip(prev, s);
    if (entry != FACE_NONE) {
        s_face.reaction = entry; s_face.reaction_kind = FACE_RK_ENTRY;
    }
    face_apply_locked();
}

static void face_on_emotion_locked(vimate_emotion_t e) {
    if (!s_display.emoji_gif) return;
    face_id_t id = face_for_emotion(e);
    if (id == FACE_NONE) {
        /* neutral (server gửi sau mỗi câu TTS): chỉ bỏ phản ứng CẢM XÚC, không cắt
         * clip vào trạng thái (connect_success đi ngay sau READY) hay idle. */
        if (s_face.reaction_kind == FACE_RK_EMOTION) {
            s_face.reaction = FACE_NONE; s_face.reaction_kind = FACE_RK_NONE;
        }
        face_apply_locked();
        return;
    }
    s_face.reaction = id; s_face.reaction_kind = FACE_RK_EMOTION;
    if (id == s_face.current && s_gif_controller) {
        (void)face_play_locked(id);                       /* cùng cảm xúc → chạy lại */
        return;
    }
    face_apply_locked();
}

static void face_set_overlay_locked(face_id_t id) {
    if (!s_display.emoji_gif) return;
    s_face.overlay = id;
    face_apply_locked();
}

/* Ẩn/hiện theo đồng hồ màn chờ: ẩn = pause decode; hiện = clear HIDDEN + chạy tiếp. */
static void face_set_visible_locked(bool visible) {
    s_face.hidden = !visible;
    if (!s_display.emoji_gif || !s_gif_controller) return;
    if (visible) {
        lv_obj_clear_flag(s_display.emoji_gif, LV_OBJ_FLAG_HIDDEN);
        if (!s_face.covered) lvgl_gif_start(s_gif_controller);
    } else {
        lvgl_gif_pause(s_gif_controller);
    }
}

/* Màn nào đang che kín mặt? (chỉ gọi khi giữ lock LVGL) */
static bool face_covered_locked(void) {
    bool popup = s_display.popup_panel && !lv_obj_has_flag(s_display.popup_panel, LV_OBJ_FLAG_HIDDEN);
    bool preview = s_display.preview_image && !lv_obj_has_flag(s_display.preview_image, LV_OBJ_FLAG_HIDDEN);
    return popup || preview || display_home_visible() || display_stats_visible() ||
           display_progress_visible() || display_water_visible() ||
           display_timetable_visible() || display_countdown_visible() ||
           display_alarm_visible() || display_quiz_visible() || display_clock_visible();
}

/* lv_timer 1 s (LVGL task, đã giữ lock port): (1) tạm dừng decode khi bị che;
 * (2) chen clip idle khi READY rảnh. Việc đổi clip đẩy sang display task. */
static void face_idle_tick_cb(lv_timer_t *t) {
    (void)t;
    if (!s_display.emoji_gif) return;
    if (++s_face.tick_n % 10 == 0 && s_gif_controller) {
        uint32_t fr = 0, us = 0, px = 0;
        lvgl_gif_take_stats(s_gif_controller, &fr, &us, &px);
        if (fr) {
            ESP_LOGI(TAG_UI, "face 10s: %s frames=%u decode+publish=%u us/khung dirty=%u px/khung",
                     s_gif_controller_key, (unsigned)fr, (unsigned)(us / fr), (unsigned)(px / fr));
        }
    }
    bool covered = face_covered_locked();
    if (covered != s_face.covered) {
        s_face.covered = covered;
        if (s_gif_controller) {
            if (covered) lvgl_gif_pause(s_gif_controller);
            else if (!s_face.hidden) lvgl_gif_start(s_gif_controller);
        }
    }
    if (covered || s_face.hidden || s_display.idle_clock_active) return;
    if (s_face.state != (int)DEV_STATE_READY) return;
    if (s_face.reaction != FACE_NONE || s_face.overlay != FACE_NONE) return;
    if (s_face.current != FACE_IDLE_NORMAL) return;
    if (esp_timer_get_time() < s_face.next_variation_us) return;
    display_schedule(apply_face_idle, NULL);
}

static void apply_face_idle(void *arg) {
    (void)arg;
    display_lock();
    if (s_face.state == (int)DEV_STATE_READY && s_face.reaction == FACE_NONE &&
        s_face.overlay == FACE_NONE && s_face.current == FACE_IDLE_NORMAL &&
        !s_face.hidden && !s_face.covered) {
        uint32_t idle_s = (uint32_t)((esp_timer_get_time() - s_display.last_activity_us) / 1000000LL);
        uint32_t delay_s = BOARD_FACE_IDLE_VARIATION_MIN_S;
        face_id_t v = face_idle_variation(idle_s, esp_random(), &delay_s);
        s_face.next_variation_us = esp_timer_get_time() + (int64_t)delay_s * 1000000LL;
        if (v != FACE_NONE) {
            s_face.reaction = v; s_face.reaction_kind = FACE_RK_IDLE;
            face_apply_locked();
        }
    }
    display_unlock();
}

static void apply_face_done(void *arg) {
    face_id_t done_id = (face_id_t)(intptr_t)arg;
    display_lock();
    if (done_id == s_face.current && face_play_mode(done_id) == FACE_PLAY_ONCE) {
        if (s_face.reaction == done_id) {
            s_face.reaction = FACE_NONE; s_face.reaction_kind = FACE_RK_NONE;
        }
        face_apply_locked();
    }
    /* ONCE_HOLD: đứng ở khung cuối tới khi trạng thái/biểu tượng đổi. */
    display_unlock();
}

/* Nạp trước cả bộ vào PSRAM, mỗi lượt một file (≤ 145 KB, 50–310 ms đọc SPIFFS) rồi
 * NGHỈ FACE_PRELOAD_GAP_MS qua esp_timer: display task (prio 6) mà tự xếp hàng liên tục
 * thì IDLE0 và vimate_main (5) đói CPU — lần đầu 15/09 dính `task_wdt IDLE0` ×2 và boot
 * chậm 10 s. Thứ tự enum = boot/connect/idle trước. */
#define FACE_PRELOAD_GAP_MS 80
static void on_face_preload_timer(void *arg) {
    (void)arg;
    if (!display_schedule(apply_face_preload, NULL)) {
        ESP_LOGW(TAG_UI, "face: hang doi day, preload dung o %d/%d (con lai nap khi dung)",
                 s_face.preload_idx, (int)FACE_COUNT_);
    }
}
static void apply_face_preload(void *arg) {
    (void)arg;
    while (s_face.preload_idx < FACE_COUNT_) {
        face_id_t id = (face_id_t)s_face.preload_idx++;
        const char *key = face_key(id);
        if (!key || gif_cache_slot(key, false)) continue;
        (void)face_cache_get(id, NULL);
        break;
    }
    if (s_face.preload_idx < FACE_COUNT_) {
        if (s_face.preload_timer) {
            esp_timer_start_once(s_face.preload_timer, (uint64_t)FACE_PRELOAD_GAP_MS * 1000ULL);
        }
    } else {
        ESP_LOGI(TAG_UI, "face: preload xong, cache %u KB PSRAM", (unsigned)(s_face.preload_bytes / 1024));
    }
}

/* Gọi trong display_setup_ui (đang giữ lock) sau khi tạo emoji_gif. */
static void face_init_locked(void) {
    /* emo_spiffs mount ở vimate_main_task (sau UI) — mount sớm ở đây, idempotent. */
    if (emotion_sync_init() != ESP_OK) {
        ESP_LOGW(TAG_UI, "face: emo_spiffs chua mount, se rơi ve PNG");
    }
    s_face.state = -1;
    s_face.current = FACE_NONE;
    face_arm_idle_variation();
    s_face.idle_timer = lv_timer_create(face_idle_tick_cb, 1000, NULL);
    const esp_timer_create_args_t pl_args = {
        .callback = on_face_preload_timer,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "face_preload",
    };
    if (esp_timer_create(&pl_args, &s_face.preload_timer) != ESP_OK) s_face.preload_timer = NULL;
    face_on_state_locked(DEV_STATE_BOOT);          /* boot_up rồi về idle_normal */
    (void)display_schedule(apply_face_preload, NULL);
}

typedef struct { char key[24]; } sched_face_t;
static void apply_face_symbol(void *arg) {
    sched_face_t *p = (sched_face_t *)arg;
    display_lock();
    face_set_overlay_locked(face_from_key(p->key));
    display_unlock();
    free(p);
}
#endif /* BOARD_FACE_GIF_FULLSCREEN */

/* Biểu tượng tính năng trên mặt robot (sym_alarm, sym_music, sym_weather_sun…): giữ
 * tới khi gọi lại với NULL/"" hoặc thiết bị vào lượt nghe/nghĩ/nói. Board không có mặt
 * robot: no-op. */
void display_show_face(const char *key) {
#if BOARD_FACE_GIF_FULLSCREEN
    if (!s_display.sched_q) return;
    sched_face_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    if (key) strlcpy(p->key, key, sizeof(p->key));
    if (!display_schedule(apply_face_symbol, p)) free(p);
#else
    (void)key;
#endif
}

static void apply_emotion(void *arg) {
    sched_emotion_t *p = (sched_emotion_t *)arg;
    if (!s_display.setup_ui_called || !s_display.emoji_image) goto done;
    display_note_activity();
    display_lock();
    s_display.current_emotion = p->e;
    slideshow_interrupt_locked();
    overlays_dismiss_locked();
    const char *key = vimate_emotion_to_str(p->e);
#if BOARD_FACE_GIF_FULLSCREEN
    /* Mặt robot: cảm xúc = clip phản ứng một lượt rồi về mặt nền (face.c). */
    (void)key;
    face_on_emotion_locked(p->e);
    display_unlock();
    goto done;
#elif EMOJI_GIF_RUNTIME_ENABLE
    /* Priority 1: custom GIF synced từ server asset pack (/spiffs_emo/<key>.gif).
     * Firmware Edu flash sẵn vimate_emo_128 vào partition này và có thể cập nhật
     * pack động. Không dùng cherry làm mặc định cho EDU. */
    if (s_display.emoji_gif) {
        char custom_path[64];
        size_t custom_size = 0;
        time_t custom_mtime = 0;
        char custom_key[32];
        snprintf(custom_key, sizeof(custom_key), "custom:%s", key);
        bool has_custom = custom_gif_path_for_key(key, custom_path, sizeof(custom_path),
                                                  &custom_size, &custom_mtime);
        bool same_custom = has_custom && (s_gif_controller != NULL) &&
                           (strcmp(s_gif_controller_key, custom_key) == 0) &&
                           s_custom_gif_size == custom_size &&
                           s_custom_gif_mtime == custom_mtime;
        if (has_custom) {
            if (!same_custom) {
                destroy_gif_controller_locked();
                release_custom_gif_data();
                if (!load_custom_gif_from_file(key, custom_path, custom_size, custom_mtime)) {
                    goto try_embedded;
                }
            }
            if (show_gif_locked(custom_key)) {
                display_unlock();
                goto done;
            }
            destroy_gif_controller_locked();
            release_custom_gif_data();
        }
    }

try_embedded:
    /* Priority 2: embedded GIFs (flash, instant lookup). */
    if (s_gif_controller && strncmp(s_gif_controller_key, "custom:", 7) == 0) {
        destroy_gif_controller_locked();
        release_custom_gif_data();
    }
    if (s_display.emoji_gif && find_embedded_gif(key)) {
        if (show_gif_locked(key)) {
            display_unlock();
            goto done;
        }
    }
#endif
    /* Fallback: built-in PNG khi không có embedded GIF / decode fail. */
    if (s_display.emoji_gif) {
        lv_obj_add_flag(s_display.emoji_gif, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_gif_controller) {
        lvgl_gif_stop(s_gif_controller);
    }
    const lv_image_dsc_t *dsc = vimate_emotion_image(p->e);
    if (dsc) {
        lv_image_set_src(s_display.emoji_image, dsc);
        lv_obj_clear_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
    }
    display_unlock();
done:
    free(p);
}

typedef struct { char *text; } sched_chat_t;
typedef struct { uint32_t seq; } sched_chat_hide_t;
/* Anim wrapper: lv_obj_set_style_opa cần 3 args, anim callback chỉ truyền 2.
 * Cast trực tiếp → undefined behavior (chữ stuck opa=0). Wrapper này đúng sig. */
static void anim_set_opa_cb(void *obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void apply_chat(void *arg) {
    sched_chat_t *p = (sched_chat_t *)arg;
    if (s_display.setup_ui_called && s_display.chat_label) {
        display_note_activity();
        display_lock();
        slideshow_interrupt_locked();
        const char *t = p->text ? p->text : "";
        /* Xoá chat ("") đi kèm lệnh HOME ngay trước payload Home: KHÔNG dismiss
         * overlay ở đây, kẻo lịch/đồng hồ biến mất 1 khung trước khi Home vẽ. */
        if (t[0]) overlays_dismiss_locked();
        uint32_t seq = ++s_display.chat_seq;
        /* Agent giữ bottom bar cố định, nên bubble phải kết thúc phía trên bar.
         * Lesson/voice scene cũ vẫn dùng vị trí sát đáy như trước. */
        int chat_bottom_offset = display_agent_active()
                                     ? -(HOME_BAR_RESERVED_H + AGENT_CHAT_BOTTOM_GAP)
                                     : -36;
        lv_obj_align(s_display.chat_label, LV_ALIGN_BOTTOM_MID,
                     0, chat_bottom_offset);
        /* Inner label được lưu trong user_data của container. */
        lv_obj_t *txt = (lv_obj_t *)lv_obj_get_user_data(s_display.chat_label);
        if (txt) {
            bool compact = strlen(t) <= 44 && strchr(t, '\n') == NULL;
            if (compact) {
                lv_label_set_long_mode(txt, LV_LABEL_LONG_CLIP);
                lv_obj_set_width(txt, LV_SIZE_CONTENT);
                lv_obj_set_width(s_display.chat_label, LV_SIZE_CONTENT);
                lv_obj_set_height(s_display.chat_label, LV_SIZE_CONTENT);
            } else {
                lv_label_set_long_mode(txt, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(txt, BOARD_LCD_H_RES - 48);
                lv_obj_set_width(s_display.chat_label, BOARD_LCD_H_RES - 24);
                lv_obj_set_height(s_display.chat_label, 78);
            }
            lv_label_set_text(txt, t);
        }
        if (t[0] == '\0') {
            lv_obj_add_flag(s_display.chat_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_display.chat_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(s_display.chat_label, 255, 0);
            lv_obj_move_foreground(s_display.chat_label);
            /* Refresh layout để container biết label mới cao bao nhiêu. */
            lv_obj_update_layout(s_display.chat_label);
            /* Karaoke scroll: nếu text > 2 dòng (label cao hơn container),
             * scroll xuống cuối để hiện 2 dòng mới nhất, có animation 300ms.
             * Nếu text ngắn, scroll về top. */
            if (txt) {
                int label_h = lv_obj_get_height(txt);
                int cont_h_inner = lv_obj_get_content_height(s_display.chat_label);
                if (label_h > cont_h_inner) {
                    lv_obj_scroll_to_y(s_display.chat_label,
                                       label_h - cont_h_inner,
                                       LV_ANIM_OFF);
                } else {
                    lv_obj_scroll_to_y(s_display.chat_label, 0, LV_ANIM_OFF);
                }
            }
            /* Fade-in mờ→rõ cho text mới (300ms). */
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, s_display.chat_label);
            lv_anim_set_values(&a, 120, 255);
            lv_anim_set_time(&a, 300);
            lv_anim_set_exec_cb(&a, anim_set_opa_cb);
            lv_anim_start(&a);
            (void)seq;
            esp_timer_stop(s_display.chat_hide_timer);
            esp_timer_start_once(s_display.chat_hide_timer, CHAT_AUTO_HIDE_MS * 1000ULL);
        }
        display_unlock();
    }
    free(p->text);
    free(p);
}

static void hide_chat_if_current(void *arg) {
    sched_chat_hide_t *p = (sched_chat_hide_t *)arg;
    if (!p) return;
    if (s_display.setup_ui_called && s_display.chat_label &&
        p->seq == s_display.chat_seq) {
        display_lock();
        lv_obj_t *txt = (lv_obj_t *)lv_obj_get_user_data(s_display.chat_label);
        if (txt) lv_label_set_text(txt, "");
        lv_obj_add_flag(s_display.chat_label, LV_OBJ_FLAG_HIDDEN);
        display_unlock();
    }
    free(p);
}

static void on_chat_hide_timer(void *arg) {
    (void)arg;
    sched_chat_hide_t *p = calloc(1, sizeof(*p));
    if (p) {
        p->seq = s_display.chat_seq;
        display_schedule(hide_chat_if_current, p);
    }
}

/* Home helpers được định nghĩa cùng block Home phía dưới; callback ảnh bìa cần
 * tra URL hiện tại để loại kết quả cũ khi trẻ vuốt nhanh. */
static char *home_strdup_psram(const char *src);
static const char *home_course_cover_locked(void);

typedef struct {
    /* Pointer to const dsc — caller giữ ownership. */
    const lv_image_dsc_t *dsc;
    bool clear;
    bool as_slideshow; /* true = frame slideshow màn chờ; false = ảnh nội dung */
    bool home_cover;
    char *url;         /* chỉ home_cover: loại kết quả cũ khi trẻ đổi trang nhanh */
} sched_preview_t;
static void apply_preview(void *arg) {
    sched_preview_t *p = (sched_preview_t *)arg;
    if (!s_display.setup_ui_called ||
        (p->home_cover && !s_display.home_course_cover_image) ||
        (!p->home_cover && !s_display.preview_image)) goto done;
    display_note_activity();
    display_lock();
    if (p->home_cover) {
        bool home_visible = s_display.home_course_mode && s_display.home_panel &&
                            !lv_obj_has_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
        const char *wanted = home_course_cover_locked();
        bool matches = home_visible && p->url && wanted[0] &&
                       strcmp(p->url, wanted) == 0;
        if (p->clear || !p->dsc) {
            lv_obj_add_flag(s_display.home_course_cover_image, LV_OBJ_FLAG_HIDDEN);
            s_display.home_cover_applied_url[0] = ' ';
            if (home_visible) {
                if (s_display.home_course_placeholder) {
                    lv_obj_clear_flag(s_display.home_course_placeholder, LV_OBJ_FLAG_HIDDEN);
                }
            }
        } else if (matches) {
            esp_timer_stop(s_display.preview_hide_timer);
            lv_image_set_src(s_display.home_course_cover_image, p->dsc);
            lv_obj_set_style_opa(s_display.home_course_cover_image, LV_OPA_COVER, 0);
            lv_obj_clear_flag(s_display.home_course_cover_image, LV_OBJ_FLAG_HIDDEN);
            if (s_display.home_course_placeholder) {
                lv_obj_add_flag(s_display.home_course_placeholder, LV_OBJ_FLAG_HIDDEN);
            }
            strlcpy(s_display.home_cover_applied_url, p->url,
                    sizeof(s_display.home_cover_applied_url));
            ESP_LOGI(TAG_UI, "home course cover applied: %s", p->url);
        } else {
            ESP_LOGI(TAG_UI, "home course cover ignored (stale/hidden): %s",
                     p->url ? p->url : "(null)");
        }
        display_unlock();
        goto done;
    }
    if (p->clear || !p->dsc) {
        esp_timer_stop(s_display.preview_hide_timer);
        lv_obj_add_flag(s_display.preview_image, LV_OBJ_FLAG_HIDDEN);
    } else {
        /* Ảnh NỘI DUNG (không phải frame slideshow) tới khi slideshow đang chạy →
         * DIỆT slideshow, nếu không ~1 interval sau ss_timer sẽ đè ảnh gia đình lên
         * nội dung (yêu cầu: không chạy slideshow khi đang chạy chương trình). */
        bool foreign_during_ss = !p->as_slideshow && s_display.ss_active;
        if (foreign_during_ss) slideshow_interrupt_locked();
        /* Chế độ đồng hồ-nền-ảnh (frame slideshow HOẶC app Đồng hồ wallpaper): ảnh
         * CHÍNH là nền → KHÔNG dismiss đồng hồ; hiện ảnh rồi nâng clock_panel lên.
         * Ảnh nội dung lạ (foreign_during_ss) thì KHÔNG coi là nền → dismiss overlay. */
        bool clock_bg = s_display.clock_wallpaper && s_display.clock_panel &&
                        !lv_obj_has_flag(s_display.clock_panel, LV_OBJ_FLAG_HIDDEN) &&
                        !foreign_during_ss;
        bool agent_frame = display_agent_active() && !clock_bg && !p->as_slideshow;
        int preview_h = agent_frame ? BOARD_LCD_V_RES - HOME_BAR_RESERVED_H
                                    : BOARD_LCD_V_RES;
        lv_obj_set_size(s_display.preview_image, BOARD_LCD_H_RES, preview_h);
        lv_obj_align(s_display.preview_image, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(s_display.preview_image,
                                  lv_color_hex(agent_frame ? 0xf8fafc : 0x000000), 0);
        lv_obj_set_style_opa(s_display.preview_image, LV_OPA_TRANSP, 0);
        lv_image_set_src(s_display.preview_image, p->dsc);
        lv_obj_move_foreground(s_display.preview_image);
        if (clock_bg) lv_obj_move_foreground(s_display.clock_panel);
        if (s_display.emoji_gif && !lv_obj_has_flag(s_display.emoji_gif, LV_OBJ_FLAG_HIDDEN)) {
            /* Edu: emoji nằm dưới ảnh bài học (không cần overlay) */
        } else if (s_display.emoji_image && !lv_obj_has_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN)) {
            /* Edu: emoji nằm dưới */
        }
        if (s_display.chat_label) {
            lv_obj_move_foreground(s_display.chat_label);
        }
        lv_obj_clear_flag(s_display.preview_image, LV_OBJ_FLAG_HIDDEN);
        if (s_display.emoji_gif && !lv_obj_has_flag(s_display.emoji_gif, LV_OBJ_FLAG_HIDDEN)) {
            /* Edu: giữ nguyên */
        } else if (s_display.emoji_image && !lv_obj_has_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN)) {
            /* Edu: giữ nguyên */
        }
        if (s_display.chat_label) {
            lv_obj_move_foreground(s_display.chat_label);
        }
        /* EDU ảnh bài học cần hiện sắc nét, không quét/fade full-screen trên SPI LCD. */
        lv_obj_set_style_opa(s_display.preview_image, LV_OPA_COVER, 0);
        if (agent_frame) {
            if (s_display.bottom_bar) {
                lv_obj_clear_flag(s_display.bottom_bar, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(s_display.bottom_bar);
            }
            if (s_display.nav_home_btn) {
                lv_obj_clear_flag(s_display.nav_home_btn, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(s_display.nav_home_btn);
            }
        }
#if PREVIEW_HIDE_MS > 0
        esp_timer_stop(s_display.preview_hide_timer);
        esp_timer_start_once(s_display.preview_hide_timer, PREVIEW_HIDE_MS * 1000ULL);
#endif
    }
    display_unlock();
done:
    free(p->url);
    free(p);
}
static void on_preview_hide_timer(void *arg) {
    (void)arg;
    /* Push qua queue để xử lý ở display task (không LVGL từ esp_timer task). */
    sched_preview_t *p = calloc(1, sizeof(sched_preview_t));
    if (!p) return;
    p->clear = true;
    display_schedule(apply_preview, p);
}

typedef struct {
    char *title;
    char *body;
    char *code;       /* nếu NULL → ẩn popup_code, body bình thường */
    int auto_hide_ms;
} sched_popup_t;
typedef struct { uint32_t seq; } sched_popup_hide_t;

static void apply_popup(void *arg) {
    sched_popup_t *p = (sched_popup_t *)arg;
    if (!s_display.setup_ui_called || !s_display.popup_panel) goto done;
    display_note_activity();
    display_lock();
    uint32_t seq = ++s_display.popup_seq;
    /* Empty title+body+code = hide popup only. Do not dismiss preview/slideshow:
     * callers use display_set_message(NULL, NULL) as a cheap popup clear while
     * a banner is already visible; hiding overlays here exposes a white screen. */
    bool empty = (!p->title || !p->title[0]) &&
                 (!p->body  || !p->body[0])  &&
                 (!p->code  || !p->code[0]);
    if (empty) {
        esp_timer_stop(s_display.popup_hide_timer);
        lv_label_set_text(s_display.popup_title, "");
        lv_label_set_text(s_display.popup_body, "");
        lv_label_set_text(s_display.popup_code, "");
        lv_obj_add_flag(s_display.popup_code, LV_OBJ_FLAG_HIDDEN);
        if (s_display.popup_qr) {
            lv_obj_add_flag(s_display.popup_qr, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_display.popup_brand) {
            lv_obj_add_flag(s_display.popup_brand, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(s_display.popup_panel, LV_OBJ_FLAG_HIDDEN);
        display_unlock();
        goto done;
    }

    slideshow_interrupt_locked();
    overlays_dismiss_locked();
    /* Popup trong Agent cũng phải nằm hoàn toàn phía trên bottom bar. Các màn
     * provisioning/error ngoài Agent vẫn giữ nguyên kích thước toàn màn. */
    int popup_h = display_agent_active()
                      ? BOARD_LCD_V_RES - HOME_BAR_RESERVED_H
                      : BOARD_LCD_V_RES;
    lv_obj_set_size(s_display.popup_panel, BOARD_LCD_H_RES, popup_h);
    lv_obj_align(s_display.popup_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    idle_clock_hide_locked();
    lv_label_set_text(s_display.popup_title, p->title ? p->title : "");
    lv_label_set_text(s_display.popup_body,  p->body  ? p->body  : "");
    if (p->code && p->code[0]) {
        lv_label_set_text(s_display.popup_code, p->code);
        lv_obj_clear_flag(s_display.popup_code, LV_OBJ_FLAG_HIDDEN);
        if (s_display.popup_brand) {
            lv_obj_add_flag(s_display.popup_brand, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_display.popup_qr) {
            lv_qrcode_update(s_display.popup_qr, p->code, strlen(p->code));
            lv_obj_clear_flag(s_display.popup_qr, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(s_display.popup_code, LV_OBJ_FLAG_HIDDEN);
        if (s_display.popup_qr) {
            lv_obj_add_flag(s_display.popup_qr, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_display.popup_brand) {
            lv_label_set_text(s_display.popup_brand, VIMATE_BRAND_NAME);
            lv_obj_clear_flag(s_display.popup_brand, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_move_foreground(s_display.popup_panel);
    lv_obj_clear_flag(s_display.popup_panel, LV_OBJ_FLAG_HIDDEN);
    if (p->auto_hide_ms > 0) {
        (void)seq;
        esp_timer_stop(s_display.popup_hide_timer);
        esp_timer_start_once(s_display.popup_hide_timer, (uint64_t)p->auto_hide_ms * 1000ULL);
    }
    display_unlock();
done:
    free(p->title); free(p->body); free(p->code);
    free(p);
}

static void hide_popup_if_current(void *arg) {
    sched_popup_hide_t *p = (sched_popup_hide_t *)arg;
    if (!p) return;
    if (s_display.setup_ui_called && s_display.popup_panel &&
        p->seq == s_display.popup_seq) {
        display_lock();
        lv_label_set_text(s_display.popup_title, "");
        lv_label_set_text(s_display.popup_body, "");
        lv_label_set_text(s_display.popup_code, "");
        lv_obj_add_flag(s_display.popup_code, LV_OBJ_FLAG_HIDDEN);
        if (s_display.popup_qr) {
            lv_obj_add_flag(s_display.popup_qr, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_display.popup_brand) {
            lv_obj_add_flag(s_display.popup_brand, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(s_display.popup_panel, LV_OBJ_FLAG_HIDDEN);
        display_unlock();
    }
    free(p);
}

static void on_popup_hide_timer(void *arg) {
    (void)arg;
    sched_popup_hide_t *p = calloc(1, sizeof(*p));
    if (p) {
        p->seq = s_display.popup_seq;
        display_schedule(hide_popup_if_current, p);
    }
}

typedef struct { int stars; } sched_reward_t;
static void apply_reward(void *arg) {
    sched_reward_t *p = (sched_reward_t *)arg;
    if (!s_display.setup_ui_called || !s_display.reward_panel) goto done;
    display_note_activity();
    int n = p->stars;
    if (n < 0) n = 0;
    if (n > 5) n = 5;
    const char *praise = "Cố lên!";
    if (n >= 5) praise = "Tuyệt vời!";
    else if (n >= 4) praise = "Rất giỏi!";
    else if (n >= 3) praise = "Tốt lắm!";
    else if (n >= 2) praise = "Khá hơn nào!";
    else if (n >= 1) praise = "Cố gắng nhé!";
    char msg[48];
    /* "·" U+00B7 (có trong dải Latin-1 của font vimate); "•" U+2022 cũ KHÔNG có glyph → trống. */
    if (n > 0) snprintf(msg, sizeof(msg), "+%d sao · %s", n, praise);
    else       snprintf(msg, sizeof(msg), "%s", praise);
    display_lock();
    /* Sao đầy = vàng SECONDARY, sao rỗng = xám mờ 60 % (hình + độ mờ, không chỉ màu). */
    for (int i = 0; i < 5; i++) {
        lv_obj_t *im = s_display.reward_star_img[i];
        if (!im) continue;
        lv_obj_set_style_image_recolor(
            im, lv_color_hex(i < n ? UI_CLR_STAR_FULL : UI_CLR_STAR_EMPTY), 0);
        lv_obj_set_style_image_opa(im, i < n ? LV_OPA_COVER : LV_OPA_60, 0);
    }
    lv_label_set_text(s_display.reward_msg, msg);
#if BOARD_FACE_GIF_FULLSCREEN
    face_set_overlay_locked(FACE_SYM_CELEBRATE);   /* pháo hoa dưới lớp sao */
#endif
    lv_obj_move_foreground(s_display.reward_panel);
    lv_obj_clear_flag(s_display.reward_panel, LV_OBJ_FLAG_HIDDEN);
    esp_timer_stop(s_display.reward_hide_timer);
    esp_timer_start_once(s_display.reward_hide_timer, REWARD_HIDE_MS * 1000ULL);
    display_unlock();
done:
    free(p);
}
static void hide_reward_cb(void *arg) {
    (void)arg;
    if (!s_display.setup_ui_called || !s_display.reward_panel) return;
    display_lock();
    lv_obj_add_flag(s_display.reward_panel, LV_OBJ_FLAG_HIDDEN);
#if BOARD_FACE_GIF_FULLSCREEN
    face_set_overlay_locked(FACE_NONE);
#endif
    display_unlock();
}
static void on_reward_hide_timer(void *arg) {
    (void)arg;
    display_schedule(hide_reward_cb, NULL);
}

/* ===== Public API ===== */
void display_set_state(vimate_dev_state_t s) {
    if (!s_display.sched_q) return;
    sched_state_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->s = s;
    display_schedule(apply_state, p);
}

void display_set_emotion(vimate_emotion_t e) {
    if (!s_display.sched_q) return;
    if (e < 0 || e >= EMOTION_COUNT_) e = EMOTION_NEUTRAL;
    sched_emotion_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->e = e;
    display_schedule(apply_emotion, p);
}

vimate_emotion_t display_current_emotion(void) {
    vimate_emotion_t e = s_display.current_emotion;
    if (e < 0 || e >= EMOTION_COUNT_) return EMOTION_NEUTRAL;
    return e;
}

void display_set_chat_message(const char *role, const char *text) {
    (void)role;
    if (!s_display.sched_q) return;
    sched_chat_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->text = text ? strdup(text) : NULL;
    display_schedule(apply_chat, p);
}

void display_show_preview_image_ex(const lv_image_dsc_t *dsc, bool as_slideshow) {
    if (!s_display.sched_q) return;
    sched_preview_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->dsc = dsc;
    p->clear = (dsc == NULL);
    p->as_slideshow = as_slideshow;
    if (!display_schedule(apply_preview, p)) free(p);
}

void display_show_preview_image(const lv_image_dsc_t *dsc) {
    display_show_preview_image_ex(dsc, false);
}

void display_show_home_cover_image(const char *url, const lv_image_dsc_t *dsc) {
    if (!s_display.sched_q) return;
    sched_preview_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->dsc = dsc;
    p->clear = (dsc == NULL);
    p->home_cover = true;
    p->url = url ? home_strdup_psram(url) : NULL;
    if (!display_schedule(apply_preview, p)) {
        free(p->url);
        free(p);
    }
}

void display_set_message(const char *title, const char *body) {
    if (!s_display.sched_q) return;
    sched_popup_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->title = title ? strdup(title) : NULL;
    p->body  = body  ? strdup(body)  : NULL;
    p->code  = NULL;
    display_schedule(apply_popup, p);
}

void display_set_message_timed(const char *title, const char *body, int timeout_ms) {
    if (!s_display.sched_q) return;
    sched_popup_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->title = title ? strdup(title) : NULL;
    p->body  = body  ? strdup(body)  : NULL;
    p->code  = NULL;
    p->auto_hide_ms = timeout_ms > 0 ? timeout_ms : POPUP_AUTO_HIDE_MS;
    display_schedule(apply_popup, p);
}

void display_show_test_pattern(const char *reason) {
    const char *why = (reason && reason[0]) ? reason : "display test";
    if (!s_display.sched_q) {
        ESP_LOGW(TAG_UI, "display test ignored before UI init reason=%s", why);
        return;
    }
    char body[192];
    snprintf(body, sizeof(body), "FW %s | %dx%d | %s",
             VIMATE_FW_VERSION,
             BOARD_LCD_H_RES,
             BOARD_LCD_V_RES,
             why);
    display_set_backlight(100);
    display_set_sleep_timeout(0);
    display_set_message("Display OK", body);
}

void display_show_activation(const char *code, const char *message) {
    if (!s_display.sched_q) return;
    sched_popup_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    char title[64];
    snprintf(title, sizeof(title), "Kích hoạt %s", VIMATE_BRAND_NAME);
    p->title = strdup(title);
    p->body  = strdup((message && message[0]) ? message
                      : "Mở app " VIMATE_BRAND_NAME " → Thiết bị → Nhập mã");
    p->code  = strdup((code && code[0]) ? code : "------");
    display_schedule(apply_popup, p);
}

/* ===== Pha C: home grid (lưới chọn hoạt động trên máy) ===== */
typedef struct {
    int   n;
    char *screen;
    char *ids[VIMATE_HOME_MAX];
    char *titles[VIMATE_HOME_MAX];
    char *subtitles[VIMATE_HOME_MAX];
    char *cover_urls[VIMATE_HOME_MAX];
    char *greeting;   /* header: lời chào (server đẩy); NULL = brand name */
    int   stars;      /* header: tổng sao của bé */
} sched_home_t;

static char *home_strdup_psram(const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src) + 1;
    char *copy = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) copy = malloc(len);
    if (!copy) return NULL;
    memcpy(copy, src, len);
    return copy;
}

static void home_course_state_clear_locked(void) {
    for (int i = 0; i < VIMATE_HOME_MAX; i++) {
        free(s_display.home_course_ids[i]);
        free(s_display.home_course_titles[i]);
        free(s_display.home_course_subtitles[i]);
        free(s_display.home_course_cover_urls[i]);
        s_display.home_course_ids[i] = NULL;
        s_display.home_course_titles[i] = NULL;
        s_display.home_course_subtitles[i] = NULL;
        s_display.home_course_cover_urls[i] = NULL;
    }
    s_display.home_course_count = 0;
    s_display.home_course_index = 0;
    s_display.home_course_mode = false;
}

static const char *home_course_cover_locked(void) {
    int i = s_display.home_course_index;
    if (!s_display.home_course_mode || i < 0 || i >= s_display.home_course_count) {
        return "";
    }
    return s_display.home_course_cover_urls[i] ? s_display.home_course_cover_urls[i] : "";
}

static void home_course_render_locked(bool reset_cover) {
    if (!s_display.home_course_panel || !s_display.home_course_mode ||
        s_display.home_course_count <= 0) {
        return;
    }
    int i = s_display.home_course_index;
    if (i < 0 || i >= s_display.home_course_count) i = 0;
    s_display.home_course_index = i;
    lv_label_set_text(s_display.home_course_title,
                      s_display.home_course_titles[i] ? s_display.home_course_titles[i] : "");
    lv_label_set_text(s_display.home_course_subtitle,
                      s_display.home_course_subtitles[i] ? s_display.home_course_subtitles[i] : "");
    lv_label_set_text_fmt(s_display.home_course_page, "%d / %d",
                          i + 1, s_display.home_course_count);

    bool many = s_display.home_course_count > 1;
    if (s_display.home_course_prev) {
        if (many) lv_obj_clear_flag(s_display.home_course_prev, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_display.home_course_prev, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_display.home_course_next) {
        if (many) lv_obj_clear_flag(s_display.home_course_next, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_display.home_course_next, LV_OBJ_FLAG_HIDDEN);
    }

    if (reset_cover && s_display.home_course_cover_image) {
        lv_obj_add_flag(s_display.home_course_cover_image, LV_OBJ_FLAG_HIDDEN);
        s_display.home_cover_applied_url[0] = ' ';
    }
    bool waiting = reset_cover || home_course_cover_locked()[0] == '\0';
    if (s_display.home_course_placeholder) {
        if (waiting) lv_obj_clear_flag(s_display.home_course_placeholder, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_display.home_course_placeholder, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_opa(s_display.home_panel, LV_OPA_COVER, 0);
}

static void sched_home_free(sched_home_t *p) {
    if (!p) return;
    for (int i = 0; i < p->n && i < VIMATE_HOME_MAX; i++) {
        free(p->ids[i]);
        free(p->titles[i]);
        free(p->subtitles[i]);
        free(p->cover_urls[i]);
    }
    free(p->screen);
    free(p->greeting);
    free(p);
}

static const lv_image_dsc_t *home_icon_for(const char *id) {
	if (!id) return &icon_hoc;
	if (strstr(id, "_off")) return &icon_off;                 /* Tắt nhắc (X) */
	if (!strcmp(id, "back") || !strcmp(id, "home")) return &icon_back;
	if (!strcmp(id, "schedule") || !strcmp(id, "app_schedule") ||
	    !strncmp(id, "study_", 6)) return &icon_hoc;          /* Lịch học */
	if (!strncmp(id, "timer", 5) || !strcmp(id, "app_timer") ||
	    !strcmp(id, "app_focus")) return &icon_timer;         /* Đếm ngược */
	if (!strcmp(id, "clock") || !strcmp(id, "app_clock")) return &icon_clock;
	if (!strcmp(id, "idle") || !strcmp(id, "app_slideshow")) return &icon_idle;
	if (!strncmp(id, "water", 5) || !strcmp(id, "app_water")) return &icon_water;
    if (!strcmp(id, "chat") || !strncmp(id, "agent_", 6)) return &icon_chat;
    if (!strncmp(id, "english", 7) || !strncmp(id, "dictionary", 10) ||
        !strncmp(id, "translate", 9) || !strncmp(id, "vocab", 5)) return &icon_chat;
    if (!strncmp(id, "focus", 5) || !strncmp(id, "pomodoro", 8) ||
        !strncmp(id, "stopwatch", 9) || !strncmp(id, "countdown", 9)) return &icon_timer;
    if (!strcmp(id, "course") || !strcmp(id, "lesson")) return &icon_hoc;
    return &icon_hoc;
}

static void apply_home(void *arg) {
    sched_home_t *p = (sched_home_t *)arg;
    char cover_url[384] = {0};
    display_lock();
    slideshow_interrupt_locked();
    screens_dismiss_locked();
    if (s_display.preview_image) {
        lv_obj_add_flag(s_display.preview_image, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_display.home_panel) {
        int n = p->n > VIMATE_HOME_MAX ? VIMATE_HOME_MAX : p->n;
        bool courses = p->screen && strcmp(p->screen, "courses") == 0 && n > 0;
        char previous_id[24] = {0};
        if (s_display.home_course_mode && s_display.home_course_count > 0 &&
            s_display.home_course_index >= 0 &&
            s_display.home_course_index < s_display.home_course_count &&
            s_display.home_course_ids[s_display.home_course_index]) {
            strlcpy(previous_id,
                    s_display.home_course_ids[s_display.home_course_index],
                    sizeof(previous_id));
        }
        home_course_state_clear_locked();

        if (courses) {
            for (int i = 0; i < VIMATE_HOME_MAX; i++) {
                if (s_display.home_cards[i]) {
                    lv_obj_add_flag(s_display.home_cards[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
            s_display.home_card_count = 0;
            s_display.home_course_mode = true;
            s_display.home_course_count = n;
            for (int i = 0; i < n; i++) {
                s_display.home_course_ids[i] = p->ids[i];
                s_display.home_course_titles[i] = p->titles[i];
                s_display.home_course_subtitles[i] = p->subtitles[i];
                s_display.home_course_cover_urls[i] = p->cover_urls[i];
                p->ids[i] = NULL;
                p->titles[i] = NULL;
                p->subtitles[i] = NULL;
                p->cover_urls[i] = NULL;
                if (previous_id[0] && s_display.home_course_ids[i] &&
                    strcmp(previous_id, s_display.home_course_ids[i]) == 0) {
                    s_display.home_course_index = i;
                }
            }
            lv_obj_clear_flag(s_display.home_course_panel, LV_OBJ_FLAG_HIDDEN);
            /* Server đẩy lại payload Home ở MỌI lần chạm tab (Home/Lịch/Hẹn giờ →
             * HOME). Bìa cùng URL và còn trong kho ui_image → GIỮ NGUYÊN widget, không
             * ẩn → placeholder → decode lại (13/09: người dùng thấy "load lại cả màn"). */
            const char *want = home_course_cover_locked();
            bool keep_cover = want[0] && s_display.home_cover_applied_url[0] &&
                              strcmp(want, s_display.home_cover_applied_url) == 0 &&
                              s_display.home_course_cover_image &&
                              !lv_obj_has_flag(s_display.home_course_cover_image,
                                               LV_OBJ_FLAG_HIDDEN) &&
                              ui_image_home_cover_ready(want);
            home_course_render_locked(!keep_cover);
            if (!keep_cover) strlcpy(cover_url, want, sizeof(cover_url));
            else ESP_LOGI(TAG_UI, "home cover kept (same URL)");
        } else {
            if (s_display.home_course_panel) {
                lv_obj_add_flag(s_display.home_course_panel, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_set_style_bg_opa(s_display.home_panel, LV_OPA_COVER, 0);
            int cols = home_cols();
            int rows = (n + cols - 1) / cols;
            if (rows < 1) rows = 1;
            const int gx = 10, gy = 8;
            int blockH = rows * HOME_CELL_H + (rows - 1) * gy;
            const int regionTop = 42, regionBot = BOARD_LCD_V_RES - 70;
            int top = regionTop + ((regionBot - regionTop) - blockH) / 2;
            if (top < regionTop) top = regionTop;
            for (int i = 0; i < VIMATE_HOME_MAX; i++) {
                if (!s_display.home_cards[i]) continue;
                if (i < n) {
                    int row = i / cols;
                    int rowItems = n - row * cols;
                    if (rowItems > cols) rowItems = cols;
                    int rowW = rowItems * HOME_CELL_W + (rowItems - 1) * gx;
                    int x = (BOARD_LCD_H_RES - rowW) / 2 +
                            (i % cols) * (HOME_CELL_W + gx);
                    int y = top + row * (HOME_CELL_H + gy);
                    lv_obj_align(s_display.home_cards[i], LV_ALIGN_TOP_LEFT, x, y);
                    lv_label_set_text(s_display.home_card_titles[i],
                                      p->titles[i] ? p->titles[i] : "");
                    if (s_display.home_card_icons[i]) {
                        lv_image_set_src(s_display.home_card_icons[i],
                                         home_icon_for(p->ids[i]));
                    }
                    strlcpy(s_display.home_card_ids[i], p->ids[i] ? p->ids[i] : "",
                            sizeof(s_display.home_card_ids[i]));
                    lv_obj_clear_flag(s_display.home_cards[i], LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(s_display.home_cards[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
            s_display.home_card_count = n;
        }
        /* Header top-bar: chào (trái) · giờ (giữa) · badge sao (phải). Font Việt
         * render "sao" (⭐ unicode ngoài range font). greeting rỗng → brand name. */
        if (s_display.home_pill_lbl) {
            lv_label_set_text(s_display.home_pill_lbl,
                              p->greeting ? p->greeting : VIMATE_BRAND_NAME);
        }
        if (s_display.home_star_lbl) {
            lv_label_set_text_fmt(s_display.home_star_lbl, "%d sao", p->stars);
        }
        home_clock_render();  /* hiện giờ ngay (timer 20s cập nhật tiếp) */
        /* Ẩn widget khác, đưa home lên trước. */
        if (s_display.emoji_image) lv_obj_add_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
        if (s_display.emoji_gif) lv_obj_add_flag(s_display.emoji_gif, LV_OBJ_FLAG_HIDDEN);
        if (s_display.status_label) lv_obj_add_flag(s_display.status_label, LV_OBJ_FLAG_HIDDEN);
        if (s_display.chat_label) lv_obj_add_flag(s_display.chat_label, LV_OBJ_FLAG_HIDDEN);
        if (s_display.preview_image) lv_obj_add_flag(s_display.preview_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_display.home_panel);
        if (s_display.idle_clock_active && s_display.idle_clock_panel) {
            lv_obj_move_foreground(s_display.idle_clock_panel);
        }
    }
    display_unlock();
    if (cover_url[0]) {
        ui_image_show_home_cover_async(cover_url);
    }
    sched_home_free(p);
}

void display_show_home(const char *screen, const char *const *ids,
                       const char *const *titles, const char *const *subtitles,
                       const char *const *cover_urls, int n,
                       const char *greeting, int stars) {
    if (!s_display.sched_q) return;
    if (n < 0) n = 0;
    if (n > VIMATE_HOME_MAX) n = VIMATE_HOME_MAX;
    sched_home_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->n = n;
    p->stars = stars < 0 ? 0 : stars;
    p->screen = home_strdup_psram(screen ? screen : "");
    p->greeting = (greeting && greeting[0]) ? home_strdup_psram(greeting) : NULL;
    for (int i = 0; i < n; i++) {
        p->ids[i] = (ids && ids[i]) ? home_strdup_psram(ids[i]) : NULL;
        p->titles[i] = (titles && titles[i]) ? home_strdup_psram(titles[i]) : NULL;
        p->subtitles[i] = (subtitles && subtitles[i])
                              ? home_strdup_psram(subtitles[i]) : NULL;
        p->cover_urls[i] = (cover_urls && cover_urls[i])
                               ? home_strdup_psram(cover_urls[i]) : NULL;
    }
    if (!display_schedule(apply_home, p)) {
        sched_home_free(p);
    }
}

static void apply_hide_home(void *arg) {
    (void)arg;
    display_lock();
    if (s_display.home_panel) lv_obj_add_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_display.home_course_cover_image) {
        lv_obj_add_flag(s_display.home_course_cover_image, LV_OBJ_FLAG_HIDDEN);
        s_display.home_cover_applied_url[0] = ' ';
    }
    home_course_state_clear_locked();
    if (s_display.emoji_image) lv_obj_clear_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
    if (s_display.status_label) lv_obj_clear_flag(s_display.status_label, LV_OBJ_FLAG_HIDDEN);
    display_unlock();
}

bool display_home_visible(void) {
    display_lock();
    bool visible = s_display.home_panel &&
                   !lv_obj_has_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
    display_unlock();
    return visible;
}

void display_hide_home(void) {
    if (!s_display.sched_q) return;
    display_schedule(apply_hide_home, NULL);
}

typedef struct { int delta; } sched_home_course_nav_t;

static void apply_home_course_nav(void *arg) {
    sched_home_course_nav_t *p = (sched_home_course_nav_t *)arg;
    char cover_url[384] = {0};
    display_lock();
    bool visible = s_display.home_panel &&
                   !lv_obj_has_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
    if (visible && s_display.home_course_mode && s_display.home_course_count > 1) {
        int n = s_display.home_course_count;
        int next = (s_display.home_course_index + p->delta) % n;
        if (next < 0) next += n;
        s_display.home_course_index = next;
        home_course_render_locked(true);
        strlcpy(cover_url, home_course_cover_locked(), sizeof(cover_url));
        ESP_LOGI(TAG_UI, "home course: page %d/%d cover=%s",
                 next + 1, n, cover_url[0] ? cover_url : "(none)");
    }
    display_unlock();
    if (cover_url[0]) {
        ui_image_show_home_cover_async(cover_url);
    }
    free(p);
}

bool display_home_course_navigate(int delta) {
    if (!s_display.sched_q || delta == 0) return false;
    display_lock();
    bool can_nav = s_display.home_course_mode && s_display.home_course_count > 1 &&
                   s_display.home_panel &&
                   !lv_obj_has_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
    display_unlock();
    if (!can_nav) return false;
    sched_home_course_nav_t *p = calloc(1, sizeof(*p));
    if (!p) return false;
    p->delta = delta < 0 ? -1 : 1;
    if (!display_schedule(apply_home_course_nav, p)) {
        free(p);
        return false;
    }
    return true;
}

int display_home_course_nav_hit(int x, int y) {
    int hit = 0;
    display_lock();
    if (s_display.home_course_mode && s_display.home_course_count > 1 &&
        s_display.home_panel &&
        !lv_obj_has_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_t *buttons[2] = {s_display.home_course_prev, s_display.home_course_next};
        for (int i = 0; i < 2; i++) {
            if (hit_in(buttons[i], x, y, TOUCH_HIT_SLOP)) {
                hit = i == 0 ? -1 : 1;
                break;
            }
        }
    }
    display_unlock();
    return hit;
}

/* Trả về id của card tại (x,y) khi home đang hiện; NULL nếu không trúng/không hiện.
 * Dùng cho hit-test chạm (increment 2). */
const char *display_home_hit_id(int x, int y) {
    static char course_hit_id[24];
    const char *hit = NULL;
    display_lock();
    if (s_display.home_panel &&
        !lv_obj_has_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN)) {
        if (s_display.home_course_mode && s_display.home_course_count > 0 &&
            s_display.home_course_panel &&
            !lv_obj_has_flag(s_display.home_course_panel, LV_OBJ_FLAG_HIDDEN)) {
            lv_area_t a;
            lv_obj_get_coords(s_display.home_course_panel, &a);
            if (x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) {
                int i = s_display.home_course_index;
                strlcpy(course_hit_id,
                        (i >= 0 && i < s_display.home_course_count &&
                         s_display.home_course_ids[i])
                            ? s_display.home_course_ids[i] : "",
                        sizeof(course_hit_id));
                hit = course_hit_id[0] ? course_hit_id : NULL;
            }
        } else {
            for (int i = 0; i < s_display.home_card_count && i < VIMATE_HOME_MAX; i++) {
                lv_obj_t *c = s_display.home_cards[i];
                if (!c || lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
                lv_area_t a;
                lv_obj_get_coords(c, &a);
                if (x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) {
                    hit = s_display.home_card_ids[i];
                    break;
                }
            }
        }
    }
    display_unlock();
    return hit;
}

/* Bottom bar hit-test (mockup): trả id app cố định tại (x,y), NULL nếu trượt. */
const char *display_home_bar_hit_id(int x, int y) {
    const char *hit = NULL;
    display_lock();
    /* Bar giờ GLOBAL — gate theo bar hiện (không chỉ Home). */
    if (s_display.bottom_bar &&
        !lv_obj_has_flag(s_display.bottom_bar, LV_OBJ_FLAG_HIDDEN)) {
        for (int i = 0; i < HOME_BAR_N; i++) {
            if (hit_in(s_display.home_bar_btns[i], x, y, TOUCH_HIT_SLOP)) {
                hit = kHomeBarIds[i];
                break;
            }
        }
    }
    display_unlock();
    return hit;
}

/* ===== EDU timetable screen ===== */
typedef struct {
    char *class_label;
    char *morning[DISPLAY_TIMETABLE_DAYS];
    char *afternoon[DISPLAY_TIMETABLE_DAYS];
    char *morning_detail[DISPLAY_TIMETABLE_DAYS];
    char *afternoon_detail[DISPLAY_TIMETABLE_DAYS];
} sched_timetable_t;

static char *timetable_strdup_psram(const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src) + 1;
    char *copy = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) copy = malloc(len);
    if (!copy) return NULL;
    memcpy(copy, src, len);
    return copy;
}

static void timetable_free_sched(sched_timetable_t *p) {
    if (!p) return;
    free(p->class_label);
    for (int i = 0; i < DISPLAY_TIMETABLE_DAYS; i++) {
        free(p->morning[i]);
        free(p->afternoon[i]);
        free(p->morning_detail[i]);
        free(p->afternoon_detail[i]);
    }
    free(p);
}

static int timetable_count_text_lines(const char *text) {
    if (!text || !text[0]) return 0;
    int lines = 1;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') lines++;
    }
    return lines;
}

static int timetable_detail_total_lines_for_day(int day) {
    if (day < 1 || day > DISPLAY_TIMETABLE_DAYS) return 0;
    const char *morning = s_display.timetable_morning_detail[day - 1];
    const char *afternoon = s_display.timetable_afternoon_detail[day - 1];
    return 2 + (morning && morning[0] ? timetable_count_text_lines(morning) : 1) +
           (afternoon && afternoon[0] ? timetable_count_text_lines(afternoon) : 1);
}

static void timetable_append_visible_line(char *buf, size_t buf_len, int *idx,
                                          int offset, int max_lines, int *added,
                                          const char *line, size_t line_len) {
    if (!buf || !idx || !added || !line) return;
    if (*idx >= offset && *added < max_lines) {
        size_t cur = strlen(buf);
        if (cur > 0 && cur + 1 < buf_len) {
            buf[cur++] = '\n';
            buf[cur] = '\0';
        }
        size_t room = (cur < buf_len) ? (buf_len - cur - 1) : 0;
        if (line_len > room) line_len = room;
        if (line_len > 0) {
            memcpy(buf + cur, line, line_len);
            buf[cur + line_len] = '\0';
        }
        (*added)++;
    }
    (*idx)++;
}

static void timetable_append_visible_text(char *buf, size_t buf_len, int *idx,
                                          int offset, int max_lines, int *added,
                                          const char *prefix, const char *text) {
    if (!text || !text[0]) {
        timetable_append_visible_line(buf, buf_len, idx, offset, max_lines, added,
                                      "- Chưa có lịch", strlen("- Chưa có lịch"));
        return;
    }
    const char *p = text;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t src_len = end ? (size_t)(end - p) : strlen(p);
        char line[96];
        int n = snprintf(line, sizeof(line), "%s%.*s", prefix ? prefix : "",
                         (int)src_len, p);
        if (n < 0) n = 0;
        size_t line_len = (size_t)n;
        if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
        timetable_append_visible_line(buf, buf_len, idx, offset, max_lines, added,
                                      line, line_len);
        if (!end) break;
        p = end + 1;
    }
}

static void timetable_render_detail_locked(int day) {
    if (day < 1 || day > DISPLAY_TIMETABLE_DAYS || !s_display.timetable_detail_body) return;
    const int visible_lines = 7;
    int total = timetable_detail_total_lines_for_day(day);
    int max_offset = total > visible_lines ? total - visible_lines : 0;
    if (s_display.timetable_detail_offset < 0) s_display.timetable_detail_offset = 0;
    if (s_display.timetable_detail_offset > max_offset) {
        s_display.timetable_detail_offset = max_offset;
    }

    char buf[640] = {0};
    int idx = 0;
    int added = 0;
    int offset = s_display.timetable_detail_offset;
    timetable_append_visible_line(buf, sizeof(buf), &idx, offset, visible_lines, &added,
                                  "Sáng", strlen("Sáng"));
    timetable_append_visible_text(buf, sizeof(buf), &idx, offset, visible_lines, &added,
                                  "- ", s_display.timetable_morning_detail[day - 1]);
    timetable_append_visible_line(buf, sizeof(buf), &idx, offset, visible_lines, &added,
                                  "Chiều", strlen("Chiều"));
    timetable_append_visible_text(buf, sizeof(buf), &idx, offset, visible_lines, &added,
                                  "- ", s_display.timetable_afternoon_detail[day - 1]);

    s_display.timetable_detail_total_lines = total;
    lv_label_set_text(s_display.timetable_detail_body, buf[0] ? buf : "Chưa có lịch");
    if (s_display.timetable_detail_hint) {
        if (max_offset > 0) {
            lv_label_set_text(s_display.timetable_detail_hint,
                              s_display.timetable_detail_offset == 0
                                  ? "Vuốt lên để xem thêm"
                                  : (s_display.timetable_detail_offset >= max_offset
                                         ? "Vuốt xuống để xem lại"
                                         : "Vuốt lên/xuống để xem"));
        } else {
            lv_label_set_text(s_display.timetable_detail_hint, "Chạm để quay lại bảng");
        }
    }
}

static void apply_timetable(void *arg) {
    sched_timetable_t *p = (sched_timetable_t *)arg;
    if (!s_display.setup_ui_called || !s_display.timetable_panel) {
        timetable_free_sched(p);
        return;
    }
    display_note_activity();
    display_lock();
    slideshow_interrupt_locked();
    screens_dismiss_locked();
    if (s_display.emoji_image) lv_obj_add_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
    if (s_display.emoji_gif) lv_obj_add_flag(s_display.emoji_gif, LV_OBJ_FLAG_HIDDEN);
    if (s_display.status_label) lv_obj_add_flag(s_display.status_label, LV_OBJ_FLAG_HIDDEN);
    if (s_display.chat_label) lv_obj_add_flag(s_display.chat_label, LV_OBJ_FLAG_HIDDEN);
    if (s_display.preview_image) lv_obj_add_flag(s_display.preview_image, LV_OBJ_FLAG_HIDDEN);
    if (s_display.popup_panel) lv_obj_add_flag(s_display.popup_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_display.reward_panel) lv_obj_add_flag(s_display.reward_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_display.home_panel) lv_obj_add_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);

    if (s_display.timetable_class_label) {
        lv_label_set_text(s_display.timetable_class_label,
                          (p->class_label && p->class_label[0])
                              ? p->class_label
                              : "Lớp: ...");
    }
    for (int day = 0; day < DISPLAY_TIMETABLE_DAYS; day++) {
        if (s_display.timetable_cells[0][day]) {
            strlcpy(s_display.timetable_morning_text[day],
                    p->morning[day] ? p->morning[day] : "",
                    sizeof(s_display.timetable_morning_text[day]));
            free(s_display.timetable_morning_detail[day]);
            s_display.timetable_morning_detail[day] =
                timetable_strdup_psram(p->morning_detail[day]
                                           ? p->morning_detail[day]
                                           : s_display.timetable_morning_text[day]);
            lv_label_set_text(s_display.timetable_cells[0][day],
                              s_display.timetable_morning_text[day]);
        }
        if (s_display.timetable_cells[1][day]) {
            strlcpy(s_display.timetable_afternoon_text[day],
                    p->afternoon[day] ? p->afternoon[day] : "",
                    sizeof(s_display.timetable_afternoon_text[day]));
            free(s_display.timetable_afternoon_detail[day]);
            s_display.timetable_afternoon_detail[day] =
                timetable_strdup_psram(p->afternoon_detail[day]
                                           ? p->afternoon_detail[day]
                                           : s_display.timetable_afternoon_text[day]);
            lv_label_set_text(s_display.timetable_cells[1][day],
                              s_display.timetable_afternoon_text[day]);
        }
    }
    s_display.timetable_selected_day = 0;
    s_display.timetable_detail_offset = 0;
    s_display.timetable_detail_total_lines = 0;
    if (s_display.timetable_detail_panel) {
        lv_obj_add_flag(s_display.timetable_detail_panel, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(s_display.timetable_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_display.timetable_panel);
    display_unlock();
    timetable_free_sched(p);
}

void display_show_timetable(const char *class_label, const char *const *morning,
                            const char *const *afternoon,
                            const char *const *morning_detail,
                            const char *const *afternoon_detail, int day_count) {
    if (!s_display.sched_q) return;
    if (day_count < 0) day_count = 0;
    if (day_count > DISPLAY_TIMETABLE_DAYS) day_count = DISPLAY_TIMETABLE_DAYS;
    sched_timetable_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->class_label = class_label ? strdup(class_label) : NULL;
    for (int i = 0; i < day_count; i++) {
        p->morning[i] = (morning && morning[i]) ? strdup(morning[i]) : NULL;
        p->afternoon[i] = (afternoon && afternoon[i]) ? strdup(afternoon[i]) : NULL;
        p->morning_detail[i] =
            (morning_detail && morning_detail[i]) ? strdup(morning_detail[i]) : NULL;
        p->afternoon_detail[i] =
            (afternoon_detail && afternoon_detail[i]) ? strdup(afternoon_detail[i]) : NULL;
    }
    if (!display_schedule(apply_timetable, p)) {
        timetable_free_sched(p);
    }
}

static void apply_hide_timetable(void *arg) {
    (void)arg;
    display_lock();
    if (s_display.timetable_detail_panel) {
        lv_obj_add_flag(s_display.timetable_detail_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_display.timetable_panel) {
        lv_obj_add_flag(s_display.timetable_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_display.home_card_count > 0 && s_display.home_panel) {
        lv_obj_clear_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_display.home_panel);
    } else {
        if (s_display.emoji_image) lv_obj_clear_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
        if (s_display.status_label) lv_obj_clear_flag(s_display.status_label, LV_OBJ_FLAG_HIDDEN);
    }
    display_unlock();
}

void display_hide_timetable(void) {
    if (!s_display.sched_q) return;
    display_schedule(apply_hide_timetable, NULL);
}

bool display_timetable_visible(void) {
    display_lock();
    bool visible = s_display.timetable_panel &&
                   !lv_obj_has_flag(s_display.timetable_panel, LV_OBJ_FLAG_HIDDEN);
    display_unlock();
    return visible;
}

static bool display_timetable_detail_visible(void) {
    display_lock();
    bool visible = s_display.timetable_detail_panel &&
                   !lv_obj_has_flag(s_display.timetable_detail_panel, LV_OBJ_FLAG_HIDDEN);
    display_unlock();
    return visible;
}

static void apply_timetable_day_detail(void *arg) {
    int day = (int)(intptr_t)arg;
    if (day < 1 || day > DISPLAY_TIMETABLE_DAYS) return;
    if (!s_display.timetable_detail_panel ||
        lv_obj_has_flag(s_display.timetable_panel, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    display_note_activity();
    display_lock();
    if (s_display.timetable_detail_title) {
        lv_label_set_text(s_display.timetable_detail_title, timetable_day_name(day));
    }
    s_display.timetable_selected_day = day;
    s_display.timetable_detail_offset = 0;
    s_display.timetable_detail_total_lines = timetable_detail_total_lines_for_day(day);
    timetable_render_detail_locked(day);
    lv_obj_clear_flag(s_display.timetable_detail_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_display.timetable_detail_panel);
    display_unlock();
}

static void apply_hide_timetable_detail(void *arg) {
    (void)arg;
    display_lock();
    if (s_display.timetable_detail_panel) {
        lv_obj_add_flag(s_display.timetable_detail_panel, LV_OBJ_FLAG_HIDDEN);
    }
    s_display.timetable_selected_day = 0;
    s_display.timetable_detail_offset = 0;
    s_display.timetable_detail_total_lines = 0;
    display_unlock();
}

static void apply_timetable_scroll_detail(void *arg) {
    int direction = (int)(intptr_t)arg;
    if (direction == 0 || s_display.timetable_selected_day < 1 ||
        !display_timetable_detail_visible()) {
        return;
    }
    display_lock();
    int old = s_display.timetable_detail_offset;
    s_display.timetable_detail_offset += direction > 0 ? 2 : -2;
    timetable_render_detail_locked(s_display.timetable_selected_day);
    if (s_display.timetable_detail_offset != old) {
        display_note_activity();
    }
    display_unlock();
}

bool display_timetable_handle_tap(int x, int y) {
    if (!display_timetable_visible() || !s_display.sched_q) return false;
    if (display_timetable_detail_visible()) {
        display_schedule(apply_hide_timetable_detail, NULL);
        return true;
    }
    for (int i = 0; i < DISPLAY_TIMETABLE_DAYS; i++) {
        int x1 = s_display.timetable_day_hit_x[i];
        int x2 = x1 + s_display.timetable_day_hit_w[i];
        int y1 = s_display.timetable_day_hit_y;
        int y2 = y1 + s_display.timetable_day_hit_h;
        if (s_display.timetable_day_hit_w[i] > 0 &&
            x >= x1 && x <= x2 && y >= y1 && y <= y2) {
            display_schedule(apply_timetable_day_detail, (void *)(intptr_t)(i + 1));
            return true;
        }
    }
    return false;
}

bool display_timetable_scroll_detail(int direction) {
    if (!display_timetable_visible() || !display_timetable_detail_visible() ||
        !s_display.sched_q || direction == 0) {
        return false;
    }
    display_schedule(apply_timetable_scroll_detail, (void *)(intptr_t)direction);
    return true;
}

/* ===== Alarm screen (Hẹn giờ/Uống nước) ===== */
typedef struct {
    char                 *text;
    const lv_image_dsc_t *icon;
    uint32_t              bg1, bg2, txtcol;
} sched_alarm_t;

static void apply_hide_alarm(void *arg) {
    (void)arg;
    display_lock();
    if (s_display.alarm_panel) lv_obj_add_flag(s_display.alarm_panel, LV_OBJ_FLAG_HIDDEN);
    /* Về Home nếu có lưới; nếu không → mặt cười + status. */
    if (s_display.home_card_count > 0 && s_display.home_panel) {
        lv_obj_clear_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_display.home_panel);
    } else {
        if (s_display.emoji_image) lv_obj_clear_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
        if (s_display.status_label) lv_obj_clear_flag(s_display.status_label, LV_OBJ_FLAG_HIDDEN);
    }
    display_unlock();
}

static void on_alarm_hide_timer(void *arg) {
    (void)arg;
    display_schedule(apply_hide_alarm, NULL);
}

static void apply_alarm(void *arg) {
    sched_alarm_t *p = (sched_alarm_t *)arg;
    display_lock();
    /* Nếu đang hiện đồng hồ → ẩn + dừng tick để màn nhắc lên sạch. */
	if (s_display.clock_panel &&
	    !lv_obj_has_flag(s_display.clock_panel, LV_OBJ_FLAG_HIDDEN)) {
		lv_obj_add_flag(s_display.clock_panel, LV_OBJ_FLAG_HIDDEN);
		if (s_display.clock_timer) esp_timer_stop(s_display.clock_timer);
	}
	if (s_display.countdown_panel) lv_obj_add_flag(s_display.countdown_panel, LV_OBJ_FLAG_HIDDEN);
	if (s_display.countdown_timer) esp_timer_stop(s_display.countdown_timer);
	s_display.countdown_active = false;
	if (s_display.alarm_panel) {
        lv_obj_set_style_bg_color(s_display.alarm_panel, lv_color_hex(p->bg1), 0);
        lv_obj_set_style_bg_grad_color(s_display.alarm_panel, lv_color_hex(p->bg2), 0);
        if (s_display.alarm_icon && p->icon) lv_image_set_src(s_display.alarm_icon, p->icon);
        if (s_display.alarm_text) {
            lv_label_set_text(s_display.alarm_text, p->text ? p->text : "");
            lv_obj_set_style_text_color(s_display.alarm_text, lv_color_hex(p->txtcol), 0);
        }
        if (s_display.emoji_image) lv_obj_add_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
        if (s_display.emoji_gif) lv_obj_add_flag(s_display.emoji_gif, LV_OBJ_FLAG_HIDDEN);
        if (s_display.status_label) lv_obj_add_flag(s_display.status_label, LV_OBJ_FLAG_HIDDEN);
        if (s_display.chat_label) lv_obj_add_flag(s_display.chat_label, LV_OBJ_FLAG_HIDDEN);
        if (s_display.preview_image) lv_obj_add_flag(s_display.preview_image, LV_OBJ_FLAG_HIDDEN);
        if (s_display.home_panel) lv_obj_add_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
        if (s_display.timetable_panel) lv_obj_add_flag(s_display.timetable_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_display.alarm_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_display.alarm_panel);
    }
    display_unlock();
    if (s_display.alarm_hide_timer) {
        esp_timer_stop(s_display.alarm_hide_timer);
        esp_timer_start_once(s_display.alarm_hide_timer, 14 * 1000 * 1000ULL);
    }
    free(p->text);
    free(p);
}

void display_show_alarm(const char *text, bool is_water) {
    if (!s_display.sched_q) return;
    sched_alarm_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->text = text ? strdup(text) : NULL;
    if (is_water) {
        p->icon = &icon_alarm_water;
        p->bg1 = 0xcdeffd; p->bg2 = 0xf0f9ff; p->txtcol = 0x075985;
    } else {
        p->icon = &icon_alarm_timer;
        p->bg1 = 0xffe9c7; p->bg2 = 0xfff7ed; p->txtcol = 0x7c2d12;
    }
    if (!display_schedule(apply_alarm, p)) { free(p->text); free(p); }
}

void display_hide_alarm(void) {
    if (!s_display.sched_q) return;
    display_schedule(apply_hide_alarm, NULL);
}

bool display_alarm_visible(void) {
	display_lock();
	bool visible = s_display.alarm_panel &&
	               !lv_obj_has_flag(s_display.alarm_panel, LV_OBJ_FLAG_HIDDEN);
	display_unlock();
	return visible;
}

/* ===== Countdown screen ===== */
typedef struct {
	char *label;
	int   total_seconds;
} sched_countdown_t;

static void countdown_render_locked(void) {
	if (!s_display.countdown_time) return;
	int64_t now = esp_timer_get_time();
	int64_t remain = (s_display.countdown_end_us - now + 999999) / 1000000;
	if (remain <= 0) {
		remain = 0;
		if (s_display.countdown_active) {
			s_display.countdown_active = false;
			if (s_display.countdown_timer) esp_timer_stop(s_display.countdown_timer);
			if (s_display.countdown_title) lv_label_set_text(s_display.countdown_title, "Hết giờ rồi!");
		}
	}
	int minutes = (int)(remain / 60);
	int seconds = (int)(remain % 60);
	char buf[16];
	snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
	lv_label_set_text(s_display.countdown_time, buf);
}

static void apply_countdown_tick(void *arg) {
	(void)arg;
	display_lock();
	if (s_display.countdown_panel &&
	    !lv_obj_has_flag(s_display.countdown_panel, LV_OBJ_FLAG_HIDDEN)) {
		countdown_render_locked();
	}
	display_unlock();
}

static void on_countdown_timer(void *arg) {
	(void)arg;
	display_schedule(apply_countdown_tick, NULL);
}

static void apply_hide_countdown(void *arg) {
	(void)arg;
	display_lock();
	if (s_display.countdown_timer) esp_timer_stop(s_display.countdown_timer);
	s_display.countdown_active = false;
	if (s_display.countdown_panel) lv_obj_add_flag(s_display.countdown_panel, LV_OBJ_FLAG_HIDDEN);
	if (s_display.home_card_count > 0 && s_display.home_panel) {
		lv_obj_clear_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_move_foreground(s_display.home_panel);
	} else {
		if (s_display.emoji_image) lv_obj_clear_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
		if (s_display.status_label) lv_obj_clear_flag(s_display.status_label, LV_OBJ_FLAG_HIDDEN);
	}
	display_unlock();
}

static void apply_countdown(void *arg) {
	sched_countdown_t *p = (sched_countdown_t *)arg;
	int total = p->total_seconds;
	if (total <= 0) total = 60;
	if (total > 36000) total = 36000;
	display_note_activity();
	display_lock();
	slideshow_interrupt_locked();
	if (s_display.clock_panel &&
	    !lv_obj_has_flag(s_display.clock_panel, LV_OBJ_FLAG_HIDDEN)) {
		lv_obj_add_flag(s_display.clock_panel, LV_OBJ_FLAG_HIDDEN);
		if (s_display.clock_timer) esp_timer_stop(s_display.clock_timer);
	}
	if (s_display.alarm_panel) lv_obj_add_flag(s_display.alarm_panel, LV_OBJ_FLAG_HIDDEN);
	if (s_display.alarm_hide_timer) esp_timer_stop(s_display.alarm_hide_timer);
	if (s_display.emoji_image) lv_obj_add_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
	if (s_display.emoji_gif) lv_obj_add_flag(s_display.emoji_gif, LV_OBJ_FLAG_HIDDEN);
	if (s_display.status_label) lv_obj_add_flag(s_display.status_label, LV_OBJ_FLAG_HIDDEN);
	if (s_display.chat_label) lv_obj_add_flag(s_display.chat_label, LV_OBJ_FLAG_HIDDEN);
	if (s_display.preview_image) lv_obj_add_flag(s_display.preview_image, LV_OBJ_FLAG_HIDDEN);
	if (s_display.popup_panel) lv_obj_add_flag(s_display.popup_panel, LV_OBJ_FLAG_HIDDEN);
	if (s_display.home_panel) lv_obj_add_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
	if (s_display.timetable_panel) lv_obj_add_flag(s_display.timetable_panel, LV_OBJ_FLAG_HIDDEN);
	s_display.countdown_end_us = esp_timer_get_time() + (int64_t)total * 1000000LL;
	s_display.countdown_active = true;
	if (s_display.countdown_title) {
		lv_label_set_text(s_display.countdown_title,
		                  (p->label && p->label[0]) ? p->label : "Đếm ngược");
	}
	countdown_render_locked();
	if (s_display.countdown_panel) {
		lv_obj_clear_flag(s_display.countdown_panel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_move_foreground(s_display.countdown_panel);
	}
	display_unlock();
	if (s_display.countdown_timer) {
		esp_timer_stop(s_display.countdown_timer);
		esp_timer_start_periodic(s_display.countdown_timer, 1000 * 1000ULL);
	}
	free(p->label);
	free(p);
}

void display_show_countdown(const char *label, int total_seconds) {
	if (!s_display.sched_q) return;
	sched_countdown_t *p = calloc(1, sizeof(*p));
	if (!p) return;
	p->label = label ? strdup(label) : NULL;
	p->total_seconds = total_seconds;
	if (!display_schedule(apply_countdown, p)) { free(p->label); free(p); }
}

void display_hide_countdown(void) {
	if (!s_display.sched_q) return;
	display_schedule(apply_hide_countdown, NULL);
}

bool display_countdown_visible(void) {
	display_lock();
	bool visible = s_display.countdown_panel &&
	               !lv_obj_has_flag(s_display.countdown_panel, LV_OBJ_FLAG_HIDDEN);
	display_unlock();
	return visible;
}

/* ===== Clock screen (app Đồng hồ) ===== */
typedef struct { int64_t base_local; int64_t base_us; bool wallpaper; } sched_clock_t;

/* clock_render — tính giờ địa phương từ mốc server + đồng hồ đơn điệu, cập nhật
 * 2 nhãn. GỌI KHI ĐANG GIỮ display_lock(). Dùng gmtime_r vì base_local đã cộng
 * sẵn lệch múi giờ (FW không set TZ từ SNTP). */
static void clock_render(void) {
    if (!s_display.clock_time) return;
    int64_t elapsed = (esp_timer_get_time() - s_display.clock_base_us) / 1000000;
    time_t local = (time_t)(s_display.clock_base_local + elapsed);
    struct tm tmv;
    gmtime_r(&local, &tmv);
    char hhmm[8];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    lv_label_set_text(s_display.clock_time, hhmm);
    if (s_display.clock_date) {
        int w = tmv.tm_wday; if (w < 0 || w > 6) w = 0;
        char date[64];
        if (s_display.clock_wallpaper && s_display.ss_active) {
            /* Màn chờ slideshow: gọn kiểu Google ("CN, 14 Thg 6") cho góc dưới-trái. */
            static const char *const wds[7] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};
            snprintf(date, sizeof(date), "%s, %d Thg %d",
                     wds[w], tmv.tm_mday, tmv.tm_mon + 1);
        } else if (s_display.clock_wallpaper) {
            /* Đồng hồ nền-ảnh (One Piece): "DD / MM / YYYY" + "Thứ X" 2 dòng giữa khung. */
            const char *wk = (w == 0) ? "Chủ nhật"
                           : (w == 1) ? "Thứ 2" : (w == 2) ? "Thứ 3"
                           : (w == 3) ? "Thứ 4" : (w == 4) ? "Thứ 5"
                           : (w == 5) ? "Thứ 6" : "Thứ 7";
            int yr = tmv.tm_year + 1900;
            if (yr < 2000 || yr > 2099) yr = 2000;
            snprintf(date, sizeof(date), "%02d / %02d / %04d\n%s",
                     tmv.tm_mday, tmv.tm_mon + 1, yr, wk);
        } else {
            /* App Đồng hồ full màn: dạng đầy đủ. */
            static const char *const wd[7] = {
                "Chủ nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư",
                "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};
            snprintf(date, sizeof(date), "%s, %02d/%02d",
                     wd[w], tmv.tm_mday, tmv.tm_mon + 1);
        }
        lv_label_set_text(s_display.clock_date, date);
    }
}

static void apply_clock_tick(void *arg) {
    (void)arg;
    display_lock();
    if (s_display.clock_panel &&
        !lv_obj_has_flag(s_display.clock_panel, LV_OBJ_FLAG_HIDDEN)) {
        clock_render();
    }
    display_unlock();
}

static void on_clock_timer(void *arg) {
    (void)arg;
    display_schedule(apply_clock_tick, NULL);
}

static void apply_clock(void *arg) {
    sched_clock_t *p = (sched_clock_t *)arg;
    display_lock();
    s_display.clock_base_local = p->base_local;
    s_display.clock_base_us = p->base_us;
    s_display.clock_wallpaper = p->wallpaper;
    if (s_display.clock_panel) {
        if (s_display.emoji_image) lv_obj_add_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
        if (s_display.emoji_gif) lv_obj_add_flag(s_display.emoji_gif, LV_OBJ_FLAG_HIDDEN);
        if (s_display.status_label) lv_obj_add_flag(s_display.status_label, LV_OBJ_FLAG_HIDDEN);
        if (s_display.chat_label) lv_obj_add_flag(s_display.chat_label, LV_OBJ_FLAG_HIDDEN);
        if (s_display.home_panel) lv_obj_add_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
	    if (s_display.timetable_panel) lv_obj_add_flag(s_display.timetable_panel, LV_OBJ_FLAG_HIDDEN);
	    if (s_display.alarm_panel) lv_obj_add_flag(s_display.alarm_panel, LV_OBJ_FLAG_HIDDEN);
	    if (s_display.countdown_panel) lv_obj_add_flag(s_display.countdown_panel, LV_OBJ_FLAG_HIDDEN);
	    if (s_display.countdown_timer) esp_timer_stop(s_display.countdown_timer);
	    s_display.countdown_active = false;
	    if (p->wallpaper) {
            /* Nền = ảnh (preview_image do server gửi ShowImage). Panel trong suốt,
             * KHÔNG scrim. Vị trí + màu chữ KHÁC nhau theo nguồn:
             *  - SLIDESHOW (ss_active): chữ TRẮNG ở GÓC, không che mặt (Google Nest Hub).
             *  - ĐỒNG HỒ APP nền-ảnh (vd One Piece): giờ TO màu ĐỎ ở GIỮA (trong khung
             *    giấy da), ngày dưới — khớp khung tranh trung tâm. */
            lv_obj_set_style_bg_opa(s_display.clock_panel, LV_OPA_TRANSP, 0);
            if (s_display.clock_sun) lv_obj_add_flag(s_display.clock_sun, LV_OBJ_FLAG_HIDDEN);
            if (s_display.clock_cloud) lv_obj_add_flag(s_display.clock_cloud, LV_OBJ_FLAG_HIDDEN);
            if (s_display.clock_scrim) lv_obj_add_flag(s_display.clock_scrim, LV_OBJ_FLAG_HIDDEN);
            if (s_display.clock_hint)
                lv_obj_set_style_text_color(s_display.clock_hint, lv_color_hex(UI_CLR_ON_PRIMARY), 0);
            if (s_display.ss_active) {
                /* Slideshow: ngày-giờ NHỎ ở GÓC TRÊN-TRÁI (như edu/Nest Hub), chữ
                 * trắng, không che ảnh. Font nhỏ (24/18). */
                lv_obj_set_style_text_font(s_display.clock_time, &lv_font_vimate_24, 0);
                lv_obj_set_style_text_font(s_display.clock_date, &lv_font_vimate_18, 0);
                lv_obj_set_style_text_color(s_display.clock_time, lv_color_hex(0xffffff), 0);
                lv_obj_set_style_text_color(s_display.clock_date, lv_color_hex(0xf0f0f0), 0);
                lv_obj_set_style_text_align(s_display.clock_date, LV_TEXT_ALIGN_LEFT, 0);
                lv_obj_align(s_display.clock_date, LV_ALIGN_BOTTOM_LEFT, 22, -72);
                lv_obj_align(s_display.clock_time, LV_ALIGN_BOTTOM_LEFT, 20, -16);
            } else {
                /* Đồng hồ nền-ảnh: giờ ĐỎ to giữa khung, ngày đỏ-nâu dưới (font to lại). */
                lv_obj_set_style_text_font(s_display.clock_time, &lv_font_vimate_48, 0);
                lv_obj_set_style_text_font(s_display.clock_date, &lv_font_vimate_24, 0);
                lv_obj_set_style_text_color(s_display.clock_time, lv_color_hex(0xd62828), 0);
                lv_obj_set_style_text_color(s_display.clock_date, lv_color_hex(0xc0392b), 0);
                lv_obj_set_style_text_align(s_display.clock_date, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_align(s_display.clock_time, LV_ALIGN_CENTER, 0, -16);
                lv_obj_align(s_display.clock_date, LV_ALIGN_CENTER, 0, 40);
            }
            /* KHÔNG ẩn preview_image — nó là ảnh nền. */
        } else {
            /* Digital HIỆN ĐẠI: nền tối deep-navy → slate, giờ cyan sáng, ngày xám
             * dịu, bỏ mặt trời/mây (tối giản, futuristic). */
            lv_obj_set_style_bg_color(s_display.clock_panel, lv_color_hex(0x0b1220), 0);
            lv_obj_set_style_bg_grad_color(s_display.clock_panel, lv_color_hex(0x1e293b), 0);
            lv_obj_set_style_bg_grad_dir(s_display.clock_panel, LV_GRAD_DIR_VER, 0);
            lv_obj_set_style_bg_opa(s_display.clock_panel, LV_OPA_COVER, 0);
            if (s_display.clock_sun) lv_obj_add_flag(s_display.clock_sun, LV_OBJ_FLAG_HIDDEN);
            if (s_display.clock_cloud) lv_obj_add_flag(s_display.clock_cloud, LV_OBJ_FLAG_HIDDEN);
            if (s_display.clock_scrim) lv_obj_add_flag(s_display.clock_scrim, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(s_display.clock_time, lv_color_hex(0x67e8f9), 0);
            lv_obj_set_style_text_color(s_display.clock_date, lv_color_hex(0x94a3b8), 0);
            /* hint cùng xám dịu với ngày: 94a3b8 trên 1e293b (đáy gradient) = 5,6:1 */
            if (s_display.clock_hint)
                lv_obj_set_style_text_color(s_display.clock_hint, lv_color_hex(0x94a3b8), 0);
            /* Khôi phục font TO (app Đồng hồ) + căn GIỮA (nhãn dùng chung với
             * nhánh slideshow-góc-nhỏ ở trên). */
            lv_obj_set_style_text_font(s_display.clock_time, &lv_font_vimate_48, 0);
            lv_obj_set_style_text_font(s_display.clock_date, &lv_font_vimate_24, 0);
            lv_obj_set_style_text_align(s_display.clock_date, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(s_display.clock_time, LV_ALIGN_CENTER, 0, -18);
            lv_obj_align(s_display.clock_date, LV_ALIGN_CENTER, 0, 40);
            if (s_display.preview_image) lv_obj_add_flag(s_display.preview_image, LV_OBJ_FLAG_HIDDEN);
        }
        clock_render();
        lv_obj_clear_flag(s_display.clock_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_display.clock_panel); /* trên cả ảnh nền */
    }
    display_unlock();
    if (s_display.clock_timer) {
        esp_timer_stop(s_display.clock_timer);
        esp_timer_start_periodic(s_display.clock_timer, 1000 * 1000ULL);
    }
    free(p);
}

static void apply_hide_clock(void *arg) {
    (void)arg;
    if (s_display.clock_timer) esp_timer_stop(s_display.clock_timer);
    /* Nếu đang slideshow → dừng vòng đổi ảnh (clock-hide = thoát màn chờ). */
    if (s_display.ss_active) {
        if (s_display.ss_timer) esp_timer_stop(s_display.ss_timer);
        s_display.ss_active = false;
    }
    display_lock();
    bool was_wp = s_display.clock_wallpaper;
    s_display.clock_wallpaper = false;
    if (s_display.clock_panel) lv_obj_add_flag(s_display.clock_panel, LV_OBJ_FLAG_HIDDEN);
    /* CHỈ gỡ ảnh nền nếu đồng hồ đang dùng wallpaper (tránh ẩn nhầm ảnh khác). */
    if (was_wp && s_display.preview_image) lv_obj_add_flag(s_display.preview_image, LV_OBJ_FLAG_HIDDEN);
    /* Về Home khi thoát màn chờ (slideshow/đồng hồ). */
    if (s_display.home_card_count > 0 && s_display.home_panel) {
        lv_obj_clear_flag(s_display.home_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_display.home_panel);
    } else {
        if (s_display.emoji_image) lv_obj_clear_flag(s_display.emoji_image, LV_OBJ_FLAG_HIDDEN);
        if (s_display.status_label) lv_obj_clear_flag(s_display.status_label, LV_OBJ_FLAG_HIDDEN);
    }
    display_unlock();
}

void display_show_clock(int64_t epoch_local, bool wallpaper) {
    if (!s_display.sched_q) return;
    sched_clock_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->base_local = epoch_local;
    p->base_us = esp_timer_get_time();
    p->wallpaper = wallpaper;
    if (!display_schedule(apply_clock, p)) free(p);
}

void display_hide_clock(void) {
    if (!s_display.sched_q) return;
    display_schedule(apply_hide_clock, NULL);
}

bool display_clock_visible(void) {
    display_lock();
    bool visible = s_display.clock_panel &&
                   !lv_obj_has_flag(s_display.clock_panel, LV_OBJ_FLAG_HIDDEN);
    display_unlock();
    return visible;
}

/* ===== Slideshow màn chờ (ảnh gia đình + giờ đè lên) ===== */
/* Tái dùng: ui_image (preview_image) cho ảnh + clock_panel (wallpaper mode) cho
 * lớp giờ. slideshow_start chiếu ảnh đầu + overlay giờ; ss_timer đổi ảnh; dừng qua
 * apply_hide_clock (chạm để tắt → về Home). Chạy trên display_task. */
/* COPY URL dưới display_lock rồi mới ui_image_show_async → tránh use-after-free khi
 * display_set_slideshow (WS task) free ss_urls song song. */
static void slideshow_show_current(void) {
    char url[256] = {0};
    display_lock();
    if (s_display.ss_count > 0) {
        int i = s_display.ss_idx % s_display.ss_count;
        if (s_display.ss_urls[i]) strlcpy(url, s_display.ss_urls[i], sizeof(url));
    }
    display_unlock();
    if (url[0]) ui_image_show_async_ex(url, true); /* frame slideshow → giữ nền đồng hồ */
}

static void apply_slideshow_start(void *arg) {
    (void)arg;
    if (!s_display.ss_enabled || s_display.ss_count <= 0) return;
    s_display.ss_active = true;
    bool ss_clock = s_display.ss_show_clock;
    display_lock();
    s_display.ss_idx = 0;
    if (!ss_clock && s_display.clock_panel) {
        if (s_display.clock_timer) esp_timer_stop(s_display.clock_timer);
        lv_obj_add_flag(s_display.clock_panel, LV_OBJ_FLAG_HIDDEN);
        s_display.clock_wallpaper = false;
    }
    display_unlock();
    slideshow_show_current();
    if (ss_clock) {
        display_show_clock(s_display.ss_clock_local, true);
    }
    if (s_display.ss_timer) {
        esp_timer_stop(s_display.ss_timer);
        int iv = s_display.ss_interval_sec > 0 ? s_display.ss_interval_sec : 8;
        esp_timer_start_periodic(s_display.ss_timer, (int64_t)iv * 1000000LL);
    }
}

static void apply_slideshow_next(void *arg) {
    (void)arg;
    if (!s_display.ss_active) return;
    display_lock();
    if (s_display.ss_count > 0) s_display.ss_idx = (s_display.ss_idx + 1) % s_display.ss_count;
    display_unlock();
    slideshow_show_current();
}

static void on_ss_timer(void *arg) {
    (void)arg;
    display_schedule(apply_slideshow_next, NULL);
}

/* "Màn nghỉ" để bật slideshow: EDU = lưới Home khi KHÔNG có popup/menu/list/
 * settings. Mọi tương tác (emotion/chat/state/popup/chạm) đều gọi
 * display_note_activity() → idle timer tự chặn slideshow giữa hội thoại/phát;
 * predicate này chỉ chốt "đang đứng ở màn nghỉ". */
static bool device_idle_scene_visible(void) {
    return display_home_visible();
}

/* Chạy trên display_task (do on_sleep_timer schedule) → đọc LVGL + ss an toàn. */

static void apply_ss_idle_tick(void *arg) {
    (void)arg;
    int64_t idle_us = esp_timer_get_time() - s_display.last_activity_us;
    int64_t idle_need = (int64_t)s_display.ss_idle_after_sec * 1000000LL;
    if (s_display.ss_enabled && s_display.ss_count > 0 && !s_display.ss_active &&
        !s_display.clock_wallpaper && !display_alarm_visible() &&
        device_idle_scene_visible() &&
        idle_us >= idle_need) {
        apply_slideshow_start(NULL);
    }
}

static void slideshow_stop(void) {
    /* Dừng timer; clock-hide (apply_hide_clock) lo ẩn ảnh + về Home. */
    if (s_display.ss_timer) esp_timer_stop(s_display.ss_timer);
    s_display.ss_active = false;
}

bool display_slideshow_active(void) { return s_display.ss_active; }

void display_stop_slideshow(void) {
    if (!s_display.ss_active) return;
    slideshow_stop();
    display_hide_clock(); /* ẩn overlay + ảnh nền + về Home */
}

/* Bật slideshow NGAY (không chờ idle timer) — dùng cho icon "Màn hình nghỉ".
 * Schedule lên display_task; apply_slideshow_start tự bỏ qua nếu chưa có ảnh
 * (ss_enabled==false || ss_count<=0) → khi đó server fallback gửi command clock. */
void display_start_slideshow(void) {
    if (!s_display.sched_q) return;
    display_schedule(apply_slideshow_start, NULL);
}

void display_set_slideshow(bool enabled, int interval_sec, bool show_clock,
                           int idle_after_sec, int64_t clock_local,
                           const char *const *urls, int n) {
    display_lock();
    /* Giải phóng URL cũ. */
    for (int i = 0; i < s_display.ss_count && i < VIMATE_SS_MAX; i++) {
        free(s_display.ss_urls[i]);
        s_display.ss_urls[i] = NULL;
    }
    s_display.ss_count = 0;
    s_display.ss_enabled = enabled;
    s_display.ss_show_clock = show_clock;
    s_display.ss_interval_sec = interval_sec;
    s_display.ss_idle_after_sec = idle_after_sec > 0 ? idle_after_sec : 60;
    s_display.ss_clock_local = clock_local;
    if (enabled && urls && n > 0) {
        int c = n > VIMATE_SS_MAX ? VIMATE_SS_MAX : n;
        for (int i = 0; i < c; i++) {
            s_display.ss_urls[i] = (urls[i] && urls[i][0]) ? strdup(urls[i]) : NULL;
        }
        s_display.ss_count = c;
    }
    display_unlock();
    /* Nếu tắt mà đang chạy → dừng ngay. */
    if (!enabled && s_display.ss_active) display_stop_slideshow();
    ESP_LOGI(TAG_UI, "slideshow set: en=%d n=%d iv=%d idle=%d",
             enabled, s_display.ss_count, interval_sec, s_display.ss_idle_after_sec);
}

void display_show_reward(int stars) {
    if (!s_display.sched_q) return;
    sched_reward_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    p->stars = stars;
    display_schedule(apply_reward, p);
}
