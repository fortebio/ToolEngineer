# CLAUDE.md — firmware/rapid4p (Forte Rapid4P — RAPID READER 4 SLOT, ESP32-P4, ESP-IDF)

> Luật toàn hệ thống ở `CLAUDE.md` gốc. File này: **luật + sự thật phần cứng + chỉ mục** của
> cây rapid4p. Khảo sát gốc và mọi quyết định: `firmware/maping new product/MAPPING-Rapid4P.md`.
> Cây tham chiếu (chỉ đọc, KHÔNG sửa): `firmware/maping new product/firmware-vimate-p4/` —
> `AGENTS.md` + `README-P4.md` ở đó là nhật ký bring-up của chính board này (mọi số đo).

## 0. Trạng thái (2026-09-17)

- **Build**: `BUILD_EXIT=0`, ESP-IDF **5.5.1**, app **1,97 MB / slot 3 MB** (37 % trống).
  **CHƯA NẠP máy thật** — chưa có bo cảm biến; mọi thứ ngoài LCD/touch/WiFi (đã chạy ở cây
  vimate-p4) là code chưa có bằng chứng phần cứng.
- Kế thừa từ vimate-p4 (đã chạy thật): `components/esp_lcd_st7102`, `boards/`, `core/{nvs_store,
  system_info, captive_dns, wifi_mgr}`, `ui/ui_wifi_setup.c`, `ui/fonts/*`, khối HW-init +
  xoay PPA của `ui/display.c`, giao thức reg16 của `input/touch.c`, `scripts/readlog.py`.
- Viết mới: `sensor/*`, `app/*`, `ui/ui_reader.c`, `ui/ui_strings.c`, `network/*`,
  `input/button.c`, `app_main.c`, `rapid4p.h`, partition, sdkconfig, scripts.

## 1. Điều hướng nhanh

| Khu vực | Đọc trước | Sở hữu |
|---|---|---|
| Pinout + knob | `main/boards/board_esp32p4_43lcd.h`, `docs/HARDWARE-PINOUT.md` | mọi chân/địa chỉ/tần số; khối `BOARD_SENSOR_*` / `BOARD_SLOT_LED_*` (JP1, **đề xuất** — chưa có schematic bo con) |
| Boot | `main/app_main.c` | thứ tự init, main task (event bits → ui_reader), SNTP TZ `ICT-7`, diag 60 s |
| Kiểu chung | `main/rapid4p.h` | `R4P_FW_VERSION` (registry đọc regex ở đây), enum bệnh/mẫu/ngôn ngữ, event bits, `g_r4p_cfg` |
| Màn hình HW | `main/ui/display.c` | LDO DPHY → reset GPIO22 → DSI → DPI 2 fb → lvgl_port → PPA xoay 270° → đèn nền LEDC ch0; `display_schedule()`; ngủ màn |
| Màn hình app | `main/ui/ui_reader.c`, `ui_strings.[ch]` | máy trạng thái 13 màn (theo ReaderPlus), nút chạm, tiến độ đo, calib, ngưỡng, WiFi, OTA |
| Chạm | `main/input/touch.c` | ST7123 reg16 → `lv_indev` (đọc trong LVGL task, không task riêng) |
| Nút | `main/input/button.c` | BOOT GPIO35 (tap/giữ 5 s), ĐO GPIO0 |
| Cảm biến | `main/sensor/{sensor_bus,tca9548,tcs34725,slot_led}.c` | I2C_NUM_1 riêng, mux 0x70, TCS 0x29 ×4 (AGC riêng từng slot), LED enable ×4 + PWM LEDC ch1 |
| Đo / calib | `main/app/measure.c`, `calib_store.c` | task đo core 1 prio 4 (~34 s/chu trình), map 0..3000, ngưỡng, NVS |
| Mạng | `main/network/{engineer_api,ota_client,result_upload}.c` | Bearer token từ NVS, `/ota/check` + esp_https_ota + rollback guard, POST kết quả + hàng đợi offline NVS 16 bản |
| WiFi | `main/core/wifi_mgr.c` (+ `captive_dns.c`, `ui/ui_wifi_setup.c`) | STA 2,4 GHz qua C5, SoftAP `FBT-Rapid4P-XX:XX` + portal (thêm ô **Mã máy**, **Token**) |
| Build | `CMakeLists.txt`, `main/CMakeLists.txt`, `main/idf_component.yml`, `sdkconfig.defaults`, `partitions/partitions.rapid4p.csv`, `scripts/build.bat` | ghim lvgl `~9.6.0`, esp_lvgl_port `~2.8.0`, esp_hosted `2.12.*`, esp_wifi_remote `0.14.*` |

