# PRV-Reader - Rapid Disease Detection System

Thiết bị đọc nhanh phát hiện bệnh trên động vật nuôi (tôm, cá, heo, gà) sử dụng cảm biến màu quang học, tích hợp kết nối cloud và hỗ trợ đa ngôn ngữ.

**Firmware version:** v2.6.6

---

## Mục lục

- [Tổng quan hệ thống](#tổng-quan-hệ-thống)
- [Phần cứng](#phần-cứng)
- [Cấu trúc thư mục](#cấu-trúc-thư-mục)
- [Kiến trúc phần mềm](#kiến-trúc-phần-mềm)
- [Luồng hoạt động](#luồng-hoạt-động)
- [Giao thức truyền thông](#giao-thức-truyền-thông)
- [Quản lý bộ nhớ EEPROM](#quản-lý-bộ-nhớ-eeprom)
- [Hệ thống động vật & bệnh](#hệ-thống-động-vật--bệnh)
- [Đa ngôn ngữ](#đa-ngôn-ngữ)
- [Build & Deploy](#build--deploy)

---

## Tổng quan hệ thống

PRV-Reader là thiết bị IoT nhúng sử dụng ESP32 để đọc mẫu xét nghiệm bệnh trên động vật nuôi thông qua cảm biến màu TCS34725. Kết quả đo được hiển thị trên màn hình LCD, truyền qua Bluetooth đến ứng dụng di động, và đẩy lên Google Sheets để lưu trữ.

### Sơ đồ kiến trúc tổng quan

```text
┌─────────────────────────────────────────────────────┐
│                    ESP32 DevKit-C v4                 │
│                                                     │
│  ┌──────────┐  ┌──────────┐  ┌───────────────────┐  │
│  │ TCS34725 │  │ ILI9341  │  │   3 Buttons       │  │
│  │  Sensor  │  │   LCD    │  │  RED/BLUE/WHITE   │  │
│  │  (I2C)   │  │  (SPI)   │  │  (GPIO 13/16/4)   │  │
│  └──────────┘  └──────────┘  └───────────────────┘  │
│                                                     │
│  ┌──────────┐  ┌──────────┐  ┌───────────────────┐  │
│  │  WiFi    │  │Bluetooth │  │   LED (GPIO 25)   │  │
│  │          │  │ Classic  │  │                   │  │
│  └────┬─────┘  └────┬─────┘  └───────────────────┘  │
└───────┼──────────────┼──────────────────────────────┘
        │              │
        ▼              ▼
  ┌───────────┐  ┌───────────┐
  │  Google   │  │  Mobile   │
  │  Sheets   │  │   App     │
  │  (HTTPS)  │  │  (BT)    │
  └───────────┘  └───────────┘
```

---

## Phần cứng

| Thành phần | Model | Giao tiếp | Mô tả |
| --- | --- | --- | --- |
| MCU | ESP32-DevKit-C v4 | - | Vi điều khiển lõi kép, WiFi + BT |
| Màn hình | ILI9341 TFT LCD | SPI | 240x320 pixels, xoay 90° |
| Cảm biến màu | TCS34725 | I2C | Đo RGB, lux, nhiệt độ màu |
| Nút bấm | 3 nút vật lý | GPIO | RED (13), BLUE (16), WHITE (4) |
| LED trạng thái | LED đơn | GPIO 25 | Chỉ thị trạng thái hoạt động |

### Sơ đồ chân (Pin Map)

```text
ESP32 GPIO    Thiết bị
─────────────────────────
GPIO 13  ──→  Nút RED (xuống/chọn)
GPIO 16  ──→  Nút BLUE (lên)
GPIO  4  ──→  Nút WHITE (quay lại)
GPIO 25  ──→  LED trạng thái
SPI Bus  ──→  ILI9341 LCD
I2C Bus  ──→  TCS34725 Sensor
```

---

## Cấu trúc thư mục

```text
reader/
├── src/                          # Mã nguồn chính
│   ├── main.cpp                  # Entry point: setup() & loop()
│   ├── define.h                  # Hằng số, macro, cấu hình chân GPIO, EEPROM map
│   ├── sensor.cpp / sensor.h     # Logic cảm biến TCS34725, Kalman filter
│   ├── displayLCD.cpp / .h       # Quản lý giao diện LCD, các màn hình UI
│   ├── button.cpp / button.h     # Xử lý nút bấm, interrupt
│   ├── menu.cpp / menu.h         # Hệ thống menu điều hướng
│   ├── Sample.cpp / Sample.h     # Dữ liệu động vật, định nghĩa bệnh
│   ├── Bluetooth.cpp / .h        # Bluetooth, WiFi, cloud integration
│   ├── displayresources.h        # Tài nguyên UI (font, icon)
│   ├── tcs.h                     # Auto-ranging logic cho TCS34725
│   └── Icon/iconSample.h         # Bitmap icon
├── platformio.ini                # Cấu hình build PlatformIO
├── wokwi.toml                    # Cấu hình simulator Wokwi
├── diagram.json                  # Sơ đồ phần cứng (Wokwi)
└── .vscode/                      # Cấu hình VS Code
```

---

## Kiến trúc phần mềm

### Module và trách nhiệm

```text
┌─────────────────────────────────────────────┐
│                  main.cpp                   │
│            (setup + loop chính)             │
├──────────┬──────────┬───────────┬───────────┤
│ display  │  sensor  │  button   │ Bluetooth │
│  LCD.cpp │  .cpp    │  .cpp     │   .cpp    │
├──────────┼──────────┼───────────┼───────────┤
│ menu.cpp │  tcs.h   │           │           │
│          │ (auto-   │           │           │
│          │  range)  │           │           │
├──────────┴──────────┴───────────┴───────────┤
│               Sample.cpp                    │
│     (Dữ liệu động vật & định nghĩa bệnh)  │
├─────────────────────────────────────────────┤
│               define.h                      │
│  (Hằng số, macro, EEPROM map, pin config)   │
└─────────────────────────────────────────────┘
```

### Chi tiết từng module

#### `main.cpp` — Entry Point

- `setup()`: Khởi tạo Serial, EEPROM, WiFi, cảm biến, LCD, database động vật
- `loop()`: Gọi vòng lặp display và sensor liên tục

#### `sensor.cpp` — Cảm biến màu

- Đọc giá trị RGB từ TCS34725
- Auto-ranging: tự động điều chỉnh gain (1x, 4x, 16x, 60x) và integration time (154ms - 614ms)
- Bộ lọc Kalman cho tín hiệu ổn định
- Phát hiện bão hòa (ngưỡng 75%)
- Tính toán lux và nhiệt độ màu

#### `displayLCD.cpp` — Giao diện người dùng

- Quản lý tất cả màn hình UI (>15 trạng thái)
- Render font Unicode (Tiếng Việt, Trung Quốc, Đài Loan) qua U8g2
- Hiển thị kết quả đo, biểu đồ, trạng thái

#### `button.cpp` — Xử lý nút bấm

- 3 nút: RED (xuống/chọn), BLUE (lên), WHITE (quay lại)
- Hỗ trợ nhấn giữ (5s cho calibration)
- Callback-based event handling

#### `menu.cpp` — Điều hướng menu

- Menu phân cấp với cuộn trang
- Chọn mục bằng icon
- Hiệu ứng cursor hoạt hình

#### `Sample.cpp` — Dữ liệu mẫu

- Định nghĩa 4 nhóm động vật và các bệnh tương ứng
- Parse JSON → cấu trúc dữ liệu trong bộ nhớ
- Quản lý ngưỡng (threshold) cho từng bệnh

#### `Bluetooth.cpp` — Kết nối

- Bluetooth Classic: giao tiếp với ứng dụng di động
- WiFi: kết nối mạng qua WiFiManager (AP mode)
- HTTPS: đẩy dữ liệu lên Google Sheets
- OTA: cập nhật firmware qua mạng
- NTP: đồng bộ thời gian (GMT+7)

---

## Luồng hoạt động

### Quy trình đo mẫu

```text
 ① Khởi động
    │
 ② Chọn nhóm động vật (Tôm / Cá / Heo / Gà)
    │
 ③ Chọn loại mẫu (mô, nước, ...)
    │
 ④ Chọn loại bệnh cần xét nghiệm
    │
 ⑤ Đặt ống mẫu vào khe đọc
    │
 ⑥ Đo lần 1 → Hiển thị kết quả
    │
 ⑦ Đo lần 2 → Hiển thị kết quả
    │
 ⑧ Đo lần 3 → Hiển thị kết quả
    │
 ⑨ Tính trung bình 3 lần đo
    │
 ⑩ Hiển thị kết quả cuối + trạng thái (Dương tính / Âm tính)
    │
 ⑪ Đẩy dữ liệu lên Google Sheets (tùy chọn)
```

### Các trạng thái màn hình (Screen States)

| Trạng thái | Mô tả |
| --- | --- |
| `escreenStart` | Màn hình khởi động |
| `echooseAnimals` | Chọn nhóm động vật |
| `echooseSample` | Chọn loại mẫu |
| `echoosetube` | Chọn ống xét nghiệm |
| `eprepare` | Chờ đặt ống mẫu |
| `ewaitingReadsensor` | Đang đọc cảm biến |
| `escreenResult` | Kết quả từng lần đo |
| `escreenAverageResult` | Kết quả trung bình |
| `ecalibSensor` | Chế độ hiệu chuẩn |
| `e_setting` | Menu cài đặt |
| `e_settingWifi` | Cấu hình WiFi |
| `e_settingUpdate` | Cập nhật firmware OTA |
| `e_language` | Chọn ngôn ngữ |
| `e_setThreshold` | Cấu hình ngưỡng |
| `e_loginCalib` | Nhập mật khẩu hiệu chuẩn |
| `e_groupData` | Theo dõi đo theo nhóm |
| `logdata` | Xem log dữ liệu |

---

## Giao thức truyền thông

### Bluetooth Classic

- Tên thiết bị: `ESP_READER-<MAC_ADDRESS>`
- Giao tiếp serial qua BluetoothSerial
- Dùng để: cấu hình ngưỡng, truyền kết quả đến app

### WiFi & Cloud

- Kết nối WiFi qua WiFiManager (AP mode khi chưa cấu hình)
- Mật khẩu AP mặc định: `FBT`
- Đẩy dữ liệu lên Google Sheets qua Google Apps Script (HTTPS POST)

### Cấu trúc dữ liệu gửi lên Cloud

```json
{
  "method": "append",
  "sick": "EHP",
  "sensor_value1": 125.5,
  "sensor_value2": 130.2,
  "sensor_value3": 128.0,
  "data_IDdevice": "DEVICE_001",
  "date": "17-03-2026 14:30:00",
  "version": "v2.6.6"
}
```

---

## Quản lý bộ nhớ EEPROM

Tổng dung lượng: **1024 bytes** (partition: `min_spiffs.csv`)

| Địa chỉ | Dữ liệu | Kích thước |
| --- | --- | --- |
| 0 - 1 | Giá trị hiệu chuẩn Min | 2 bytes |
| 2 - 7 | Giá trị hiệu chuẩn Max | 2 bytes |
| 8 - 11 | Slope (hệ số góc) | 4 bytes |
| 12 - 15 | Origin (gốc tọa độ) | 4 bytes |
| 16 - 35 | Ngưỡng bệnh (5 bệnh × 4 bytes) | 20 bytes |
| 36 - 39 | Cài đặt ngôn ngữ | 4 bytes |
| 40 - 59 | Device ID | 20 bytes |
| 60 - 94 | WiFi SSID | 35 bytes |
| 95 - 129 | WiFi Password | 35 bytes |
| 130 - 149 | Bluetooth ID | 20 bytes |
| 200+ | Checksum / Status Flags | varies |
| 512+ | Dữ liệu mẫu theo nhóm | 100 bytes/nhóm |

### Hệ thống hiệu chuẩn (Calibration)

- Phạm vi giá trị: 0 - 3000
- Tính slope và origin cho ánh xạ tuyến tính
- Kích hoạt bằng giữ nút RED 5 giây
- Bảo vệ bằng mật khẩu

---

## Hệ thống động vật & bệnh

### Các nhóm được hỗ trợ

| Nhóm | Mẫu | Bệnh phát hiện |
| --- | --- | --- |
| **Tôm (PRAWN)** | 3 loại mẫu | PC, EHP, EMS, WSSV, TPD |
| **Cá (FISH)** | 2 loại mẫu | ISKNV, TILV, PC |
| **Heo (PIG)** | 3 loại mẫu | PC, ASF |
| **Gà (CHICKEN)** | 3 loại mẫu | (đang phát triển) |

### Cấu trúc dữ liệu

```text
group_t (Nhóm)
├── name: tên nhóm
└── animal_t[] (Mẫu)
    ├── sample_name: tên mẫu
    └── sick_t[] (Bệnh)
        ├── name: tên bệnh (20 bytes)
        ├── threshold: ngưỡng phát hiện (mặc định 500)
        └── color: màu hiển thị
```

---

## Đa ngôn ngữ

| Ngôn ngữ | Mô tả |
| --- | --- |
| Tiếng Việt | Mặc định |
| English | Tiếng Anh |
| 中文 | Tiếng Trung (Giản thể) |
| 繁體中文 | Tiếng Đài Loan (Phồn thể) |

Sử dụng thư viện **U8g2** để render font Unicode trên LCD.

---

## Build & Deploy

### Yêu cầu

- [PlatformIO](https://platformio.org/) (VS Code extension hoặc CLI)
- ESP32 DevKit-C v4

### Thư viện phụ thuộc

| Thư viện | Mục đích |
| --- | --- |
| U8g2lib | Font Unicode đa ngôn ngữ |
| Arduino_GFX_Library | Render đồ họa |
| Adafruit_ILI9341 | Driver LCD |
| Adafruit_TCS34725 | Driver cảm biến màu |
| WiFiManager | Cấu hình WiFi qua AP |
| ArduinoJson | Parse/generate JSON |
| NTPClient | Đồng bộ thời gian |

### Build & Upload

```bash
# Build firmware
pio run

# Upload qua USB Serial
pio run --target upload

# Monitor serial output
pio device monitor --baud 115200
```

### Mô phỏng (Simulation)

Hỗ trợ [Wokwi Simulator](https://wokwi.com/) - cấu hình trong `wokwi.toml` và `diagram.json`.

```bash
# File firmware cho simulator
.pio/build/esp32dev/firmware.bin
.pio/build/esp32dev/firmware.elf
```

### Cập nhật OTA

Thiết bị hỗ trợ cập nhật firmware Over-The-Air qua WiFi sử dụng `HTTPUpdate`. Thiết bị tự kiểm tra phiên bản mới từ server và tải về tự động.

---

## Bảo mật

- Hiệu chuẩn được bảo vệ bằng mật khẩu
- WiFi credentials lưu trong EEPROM
- Bluetooth yêu cầu ghép nối (pairing) tiêu chuẩn
- OTA update có kiểm tra phiên bản

---

Forte Biotech - PRV-Reader Disease Detection System
