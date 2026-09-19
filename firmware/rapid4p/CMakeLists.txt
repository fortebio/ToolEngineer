# Firmware Rapid4P (RAPID READER 5 SLOT) — ESP-IDF native, HAI board (2026-09-19):
#   p4_43lcd : FBT ESP32-P4C5 + LCD 4.3" ST7102 MIPI-DSI + touch ST7123 (khoá sản phẩm `rapid4p`)
#   s3_28lcd : AI-IoT VN ES3N28P ESP32-S3 N16R8 + LCD 2.8" ILI9341 SPI + touch FT6236G (khoá `rapid4p-s3`)
# Bo cảm biến 5 slot (TCA9548A + 5x TCS34725) chung cho cả hai. Xem CLAUDE.md, README.md.
#
# Build (Windows, cmd.exe):  scripts\build.bat [p4_43lcd|s3_28lcd] [COMxx]   → BUILD_EXIT=0/1
#        (Git Bash)          MSYS_NO_PATHCONV=1 cmd.exe /c "scripts\build.bat s3_28lcd"
# Trực tiếp: idf.py -B build_s3_28lcd -DR4P_BOARD=s3_28lcd set-target esp32s3 && idf.py -B build_s3_28lcd build
#
# Cách chọn board: biến cache R4P_BOARD (build.bat truyền -DR4P_BOARD= lúc set-target, sau đó nằm
# trong CMakeCache của build dir). Từ đó suy ra: profile sdkconfig.defaults.<board>, vị trí sdkconfig
# (TRONG build dir để hai board không đè nhau — idf.py đọc lại đường dẫn này từ
# build/project_description.json), lock file component manager theo target (dependencies.lock có
# trường `target:` nên mỗi target một file), và TÊN project = khoá sản phẩm (esp_app_desc_t.project_name
# trong ảnh .bin là thẻ nhận dạng kho OTA trên Engineer Server — CLAUDE.md §3 luật 7).

cmake_minimum_required(VERSION 3.16)

set(R4P_BOARD "p4_43lcd" CACHE STRING "Board profile: p4_43lcd | s3_28lcd")
if(NOT R4P_BOARD MATCHES "^(p4_43lcd|s3_28lcd)$")
    message(FATAL_ERROR "R4P_BOARD='${R4P_BOARD}' khong hop le (p4_43lcd | s3_28lcd)")
endif()

set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.${R4P_BOARD}")
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
# Chỉ build main + phụ thuộc của nó: components/esp_lcd_st7102 (vendor, chỉ P4) vào build qua
# PRIV_REQUIRES của main khi target là esp32p4, không bị kéo vào build S3.
set(COMPONENTS main)
set(SUPPORTED_TARGETS esp32p4 esp32s3)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# IDF_TARGET (biến cache) đã được project.cmake chốt ngay khi include (targets.cmake __target_init).
idf_build_set_property(DEPENDENCIES_LOCK "dependencies.lock.${IDF_TARGET}")

if(R4P_BOARD STREQUAL "s3_28lcd")
    set(_r4p_project rapid4p-s3)
else()
    set(_r4p_project rapid4p)
endif()

# VERSION phải bằng R4P_FW_VERSION trong main/rapid4p.h (không chữ `v`) — registry_check + OTA.
project(${_r4p_project}
    VERSION 0.1.0
    DESCRIPTION "Forte Biotech Rapid4P - Rapid Reader 5 Slot (${R4P_BOARD})"
    LANGUAGES C CXX)
