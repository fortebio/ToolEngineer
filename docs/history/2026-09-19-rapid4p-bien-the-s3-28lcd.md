# 2026-09-19 — Rapid4P: biến thể **ESP32-S3 + LCD 2.8"** (board ES3N28P), một mã nguồn hai board

Chạm: `firmware/rapid4p/` (build system, board HAL, display/touch, UI token) · `system/products.yaml` (khoá mới `rapid4p-s3`) ·
`system/brand/tokens.json` (thang `lcd-2.8in`) · `.gitignore` · `docs/plan/rapid4p-5-slot.md` (Q5) · CLAUDE.md gốc.

## Vì sao

Cần bản Rapid Reader 5 khe cho **màn 2.8"** dựa trên tài liệu `firmware-vimate` (dự án Bizgeni, biến thể `s3-28lcd`
đã build và chạy thật 12/09/2026 trên board AI-IoT VN **ES3N28P-LCD-2.8**: ESP32-S3 N16R8, ILI9341 SPI 320×240,
touch FT6236G). Firmware `rapid4p` hiện chỉ build cho ESP32-P4C5 + LCD 4.3" DSI; logic đo/mạng/dashboard đã sạch
`BOARD_*` nên chỉ phần màn hình/chạm/kích thước UI gắn cứng P4.

## Quyết định (đã chốt với người dùng)

| # | Quyết định | Lý do |
|---|---|---|
| 1 | **Khoá registry riêng `rapid4p-s3`**, `variant_of: rapid4p`, `chip: esp32s3`, cùng `firmware/rapid4p`, env `s3_28lcd`, ảnh `rapid4p-s3_{ver}.bin`, tag `fw/rapid4p-s3/` | Quy ước `products.yaml` (biến thể cùng mã nguồn = khoá riêng); server OTA một target/khoá và lọc `hw` cần thẻ FBTIMG1 chưa nhúng → khoá riêng là cách duy nhất chắc chắn không đẩy ảnh P4 xuống S3. `esp_app_desc.project_name` = khoá (CMake đặt tên project theo board). Cùng `R4P_FW_VERSION`. |
| 2 | Phần cứng đúng ES3N28P; **chân bo cảm biến 5 khe ĐỀ XUẤT** từ 10 GPIO trống (I2C1 SDA41/SCL40, LED 2/9/14/21/38, PWM 39, nút ĐO 47, dư 48) | Chưa có schematic/header thật; mọi chân trong `board_esp32s3_28lcd.h`, sửa 1 chỗ. |
| 3 | UI 320×240: **1 hàng 5 ô**, số 24 tự hạ 18 (`fit_font_from`), header 32 · footer 52 · nút 44 · gap 6; header chỉ icon WiFi + số chờ gửi (IP/version xuống màn chính); màn WiFi **một thẻ bố cục hàng** (QR 128 px) tự hiện bước ② khi có client, chạm để đổi | Content chỉ 156 px; 2 thẻ QR cột cho QR 74 px không quét được. |
| 4 | Phạm vi: build cả hai board `BUILD_EXIT=0`; nạp thử bo 2.8" khi có (chưa có bo hôm nay) | — |

## Đã làm

**Build đa-board** (`firmware/rapid4p/`)
- `CMakeLists.txt`: biến cache `R4P_BOARD` (`p4_43lcd` | `s3_28lcd`) → `SDKCONFIG_DEFAULTS = sdkconfig.defaults;sdkconfig.defaults.<board>`,
  `SDKCONFIG = build_<board>/sdkconfig` (hai board không đè nhau), `DEPENDENCIES_LOCK dependencies.lock.${IDF_TARGET}`,
  `set(COMPONENTS main)`, `project(rapid4p | rapid4p-s3)`, `SUPPORTED_TARGETS esp32p4 esp32s3`.
- `dependencies.lock` → `dependencies.lock.esp32p4` (git mv) + `dependencies.lock.esp32s3` mới (track cả hai).
- `main/idf_component.yml`: `esp_wifi_remote`/`esp_hosted` `rules: target == esp32p4`; thêm `esp_lcd_ili9341 ^2.0.0` `target == esp32s3`.
- `main/CMakeLists.txt`: SRCS `display_hw_dsi.c`/`display_hw_spi.c` theo `CONFIG_RAPID4P_BOARD_*`; REQUIRES `esp_driver_ppa` + PRIV `esp_lcd_st7102`
  chỉ khi `IDF_TARGET == esp32p4` (đọc bằng `idf_build_get_property` — có ở early expansion), S3 PRIV `espressif__esp_lcd_ili9341`.
- `main/Kconfig.projbuild`: choice 2 board, `depends on IDF_TARGET_*`, default theo target.
- `sdkconfig.defaults` tách 3: chung / `.p4_43lcd` (P4 rev<3, PSRAM 200M, L2 cache, ESP-Hosted, CPU 360, LV IRAM) / `.s3_28lcd`
  (OCT PSRAM 80M, `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`, `ALWAYSINTERNAL 512`, CPU 240, WiFi native).
- `scripts/build.bat [board] [COM]` (tương thích `build.bat COMxx` = P4), `scripts/flash.bat [board] COM`; build dir `build_<board>/`.
- `.gitignore`: `firmware/rapid4p/build_*/`.

