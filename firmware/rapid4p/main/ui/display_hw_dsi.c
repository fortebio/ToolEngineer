/**
 * display_hw_dsi.c — lớp phần cứng màn hình board ESP32-P4C5 + LCD 4.3" ST7102 MIPI-DSI.
 * Xem display_hw.h (giao diện) và display.c (phần chung: đèn nền, ngủ, hàng đợi, task).
 *
 * Nguồn: firmware-vimate-p4/main/ui/display.c (khối BOARD_LCD_USE_MIPI_DSI + DSI_ROTATE,
 * đã chạy thật trên board 11–15/09/2026), tách khỏi display.c của Rapid4P 2026-09-19 khi thêm
 * board S3 2.8" SPI. Giữ NGUYÊN trình tự và các ghi chú "vì sao" — mỗi bước ở đây từng là
 * một lỗi thật trên board.
 */
#include "display_hw.h"
#include "rapid4p.h"
#include "boards/board.h"

#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_st7102.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

#if !BOARD_LCD_USE_MIPI_DSI
#error "display_hw_dsi.c chi cho board MIPI-DSI (BOARD_LCD_USE_MIPI_DSI 1)"
#endif

/* Chống xé hình cho panel DPI (video mode): driver DPI cấp 2 frame buffer, LVGL vẽ
 * THẲNG vào fb (direct mode), đổi fb đúng ranh giới khung qua ISR on_refresh_done.
 * Không cần TE (chỉ dành cho panel command mode). */
#ifndef BOARD_LCD_DSI_AVOID_TEARING
#define BOARD_LCD_DSI_AVOID_TEARING 1
#endif
#if BOARD_LCD_ROTATION == 90 || BOARD_LCD_ROTATION == 270
#define DSI_ROTATE 1
#include "driver/ppa.h"
#elif BOARD_LCD_ROTATION == 0
#define DSI_ROTATE 0
#else
#error "BOARD_LCD_ROTATION: DSI chi ho tro 0 / 90 / 270"
#endif

/* Bảng init ST7102 — copy NGUYÊN VĂN từ demo chính chủ của board
 * (`ESP-IDF 5/P4-IDF_ST7102-MIPI_ESP-LVGL-PORT_V9/main/main.c`). BẮT BUỘC: init mặc
 * định trong component KHÔNG khớp panel 4.3" này (gamma/power/GIP riêng của nhà sản
 * xuất). 0x11 sleep-out chờ 600 ms, 0x29 display-on chờ 120 ms. */
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

/* Số lần flush (diag). Khi xoay: đếm trong dsi_rot_flush_cb; không xoay: LV_EVENT_FLUSH_FINISH. */
static uint32_t s_flush_count;

/* ===== Xoay landscape → fb DPI dọc bằng PPA =====
 * Toạ độ: logical (lx,ly) ∈ 800×480; native panel (px,py) ∈ 480×800.
 *   ROTATION 270: (px,py) = (479-ly, lx)     ROTATION 90: (px,py) = (ly, 799-lx)
 * PPA quay NGƯỢC kim đồng hồ, nên 270 ↔ PPA_ANGLE_270, 90 ↔ 90.
 * Đồng bộ 2 fb: xoay HỢP vùng bẩn khung N-1 và N là đủ để fb ẩn thành khung N (cách
 * LVGL làm với sync_areas). Hai khung đầu và khi tràn danh sách thì xoay cả màn. */
