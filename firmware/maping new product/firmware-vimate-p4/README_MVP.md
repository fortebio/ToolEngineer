> **Cay firmware nay build cho ESP32-P4** (board FBT + LCD 4.3" ST7102 MIPI-DSI).
> File README nay la ban sao tu cay S3 va van mo ta phan cung S3.
> **Doc [README-P4.md](README-P4.md) truoc** — do la tai lieu dung cho cay nay.

# VIMATE Firmware MVP - LCD 2.8 ESP32-S3

Tài liệu này dành cho khách hàng/đối tác kỹ thuật nhận gói source MVP của
`firmware-vimate`.

## 1. Phạm Vi Bản MVP

`firmware-vimate` là firmware native ESP-IDF do VIMATE viết mới cho thiết bị
ESP32-S3 + LCD 2.8 inch. Đây không phải firmware gốc Xiaozhi.

Bản MVP này tập trung vào các luồng chính:

- Kết nối WiFi và cấu hình lại WiFi bằng BLE provisioning.
- Kết nối WebSocket tới server VIMATE.
- OTA firmware qua server VIMATE.
- Hiển thị UI LCD 2.8 inch: trạng thái, activation code, bài học, ảnh bài học,
  emotion/reward.
- Audio hai chiều: microphone upstream và speaker downstream qua Opus/I2S.
- Đồng bộ asset pack, lesson image và cache media bài học.
- Telemetry/heartbeat gồm heap, PSRAM, RSSI, uptime, FW version và last error.

## 2. Hardware Target

Target chính:

- MCU: ESP32-S3, khuyến nghị N16R8 hoặc tương đương.
- Flash: 16 MB.
- PSRAM: 8 MB.
- LCD: 2.8 inch, 320x240, SPI ILI9341 hoặc ST7789.
- Audio codec: ES8311 qua I2C + I2S.
- Button: BOOT GPIO0.
- Storage: SD card cho media cache nếu board có gắn thẻ.

Pinout tham chiếu nằm tại:

```text
firmware-vimate/main/boards/board_esp32s3_28lcd.h
```

Nếu board của khách hàng khác pinout, cần sửa file board header và cấu hình
`menuconfig -> VIMATE` trước khi build.

## 3. Yêu Cầu Môi Trường Build

- ESP-IDF 5.2 trở lên.
- Python/IDF tools đã cài theo hướng dẫn của Espressif.
- Board target: `esp32s3`.

Ví dụ macOS/Linux:

```bash
. /path/to/esp-idf/export.sh
cd firmware-vimate
idf.py set-target esp32s3
idf.py build
```

ESP-IDF sẽ tự tải managed components theo `main/idf_component.yml`.

## 4. Build Và Flash Để Test MVP

Build debug/MVP:

```bash
cd firmware-vimate
. /path/to/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
```

Flash vào thiết bị:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Trên macOS, port thường có dạng:

```bash
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Nếu muốn xóa trắng cấu hình cũ trước khi flash:

```bash
idf.py -p /dev/ttyUSB0 erase-flash
idf.py -p /dev/ttyUSB0 flash monitor
```

## 5. Cấu Hình Server

Trong `menuconfig -> VIMATE`, cần kiểm tra các mục quan trọng:

- `VIMATE_SERVER_BASE`: URL server, mặc định `https://vimate.vn`.
- `VIMATE_OTA_INTERVAL_SEC`: chu kỳ check OTA.
- `VIMATE_SD_CACHE_ENABLE`: bật/tắt cache media xuống SD.
- `VIMATE_DIAG_ENABLE`: bật log/telemetry phục vụ debug MVP.

Thiết bị cần kết nối được tới các endpoint server:

- OTA: `https://vimate.vn/ota/v1/`
- WebSocket: `wss://vimate.vn/ws/`
- Media/firmware: `https://vimate.vn/media/`, `https://vimate.vn/firmware/`

## 6. OTA Và File BIN

Gói source này không kèm build output. Để flash cho người dùng cuối, nên dùng
file `.bin` đã build và đặt tên theo version, ví dụ:

```text
vimate-fw-1.0.x.bin
```

File OTA public trên server phải:

- Tải được qua HTTPS.
- Bắt đầu bằng ESP32 image magic byte `0xE9`.
- Có version rõ ràng để rollback khi cần.

Người dùng cuối không nên nhận gói source này. Người dùng cuối nên nhận OTA URL
hoặc file BIN đã build sẵn.

## 7. Production/Soak Build

Build soak để test ổn định:

```bash
scripts/build_soak.sh
scripts/soak_monitor.sh /dev/ttyUSB0
```

Build production:

```bash
scripts/prepare_production_keys.sh
scripts/build_production.sh
```

Đọc checklist trước khi làm production:

```text
firmware-vimate/docs/PRODUCTION_CHECKLIST.md
```

Lưu ý: Secure Boot và Flash Encryption là thao tác eFuse một chiều. Chỉ thực
hiện trên quy trình factory đã kiểm soát.

## 8. Không Bao Gồm Trong Gói Zip

Gói zip MVP chỉ gồm source/doc/script cần thiết. Đã loại:

- Thư mục build/cache: `build/`, `build-*`, `managed_components/`, `.venv*`.
- Output flash: `.bin`, `.elf`, `.map`, `.o`, `.obj`.
- Cấu hình local: `sdkconfig`, `sdkconfig.old`.
- File IDE/cache hệ điều hành.

## 9. Giới Hạn MVP Cần Thông Báo

- Cần test lại pinout nếu dùng board khác LCD 2.8 ESP32-S3 của VIMATE.
- Voice/audio phụ thuộc codec, loa, mic và gain trên board thật.
- SD cache chỉ dùng khi board có SD và đúng pin SPI đã cấu hình.
- Production security như Secure Boot/Flash Encryption chỉ nên bật sau soak test.
- Nếu server/OTA URL thay đổi, cần cập nhật `menuconfig` hoặc OTA profile.

## 10. Liên Hệ Kỹ Thuật

Nếu cần build bản BIN theo board riêng, vui lòng cung cấp:

- Sơ đồ pin LCD/audio/SD/button.
- Loại LCD driver và resolution.
- Dung lượng flash/PSRAM.
- Yêu cầu server OTA/WebSocket.
- Version mong muốn cho file `.bin`.