## 2. Phần cứng — sự thật (từ cây vimate-p4, đã đo)

- ESP32-P4 rev v1.3, flash 16 MB, PSRAM 32 MB. **Không radio**: WiFi qua ESP32-C5 (ESP-Hosted
  SDIO slot 1), **không BT/BLE**, khoá 2,4 GHz (5 GHz không DHCP được).
- LCD ST7102 native **dọc 480×800**, UI logical **800×480** (`BOARD_LCD_ROTATION 270`, PPA).
  **GPIO22 = LCD_RST = TP_RST**: chỉ `display.c` toggle, một lần trước DSI.
- Đèn nền GPIO6 pull-up → sáng mặc định; LEDC timer 0 / ch 0. **LED slot dùng timer 1 / ch 1.**
- Bus I2C chung GPIO7/8 có 7 thiết bị; touch 0x55. **Bo cảm biến đi I2C_NUM_1 riêng (JP1
  GPIO26/27 — đề xuất)**; đổi sang bus chung chỉ cần sửa `BOARD_SENSOR_I2C_*`.
- Nút: BOOT GPIO35 (chung DTR CH343 — chỉ xung), BTN3 GPIO0. WS2812 GPIO34, AXP2101 0x34 IRQ 21
  **chưa có driver** (pin/sạc chưa đọc được).
- JP1 còn trống sau khi gán cảm biến: GPIO47/48/49/50.

## 3. Luật riêng cây này

1. Mọi chân/địa chỉ/tần số qua `BOARD_*` (include `boards/board.h`). Thiếu knob → thêm vào
   board header kèm nguồn (schematic § hoặc số đo + ngày).
2. **LVGL chỉ trong LVGL task / display task hoặc dưới `display_lock()`.** Task đo, mạng, nút →
   `display_schedule(cb, arg)`. Callback sự kiện LVGL muốn đổi màn → `show_async()` (không xoá
   widget đang phát sự kiện trong chính callback của nó).
3. Task đo (`measure.c`) **không bao giờ** gọi lv_*; báo tiến độ qua callback → ui_reader bọc
   `display_schedule` với bản sao `measure_result_t` (malloc, free trong apply).
4. Khoá payload/NVS (`"PC"`, `"EHP"`, `thr_PC`, `cal_min`…) **không đổi, không dịch** —
   `r4p_sick_name()`; chuỗi hiển thị qua `ui_strings.c`.
5. Payload lên Engineer Server **không dùng tên mảng** `result`/`CT_value`/`record_out`/
   `amplification` (server bắt 10 phần tử cho Rapid+). Dùng `slot_*` 4 phần tử.
6. **Không có bí mật trong repo/firmware**: token server nhập qua portal → NVS `api_token`, không
   log giá trị. `sdkconfig.defaults` ASCII thuần.
7. `R4P_FW_VERSION` đổi = đổi ở `rapid4p.h` + tag `fw/rapid4p/vX.Y.Z`; chạy
   `python tools/registry_check.py` (venv `%LOCALAPPDATA%\fbt-localtest\venv` có pyyaml/jsonschema).
8. Sửa `sdkconfig.defaults` → xoá `sdkconfig` (script tự làm). Không sửa `sdkconfig` sinh ra.

