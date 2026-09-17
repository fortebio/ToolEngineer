# Main component CMakeLists — list tất cả source files.

set(SOURCES
    # Entry
    main.c
    app_main.c
    vimate.c

    # Core
    core/wifi_mgr.c
    core/ble_wifi_prov.c
    core/captive_dns.c
    core/nvs_store.c
    core/system_info.c
    core/diagnostics.c
    core/telemetry.c

    # Network
    network/ota_client.c
    network/ota_boot_validation.c
    network/ota_boot_validation_policy.c
    network/ws_client.c
    network/http_dl.c

    # Protocol
    protocol/envelope.c
    protocol/mcp_handler.c

    # Audio
    audio/i2s_input.c
    audio/i2s_output.c
    audio/opus_codec.c
    audio/audio_pipeline.c
    audio/barge_in.c
    audio/vimate_es8311.c
    audio/audio_afe.c

    # UI (LVGL) — Vietnamese fonts cần compile vào image
    ui/fonts/lv_font_vimate_14.c
    ui/fonts/lv_font_vimate_18.c
    ui/fonts/lv_font_vimate_24.c
    ui/fonts/lv_font_vimate_48.c
    ui/fonts/lv_font_countdown_72.c

    # Default emotion library — map enum to built-in image descriptors.
    ui/emo/vimate_emotions.c
    ui/display.c
    # Mặt robot toàn màn P4 (bảng 32 clip + luật chọn; display.c dùng khi
    # BOARD_FACE_GIF_FULLSCREEN). Build S3 vẫn biên dịch, không gọi.
    ui/face.c
    ui/icons_home.c
    ui/ui_home.c
    ui/ui_emotion.c
    ui/ui_activation.c
    ui/ui_image.c
    ui/ui_card.c
    ui/ui_reward.c
    ui/ui_error.c
    ui/ui_wifi_setup.c

    # Store (asset pack + lesson image cache + custom emotion download)
    store/asset_pack.c
    store/course_media_cache.c
    store/emotion_sync.c

    # Media apps
    media/video_control.c

    # Input
    input/button.c
    input/touch.c

    # Actuator
    actuator/servo_emotion.c

    # Util — JPEG decoder xiaozhi (esp_new_jpeg wrapper, RGB565 output)
    util/jpeg_to_image.c
    util/url_rewrite.c

    # Util — GIF decoder + LVGL controller (ported từ xiaozhi)
    # Replace lv_gif widget bằng controller riêng để frame timing đúng
    # khi LVGL render task pri thấp + sang ARGB8888 canvas → ổn định cho
    # PSRAM-backed image set_src per frame.
    util/gif/gifdec.c
    util/gif/lvgl_gif.c
)

list(APPEND SOURCES
    # The 13 common emotions below are already embedded as GIF files. Keep
    # only neutral as their decode-failure fallback, plus the 8 emotions that
    # do not have an embedded GIF. This avoids storing the same artwork twice.
    ui/emo/neutral.c ui/emo/laughing.c ui/emo/crying.c ui/emo/shocked.c
    ui/emo/winking.c ui/emo/cool.c ui/emo/kissy.c ui/emo/confident.c
    ui/emo/silly.c
)

# SPIFFS cache ảnh bài học VIMATE Edu.
list(APPEND SOURCES store/lesson_image_cache.c)

if(CONFIG_VIMATE_MP4_PLAYER_ENABLE)
    list(APPEND SOURCES media/mp4_player.cpp)
else()
    list(APPEND SOURCES media/mp4_player_stub.c)
endif()

set(INCLUDE_DIRS
    "."
    "core"
    "network"
    "protocol"
    "audio"
    "ui"
    "store"
    "input"
    "actuator"
    "boards"
    "media"
    "util"
    "util/gif"
)

