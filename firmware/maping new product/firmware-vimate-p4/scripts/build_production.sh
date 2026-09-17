#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build-prod"
KEY_PATH="$ROOT/keys/secure_boot_signing_key.pem"

if [[ -z "${IDF_PATH:-}" ]]; then
  echo "IDF_PATH is not set. Run: . /path/to/esp-idf/export.sh" >&2
  exit 1
fi

if [[ ! -f "$KEY_PATH" ]]; then
  echo "Missing secure boot key: $KEY_PATH" >&2
  echo "Run: $ROOT/scripts/prepare_production_keys.sh" >&2
  exit 1
fi

cd "$ROOT"
idf_args=(
  -B "$BUILD_DIR"
  -DIDF_TARGET=esp32s3
  "-DSDKCONFIG=$BUILD_DIR/sdkconfig"
  "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.production"
  build
)
idf.py "${idf_args[@]}"

echo "Production build output: $BUILD_DIR/vimate-fw.bin"
echo "Do not burn Secure Boot/Flash Encryption eFuses until the factory checklist is complete."
