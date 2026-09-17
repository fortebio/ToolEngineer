/**
 * dev_console.c — xem dev_console.h. Mọi lệnh đổi UI đi qua ui_reader_* (đã bọc
 * display_schedule) nên task REPL không bao giờ gọi lv_* trực tiếp (luật 2 CLAUDE.md).
 */
#include "dev_console.h"
#include "rapid4p.h"
#include "ui/ui_reader.h"

#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
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
    [UI_THRESHOLD_EDIT] = "thredit",
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

static int cmd_btn(int argc, char **argv)
{
    if (argc < 2) {
        printf("btn do|boot\n");
        return 1;
    }
    if (strcmp(argv[1], "do") == 0) {
        ui_reader_on_measure_button();
    } else if (strcmp(argv[1], "boot") == 0) {
        ui_reader_on_boot_button();
    } else {
        printf("btn: khong biet '%s'\n", argv[1]);
        return 1;
    }
    printf("btn: %s\n", argv[1]);
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
    esp_console_dev_uart_config_t hw_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t r = esp_console_new_repl_uart(&hw_cfg, &repl_cfg, &repl);
    if (r != ESP_OK) {
        ESP_LOGW(TAG_MAIN, "dev console: %s", esp_err_to_name(r));
        return r;
    }
    const esp_console_cmd_t cmds[] = {
        { .command = "ui",   .help = "ui [<0..12>|<ten man>]  - chuyen man (khong tham so: liet ke)", .func = cmd_ui },
        { .command = "btn",  .help = "btn do|boot             - gia lap nut DO / BOOT",              .func = cmd_btn },
        { .command = "heap", .help = "heap                    - heap internal/psram",                .func = cmd_heap },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    esp_console_register_help_command();
    r = esp_console_start_repl(repl);
    ESP_LOGI(TAG_MAIN, "dev console UART0 san sang (ui/btn/heap/help): %s", esp_err_to_name(r));
    return r;
}

#else
esp_err_t dev_console_start(void) { return ESP_OK; }
#endif
