/**
 * display_hw_spi.c — lớp phần cứng màn hình board ESP32-S3 ES3N28P + LCD 2.8" ILI9341 SPI.
 * Xem display_hw.h (giao diện) và display.c (phần chung: đèn nền, ngủ, hàng đợi, task).
 *
 * Nguồn: firmware-vimate/main/ui/display.c nhánh ILI9341 (L1186–1332, Bizgeni, biến thể s3-28lcd
 * đã build + chạy thật 12/09/2026). Khác vimate: draw buffer để RAM NỘI (buff_spiram = 0) — LVGL
 * chỉ vẽ 40 dòng mỗi lượt nên 51 KB là đủ, tránh DMA đọc PSRAM qua SPI; xoay bằng MADCTL của
 * panel (esp_lvgl_port gọi esp_lcd_panel_swap_xy/mirror theo `rotation`), không cần PPA.
 */
#include "display_hw.h"
#include "rapid4p.h"
#include "boards/board.h"

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"
#include "esp_lvgl_port.h"
#include "esp_check.h"

#if !BOARD_LCD_USE_SPI
#error "display_hw_spi.c chi cho board LCD SPI (BOARD_LCD_USE_SPI 1)"
#endif

static uint32_t s_flush_count;

bool display_hw_rotate_stats(uint32_t *full_frames, uint32_t *area_blits, uint32_t *full_us_max)
{
    (void)full_frames; (void)area_blits; (void)full_us_max;
    return false;   /* không xoay bằng phần cứng — MADCTL của panel lo */
}

uint32_t display_hw_flush_count(void) { return s_flush_count; }

static void flush_count_cb(lv_event_t *e)
{
    (void)e;
    s_flush_count++;
}

/* SPI bus → panel IO → ILI9341 → lvgl_port_add_disp. lvgl_port_init đã chạy trong display.c.
 * Mọi bước ESP_RETURN_ON_ERROR: LCD hỏng thì máy vẫn boot (WiFi/OTA cứu được từ xa). */
esp_err_t display_hw_init(lv_display_t **out_disp)
{
    if (!out_disp) return ESP_ERR_INVALID_ARG;
    *out_disp = NULL;

    const spi_bus_config_t buscfg = {
        .sclk_io_num = BOARD_LCD_PIN_SCLK,
        .mosi_io_num = BOARD_LCD_PIN_MOSI,
        .miso_io_num = BOARD_LCD_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOARD_LCD_H_RES * BOARD_LCD_DRAW_BUF_LINES * 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG_UI, "spi_bus_initialize loi - bo display, van boot");

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = BOARD_LCD_PIN_DC,
        .cs_gpio_num = BOARD_LCD_PIN_CS,
        .pclk_hz = BOARD_LCD_SPI_FREQ_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = BOARD_LCD_SPI_MODE,
        .trans_queue_depth = 6,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_HOST, &io_cfg, &io),
                        TAG_UI, "esp_lcd_new_panel_io_spi loi");

    const esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = BOARD_LCD_PIN_RST,
#if BOARD_LCD_USE_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = BOARD_LCD_BITS_PER_PIXEL,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &pcfg, &panel), TAG_UI, "ili9341 panel loi");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG_UI, "ili9341 reset loi");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG_UI, "ili9341 init loi");
#if BOARD_LCD_INVERT_COLOR
    esp_lcd_panel_invert_color(panel, true);
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG_UI, "ili9341 disp on loi");
    ESP_LOGI(TAG_UI, "Init SPI ILI9341 panel %dx%d @ %d MHz, logical %dx%d (swap_xy=%d mirror=%d/%d)",
             BOARD_LCD_NATIVE_W, BOARD_LCD_NATIVE_H, BOARD_LCD_SPI_FREQ_HZ / 1000000,
             BOARD_LCD_H_RES, BOARD_LCD_V_RES, BOARD_LCD_SWAP_XY, BOARD_LCD_MIRROR_X, BOARD_LCD_MIRROR_Y);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        /* 2 buffer × BOARD_LCD_DRAW_BUF_LINES dòng, RAM nội DMA (không PSRAM). */
        .buffer_size = BOARD_LCD_H_RES * BOARD_LCD_DRAW_BUF_LINES,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = BOARD_LCD_SWAP_XY, .mirror_x = BOARD_LCD_MIRROR_X, .mirror_y = BOARD_LCD_MIRROR_Y },
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
#if BOARD_LCD_SWAP_BYTES
            .swap_bytes = true,
#endif
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (!disp) {
        ESP_LOGE(TAG_UI, "LVGL display add failed");
        return ESP_FAIL;
    }
    lv_display_add_event_cb(disp, flush_count_cb, LV_EVENT_FLUSH_FINISH, NULL);
    ESP_LOGI(TAG_UI, "SPI: draw buf %d dong x2 = %d B RAM noi", BOARD_LCD_DRAW_BUF_LINES,
             (int)(BOARD_LCD_H_RES * BOARD_LCD_DRAW_BUF_LINES * 2 * 2));
    *out_disp = disp;
    return ESP_OK;
}
