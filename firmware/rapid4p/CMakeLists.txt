# Firmware Rapid4P (RAPID READER 4 SLOT) — ESP-IDF native, target ESP32-P4.
# Board tham chiếu: FBT ESP32-P4C5 + LCD 4.3" ST7102 MIPI-DSI + touch ST7123, bo cảm
# biến 4 slot (TCA9548A + 4x TCS34725) qua JP1. Xem CLAUDE.md, README.md.
#
# Build (Windows, cmd.exe):  scripts\build.bat          → BUILD_EXIT=0/1
#        (Git Bash)          MSYS_NO_PATHCONV=1 cmd.exe /c "scripts\build.bat"
# Trực tiếp: idf.py -B build -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.p4-43lcd" set-target esp32p4 && idf.py -B build build

cmake_minimum_required(VERSION 3.16)

set(SUPPORTED_TARGETS esp32p4)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rapid4p
    VERSION 0.1.0
    DESCRIPTION "Forte Biotech Rapid4P - Rapid Reader 4 Slot"
    LANGUAGES C CXX)