**Board / HAL**
- `main/boards/board_esp32s3_28lcd.h` (MỚI): LCD SPI3 ILI9341, touch FT6236 I2C0, BOOT GPIO0, LED 42, khối cảm biến ĐỀ XUẤT,
  `BOARD_PRODUCT_KEY "rapid4p-s3"`, `BOARD_HW_VERSION "S3-28"`, `BOARD_SENSOR_SLOTS 5`.
- `board.h`: chọn 2 header, `#ifndef` mặc định (`BOARD_LCD_USE_SPI`, `BOARD_TOUCH_USE_*`, `BOARD_LCD_DRAW_BUF_LINES 40`, `BOARD_PRODUCT_KEY`, …),
  `#error` nếu board không chọn đúng một giao tiếp màn/một driver chạm. P4 header thêm `BOARD_PRODUCT_KEY "rapid4p"`, `BOARD_HW_VERSION "P4C5-43"`.
- `rapid4p.h`: `R4P_PRODUCT_KEY`/`R4P_HW_VERSION` = `BOARD_*` (ota_client/result_upload không đổi).

**Màn hình / chạm**
- `ui/display.c` chỉ còn phần chung (LEDC, ngủ, `lvgl_port_init`, queue/task); `ui/display_hw.h` giao diện 3 hàm;
  `ui/display_hw_dsi.c` = cắt nguyên văn khối DSI/PPA cũ (script cắt theo dòng, anchor kiểm); `ui/display_hw_spi.c` mới
  (SPI → ILI9341 → `lvgl_port_add_disp`, 2 buffer × 40 dòng RAM nội DMA, swap_bytes).
- `input/touch.c`: hai nhánh `#if BOARD_TOUCH_USE_ST7123` (reg16 giữ nguyên) / `BOARD_TOUCH_USE_FT6236` (reg8 + xung RST), map toạ độ chung.

**UI token**
- `system/brand/tokens.json` › `typography.scales.lcd-4.3in` / `lcd-2.8in` (+ ghi chú touch) → `ui/ui_theme.h` 2 thang
  (`UI_SCALE_SMALL`, ~40 token `UI_*`); `ui_reader.c` không còn số px (fit_font theo token, `fit_font_from` cho số khe);
  `ui_wifi_setup.c` thang nhỏ một thẻ hàng (`small_pick_card`, chạm đổi thẻ). Thang 4.3" giữ đúng số cũ (`.bin` P4 cùng kích thước).

**Registry / docs**
- `system/products.yaml`: `rapid4p.envs: [p4_43lcd]`; mục `rapid4p-s3`. `python tools/registry_check.py` → 0 lỗi.
- `firmware/rapid4p/CLAUDE.md` (viết lại §0–§6), `README.md`, `docs/HARDWARE-PINOUT.md` §15, `docs/plan/rapid4p-5-slot.md` Q5, CLAUDE.md gốc.

## Kết quả kiểm

- `scripts\build.bat p4_43lcd` → `BUILD_EXIT=0`, `rapid4p.bin` 2 083 728 B, `esp_app_desc` `0.1.0`/`rapid4p`.
- `scripts\build.bat s3_28lcd` → `BUILD_EXIT=0`, `rapid4p-s3.bin` 1 999 664 B, `esp_app_desc` `0.1.0`/`rapid4p-s3`;
  `dependencies.lock.esp32s3` có `esp_lcd_ili9341 2.0.x`, không `esp_hosted`; sdkconfig S3: OCT PSRAM, CPU 240, không `LV_ATTRIBUTE_FAST_MEM_USE_IRAM`.
- Chỉ còn warning deprecated `lv_obj_add/remove_flag` cũ. `registry_check.py`: ĐẠT 0 lỗi (3 cảnh báo hợp đồng rapidplus cũ).
- **Chưa nạp bo 2.8"** (không có bo trên bàn) — tiêu chuẩn "xong" ghi ở `firmware/rapid4p/CLAUDE.md` §4.

## Bài học (đã ghi vào `firmware/rapid4p/CLAUDE.md` §5)

- `SDKCONFIG` đặt trong build dir hoạt động với `idf.py` (đọc lại từ `project_description.json`); `DEPENDENCIES_LOCK` theo target vì lock có `target:`.
- REQUIRES điều kiện theo `IDF_TARGET` (early expansion), không theo `CONFIG_*`; `set(COMPONENTS main)` loại component vendor P4-only khỏi build S3.
- cmd.exe: dấu `)` trong `echo` bên trong khối `if ( … )` đóng khối sớm → `exit /b` chạy vô điều kiện, không in gì.
- `sensor/*` trong comment C = lỗi `-Werror=comment`.

## Còn mở

1. Nạp bo ES3N28P: xác nhận xoay/chạm (`BOARD_TOUCH_*`), 13 màn + WiFi qua webcam, chỉnh token 2.8" theo mắt, soak 60 s.
2. Header thật của board → chốt chân bo cảm biến; bo giao tiếp kiểu `Rapid4P-IF` cho S3.
3. Server: kho `rapid4p-s3` (ảnh `rapid4p-s3_{ver}.bin`), thử `/ota/check?product=rapid4p-s3&hw=S3-28` với `localtest.ps1`.
4. Q1 tên thương mại vẫn mở (khoá `rapid4p`/`rapid4p-s3` chưa phát hành, đổi được).
