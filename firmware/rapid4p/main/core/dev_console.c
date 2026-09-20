/**
 * dev_console.c — xem dev_console.h. Mọi lệnh đổi UI đi qua ui_reader_* (đã bọc
 * display_schedule) nên task REPL không bao giờ gọi lv_* trực tiếp (luật 2 CLAUDE.md).
 */
#include "dev_console.h"
#include "rapid4p.h"
#include "ui/ui_reader.h"
#include "ui/display.h"

#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "mbedtls/base64.h"
#include "lvgl.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if CONFIG_RAPID4P_DEV_CONSOLE

static const char *const s_ui_names[UI_COUNT] = {
    [UI_START] = "start",       [UI_CHOOSE_SAMPLE] = "sample", [UI_CHOOSE_TUBE] = "tube",
    [UI_PREPARE] = "prepare",   [UI_MEASURING] = "measuring", [UI_RESULT] = "result",
    [UI_CALIB] = "calib",       [UI_SETTINGS] = "settings",   [UI_LANGUAGE] = "language",
    [UI_WIFI] = "wifi",         [UI_UPDATE] = "update",       [UI_THRESHOLD] = "threshold",
    [UI_THRESHOLD_EDIT] = "thredit", [UI_MEASURE_ERROR] = "measerr",
};

static int cmd_ui(int argc, char **argv)
{
    if (argc < 2) {
        printf("ui: dang o man %d (%s). Man co the chon:\n", (int)ui_reader_state(),
               s_ui_names[ui_reader_state()]);
        for (int i = 0; i < UI_COUNT; i++) printf("  %2d %s\n", i, s_ui_names[i]);
        return 0;
    }
    int st = -1;
    char *end = NULL;
    long v = strtol(argv[1], &end, 10);
    if (end && *end == '\0') {
        st = (int)v;
    } else {
        for (int i = 0; i < UI_COUNT; i++) {
            if (strcmp(argv[1], s_ui_names[i]) == 0) { st = i; break; }
        }
    }
    if (strcmp(argv[1], "confirm") == 0) st = UI_COUNT;    /* demo hop thoai xac nhan */
    if (st < 0 || st > UI_COUNT) {
        printf("ui: khong biet man '%s'\n", argv[1]);
        return 1;
    }
    /* MEASURING/RESULT dung du lieu do thuc; chi dung de xem bo cuc (ket qua = ban cuoi). */
    ui_reader_show_debug((ui_state_t)st);
    printf("ui: -> %d (%s)\n", st, st < UI_COUNT ? s_ui_names[st] : "confirm");
    return 0;
}

/* btn do|boot | green|red|white [hold|rep] — gia lap nut. 3 nut mau di qua event group nhu button.c
 * (khong goi thang ui_reader) de test ca duong app_main else-if / co REPEAT. */
static int cmd_btn(int argc, char **argv)
{
    if (argc < 2) {
        printf("btn do|boot|green|red|white [hold|rep]\n");
        return 1;
    }
    const bool hold = argc >= 3 && (strcmp(argv[2], "hold") == 0 || strcmp(argv[2], "rep") == 0);
    const bool rep = argc >= 3 && strcmp(argv[2], "rep") == 0;
    EventBits_t bit = 0;
    if (strcmp(argv[1], "do") == 0) {
        ui_reader_on_measure_button();
    } else if (strcmp(argv[1], "boot") == 0) {
        ui_reader_on_boot_button();
    } else if (strcmp(argv[1], "green") == 0) {
        bit = hold ? R4P_EVT_BTN_GREEN_LONG : R4P_EVT_BTN_GREEN;
    } else if (strcmp(argv[1], "red") == 0) {
        bit = hold ? R4P_EVT_BTN_RED_LONG : R4P_EVT_BTN_RED;
    } else if (strcmp(argv[1], "white") == 0) {
        bit = hold ? R4P_EVT_BTN_WHITE_LONG : R4P_EVT_BTN_WHITE;
    } else {
        printf("btn: khong biet '%s'\n", argv[1]);
        return 1;
    }
    /* Nut that (button.c) danh thuc man khi nhan xuong; gia lap cung vay (khong gia lap wake-only). */
    display_note_user_activity();
    if (bit) xEventGroupSetBits(g_r4p_events, bit | (rep ? R4P_EVT_BTN_REPEAT : 0));
    printf("btn: %s%s%s\n", argv[1], hold ? " hold" : "", rep ? " rep" : "");
    return 0;
}

/* screen [raw] — chup man hinh LVGL (lv_snapshot_take, RGB565 logical) roi in:
 *   SCR <w> <h> RGB565 <RLE|RAW> <bytes_ma_hoa> <bytes_tho>
 *   <base64, 76 ky tu/dong>
 *   SCR-END <crc32 cua anh tho, hex>
 * RLE = cac ban ghi (count u8 1..255, pixel u16 LE); UI phang nen con ~10-30 KB -> qua USB-JTAG < 1 s.
 * Chup duoi display_lock (LVGL cho phep goi API tu task khac khi giu khoa cua port); buffer chup + buffer nen
 * o PSRAM. Tool PC: scripts/lcdtool.py (docs/plan/2026-09-21-lcd-debug-tool.md, phuong an A). */
