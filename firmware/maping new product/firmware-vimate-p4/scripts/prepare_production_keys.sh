#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KEY_DIR="$ROOT/keys"
KEY_PATH="$KEY_DIR/secure_boot_signing_key.pem"

mkdir -p "$KEY_DIR"

if [[ -f "$KEY_PATH" ]]; then
  echo "Secure boot signing key already exists: $KEY_PATH"
  exit 0
fi

if ! command -v espsecure.py >/dev/null 2>&1; then
  echo "espsecure.py not found. Run: . \$IDF_PATH/export.sh" >&2
  exit 1
fi

umask 077
espsecure.py generate_signing_key "$KEY_PATH"
echo "Generated secure boot signing key: $KEY_PATH"
echo "Store this key offline. Anyone with this key can sign firmware accepted by devices."

