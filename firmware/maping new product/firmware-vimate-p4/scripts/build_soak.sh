#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build-soak"

if [[ -z "${IDF_PATH:-}" ]]; then
  echo "IDF_PATH is not set. Run: . /path/to/esp-idf/export.sh" >&2
  exit 1
fi

cd "$ROOT"
idf_args=(
  -B "$BUILD_DIR"
  -DIDF_TARGET=esp32s3
  "-DSDKCONFIG=$BUILD_DIR/sdkconfig"
  "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.debug_soak"
  build
)
idf.py "${idf_args[@]}"

echo "Soak build output: $BUILD_DIR/vimate-fw.bin"