#if DSI_ROTATE
#define DSI_ROT_MAX_AREAS 32
typedef struct { lv_area_t a[DSI_ROT_MAX_AREAS]; int n; bool overflow; } dsi_rot_areas_t;
static struct {
    ppa_client_handle_t ppa;
    esp_lcd_panel_handle_t panel;
    uint8_t *fb[2];
    size_t fb_size;
    int front;
    SemaphoreHandle_t vsync;
    dsi_rot_areas_t cur, prev;
    int full_pending;
    uint32_t stat_full, stat_area;
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

static esp_err_t dsi_rot_put(const uint16_t *src_pic, int pic_w, int pic_h,
                             int bx, int by, int bw, int bh, int lx, int ly, uint8_t *dst_fb)
{
    if (bw <= 0 || bh <= 0) return ESP_OK;
#if BOARD_LCD_ROTATION == 270
    const int ox = BOARD_LCD_V_RES - ly - bh;
    const int oy = lx;
    const ppa_srm_rotation_angle_t ang = PPA_SRM_ROTATION_ANGLE_270;
#else
    const int ox = ly;
    const int oy = BOARD_LCD_H_RES - lx - bw;
    const ppa_srm_rotation_angle_t ang = PPA_SRM_ROTATION_ANGLE_90;
#endif
    if (s_rot.ppa) {
        ppa_srm_oper_config_t cfg = {
            .in = {
                .buffer = src_pic, .pic_w = pic_w, .pic_h = pic_h,
                .block_w = bw, .block_h = bh, .block_offset_x = bx, .block_offset_y = by,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            },
            .out = {
                .buffer = dst_fb, .buffer_size = s_rot.fb_size,
                .pic_w = BOARD_LCD_NATIVE_W, .pic_h = BOARD_LCD_NATIVE_H,
                .block_offset_x = ox, .block_offset_y = oy,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            },
            .rotation_angle = ang, .scale_x = 1.0f, .scale_y = 1.0f,
            .mode = PPA_TRANS_MODE_BLOCKING,
        };
        esp_err_t err = ppa_do_scale_rotate_mirror(s_rot.ppa, &cfg);
        if (err == ESP_OK) return ESP_OK;
        ESP_LOGW(TAG_UI, "PPA rotate (%d,%d %dx%d) loi %s -> CPU", lx, ly, bw, bh, esp_err_to_name(err));
    }
    /* Dự phòng CPU: chậm nhưng đúng. */
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
            if (dsi_rot_area_inside(a, &s_rot.cur)) continue;
            dsi_rot_put((const uint16_t *)px_map, BOARD_LCD_H_RES, BOARD_LCD_V_RES,
                        a->x1, a->y1, lv_area_get_width(a), lv_area_get_height(a), a->x1, a->y1, fb);
            s_rot.stat_area++;
        }
    }
    /* Con trỏ nằm trong fb của driver → driver chỉ msync + đổi cur_fb_index; ISR cuối
     * khung mới chuyển DMA sang fb này (đúng ranh giới khung, không xé). */
    esp_lcd_panel_draw_bitmap(s_rot.panel, 0, 0, BOARD_LCD_NATIVE_W, BOARD_LCD_NATIVE_H, fb);
    s_rot.front = back;
    xSemaphoreTake(s_rot.vsync, 0);
    if (xSemaphoreTake(s_rot.vsync, pdMS_TO_TICKS(100)) != pdTRUE && !s_rot.vsync_timeout_logged) {
        ESP_LOGW(TAG_UI, "DSI rotate: khong thay vsync trong 100ms (DPI ngung quet?)");
        s_rot.vsync_timeout_logged = true;
    }
    s_rot.prev = s_rot.cur;
    s_rot.cur.n = 0;
    s_rot.cur.overflow = false;
    s_flush_count++;
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
    s_rot.full_pending = 2;

    ppa_client_config_t pcfg = { .oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1 };
    esp_err_t err = ppa_register_client(&pcfg, &s_rot.ppa);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_UI, "DSI rotate: PPA khong kha dung (%s) -> xoay bang CPU", esp_err_to_name(err));
        s_rot.ppa = NULL;
    }
    const esp_lcd_dpi_panel_event_callbacks_t cbs = { .on_refresh_done = dsi_rot_vsync_cb };
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(panel, &cbs, NULL),
                        TAG_UI, "DSI rotate: dang ky vsync cb loi");
    lv_display_set_flush_cb(disp, dsi_rot_flush_cb);
    ESP_LOGI(TAG_UI, "DSI rotate: logical %dx%d -> panel %dx%d, goc %d, %s, 2 fb + vsync",
             BOARD_LCD_H_RES, BOARD_LCD_V_RES, BOARD_LCD_NATIVE_W, BOARD_LCD_NATIVE_H,
             BOARD_LCD_ROTATION, s_rot.ppa ? "PPA" : "CPU");
    return ESP_OK;
}
#endif /* DSI_ROTATE */

bool display_hw_rotate_stats(uint32_t *full_frames, uint32_t *area_blits, uint32_t *full_us_max)
{
#if DSI_ROTATE
    if (full_frames) *full_frames = s_rot.stat_full;
    if (area_blits) *area_blits = s_rot.stat_area;
    if (full_us_max) *full_us_max = (uint32_t)s_rot.stat_full_us_max;
    return true;
#else
    (void)full_frames; (void)area_blits; (void)full_us_max;
    return false;
#endif
}

uint32_t display_hw_flush_count(void) { return s_flush_count; }

#if !DSI_ROTATE
static void flush_count_cb(lv_event_t *e)
{
    (void)e;
    s_flush_count++;
}
#endif