## 4. Build / nạp / log (Windows, cmd.exe — ESP-IDF tại `C:\Espressif`)

```
scripts\build.bat            → BUILD_EXIT=0/1 (build dir build/)
scripts\build.bat COMxx      → build + nạp
scripts\flash.bat COMxx
python scripts\readlog.py --list | auto 60 boot.log   (thả DTR/RTS trước khi mở cổng)
```
Từ Git Bash: `MSYS_NO_PATHCONV=1 cmd.exe /c "scripts\build.bat"`. Lần đầu cần mạng để
component manager kéo lvgl/esp_lvgl_port/esp_hosted/esp_wifi_remote (~2 phút).

**Tiêu chuẩn "xong" khi có board**: `BUILD_EXIT=0` → nạp → log ≥ 60 s không `task_wdt`/`Guru
Meditation`/`stack overflow`; có `DSI rotate: logical 800x480 -> panel 480x800`, `Touch ST7123
init OK`, `sensor bus I2C1`, `slot N (mux ch): TCS34725 OK` ×4, `measure task san sang, 4/4`,
`diag heap internal=`; chu trình đo 34 s mà chạm vẫn ăn.

## 5. Bẫy đã biết

| Triệu chứng | Nguyên nhân → xử |
|---|---|
| `unknown type name 'r4p_lang_t'` ở mọi file | header tên `strings.h` trong `ui/` (INCLUDE_DIRS) che `<strings.h>` hệ thống → include vòng. **Không đặt tên header trùng libc** (đã đổi `ui_strings.h`) |
| `ESP_ERR_WIFI_NOT_CONNECT undeclared` | cần `#include "esp_wifi.h"` |
| `lv_obj_remove_flag` deprecated (LVGL 9.6) | dùng `lv_obj_set_scrollable()` v.v.; `ui_wifi_setup.c` kế thừa vẫn còn — vô hại |
| Task chờ bit `g_r4p_events` (vd `R4P_EVT_WIFI_UP`) không bao giờ thức | main task nhận bit bằng `xClearOnExit=pdTRUE` → task khác chờ cùng bit bị nuốt. Bit sự kiện chỉ MỘT người tiêu thụ (main task); task khác **poll trạng thái** (`wifi_mgr_is_connected()`), như `ota_client.c` |
| Hàng đợi offline làm NVS đầy | NVS 24 KB (~20 KB dùng được, entry 32 B): 16 bản × 900 B ≈ 15 KB là quá sát với WiFi blob/calib/token → `RQ_CAP 8`. Muốn nhiều hơn → chuyển sang partition `results` (SPIFFS) |
| Bẫy phần cứng (panel kẹt sau reset MCU, SDIO chết cache line 128 B, TJPGD tràn stack, DHCP 5 GHz…) | `firmware/maping new product/firmware-vimate-p4/AGENTS.md` §7 — vẫn đúng cho cây này |

## 6. Chưa làm (theo thứ tự)

1. Schematic bo cảm biến 4 slot → chốt chân JP1, pull-up, nguồn LED (VCC3V3 hay BOOST_5V — đo!).
2. Nạp board thật: kiểm §4; xác nhận chiều xoay/chạm bằng mắt (README-P4 §7).
3. Font CJK (107 chữ, `lv_font_conv --symbols`) → bật `R4P_HAVE_CJK_FONT` trong `ui_strings.h`.
4. Hợp đồng JSON `system/contracts/ingest-rapid4p.schema.json` (P2) + thử POST vào
   `server/scripts/localtest.ps1`.
5. Driver AXP2101 (% pin, sạc) nếu Rapid4P chạy pin; WS2812 báo trạng thái.
6. Ngưỡng mặc định 600 (ReaderPlus) hay 500 (reader v2.6.6) — hỏi người sở hữu thuật toán.
7. Nhúng thẻ `FBTIMG1` vào .bin khi server bắt buộc cho kho `rapid4p`.
