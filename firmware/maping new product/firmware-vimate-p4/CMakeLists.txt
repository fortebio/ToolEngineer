# Top-level CMakeLists for VIMATE firmware (ESP-IDF native).
#
# CAY FIRMWARE RIENG CHO ESP32-P4 (tach tu firmware-vimate). Cay S3 goc KHONG
# bi dung toi. Xem README-P4.md.
#
# Build:
#   . $IDF_PATH/export.sh           # source ESP-IDF env (>= v5.4, dang dung 5.5.1)
#   ./scripts/build_p4_43lcd.sh     # set-target esp32p4 + build vao build_p4_43lcd/
# Project: firmware "made for VIMATE" — không phụ thuộc xiaozhi codebase.
# Target: ESP32-P4 + LCD 4.3 inch ST7102 MIPI-DSI 480x800 + touch ST7123.
# License: Proprietary © VIMATE 2026.

cmake_minimum_required(VERSION 3.16)

# Cay nay chi build cho P4. Board S3 van o firmware-vimate/.
set(SUPPORTED_TARGETS esp32p4)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(vimate-fw
    VERSION 1.0.154
    DESCRIPTION "VIMATE AI Education Device Firmware"
    LANGUAGES C CXX)

# ---- Workaround: duong dan repo co DAU CACH + component media ship .a ----
# esp_extractor / esp_audio_codec / esp_audio_effects khai
#   target_link_libraries(lib PRIVATE|INTERFACE "-L ${CMAKE_CURRENT_SOURCE_DIR}/lib/<target>")
# khong boc nhay. CMake ghi nguyen chuoi vao response file cua ld, ld tach theo
# dau cach -> "-L C:/Users/ADM/Downloads/FBT-" "ESP-IDF" "5" "4.3LCD+SC2336" ...
#   ld.exe: cannot find 4.3LCD+SC2336: No such file or directory
# Cach sua: sau khi project() dung xong cay component, go chuoi "-L <dir>" khoi
# LINK_LIBRARIES / INTERFACE_LINK_LIBRARIES cua 3 target do va dua <dir> vao
# target_link_directories() cua ELF cuoi — CMake tu boc nhay dung cach.
# Khong the chi bo cờ: esp_audio_effects dat ten target prebuilt theo ten thu
# muc (espressif__esp_audio_effects) nhung lai link "esp_audio_effects" tron
# -> thanh -lesp_audio_effects, PHAI co thu muc tim kiem.
# Khong sua managed_components (component manager ghi de + kiem hash).
# Ten ngan 8.3 (DOWNLO~1/FBT-ES~1.3LC) KHONG giup duoc: idf.py/CMake chuan hoa
# ve duong dan dai truoc khi sinh build.
idf_build_get_property(_vimate_comps BUILD_COMPONENTS)
idf_build_get_property(_vimate_elf EXECUTABLE)
foreach(_c espressif__esp_extractor espressif__esp_audio_codec espressif__esp_audio_effects)
    if(NOT _c IN_LIST _vimate_comps)
        continue()
    endif()
    idf_component_get_property(_lib ${_c} COMPONENT_LIB)
    get_target_property(_type ${_lib} TYPE)
    set(_props INTERFACE_LINK_LIBRARIES)
    if(NOT _type STREQUAL "INTERFACE_LIBRARY")
        list(APPEND _props LINK_LIBRARIES)
    endif()
    foreach(_prop IN LISTS _props)
        get_target_property(_items ${_lib} ${_prop})
        if(NOT _items)
            continue()
        endif()
        set(_kept "")
        foreach(_it IN LISTS _items)
            # PRIVATE tren static lib con sinh them "$<LINK_ONLY:-L ...>" o INTERFACE.
            if(_it MATCHES "^([$]<LINK_ONLY:)?-L ")
                string(REGEX REPLACE "^[$]<LINK_ONLY:" "" _dir "${_it}")
                string(REGEX REPLACE ">$" "" _dir "${_dir}")
                string(REGEX REPLACE "^-L " "" _dir "${_dir}")
                target_link_directories(${_vimate_elf} PRIVATE "${_dir}")
                message(STATUS "vimate: ${_lib} (${_prop}): '-L <dir>' -> target_link_directories: ${_dir}")
            else()
                list(APPEND _kept "${_it}")
            endif()
        endforeach()
        set_target_properties(${_lib} PROPERTIES ${_prop} "${_kept}")
    endforeach()
endforeach()

# Bundle 13 emotion GIFs vào emo_spiffs partition (~448KB).
# Files trong spiffs_emo_image/ → flash vào /spiffs_emo/ runtime.
# display.c apply_emotion sẽ load <key>.gif qua lv_gif_set_src.
# P4 4.3" (15/09/2026): bộ "mặt robot" 32 clip 400×240 trong spiffs_face_image/
# (tools/build_face_gifs.py, 2,87 MB) vào emo_spiffs 3,875 MB của
# partitions/partitions.p4-43lcd.csv; face.c tra /spiffs_emo/<key>.gif.
if(CONFIG_VIMATE_BOARD_P4_43LCD)
    spiffs_create_partition_image(emo_spiffs spiffs_face_image FLASH_IN_PROJECT)
else()
    spiffs_create_partition_image(emo_spiffs spiffs_emo_image FLASH_IN_PROJECT)
endif()

# Flash preformatted empty cache partitions on a full factory flash. Without
# these images, first boot formats 3 MB + 2 MB synchronously before WiFi starts.
spiffs_create_partition_image(asset_spiffs spiffs_asset_image FLASH_IN_PROJECT)
spiffs_create_partition_image(lesson_spiffs spiffs_lesson_image FLASH_IN_PROJECT)
