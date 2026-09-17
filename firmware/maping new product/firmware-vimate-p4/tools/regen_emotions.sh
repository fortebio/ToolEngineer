#!/bin/bash
# regen_emotions.sh — Regenerate 21 default emotion images cho firmware.
#
# Usage:
#   1. Download Drive folder vimate emotion assets về /tmp/vimate_emo_original/
#   2. Mỗi file phải có tên khớp enum: neutral.png, happy.png, laughing.png,
#      funny.png, sad.png, angry.png, crying.png, loving.png, embarrassed.png,
#      surprised.png, shocked.png, thinking.png, winking.png, cool.png,
#      relaxed.png, delicious.png, kissy.png, confident.png, sleepy.png,
#      silly.png, confused.png  (21 file)
#   3. Format gốc: PNG bất kỳ size (script resize 128×128 cho LCD 2.8")
#   4. Chạy: bash tools/regen_emotions.sh
#
# Sau khi chạy, build firmware lại:
#   idf.py build && idf.py -p /dev/cu.usbmodem2201 flash

set -e
SRC=${1:-/tmp/vimate_emo_original}
RESIZED=/tmp/vimate_emo_128
DST=$(dirname "$0")/../main/ui/emo
SCRIPT_PY="$(cd "$(dirname "$0")/../.." && pwd)/firmware/scripts/Image_Converter/LVGLImage.py"
PYTHON=/Users/digits/.espressif/python_env/idf5.4_py3.14_env/bin/python

REQUIRED=(neutral happy laughing funny sad angry crying loving embarrassed surprised shocked thinking winking cool relaxed delicious kissy confident sleepy silly confused)

# 1. Validate
if [ ! -d "$SRC" ]; then
  echo "ERROR: source folder $SRC không tồn tại"
  echo "Download Drive emotion về $SRC trước."
  exit 1
fi
echo "=== Check 21 file emotion gốc trong $SRC ==="
MISSING=()
for name in "${REQUIRED[@]}"; do
  if [ ! -f "$SRC/$name.png" ] && [ ! -f "$SRC/$name.jpg" ] && [ ! -f "$SRC/$name.jpeg" ]; then
    MISSING+=("$name")
  fi
done
if [ ${#MISSING[@]} -gt 0 ]; then
  echo "THIẾU ${#MISSING[@]} file:"
  printf '  - %s\n' "${MISSING[@]}"
  echo "Đặt tên file đúng convention enum rồi chạy lại."
  exit 1
fi
echo "✓ Đủ 21 file"

# 2. Resize 128×128 với alpha
echo "=== Resize 128×128 → $RESIZED ==="
mkdir -p "$RESIZED"
rm -f "$RESIZED"/*.png
for name in "${REQUIRED[@]}"; do
  for ext in png jpg jpeg; do
    if [ -f "$SRC/$name.$ext" ]; then
      magick "$SRC/$name.$ext" -resize 128x128 -background none "$RESIZED/$name.png"
      break
    fi
  done
done
echo "✓ Resized $(ls $RESIZED/*.png | wc -l) files"

# 3. Backup vimate_emotions.* + clean old emo C files
echo "=== Backup current vimate_emotions table ==="
cp "$DST/vimate_emotions.c" /tmp/
cp "$DST/vimate_emotions.h" /tmp/
rm -f "$DST"/*.c

# 4. Convert PNG → LVGL C array (RGB565A8, no compress — fast decode)
echo "=== Convert → LVGL C arrays ==="
$PYTHON $SCRIPT_PY \
  --ofmt C \
  --cf RGB565A8 \
  --compress NONE \
  -o "$DST" \
  "$RESIZED" 2>&1 | tail -2

# 5. Restore vimate_emotions.* + patch includes
cp /tmp/vimate_emotions.c "$DST/"
cp /tmp/vimate_emotions.h "$DST/"

echo "=== Patch LVGL include paths ==="
for f in "$DST"/*.c; do
  $PYTHON -c "
f='$f'
s=open(f).read()
s=s.replace('#if defined(LV_LVGL_H_INCLUDE_SIMPLE)\n#include \"lvgl.h\"\n#elif defined(LV_BUILD_TEST)\n#include \"../lvgl.h\"\n#else\n#include \"lvgl/lvgl.h\"\n#endif\n', '#include \"lvgl.h\"\n')
open(f,'w').write(s)
"
done

echo "✓ Done. $(ls $DST/*.c | wc -l) files. Now run: idf.py build"
