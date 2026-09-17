#!/usr/bin/env bash
# Build firmware VIMATE cho board ESP32-P4 + LCD 4.3" ST7102 MIPI-DSI.
#
#   . $IDF_PATH/export.sh
#   ./scripts/build_p4_43lcd.sh              # build
#   ./scripts/build_p4_43lcd.sh COM47        # build + flash + monitor
#
# Build dir tách riêng (build_p4_43lcd/) để không đụng build S3 nào.
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR=build_p4_43lcd
DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.p4-43lcd"
PORT="${1:-}"

# ESP-IDF chỉ dùng sdkconfig.defaults* để SINH sdkconfig lần đầu. Nếu sdkconfig
# đã tồn tại, giá trị mới thêm vào defaults sẽ bị BỎ QUA IM LẶNG — đã dính bẫy
# này một lần (đổi CONFIG_ESP_MAIN_TASK_STACK_SIZE mà build ra không đổi gì).
# Nên: defaults mới hơn sdkconfig thì xoá sdkconfig để sinh lại.
if [ -f sdkconfig ]; then
    for f in sdkconfig.defaults sdkconfig.defaults.p4-43lcd; do
        if [ "$f" -nt sdkconfig ]; then
            echo "==> $f mới hơn sdkconfig — xoá sdkconfig để áp cấu hình mới"
            rm -f sdkconfig
            break
        fi
    done
fi

# set-target chỉ cần chạy khi build dir chưa cấu hình (nó kéo theo fullclean).
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    rm -rf "$BUILD_DIR"
    idf.py -B "$BUILD_DIR" -DSDKCONFIG_DEFAULTS="$DEFAULTS" set-target esp32p4
fi

idf.py -B "$BUILD_DIR" -DSDKCONFIG_DEFAULTS="$DEFAULTS" build

if [ -n "$PORT" ]; then
    idf.py -B "$BUILD_DIR" -p "$PORT" -b 460800 flash monitor
fi