# Embed 13 GIF emotion files trực tiếp vào firmware binary
# Mỗi file truy cập runtime qua _binary_<name>_gif_start / _end.
# Bypass SPIFFS hoàn toàn — đảm bảo GIF luôn có sẵn.
# P4 4.3" (15/09/2026): KHÔNG embed — mặt robot đọc từ emo_spiffs (face.c),
# display.c cũng không tham chiếu _binary_*_gif nữa (BOARD_FACE_GIF_FULLSCREEN);
# bỏ 13 file này trả lại ~350 KB cho app slot (đang còn ~7 %).
set(GIF_EMBED_FILES)
if(NOT CONFIG_VIMATE_BOARD_P4_43LCD)
set(GIF_EMBED_FILES
    ../spiffs_emo_image/neutral.gif
    ../spiffs_emo_image/happy.gif
    ../spiffs_emo_image/sad.gif
    ../spiffs_emo_image/angry.gif
    ../spiffs_emo_image/confused.gif
    ../spiffs_emo_image/surprised.gif
    ../spiffs_emo_image/sleepy.gif
    ../spiffs_emo_image/embarrassed.gif
    ../spiffs_emo_image/thinking.gif
    ../spiffs_emo_image/relaxed.gif
    ../spiffs_emo_image/funny.gif
    ../spiffs_emo_image/delicious.gif
    ../spiffs_emo_image/loving.gif
)
endif()

set(PRIV_REQS
    lvgl__lvgl
    espressif__esp_lvgl_port
    espressif__esp_lcd_ili9341
    esphome__micro-opus
)

# Panel ST7102 MIPI-DSI cua board ESP32-P4 4.3". Driver KHONG co tren component
# registry -> vendor vao components/esp_lcd_st7102 (nguon: demo chinh chu cua
# board trong "ESP-IDF 5/P4-IDF_ST7102-MIPI_ESP-LVGL-PORT_V9/components").
# Khai VO DIEU KIEN: danh sach REQUIRES duoc doc o pha early expansion cua
# CMake, luc do bien CONFIG_* chua co gia tri -> boc trong if(CONFIG_...) thi
# include dir cua component khong duoc them vao main va display.c bao
# "esp_lcd_st7102.h: No such file or directory". Cay nay chi build cho P4 nen
# khai thang la dung.
list(APPEND PRIV_REQS esp_lcd_st7102)

if(CONFIG_VIMATE_MP4_PLAYER_ENABLE)
    list(APPEND PRIV_REQS
        espressif__esp_extractor
        espressif__esp_audio_codec
        esp_video_codec
        tempotian__av_render
        tempotian__media_lib_sal
    )
endif()

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    EMBED_FILES ${GIF_EMBED_FILES}
    REQUIRES
        # ESP-IDF system components
        nvs_flash esp_wifi esp_event esp_netif esp_http_client
        esp_partition app_update esp_https_ota esp_http_server esp_psram
        # esp_mm: display.c dung esp_cache.h. Tren cay S3 header nay den
        # gian tiep qua chuoi phu thuoc cua esp_video_codec/av_render; cay P4
        # da bo cac component do nen phai khai truc tiep.
        esp_mm
        bt espcoredump
        driver esp_lcd esp_timer esp_adc esp_driver_i2c
        # esp_driver_ppa: P4 xoay man ngang bang phan cung (display.c, DSI_ROTATE)
        esp_driver_ppa
        spiffs vfs fatfs sdmmc mbedtls esp-tls
        esp_driver_sdspi
        # Managed components (declared in idf_component.yml)
        espressif__esp_websocket_client
        # FreeRTOS đã built-in
        # JSON
        json
        # Audio
        esp_codec_dev
        espressif__esp-dsp
        espressif__esp-sr
        # JPEG decoder (esp_new_jpeg) — software JPG → RGB565
        espressif__esp_new_jpeg
    PRIV_REQUIRES
        # LVGL via managed_component (declared in idf_component.yml)
        ${PRIV_REQS}
)

if(CONFIG_VIMATE_MP4_PLAYER_ENABLE)
    # av_render exposes media_lib_err.h through public headers but does not export
    # media_lib_sal include dirs in its legacy component metadata.
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "${PROJECT_DIR}/managed_components/tempotian__media_lib_sal/include"
        "${PROJECT_DIR}/managed_components/tempotian__media_lib_sal/include/port"
    )
endif()
