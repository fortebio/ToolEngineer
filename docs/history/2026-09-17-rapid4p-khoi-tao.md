# 2026-09-17 — Khởi tạo sản phẩm Rapid4P (RAPID READER 4 SLOT)

Chạm ≥ 2 phần: `firmware/rapid4p/` (mới), `system/` (registry + schema), `tools/registry_check.py`,
`firmware/maping new product/` (khảo sát), `CLAUDE.md` gốc, `.gitignore`.

## Làm gì

1. **Khảo sát** (`firmware/maping new product/MAPPING-Rapid4P.md`): phần cứng tham chiếu
   `firmware-vimate-p4/` (board FBT ESP32-P4C5 + LCD 4.3" ST7102 DSI + touch ST7123, ESP-IDF 5.5.1,
   nhật ký bring-up 11–15/09) ↔ firmware gốc `FBT-ReaderPlus-1.0/` (Arduino ESP32, 4× TCS34725 qua
   TCA9548, ILI9341 SPI, 3 nút, BT SPP + WiFiManager, EEPROM, Google Sheet). Bảng mapping ngoại vi /
   module / máy trạng thái, 9 khoảng trống cần chốt.
2. **Chốt mục 1→6** và dựng cây `firmware/rapid4p/` (ESP-IDF native — sản phẩm đầu tiên không
   PlatformIO): kế thừa từ vimate-p4 phần đã chạy thật (driver ST7102, HAL board, display HW-init +
   xoay PPA, touch reg16, nvs/wifi_mgr/captive portal, fonts, scripts); viết mới sensor (bus I2C_NUM_1
   riêng trên JP1, mux, TCS34725 AGC riêng từng slot + DN40 lux, LED slot), task đo/calib giữ ngữ
   nghĩa ReaderPlus (3 vòng × 4 slot × 3 mẫu, map 0..3000, ngưỡng 600), 13 màn LVGL chạm, OTA qua
   Engineer Server `/ota/check` + rollback guard, POST kết quả + hàng đợi offline NVS, portal thêm ô
   Mã máy + Token (token không nằm trong firmware).
   Build: `BUILD_EXIT=0`, app 1,97 MB / slot 3 MB. **Chưa nạp máy thật, chưa có bo cảm biến.**
3. **Registry**: `system/products.yaml` thêm `rapid4p` (`chip: esp32p4`, `build_system: idf`,
   `optical_slots: 4`, `id_prefix: R4P` TODO ERP); `products.schema.json` thêm `firmware.build_system`
   (platformio|idf) và `product.chip`; `tools/registry_check.py` nhánh idf kiểm `CMakeLists.txt` +
   `sdkconfig.defaults[.<env>]` + `CONFIG_<FLAG>=y`. `registry_check` ĐẠT 0 lỗi.
4. `.gitignore`: `build/`, `managed_components/`, `sdkconfig` của hai cây ESP-IDF.

## Bài học (đã ghi vào `firmware/rapid4p/CLAUDE.md` §5)

- Header đặt tên trùng libc (`strings.h`) trong thư mục nằm ở INCLUDE_DIRS che `<strings.h>` hệ
  thống → include vòng, lỗi "unknown type" ở mọi file. Đổi `ui_strings.h`.
- Server ARRAY_FIELDS (`result`, `CT_value`…) bắt 10 phần tử → sản phẩm 4 slot phải dùng tên mảng
  khác (`slot_*`); registry ghi `array_fields` riêng.
- ReaderPlus có lỗi tiềm ẩn: một object `tcs34725` chung cho 4 slot → trạng thái AGC lệch thanh
  ghi thật sau mux → cpl sai bậc. Rapid4P dùng struct riêng từng slot. Và `PRAWN_Monodon` gán nhầm
  chuỗi "PRAWN Vannamei" — Rapid4P sửa thành "PRAWN Monodon".

## Còn mở (MAPPING §5 mục 7–9 + `firmware/rapid4p/CLAUDE.md` §6)

Schematic bo cảm biến; nạp board thật; font CJK; contract JSON `ingest-rapid4p`; AXP2101 nếu chạy
pin; ngưỡng 600 vs 500; nâng slave C5 2.12.x; thẻ FBTIMG1.
