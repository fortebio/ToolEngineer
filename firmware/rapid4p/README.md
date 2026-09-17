# Forte Rapid4P — firmware (RAPID READER 5 SLOT)

Thiết bị đọc test nhanh **5** ống cùng lúc (TCS34725 ×5 sau mux TCA9548A; số khe = `BOARD_SENSOR_SLOTS`, 4 → 5 ngày 2026-09-17), màn cảm ứng 4,3",
WiFi, gửi kết quả về Engineer Server và cập nhật OTA. Kế thừa luồng người dùng của
`FBT-ReaderPlus-1.0` (Arduino ESP32, 4 ống, 3 nút cơ) sang phần cứng **ESP32-P4C5 + LCD
ST7102 MIPI-DSI + touch ST7123** (board tham chiếu của `firmware-vimate-p4`).

**Phiên bản:** `v0.1.0` (`main/rapid4p.h`) · ESP-IDF 5.5.4 (5.5.1 OK) · target `esp32p4` · **đã nạp board rev v1.3 (2026-09-17), chưa có bo cảm biến**.

## Luồng người dùng

```
START ─ĐO─► Chọn mẫu (Tôm thẻ/Tôm sú/Cá rô phi/Heo/Nước) ─► Chọn bệnh (PC/EHP/EMS/WSSV/TPD)
      ─► Đặt ống vào 5 khe ─ĐO─► Đang đo (3 vòng × 5 khe, ~42 s ước tính) ─► Kết quả 5 khe (0–3000, +/−)
START ─► Cân chỉnh: mỗi khe đọc Cao nhất rồi Thấp nhất → NVS (5 khe)
START ─► Cài đặt: Ngôn ngữ (VI/EN/ZH/TW) · WiFi (SoftAP + portal: SSID, mật khẩu, Mã máy, Token) ·
         Cập nhật (OTA) · Threshold (5 bệnh, mặc định 600)
```
Nút vật lý: **ĐO** (BTN3 GPIO0) = xác nhận/đo; **BOOT** (GPIO35) tap = quay lại, giữ 5 s =
xoá WiFi + khởi động lại.

## Cấu trúc

```
main/
├── app_main.c            boot orchestrator + main task (event bits)
├── rapid4p.h             version, enum bệnh/mẫu/ngôn ngữ, event bits, cấu hình server
├── boards/               board.h (chọn board) · board_esp32p4_43lcd.h (MỌI chân/knob)
├── core/                 nvs_store · system_info · wifi_mgr (+captive_dns) · task_profile.h
├── network/              engineer_api (token/URL) · ota_client (/ota/check + rollback) · result_upload (POST + hàng đợi offline)
├── ui/                   display (HW DSI/PPA/backlight/queue) · ui_reader (13 màn) · ui_strings · ui_wifi_setup · fonts/
├── input/                touch (ST7123 → lv_indev) · button (BOOT + ĐO)
├── sensor/               sensor_bus (I2C_NUM_1) · tca9548 · tcs34725 (AGC + DN40 lux) · slot_led
└── app/                  measure (task đo/calib) · calib_store (NVS: calib, ngưỡng, ngôn ngữ, LED)
components/esp_lcd_st7102/   driver panel vendor
partitions/partitions.rapid4p.csv   ota_0/ota_1 3 MB, assets 1 MB, results 2 MB
scripts/                  build.bat · flash.bat · readlog.py
docs/HARDWARE-PINOUT.md   pinout từ schematic thật của board
```

## Build / nạp

```bat
scripts\build.bat            :: BUILD_EXIT=0/1
scripts\build.bat COM48      :: build + nạp
python scripts\readlog.py auto 60 boot.log
```
Chi tiết, luật, bẫy: `CLAUDE.md`. Khảo sát và quyết định thiết kế:
`../maping new product/MAPPING-Rapid4P.md`.

## Hợp đồng với Engineer Server

- `GET /ota/check?device=&ver=&product=rapid4p&hw=P4C5-43` → tải `url` khi `ver` khác bản đang chạy.
- `POST /rapid4p/results` JSON (xem `main/network/result_upload.h`): `id_device`, `sick`,
  `sample`, `slot_value[4]`, `slot_result[4]`, `slot_positive[4]`, `threshold`, `time` giờ VN…
- Token Bearer: nhập qua portal SoftAP → NVS; không có trong firmware.