/* Panel + DSI + lvgl_port_add_disp_dsi (+ xoay PPA). lvgl_port_init đã chạy trong display.c. */
esp_err_t display_hw_init(lv_display_t **out_disp)
{
    if (!out_disp) return ESP_ERR_INVALID_ARG;
    *out_disp = NULL;
    /* Reset CỨNG panel TRƯỚC khi mở DSI. LCD_RST trên board chỉ có RC nên panel CHỈ tự
     * reset lúc cấp nguồn — reset MCU (nạp firmware, watchdog, nút RST1) KHÔNG reset
     * panel. Dính thật 11/09/2026: panel kẹt, lệnh DSI đầu tiên không được trả lời,
     * main task quay vô hạn trong HAL (task_wdt). Xung thấp 10 ms, chờ 120 ms. */
#if BOARD_LCD_PIN_RST >= 0
    {
        gpio_config_t rst_io = {
            .pin_bit_mask = 1ULL << (unsigned)BOARD_LCD_PIN_RST,
            .mode = GPIO_MODE_OUTPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
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
    /* VDD_MIPI_DPHY 2,5 V lấy từ LDO nội — PHẢI acquire TRƯỚC khi tạo DSI bus. */
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = BOARD_LCD_DSI_PHY_LDO_CHAN, .voltage_mv = BOARD_LCD_DSI_PHY_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi_phy),
                        TAG_UI, "MIPI DPHY LDO acquire loi - bo display, van boot");

    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t dsi_bus_cfg = {
        .bus_id = BOARD_LCD_DSI_BUS_ID, .num_data_lanes = BOARD_LCD_DSI_LANES,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT, .lane_bit_rate_mbps = BOARD_LCD_DSI_LANE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&dsi_bus_cfg, &mipi_dsi_bus), TAG_UI, "esp_lcd_new_dsi_bus loi");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = ST7102_MIPI_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_cfg, &io), TAG_UI, "esp_lcd_new_panel_io_dbi loi");

    esp_lcd_dpi_panel_config_t dpi_cfg =
        ST7102_MIPI_480_800_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    dpi_cfg.num_fbs = (BOARD_LCD_DSI_AVOID_TEARING || DSI_ROTATE) ? 2 : 1;
    st7102_vendor_config_t st7102_vendor = {
        .init_cmds = s_st7102_init_cmds,
        .init_cmds_size = sizeof(s_st7102_init_cmds) / sizeof(s_st7102_init_cmds[0]),
        .flags.use_mipi_interface = 1,
        .mipi_config = { .dsi_bus = mipi_dsi_bus, .dpi_config = &dpi_cfg },
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
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7102(io, &pcfg, &panel), TAG_UI, "st7102 panel loi");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG_UI, "st7102 reset loi");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG_UI, "st7102 init loi");
#if BOARD_LCD_INVERT_COLOR
    esp_lcd_panel_invert_color(panel, true);
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG_UI, "st7102 disp on loi");
    ESP_LOGI(TAG_UI, "Init MIPI-DSI ST7102 panel %dx%d (%d lane @ %d Mbps), logical %dx%d",
             BOARD_LCD_NATIVE_W, BOARD_LCD_NATIVE_H, BOARD_LCD_DSI_LANES, BOARD_LCD_DSI_LANE_MBPS,
             BOARD_LCD_H_RES, BOARD_LCD_V_RES);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        /* DPI video mode: buffer phủ TOÀN màn (768 KB, PSRAM). */
        .buffer_size = BOARD_LCD_H_RES * BOARD_LCD_V_RES,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = BOARD_LCD_SWAP_XY, .mirror_x = BOARD_LCD_MIRROR_X, .mirror_y = BOARD_LCD_MIRROR_Y },
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            /* direct mode: LVGL chỉ vẽ vùng bẩn, tự đồng bộ 2 buffer. */
            .direct_mode = (BOARD_LCD_DSI_AVOID_TEARING || DSI_ROTATE) ? true : false,
#if BOARD_LCD_SWAP_BYTES
            .swap_bytes = true,
#endif
        },
    };
    /* Khi XOAY: avoid_tearing=false để port cấp 2 buffer LVGL logical riêng (không dùng
     * fb); đổi khung + vsync do dsi_rot_flush_cb làm. Giữ lock xuyên add_disp → đổi
     * flush cb: flush mặc định của port ở chế độ này take semaphore NULL → crash nếu
     * task LVGL kịp chạy. */
    const lvgl_port_display_dsi_cfg_t dsi_disp_cfg = {
        .flags = { .avoid_tearing = (BOARD_LCD_DSI_AVOID_TEARING && !DSI_ROTATE) ? true : false },
    };
#if DSI_ROTATE
    lvgl_port_lock(0);
#endif
    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_disp_cfg);
#if DSI_ROTATE
    if (disp) {
        esp_err_t rot_err = dsi_rot_attach(panel, disp);
        if (rot_err != ESP_OK) { lvgl_port_unlock(); return rot_err; }
    }
    lvgl_port_unlock();
#endif
    if (!disp) {
        ESP_LOGE(TAG_UI, "LVGL display add failed");
        return ESP_FAIL;
    }
#if !DSI_ROTATE
    lv_display_add_event_cb(disp, flush_count_cb, LV_EVENT_FLUSH_FINISH, NULL);
#endif
    ESP_LOGI(TAG_UI, "DSI: avoid_tearing=%d num_fbs=%d direct_mode=%d rotation=%d",
             (int)(BOARD_LCD_DSI_AVOID_TEARING || DSI_ROTATE), (int)dpi_cfg.num_fbs,
             (int)(BOARD_LCD_DSI_AVOID_TEARING || DSI_ROTATE), (int)BOARD_LCD_ROTATION);
    *out_disp = disp;
    return ESP_OK;
}
