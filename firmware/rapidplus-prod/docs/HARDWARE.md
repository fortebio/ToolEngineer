# Phần cứng — FBT-RapidPlus

> ⚠️ **CHƯA VERIFY.** Mọi mục `TODO(hw)` là chỗ trống, không phải giá trị đã
> kiểm chứng. Điền file này **trước**, rồi mới sửa code (AGENTS.md §2).
>
> Nguồn tham chiếu: `FBT/firmware/instrument-wroom32` (bản v2.4.2 đang bán).

## MCU

| | |
|---|---|
| Chip | ESP32-WROOM-32 |
| PSRAM | **KHÔNG CÓ** — ràng buộc quan trọng nhất của dự án (PRD NFR-01) |
| Flash | 8 MB, 2 app slot ([`partitions/default_8MB.csv`](../partitions/default_8MB.csv)) |
| Framework | Arduino, toolchain ghim trong [`platformio.ini`](../platformio.ini) |

## PCB version

PRD NFR-08: hỗ trợ đồng thời **V1.2** và **V1.3**; `heater_duty` khác độ dài theo PCB.

`FBT_PCB_VERSION` trong [`include/version.h`](../include/version.h) quyết định bản
build dành cho PCB nào. OTA từ chối nếu không khớp — nạp nhầm = **sai pin map = hỏng phần cứng**.

| PCB | Khác biệt |
|---|---|
| V1.2 | `TODO(hw)` |
| V1.3 | `TODO(hw)` |

## Nhiệt

| Bộ phận | Sensor | Ngưỡng cứng | Pin |
|---|---|---|---|
| Bottom heater | `TODO(hw)` (DallasTemperature/OneWire?) | **> 99 °C → cắt** | `TODO(hw)` |
| Hot-lid | `TODO(hw)` | **> 85 °C → cắt** | `TODO(hw)` |

Ngưỡng cứng định nghĩa tại [`include/safety_limits.h`](../include/safety_limits.h).
**PRD Q3 ghi ngưỡng chính xác cần team nhiệt/sinh học chốt** — xác nhận bằng văn
bản trước khi phát hành.

### Bảo vệ phần cứng (lớp 0)

- [ ] `TODO(hw)` — có cầu chì nhiệt / thermal cutoff độc lập với firmware không?

Nếu **không có**, firmware là lớp bảo vệ duy nhất. Ghi rõ điều đó ở đây và nâng
mức nghiêm ngặt của mọi thay đổi chạm nhiệt tương ứng.

## Quang học

| | |
|---|---|
| Sensor | VEML6035 (theo bản v2.4.2) |
| Số kênh | 10 (`Record::toJSON` × 10) |
| Mux | TCA9548A |
| Pin | `TODO(hw)` |

## Ngoại vi khác

| Thiết bị | IC | Pin | Ghi chú |
|---|---|---|---|
| IO expander | MCP23017 | `TODO(hw)` | |
| Display | ILI9341 (SPI) | `TODO(hw)` | |
| Fan | — | `TODO(hw)` | |
| Buzzer | — | `TODO(hw)` | |
| LED | — | `TODO(hw)` | |
| Nút | — | `TODO(hw)` | |

## Ngân sách RAM (đo, đừng đoán)

| Mốc | `ESP.getFreeHeap()` | Ngày đo |
|---|---|---|
| Sau `setup()`, scaffold | `TODO` | |
| Sau khi thêm driver | `TODO` | |
| Khi WiFi + TLS active | `TODO` | |
| Khi MQTT + TLS active | `TODO` | |

Đây là bảng quan trọng nhất file này. Điểm chật nhất đã biết: TLS + MQTT +
AsyncWebServer cùng lúc. Không đủ thì cân nhắc bỏ AsyncWebServer khi cloud
active, hoặc đẩy mTLS ra edge/VPN (PRD NFR-01).

## Bẫy đã biết

> Mỗi lần mất > 1 giờ vì một vấn đề phần cứng, **ghi vào đây**. Đây là phần có
> giá trị nhất của file này — kiến thức không tái tạo được từ code.

| Triệu chứng | Nguyên nhân đã xác minh | Cách xử lý |
|---|---|---|
| _(chưa có)_ | | |
