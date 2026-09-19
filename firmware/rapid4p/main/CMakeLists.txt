# Main component Rapid4P — liệt kê mọi source (không glob để build đọc được cấu trúc).
#
# Hai board (2026-09-19): phần phụ thuộc phần cứng màn hình tách ra ui/display_hw_<dsi|spi>.c,
# chọn theo CONFIG_RAPID4P_BOARD_* (có ở pha build thật). REQUIRES thì đọc ở pha early expansion
# của CMake — lúc đó CONFIG_* CHƯA có nhưng IDF_TARGET đã có (idf_build_get_property, cách chính
# các component IDF dùng) → component P4-only / S3-only điều kiện theo target, KHÔNG theo CONFIG.
idf_build_get_property(target IDF_TARGET)

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

    # Bo cảm biến N slot (N = BOARD_SENSOR_SLOTS)
    sensor/sensor_bus.c
    sensor/tca9548.c
    sensor/tcs34725.c
    sensor/slot_led.c

    # Ứng dụng đo / cấu hình
    app/measure.c
    app/calib_store.c
)

# Lớp phần cứng màn hình theo board (ui/display_hw.h).
if(CONFIG_RAPID4P_BOARD_P4_43LCD)
    list(APPEND SOURCES ui/display_hw_dsi.c)
elseif(CONFIG_RAPID4P_BOARD_S3_28LCD)
    list(APPEND SOURCES ui/display_hw_spi.c)
endif()

set(REQS
    nvs_flash esp_wifi esp_event esp_netif esp_http_client esp_http_server
    esp_partition app_update esp_https_ota esp_psram esp_mm
    driver esp_lcd esp_timer esp_driver_i2c esp_driver_spi esp_driver_ledc esp_driver_gpio
    mbedtls esp-tls json console
)
set(PRIV_REQS
    lvgl__lvgl
    espressif__esp_lvgl_port
)

if(target STREQUAL "esp32p4")
    # esp_driver_ppa: xoay màn ngang bằng phần cứng (display_hw_dsi.c, DSI_ROTATE).
    list(APPEND REQS esp_driver_ppa)
    # Panel ST7102 MIPI-DSI — vendor trong components/ (không có trên registry). Chỉ P4;
    # CMakeLists gốc set(COMPONENTS main) nên S3 không kéo component này vào build.
    list(APPEND PRIV_REQS esp_lcd_st7102)
else()
    # Board S3 2.8": panel ILI9341 SPI từ registry (main/idf_component.yml, rules target == esp32s3).
    list(APPEND PRIV_REQS espressif__esp_lcd_ili9341)
endif()

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "." "core" "network" "ui" "input" "sensor" "app" "boards"
    EMBED_TXTFILES "web/dashboard.html"
    REQUIRES ${REQS}
    PRIV_REQUIRES ${PRIV_REQS}
)