static int cmd_screen(int argc, char **argv)
{
    const bool raw = argc >= 2 && strcmp(argv[1], "raw") == 0;
    display_lock();
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    display_unlock();
    if (!snap) {
        printf("screen: lv_snapshot_take fail (heap?)\n");
        return 1;
    }
    const int w = (int)snap->header.w, h = (int)snap->header.h;
    const uint32_t stride = snap->header.stride;
    const size_t raw_bytes = (size_t)w * h * 2;
    /* Anh tho lien tuc (bo padding stride) de tinh CRC va nen. */
    uint8_t *img = heap_caps_malloc(raw_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *enc = heap_caps_malloc(raw ? raw_bytes : raw_bytes * 3 / 2 + 16, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!img || !enc) {
        printf("screen: khong du PSRAM\n");
        free(img); free(enc);
        lv_draw_buf_destroy(snap);
        return 1;
    }
    for (int y = 0; y < h; y++) memcpy(img + (size_t)y * w * 2, snap->data + (size_t)y * stride, (size_t)w * 2);
    lv_draw_buf_destroy(snap);
    const uint32_t crc = esp_rom_crc32_le(0, img, raw_bytes);

    size_t enc_len = 0;
    if (raw) {
        memcpy(enc, img, raw_bytes);
        enc_len = raw_bytes;
    } else {
        const uint16_t *px = (const uint16_t *)img;
        const size_t n = (size_t)w * h;
        size_t i = 0;
        while (i < n) {
            uint16_t v = px[i];
            size_t run = 1;
            while (i + run < n && px[i + run] == v && run < 255) run++;
            enc[enc_len++] = (uint8_t)run;
            enc[enc_len++] = (uint8_t)(v & 0xFF);
            enc[enc_len++] = (uint8_t)(v >> 8);
            i += run;
        }
    }
    free(img);

    printf("SCR %d %d RGB565 %s %u %u\n", w, h, raw ? "RAW" : "RLE", (unsigned)enc_len, (unsigned)raw_bytes);
    /* 57 byte -> 76 ky tu base64 moi dong (chuan MIME), khong chen log giua chung. */
    char line[80];
    for (size_t off = 0; off < enc_len; off += 57) {
        size_t chunk = enc_len - off < 57 ? enc_len - off : 57;
        size_t olen = 0;
        if (mbedtls_base64_encode((unsigned char *)line, sizeof(line), &olen, enc + off, chunk) != 0) break;
        line[olen] = '\0';
        fputs(line, stdout);
        fputc('\n', stdout);
    }
    printf("SCR-END %08lx\n", (unsigned long)crc);
    fflush(stdout);
    free(enc);
    return 0;
}

static int cmd_heap(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("heap internal=%u min=%u psram=%u min=%u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}

esp_err_t dev_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "r4p> ";
    repl_cfg.max_cmdline_length = 128;
    /* `screen` ve lai ca man (lv_snapshot_take -> draw sw) TRONG task REPL: 4 KB mac dinh -> Double exception
     * trong lv_malloc (2026-09-21). 16 KB RAM noi. */
    repl_cfg.task_stack_size = 16384;
    /* Cong console theo sdkconfig: S3 2.8" (ES3N28P) chi co cong USB native -> CONSOLE_USB_SERIAL_JTAG
     * (sdkconfig.defaults.s3_28lcd) de go lenh qua COM cua USB-JTAG (jtaglog.py/uicmd.py cung cong);
     * P4 giu UART0. */
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t hw_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_err_t r = esp_console_new_repl_usb_serial_jtag(&hw_cfg, &repl_cfg, &repl);
    const char *port_name = "USB-JTAG";
#else
    esp_console_dev_uart_config_t hw_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t r = esp_console_new_repl_uart(&hw_cfg, &repl_cfg, &repl);
    const char *port_name = "UART0";
#endif
    if (r != ESP_OK) {
        ESP_LOGW(TAG_MAIN, "dev console: %s", esp_err_to_name(r));
        return r;
    }
    const esp_console_cmd_t cmds[] = {
        { .command = "ui",   .help = "ui [<0..12>|<ten man>]  - chuyen man (khong tham so: liet ke)", .func = cmd_ui },
        { .command = "btn",  .help = "btn do|boot|green|red|white [hold|rep] - gia lap nut",       .func = cmd_btn },
        { .command = "heap", .help = "heap                    - heap internal/psram",                .func = cmd_heap },
        { .command = "screen", .help = "screen [raw]          - dump man hinh RGB565 (base64) cho scripts/lcdtool.py", .func = cmd_screen },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    esp_console_register_help_command();
    r = esp_console_start_repl(repl);
    ESP_LOGI(TAG_MAIN, "dev console %s san sang (ui/btn/heap/help): %s", port_name, esp_err_to_name(r));
    return r;
}

#else
esp_err_t dev_console_start(void) { return ESP_OK; }
#endif
