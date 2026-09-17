# Main component Rapid4P — liệt kê mọi source (không glob để build đọc được cấu trúc).
set(SOURCES
    main.c
    app_main.c

    # Core (kế thừa vimate-p4)
    core/nvs_store.c
    core/system_info.c
    core/captive_dns.c
    core/wifi_mgr.c
    core/dev_console.c

    # Mạng: Engineer Server
    network/engineer_api.c
    network/ota_client.c
    network/result_upload.c
    network/dashboard.c

    # UI (LVGL 9) — font tiếng Việt kế thừa (symbol lv_font_vimate_*)
    ui/fonts/lv_font_vimate_14.c
    ui/fonts/lv_font_vimate_18.c
    ui/fonts/lv_font_vimate_24.c
    ui/fonts/lv_font_vimate_48.c
    ui/display.c
    ui/ui_strings.c
    ui/ui_reader.c
    ui/ui_logo.c
    ui/ui_wifi_setup.c

    # Input
    input/button.c
    input/touch.c

    # Bo cảm biến 4 slot
    sensor/sensor_bus.c
    sensor/tca9548.c
    sensor/tcs34725.c
    sensor/slot_led.c

    # Ứng dụng đo / cấu hình
    app/measure.c
    app/calib_store.c
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "." "core" "network" "ui" "input" "sensor" "app" "boards"
    EMBED_TXTFILES "web/dashboard.html"
    REQUIRES
        nvs_flash esp_wifi esp_event esp_netif esp_http_client esp_http_server
        esp_partition app_update esp_https_ota esp_psram esp_mm
        driver esp_lcd esp_timer esp_driver_i2c esp_driver_ppa esp_driver_ledc esp_driver_gpio
        mbedtls esp-tls json console
    PRIV_REQUIRES
        lvgl__lvgl
        espressif__esp_lvgl_port
        # Panel ST7102 MIPI-DSI — vendor trong components/ (không có trên registry).
        # Khai vô điều kiện: REQUIRES đọc ở pha early expansion, CONFIG_* chưa có.
        esp_lcd_st7102
)
