# Mapping Rapid4P (RAPID READER 4 SLOT) — phần cứng `firmware-vimate-p4` ↔ firmware `FBT-ReaderPlus-1.0`

> Ngày lập: 2026-09-17. Nguồn: đọc trực tiếp hai cây trong thư mục này.
> - Phần cứng tiêu biểu: `firmware-vimate-p4/` (board FBT **ESP32-P4C5 + LCD 4.3" ST7102 MIPI-DSI + touch
>   ST7123**, ESP-IDF 5.5.1). Sự thật phần cứng lấy từ `firmware-vimate-p4/docs/HARDWARE-PINOUT.md`
>   (dựng từ schematic thật) và `main/boards/board_esp32p4_43lcd.h`; trạng thái đã chạy lấy từ
>   `README-P4.md` §1.
> - Firmware gốc cần chuyển: `FBT-ReaderPlus-1.0/` (Arduino/PlatformIO, ESP32 DevKit, 4 slot TCS34725
>   qua mux TCA9548, ILI9341 SPI, 3 nút, BT Classic + WiFiManager, EEPROM, Google Sheet).
>
> Mục tiêu tài liệu: (1) gom **thông tin phần cứng** và **cấu trúc code** của cây P4, (2) mô tả hiện
> trạng ReaderPlus, (3) **bảng mapping** ngoại vi/module/máy trạng thái, (4) liệt kê **khoảng trống và
> quyết định cần chốt** trước khi mở cây `firmware/rapid4p/`.

---

## 0. Tóm tắt một phút

| | FBT-ReaderPlus-1.0 (hiện tại) | Board P4 (đích Rapid4P) |
|---|---|---|
| MCU | ESP32 (Xtensa 2 core, WiFi + BT Classic nội) | **ESP32-P4** rev v1.3, 2 core RISC-V 360 MHz, flash 16 MB, PSRAM 32 MB; **không radio** — WiFi qua **ESP32-C5** trong module (ESP-Hosted SDIO); **không BT/BLE** |
| Framework | Arduino, `platform = espressif32 @ 3.2.1`, `loop()` tuần tự, `delay()` | ESP-IDF 5.5.1 native, FreeRTOS nhiều task, LVGL 9, HAL knob `BOARD_*` |
| Màn hình | ILI9341 240×320 SPI, Arduino_GFX + U8g2 unifont (VI/EN/ZH/TW) | ST7102 **480×800 MIPI-DSI**, chạy logical **800×480** (xoay bằng PPA), LVGL, font `lv_font_vimate_*` (chỉ Latin/Việt) |
| Nhập liệu | 3 nút cơ RED/GREEN/WHITE (GPIO 13/16/4) | **Touch ST7123** (I2C 0x55) + 2 nút: BTN3 GPIO0, BOOT GPIO35 |
| Cảm biến | 4× TCS34725 (0x29) sau mux TCA9548 (0x70) trên `Wire(33,32)`; 4 LED chiếu GPIO 25/26/27/14 + PWM chung GPIO12 | **Chưa có** — phải nối bo cảm biến qua header **JP1** (11 GPIO: 26–30, 45–50) hoặc treo lên bus I2C chung GPIO7/8 |
| Lưu cấu hình | EEPROM 512 B (calib min/max, 5 ngưỡng, ngôn ngữ, ID máy, SSID/pass) | NVS (`core/nvs_store.c`) |
| Kết nối | BT Classic SPP (nhập WiFi/ID/ngưỡng), WiFiManager AP `FBT_RAPID`, POST Google Apps Script, NTP | SoftAP + captive portal (`core/wifi_mgr.c`, `captive_dns.c`, `ui/ui_wifi_setup.c`), `esp_http_client`, SNTP, OTA 2 slot có rollback |
| Nguồn | USB 5 V | PMIC AXP2101 (pin Li-ion J2, sạc qua USB-C), **chưa có driver** |

Kết luận ngắn: **phần cứng P4 đáp ứng đủ mọi khối của Rapid4P trừ khối quang học** (4 TCS34725 + LED chiếu),
khối này phải làm **bo con** nối qua JP1. Về code, **tái dùng được ~40 %** cây vimate-p4 (HAL board, display
HW-init, touch, button, NVS, WiFi/portal, OTA, diag, scripts build/flash); phần **viết mới** là driver
TCS34725/TCA9548/LED slot, máy trạng thái đo–hiệu chuẩn–ngưỡng, toàn bộ màn hình LVGL, và đường tải kết quả
lên Engineer Server thay Google Sheet. Phần **bỏ hẳn**: audio/AFE/WakeNet/Opus, WebSocket/MCP, mặt robot GIF,
MP4, cache khoá học (≈ 60 % code và ~5,4 MB flash của cây vimate).

---

## 1. Phần cứng tham chiếu — board FBT ESP32-P4C5 + LCD 4.3"

### 1.1 Khối chính

| Khối | Linh kiện | Giao tiếp / chân | Trạng thái driver trong vimate-p4 |
|---|---|---|---|
| MCU | Module **ESP32-P4C5** 88 chân (P4 + C5 tích hợp) | — | ✅ |
| LCD | `YDP430BT009-V1`, panel ST7102, native **dọc 480×800**, video mode | MIPI-DSI 2 lane 520 Mbps, DPI 37,8 MHz; LDO nội **kênh 3 @ 2,5 V** cấp DPHY; `LCD_RST` **GPIO22**; đèn nền SY7200 EN/PWM **GPIO6** (pull-up → sáng mặc định, ~39 mA) | ✅ `ui/display.c` + `components/esp_lcd_st7102/`, 2 fb + vsync, xoay 270° bằng PPA |
| Touch | ST7123 | I2C `0x55` trên bus chung GPIO7/8; INT **GPIO23**; RST = **GPIO22** (chung LCD) | ✅ `input/touch.c` (giao thức reg16) |
| Camera | SC2336 MIPI-CSI (FPC1) | CSI 2 lane; SCCB chung GPIO7/8; XCLK từ thạch anh 24 MHz ngoài | ⛔ chưa làm — **Rapid4P không cần** |
| Audio out | ES8311 (`0x18`) + ampli NS4150B, loa J1 | I2S: MCLK 13, BCLK 12, WS 10, DOUT 9; PA_CTRL **GPIO3** (HIGH = bật) | ✅ (có thể dùng cho **beep** báo kết quả) |
| Audio in | ES7210 (`0x40`) + 2 mic MEMS | I2S DIN 11 (cùng bus với ES8311 → cùng sample rate 24 kHz) | ✅ — Rapid4P không cần |
| PMIC | **AXP2101** (`0x34`), IRQ **GPIO21** | DCDC1 → VCC3V3; ALDO1 1,8 V + ALDO4 2,9 V (camera); ALDO3 3,3 V (codec, IMU). Pin J2, NTC, VBUS chung 3 cổng USB-C | ⛔ chưa có driver. **Board không có chân ADC đo pin** → % pin phải hỏi PMIC |
| Thẻ nhớ | J4 TF | SDMMC **slot 0 IOMUX**: CLK 43, CMD 44, D0–D3 39–42; VDD từ **LDO nội kênh 4**; không card-detect | ✅ `store/course_media_cache.c` (mount, bounce buffer 64 B, 363/761 KB/s) |
| WiFi | ESP32-C5 trong module, hai băng | ESP-Hosted **SDIO slot 1** (GPIO14–19 nội module), slave reset GPIO54; **khoá 2,4 GHz** (5 GHz không DHCP được) | ✅ `core/wifi_mgr.c` (STA + SoftAP + captive portal) |
| BT/BLE | — | **không có** (`ble_wifi_prov.c` là stub) | ⛔ |
| 4G | ML307R-DL + SIM + IPEX | UART TX 53 / RX 52 / DTR 51 (qua level shifter), nguồn bật bằng **GPIO4** | ⛔ chưa có |
| RS485 | SN65HVD3082E | TX 31, RX 33, DE 32; cần `BOOST_ON` GPIO20 | ⛔ chưa có |
| IMU | LSM6DS3TR-C (`0x6A`) | I2C chung; **không INT**; CS thả nổi (nghi cần pull-up) | ⛔ |
| DAC | MCP4725 (`0x60`) | I2C chung; VDD = BOOST_5V nhưng bus 3,3 V → **VIH 3,5 V có thể không nhận lệnh** | ⛔ |
| LED | WS2812B ×1 | **GPIO34** (cần RMT/SPI) | ⛔ chưa có driver |
| Nút | BTN3 **GPIO0** (người dùng), BTN2 **GPIO35** (BOOT, chung DTR CH343), BTN1 = `AXP_PWRON` (nút nguồn cứng), RST1 = `CHIP_PU` | | ✅ `input/button.c` mới đọc GPIO35 |
| USB | J5 USB-C = USB-Serial/JTAG (GPIO24/25, nạp + debug); J6 USB-C = CH343P console UART0 (TX 37 / RX 38); J3 USB-C + USB1 USB-A = USB 2.0 HS (chung PHY, chỉ dùng 1) | | ✅ console qua CH343 |
| Boost 5 V | SY7088, EN = **GPIO20** | nuôi RS485, MCP4725, VBUS USB-host, JP1 chân 2 | ⚠ chia áp hồi tiếp gợi ý chỉ ~2,6 V — **đo trước khi tin** |

### 1.2 Bản đồ GPIO — cái gì đã chiếm, cái gì còn trống cho bo cảm biến

| Nhóm | GPIO | Ghi chú |
|---|---|---|
| **Đã chiếm, không đụng** | 3 (PA), 6 (BL), 7/8 (I2C chung), 9–13 (I2S), 20 (BOOST_ON), 21 (PMIC IRQ), 22 (LCD/TP RST), 23 (TP INT), 24/25 (USB FS), 34 (WS2812), 35 (BOOT), 37/38 (UART0), 39–44 (SDMMC), 14–19 + 54 (SDIO tới C5) | 22 chỉ `display.c` được giữ |
| **Dành cho tính năng chưa bật** | 2 (C5_BOOT), 4 (4G VBAT EN), 31/32/33 (RS485), 51/52/53 (UART 4G) | không lấy cho cảm biến |
| **Nút người dùng** | **0** (BTN3, pull-up 10 K, nhấn = LOW) | firmware chưa đọc — dùng làm nút "ĐO" |
| **Header JP1 (2×10) — 11 GPIO tự do, mỗi chân có 100 R nối tiếp** | **26, 27, 28, 29, 30, 45, 46, 47, 48, 49, 50** | + chân 1 `DAC`, chân 2 `BOOST_5V`, chân 4 `VCC3V3`, 3/10/17/18 GND, 19/20 RS485 A/B |
| **Chỉ ở chân module, không ra header** | 1, 5, 36 (pull-up 10 K), 54 | cần hàn dây; 54 bị esp_hosted khai làm reset slave → tránh |

⇒ **Bo cảm biến 4 slot cắm vào JP1** có đủ chân: 2 (I2C riêng) + 4 (LED enable) + 1 (PWM) = 7, còn dư 4.

### 1.3 Ba bus phải hiểu trước khi thêm cảm biến

- **I2C chung GPIO7 (SDA) / GPIO8 (SCL)**: pull-up 2,2 K, đã có **7 thiết bị** (0x55 touch, 0x18, 0x40, 0x34,
  0x6A, 0x60, SCCB camera). Luật: `i2c_master_get_bus_handle()` trước, chỉ `i2c_new_master_bus()` khi chưa
  có; tốc độ ≤ 400 kHz. Task touch quét bus này **mỗi 10 ms** ở prio 7.
- **I2S một bus, hai codec** (không liên quan Rapid4P trừ khi dùng loa beep).
- **SDMMC**: khe thẻ slot 0 và C5 slot 1 **chung một host** → lệnh SD xếp hàng sau lưu lượng WiFi.

### 1.4 Ràng buộc phần cứng "không thương lượng" (từ `AGENTS.md` §2–3, vẫn đúng cho Rapid4P)

1. GPIO22 reset **cả** panel lẫn touch; chỉ reset một lần trước khi mở DSI; toggle lại = màn chớp.
2. Panel **không xoay được bằng phần cứng**; UI logical 800×480 nhờ PPA xoay vùng bẩn (`display.c` khối
   `DSI_ROTATE`). Đổi hướng = đổi `BOARD_LCD_ROTATION`, không sửa `touch.c`/`display.c`.
3. **LVGL chỉ được gọi trong display task** hoặc dưới `display_lock()`; task input **không được chờ lock**
   (prio input 7 > LVGL 6); module khác đổi UI qua API `display_set_*`/`display_schedule`.
4. L2 cache line **64 B**; buffer DMA/PPA/DSI phải `heap_caps_aligned_alloc(64,…)` + `esp_cache_msync()`.
5. RTC RAM không vào heap; không `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`; `LWIP_MAX_SOCKETS=10`.
6. Kconfig chỉ qua `sdkconfig.defaults*` (ASCII thuần).
7. Console COM của CH343 đổi số theo lần cắm; mở cổng "chỉ nghe" phải thả DTR/RTS (`p4_readlog.py`).

---

## 2. Cấu trúc code `firmware-vimate-p4` (ESP-IDF 5.5.1, target `esp32p4`)

### 2.1 Cây thư mục và vai trò — đánh dấu tái dùng cho Rapid4P

| Đường dẫn | Vai trò | Rapid4P |
|---|---|---|
| `CMakeLists.txt` gốc | `SUPPORTED_TARGETS esp32p4`; workaround `-L` không bọc nháy của 3 component media; `spiffs_create_partition_image` | **Tái dùng** (bỏ khối media) |
| `main/CMakeLists.txt` | danh sách SRCS/REQUIRES; `esp_lcd_st7102` khai vô điều kiện | Tái dùng khung, cắt nguồn |
| `main/idf_component.yml` | ghim `lvgl ^9.2`, `esp_lvgl_port ~2.8.0` (2.9 cần IDF 5.5.2), `esp_wifi_remote 0.14.*`, `esp_hosted 2.12.*` (C5!), codec/sr/opus/mp4… | Giữ 5 dòng đầu, **bỏ** codec/esp-sr/opus/ws/mp4/jpeg |
| `main/Kconfig.projbuild` | `choice VIMATE_BOARD` (P4_43LCD…), `VIMATE_SERVER_BASE`, diag, SD cache | Đổi tên tiền tố → `RAPID4P_*` |
| `sdkconfig.defaults` + `.p4-43lcd` | CPU 360 MHz, LVGL 1 draw unit/16 ms, `LV_ATTRIBUTE_FAST_MEM_USE_IRAM`, **tắt `LV_USE_TJPGD`**, VFS 12, C5 SDIO, `CACHE_L2_CACHE_LINE_64B`, `ESP_MAIN_TASK_STACK_SIZE=10240`, `FATFS_VFS_FSTAT_BLKSIZE=16384`, `ALLOW_RTC_FAST_MEM_AS_HEAP=n` | **Tái dùng**, bỏ khối WakeNet/MP4 |
| `partitions/partitions.p4-43lcd.csv` | ota_0/ota_1 4,5 MB, emo_spiffs 5,375 MB, asset/lesson/model 512 K | **Viết bảng mới** (§6.3) |
| `components/esp_lcd_st7102/` | driver panel vendor (không có trên registry), đã bỏ lệnh đọc ID gây treo | **Tái dùng nguyên** |
| `main/boards/board.h` | chọn board theo Kconfig + `#ifndef` default mọi knob | Tái dùng |
| `main/boards/board_esp32p4_43lcd.h` | **toàn bộ pinout + knob** LCD/touch/audio/SD/WiFi/nút/LED/PMIC (§2.2) | **Tái dùng + thêm khối SENSOR** |
| `main/main.c` → `app_main.c` | boot orchestrator (§2.3), máy trạng thái thiết bị, `vimate_main_task` | Viết lại gọn theo Rapid4P |
| `main/vimate.h` | enum emotion/state, event bits, `g_vimate_server`, TAG log | Thay bằng `rapid4p.h` |
| `main/core/nvs_store.[ch]` | wrapper NVS namespace, `set/get_str/u32`, WiFi blob atomic, factory reset | **Tái dùng nguyên** |
| `main/core/wifi_mgr.c` (1536 dòng) + `captive_dns.c` + `ui/ui_wifi_setup.c` | STA + band lock 2,4 GHz + canh gác DHCP + SoftAP + DNS hijack + portal HTTP + 2 mã QR + máy trạng thái 5 bước | **Tái dùng** (thêm trường ID máy / ngưỡng vào portal) |
| `main/core/ble_wifi_prov.c` | stub trên P4 | Bỏ |
| `main/core/diagnostics.c`, `system_info.c`, `task_profile.h`, `telemetry.c` | `diag heap`/`diag rotate`, reset reason, MAC, heartbeat | Tái dùng diag + system_info; telemetry viết lại |
| `main/network/ota_client.c`, `ota_boot_validation*.c`, `http_dl.c` | OTA HTTPS 2 slot + rollback guard, tải file HTTP | **Tái dùng** — trỏ về `/ota/check` của Engineer Server |
| `main/network/ws_client.c`, `protocol/envelope.c`, `mcp_handler.c` | WebSocket + khung MCP `self.edu.*` | Bỏ |
| `main/ui/display.[ch]` (6758 dòng) | **HW init** (LDO DPHY → DSI → DPI, reset GPIO22, 2 fb, PPA xoay, backlight LEDC, display task + `sched_q`, icon WiFi/BT, `display_lock`) **+ toàn bộ UI VIMATE** (home, quiz, đồng hồ, mặt robot…) | **Tách**: giữ ~phần HW init + hàng đợi + backlight + icon WiFi; UI viết mới |
| `main/ui/face.c`, `ui_emotion.c`, `emo/`, `util/gif/` | mặt robot GIF | Bỏ |
| `main/ui/fonts/lv_font_vimate_{14,18,24,48}.c`, `lv_font_countdown_72.c` | font LVGL tiếng Việt | Tái dùng; **thiếu CJK** (§5) |
| `main/ui/ui_image.c`, `util/jpeg_to_image.c` | ảnh bài học, JPEG | Bỏ (trừ khi hiển thị logo JPG) |
| `main/input/touch.c` (703 dòng) | ST7123 reg16, map native→logical theo knob, quét 10 ms prio 7, tap/swipe/double-tap, feedback qua hàng đợi | **Tái dùng** (bỏ phần gọi `home_select` VIMATE) |
| `main/input/button.c` (76 dòng) | GPIO35 short/long (5 s → xoá WiFi) | Tái dùng, mở rộng GPIO0 |
| `main/audio/*` | ES8311/ES7210, AFE, Opus, WakeNet | Bỏ (tuỳ chọn giữ `vimate_es8311.c` tối giản cho beep) |
| `main/store/course_media_cache.c` | SDMMC mount + bounce buffer + bench | Tái dùng **phần mount** nếu log kết quả ra thẻ |
| `main/store/asset_pack.c`, `lesson_image_cache.c`, `emotion_sync.c` | SPIFFS cache | Bỏ |
| `main/media/*`, `actuator/servo_emotion.c` | MP4, servo | Bỏ |
| `scripts/build_p4_43lcd.bat/.sh`, `flash_p4_43lcd.bat`, `p4_readlog.py` | build → nạp → đọc UART (Windows cmd.exe; tự xoá `sdkconfig` cũ; `auto` tìm COM CH343) | **Tái dùng** đổi tên |
| `tools/*` | dựng GIF mặt robot, WAV dump | Bỏ |
| `AGENTS.md`, `README-P4.md`, `docs/HARDWARE-PINOUT.md` | luật + nhật ký + pinout | Chép `HARDWARE-PINOUT.md`; viết `CLAUDE.md` riêng cho `firmware/rapid4p/` |

### 2.2 HAL board — các knob `BOARD_*` đang có (P4) và khối cần thêm

Đang có trong `board_esp32p4_43lcd.h` (giá trị thật, đã đo):

```
LCD   : BOARD_LCD_USE_MIPI_DSI 1, DSI_LANES 2, DSI_LANE_MBPS 520, DSI_PHY_LDO_CHAN 3 / 2500 mV,
        NATIVE_W/H 480/800, ROTATION 270 → H_RES 800 / V_RES 480, DPI_CLK 37.8, PIN_RST 22,
        PIN_BL 6 (LEDC 5 kHz, ON_LEVEL 1), SWAP_BYTES 0
TOUCH : USE_ST7123 1, I2C_NUM_0 SCL 8 / SDA 7, INT 23, RST -1 (cố ý), ADDR 0x55, TASK_PRIO 7,
        SWAP_XY/MIRROR suy từ ROTATION
AUDIO : I2S MCLK 13 BCLK 12 WS 10 DIN 11 DOUT 9, PA 3 (HIGH), ES8311 0x18, ES7210 0x40, 24000 Hz
SD    : USE_SDMMC 1, SLOT 0, WIDTH 4, CLK 43 CMD 44 D0–D3 39–42, PWR_LDO_CHAN 4, FREQ 20000 kHz
WIFI  : BOARD_WIFI_BAND_2G_ONLY 1
BTN   : BOOT 35, STOP -1 (GPIO0 chưa dùng)
LED   : LED_GPIO -1, WS2812_GPIO 34 (tư liệu)
PMIC  : PMIC_I2C_ADDR 0x34, PMIC_IRQ_GPIO 21 (tư liệu)
UI    : UI_IMAGE_UNHOOK_MS 40, FACE_* (bỏ)
```

Khối **phải thêm** cho Rapid4P (đề xuất chân trên JP1 — chốt khi có schematic bo cảm biến):

```c
/* ===== Bo cảm biến quang 4 slot (JP1) — CHƯA CÓ SCHEMATIC, số chân là ĐỀ XUẤT ===== */
#define BOARD_SENSOR_I2C_NUM        I2C_NUM_1   /* bus RIÊNG, không dùng chung với touch */
#define BOARD_SENSOR_I2C_SDA        26          /* JP1 chân 12 */
#define BOARD_SENSOR_I2C_SCL        27          /* JP1 chân 13 */
#define BOARD_SENSOR_I2C_FREQ_HZ    100000      /* 100 R nối tiếp trên JP1 + dây FPC → đi chậm */
#define BOARD_SENSOR_MUX_ADDR       0x70        /* TCA9548A, A0–A2 = GND */
#define BOARD_SENSOR_TCS_ADDR       0x29        /* TCS34725 cố định, 4 con sau 4 kênh mux */
#define BOARD_SENSOR_SLOTS          4
#define BOARD_SENSOR_MUX_CHANNEL    {0, 1, 2, 3}
#define BOARD_SLOT_LED_GPIO         {28, 29, 30, 45}  /* JP1 14/15/16/11 — enable từng slot */
#define BOARD_SLOT_LED_PWM_GPIO     46          /* JP1 chân 9 — độ sáng chung, LEDC 5 kHz 8-bit */
#define BOARD_SLOT_LED_PWM_DEFAULT  127
#define BOARD_BTN_MEASURE_GPIO      0           /* BTN3, nhấn = LOW */
```

Thay thế được: nếu bo cảm biến **treo lên bus chung GPIO7/8** thì bỏ `SENSOR_I2C_*`, chỉ cần địa chỉ mux
`0x70` (không trùng 7 địa chỉ hiện có) — nhưng phải dùng chung `i2c_master_bus_handle_t` với touch (đang
quét 10 ms) và cộng thêm tải dung của 4 nhánh sau mux. Khuyến nghị **bus riêng I2C_NUM_1** (P4 có 2 bộ I2C
HP + 1 LP; GPIO matrix cho phép mọi chân).

### 2.3 Thứ tự boot (từ `app_main.c::vimate_app_start`) — giữ khung, thay ruột

```
nvs_flash_init (lỗi → erase + reinit, không panic) → nvs_store_init → diagnostics_log_boot
→ esp_event_loop + esp_netif → event group → diagnostics_start
→ display_init()            (LDO DPHY → reset cứng GPIO22 → DSI → DPI 2 fb → lvgl_port → backlight)
→ [audio_pipeline_init — BỎ] → button_init → touch_init (dùng lại bus I2C nếu đã có)
→ display_setup_ui() + display_set_state(BOOT)
→ ota_boot_validation_start(display_ok, audio_ok)
→ xTaskCreatePinnedToCore(main_task, 6144, prio NETWORK, core 0)
   main_task: system_info → SD mount (tuỳ) → SNTP → WiFi STA / SoftAP → OTA check → vòng sự kiện
```

Rapid4P chèn thêm **`sensor_init()`** (mở I2C_NUM_1, dò mux + 4 TCS34725, đọc calib/ngưỡng từ NVS) sau
`touch_init`, và một **task đo** riêng (core 1, stack ~4 KB, prio dưới touch) vì một chu trình đo 4 slot ×
3 lần mất **~34 s** (§3.5) — không được chạy trong display task.

### 2.4 Luật tài nguyên còn áp dụng (từ `AGENTS.md` §4) — Rapid4P nhẹ hơn nhiều

Cây vimate hết RAM nội vì AFE + WS + Opus (min 28–31 KB). Rapid4P bỏ hết ba thứ đó nên **RAM nội không còn
là nút cổ chai**; vẫn giữ: buffer > 4 KB vào PSRAM, không thêm task nếu ghép được, `readelf` PT_TLS chỉ khi
dùng `_Thread_local` (neo `g_vimate_tls_anchor` chỉ cần cho micro-opus — **bỏ được**).

### 2.5 Build / nạp / đọc log (máy Windows, cmd.exe)

```
scripts\build_p4_43lcd.bat            → BUILD_EXIT=0/1, build dir build_p4_43lcd/
scripts\build_p4_43lcd.bat COMxx      → build + nạp
scripts\flash_p4_43lcd.bat COMxx
python scripts\p4_readlog.py --list   / auto 60 boot.log   (thả DTR/RTS trước khi mở)
```
Từ Git Bash: `MSYS_NO_PATHCONV=1 cmd.exe /c "scripts\build_p4_43lcd.bat"`. Redirect log phải
`PYTHONIOENCODING=utf-8`. ESP-IDF 5.5.1; lên 5.5.3+ phải thêm lại `ESP32P4_SELECTS_REV_LESS_V3`.

---

## 3. FBT-ReaderPlus-1.0 — hiện trạng cần chuyển

### 3.1 Phần cứng ReaderPlus (từ `src/define.h`, `TCA9548.cpp`, `diagram.json`)

| Khối | Chân ESP32 | Ghi chú |
|---|---|---|
| ILI9341 SPI | SCK 18, MOSI 23, MISO 19, CS 22, DC 21, RST 17 | `Arduino_ESP32SPI` + `Arduino_ILI9341`, `setRotation(1)` → 320×240 ngang |
| Mux TCA9548A `0x70` | `Wire.begin(33 SDA, 32 SCL)` | `selectChannel(1<<ch)`; 4 kênh 0–3 |
| 4× TCS34725 `0x29` | sau mux | lớp `tcs34725` (`tcs.h`): auto-gain 5 bậc (60x/614 ms … 1x/154 ms), tính `lux` theo DN40 |
| LED chiếu slot | LED_1 25, LED_2 26, LED_3 27, LED_4 14 (enable) + PWM 12 (LEDC ch0, 5 kHz, 8-bit, mặc định 127) | `LED_on(ch)`: PWM + enable; `LED_off`: enable LOW + PWM 0 |
| Nút | RED 13, GREEN 16, WHITE 4 (INPUT_PULLUP, ngắt CHANGE, chống dội 50 ms, giữ 5 s) | RED = xuống/đo/setting(giữ), GREEN = chọn/đo lại, WHITE = lên/calib(giữ)/format |
| EEPROM | 512 B (partition `min_spiffs.csv`) | §3.4 |
| BT Classic | `BluetoothSerial` tên `ESP_READER-<efuse MAC>` | nhập SSID/pass/ID máy, nhập 5 ngưỡng |
| WiFi | `WiFiManager` AP `FBT_RAPID`, tiêu đề "Fortebiotech Rapid Setup", tham số `id_device`, 5 ngưỡng | `settingWifi/settingUpdate/settingThreshold` |
| Cloud | HTTPS POST Google Apps Script (`serverName` cứng trong `bluetooth.cpp`), NTP GMT+7 | payload §3.6 — lời gọi thực tế đang **bị comment** trong `screen_Average_Result` |
| Version | `FirmwareVer = "v1.0"` | không OTA thật (có include `HTTPUpdate` nhưng không gọi) |

### 3.2 Module và luồng

```
main.cpp  setup(): Serial → loadCredentialsFromEEPROM → WiFi.begin → _sensor.begin → _displayLCD.begin
          loop():  _displayLCD.loop()  (vẽ lại khi changeScreen)   → _sensor.loop() (đo/calib/format theo cờ)
button.cpp   ISR 3 nút → buttonProcess() sửa trực tiếp _displayLCD.type_infor / _sensor.flag*
sensor.cpp   read_All_Sensor → handle_All_Sensor → Average_All_Result; calib_Sensor; EEPROM
displayLCD.cpp  17 màn (e_statuslcd), Menu (menu.cpp) cuộn 3 item, font U8g2 theo ngôn ngữ
bluetooth.cpp   BT SPP nhập cấu hình, WiFiManager, POST Sheet, NTP
displayresources.h  4 bảng chuỗi (VI/EN/ZH/TW) × 50 màn (screen_type), icon bitmap
```

Đặc điểm phải "dịch" khi sang FreeRTOS: mọi thứ là **biến toàn cục + cờ + `delay()` blocking** (đọc 4 slot
chặn `loop()` ~11 s/lượt); ISR nút gọi thẳng hàm vẽ menu; không có mutex.

### 3.3 Máy trạng thái màn hình (`e_statuslcd`) và điều hướng bằng 3 nút

| Trạng thái | Màn | RED | GREEN | WHITE |
|---|---|---|---|---|
| `escreenStart` | logo + hướng dẫn | giữ 5 s → `e_setting` | → `echooseSample` | giữ 5 s → `ecalibSensor` |
| `echooseSample` | menu 5 mẫu (Tôm thẻ/Tôm sú/Cá rô phi/Heo/Nước) | xuống | chọn → `echooseTube` | lên |
| `echooseTube` | menu 5 bệnh (PC/EHP/EMS/WSSV/TPD) | xuống | chọn → `eprepare` | lên |
| `eprepare` | "đặt ống vào 4 khe" | → `ewaitingReadsensor` (counter=0) | — | — |
| `ewaitingReadsensor` | đang đo (3 vòng × 4 slot) | — | — | — |
| `escreenAverageResult` | bảng 4 slot: giá trị 0–3000 + `+`/`−` so ngưỡng bệnh | → `echooseSample` (+`flag_postData`) | đo lại → `eprepare` | — |
| `ecalibSensor` | calib slot hiện tại (Max rồi Min, 4 slot, xong `ESP.restart()`) | bắt đầu đo calib | huỷ → start | format calib (xoá EEPROM) |
| `e_setting` | menu Ngôn ngữ/WiFi/Update/Ngưỡng | xuống | chọn | lên |
| `e_language` | menu 4 ngôn ngữ | xuống | chọn → start | thoát |
| `e_settingWifi` / `e_settingUpdate` | chạy WiFiManager (blocking, restart) | | | |
| `e_setThreshold` | chọn cách nhập | → `THRESHOLD_SETTING_BLE` | → `THRESHOLD_SETTING_MANUAL` | thoát |
| `THRESHOLD_SETTING_MANUAL` | menu 5 bệnh + ngưỡng hiện tại | xuống / giữ 5 s → restart | chọn → `e_updateThreshold` | lên |
| `e_updateThreshold` | 4 chữ số, con trỏ `index` 105/145/185/225 | số −1 | số +1 / giữ 5 s → lưu EEPROM | chuyển chữ số |
| `THRESHOLD_SETTING_BLE` | chờ BT nhập 5 ngưỡng (blocking) | | | |

### 3.4 Bản đồ EEPROM (512 B) → khoá NVS tương ứng

| Địa chỉ | Nội dung | Kiểu | Khoá NVS đề xuất |
|---|---|---|---|
| 0–7 | `valueCalibMin[4]` | u16 ×4 | `cal_min` blob 8 B |
| 8–15 | `valueCalibMax[4]` | u16 ×4 | `cal_max` blob 8 B |
| 16–35 | ngưỡng 5 bệnh (`ADDR_THRESHOLD_POSITIVE(i)` = 16 + 4i), mặc định 600 | u32 ×5 (ghi u16!) | `thr_<sick>` u32 |
| 36 | ngôn ngữ (0 VI, 1 EN, 2 ZH, 3 TW) | u8 | `lang` u32 |
| 40–59 | ID máy | string | `device_id` |
| 60–94 / 95–129 | SSID / password | string | `nvs_store_set_wifi()` (blob atomic đã có) |
| 130–149 | ID BLE (không dùng) | | bỏ |
| 200–210 | cờ "đã khởi tạo" calib/threshold/slope/origin/lang/id | u16 | thay bằng "khoá tồn tại" |

Lưu ý lỗi kiểu trong bản gốc: `valueThreshold` là `uint16_t` nhưng EEPROM `put` số 600 dạng `int` 4 B và
`saveThresholdtoEEPROM` `put` u16 — chuyển sang NVS thì chuẩn hoá `u32`.

### 3.5 Thuật toán đo và hiệu chuẩn (giữ nguyên ngữ nghĩa khi port)

```
Đo:      3 vòng (numSampling) × { mỗi slot: LED_on → delay 1000 ms → chọn kênh mux
         → 3 mẫu (numSample) lux qua tcs34725::getData (auto-gain) }
         value = round(mean(lux) × 1000)
         result = clamp-map(value, calibMin..calibMax → 0..3000)        (valueMinsensor/valueMAXsensor)
         AverageResult[slot] = mean(3 vòng);  dương tính khi ≥ valueThreshold[sick]  (mặc định 600)
Thời gian: TCS 60x/614 ms → ~614 ms/mẫu → ~2,8 s/slot → ~11,4 s/vòng → ~34 s/lần đo 4 slot
Calib:   theo slot: LED_on → 620 ms → 5 mẫu lux → ×1000 → Max (lần 1) rồi Min (lần 2) → EEPROM;
         4 slot xong → restart. Format = ghi 0 vào 8 ô.
```

### 3.6 Payload cloud hiện tại (Google Apps Script `legacy/sheet/getData.js`)

```json
{"method":"append","sick":"EHP","value_sensor1":…,"value_sensor2":…,"value_sensor3":…,"value_sensor4":…,
 "data_IDdevice":"<id>","date":"DD-MM-YYYY HH:MM:SS","version":"v1.0"}
```

---

## 4. Bảng mapping ReaderPlus → board P4

### 4.1 Ngoại vi

| Chức năng | ReaderPlus | Trên board P4 | Mức | Việc phải làm |
|---|---|---|---|---|
| Màn hình | ILI9341 240×320 SPI, Arduino_GFX | ST7102 480×800 DSI, logical 800×480, LVGL | 🟢 có sẵn | Viết lại toàn bộ màn bằng LVGL (§4.3); tận dụng `display_init` + hàng đợi |
| Điều hướng | 3 nút cơ | Touch ST7123 + BTN3 GPIO0 + BOOT GPIO35 | 🟡 khác kiểu | Chuyển menu 3 nút → nút chạm LVGL; giữ GPIO0 làm "ĐO/xác nhận" cho tay ướt/găng |
| 4× TCS34725 + TCA9548 | `Wire(33,32)` | **Không có** — I2C_NUM_1 trên JP1 (26/27) hoặc bus chung 7/8 | 🔴 phải làm bo con | Driver `i2c_master` (IDF 5.x API mới, không Wire/Adafruit); port auto-gain DN40 từ `tcs.h` |
| LED chiếu 4 slot + PWM | 4 GPIO + LEDC | JP1 28/29/30/45 + LEDC trên 46 | 🔴 bo con | Driver `slot_led.c` (LEDC 5 kHz 8-bit) |
| LED trạng thái | (không có riêng) | WS2812B GPIO34 | 🟡 | Tuỳ chọn: driver `led_strip` (RMT) báo đang đo / kết quả |
| Loa/beep | không | ES8311 + NS4150B (PA GPIO3) | 🟡 | Tuỳ chọn beep khi xong; hoặc bỏ toàn bộ audio |
| Lưu cấu hình | EEPROM 512 B | NVS (`nvs_store`) | 🟢 | Map khoá §3.4 |
| WiFi STA | `WiFi.begin` | `wifi_mgr` qua C5 (2,4 GHz) | 🟢 | Không đổi |
| Cấu hình WiFi/ID | BT SPP **và** WiFiManager AP `FBT_RAPID` | SoftAP captive portal (không BT) | 🟡 | Bỏ nhánh BT; thêm ô `device_id` + 5 ngưỡng vào portal `wifi_mgr.c` |
| Thời gian | NTP `configTime` GMT+7 | `esp_netif_sntp` (đã có trong main_task) | 🟢 | |
| Cloud | POST Apps Script | `esp_http_client` (`http_dl.c` mẫu) | 🟡 | Đổi đích sang Engineer Server (§5) |
| OTA | không thật | `ota_client` 2 slot + rollback | 🟢 | Cấu hình `/ota/check`, khoá `product` |
| Log kết quả tại máy | `logdata` (chưa làm) | SD slot 0 (`course_media_cache` mount) | 🟢 | Ghi CSV/JSON ra `/sdcard/` khi mất mạng |
| Pin | không | AXP2101 | 🔴 chưa có driver | Cần nếu Rapid4P chạy pin; % pin/sạc qua I2C 0x34 |
| Camera / 4G / RS485 / IMU / DAC | — | có chân, chưa driver | ⚪ | Không dùng cho Rapid4P v1 |

### 4.2 Module code

| ReaderPlus | Tái dùng từ vimate-p4 | Viết mới cho `firmware/rapid4p/` |
|---|---|---|
| `main.cpp` (setup/loop) | `app_main.c` (khung boot §2.3) | `app_main.c` gọn: NVS → diag → display → button → touch → **sensor** → UI → OTA guard → main_task |
| `define.h` (pin, EEPROM map, debug macro) | `boards/board.h` + `board_esp32p4_43lcd.h` + `Kconfig.projbuild` | Khối `BOARD_SENSOR_*`/`BOARD_SLOT_LED_*` (§2.2); `rapid4p.h` (enum, event bits, TAG) |
| `TCA9548.cpp` | — | `sensor/tca9548.c` (`i2c_master_transmit` 1 byte mask) |
| `tcs.h` + Adafruit_TCS34725 | — | `sensor/tcs34725.c`: reg 0x80 ENABLE/ATIME/CONTROL, đọc CDATA/RDATA/GDATA/BDATA, auto-gain 5 bậc, DN40 lux |
| `sensor.cpp` (đo, calib, EEPROM) | `nvs_store.c` | `app/measure.c` (task đo + máy trạng thái đo/calib, gửi tiến độ qua `display_schedule`), `app/calib_store.c` |
| `button.cpp` (ISR + ticker + logic điều hướng) | `input/button.c` (short/long GPIO35), `input/touch.c` | `app/ui_flow.c`: máy trạng thái điều hướng **tách khỏi** ISR — nhận sự kiện (tap nút LVGL, GPIO0, long-press) qua queue |
| `displayLCD.cpp` + `menu.cpp` (17 màn, Arduino_GFX) | `ui/display.c` phần HW/hàng đợi/backlight/icon WiFi | `ui/ui_reader_*.c`: start, chọn mẫu, chọn bệnh, chuẩn bị, đang đo (tiến độ 4 slot), kết quả (4 dòng + `+/−`), calib, setting, ngôn ngữ, ngưỡng (bàn phím số) |
| `displayresources.h` (4 bảng chuỗi × 50 màn, icon XBM) | `ui/fonts/lv_font_vimate_*` | `ui/strings.c` (bảng chuỗi VI/EN/ZH/TW giữ nguyên nội dung), icon → PNG/LVGL image, **font CJK** (§5) |
| `bluetooth.cpp` (BT, WiFiManager, POST, NTP) | `core/wifi_mgr.c`, `captive_dns.c`, `ui/ui_wifi_setup.c`, `network/http_dl.c`, SNTP trong `app_main.c` | `net/result_upload.c` (JSON → Engineer Server, hàng đợi offline ra NVS/SD) |
| `platformio.ini` (Arduino 3.2.1, U8g2, Adafruit…) | `CMakeLists.txt`, `main/idf_component.yml`, `sdkconfig.defaults*`, `scripts/*.bat` | Bảng partition `partitions.rapid4p.csv`; đổi tên script |

### 4.3 Máy trạng thái UI — dịch từ 3 nút sang chạm

Giữ **cùng tập trạng thái** (`escreenStart … e_updateThreshold`) để giữ luồng người dùng đã quen, nhưng:

- Mỗi màn LVGL có nút chạm tương đương: `Xuống/Lên` → cuộn danh sách (list LVGL, vuốt được); `Chọn` → tap
  item; `Quay lại` → nút góc trên; `giữ 5 s` (Setting/Calib) → nút "Cài đặt"/"Hiệu chuẩn" trên màn Start
  (có thể giữ ràng buộc nhấn giữ GPIO0 5 s để vào calib nếu muốn khoá người dùng thường).
- Nhập ngưỡng 4 chữ số → `lv_keyboard`/`lv_spinbox` thay con trỏ `index` 105/145/185/225.
- `ewaitingReadsensor` thành màn tiến độ: slot đang đo, vòng x/3, thanh thời gian (~34 s).
- Kết quả 800×480 đủ chỗ hiện thêm **giá trị thô lux** và ngưỡng đang dùng (bản 240×320 chỉ in 0–3000).
- Màn WiFi/Update/Threshold-BLE (blocking WiFiManager/BT) → dùng máy trạng thái SoftAP 5 bước của
  `ui_wifi_setup.c` (đã có mã QR nối AP + QR mở portal).

### 4.4 Ngôn ngữ

ReaderPlus: 4 ngôn ngữ, font U8g2 `unifont_t_vietnamese2` / `unifont_t_chinese4` (bitmap 16 px). P4:
`lv_font_vimate_14/18/24/48` chỉ có dải Latin + tiếng Việt. Chuỗi ZH/TW trong `displayresources.h` dùng
đúng **107 chữ Hán riêng biệt** (đếm bằng script, 2026-09-17; bảng TW hiện đang chép y bảng ZH) → sinh font
LVGL **theo đúng tập ký tự** (lv_font_conv `--symbols`) cỡ 24/32 px là đủ nhỏ (vài chục KB), không cần nhúng
cả bảng CJK.

---

## 5. Khoảng trống và quyết định cần chốt (theo thứ tự chặn)

> **Cập nhật 2026-09-17 — đã tiến hành mục 1→6** (cây `firmware/rapid4p/` build `BUILD_EXIT=0`, app
> 1,97 MB / slot 3 MB; registry `rapid4p` thêm vào `system/products.yaml`, `registry_check` ĐẠT):
> 1 → chốt **I2C_NUM_1 riêng trên JP1** (GPIO26/27 SDA/SCL, LED enable 28/29/30/45, PWM 46) làm
> đề xuất trong `board_esp32p4_43lcd.h`; đổi khi có schematic. Nguồn LED chưa chốt (mục b/c vẫn mở).
> 2 → **bỏ hẳn BT**; portal SoftAP thêm ô *Mã máy* + *Token* (`core/wifi_mgr.c`).
> 3 → **Engineer Server**: `network/ota_client.c` (`/ota/check`, esp_https_ota, rollback guard) +
> `network/result_upload.c` (POST JSON `slot_*` 4 phần tử, hàng đợi offline NVS 16 bản). Contract JSON
> vẫn TODO P2. 4 → registry: khoá `rapid4p`, `chip: esp32p4`, `build_system: idf` (schema +
> `registry_check.py` mở rộng), `id_prefix: R4P` TODO xác nhận ERP. 5 → **chạy pin chưa quyết** →
> chưa viết AXP2101 (chỉ ghi chân). 6 → giữ `BOARD_LCD_ROTATION 270` (USB bên phải) tới khi cơ khí
> chốt. Mục 7–9 chưa động.

1. **Bo cảm biến 4 slot chưa có schematic.** Chốt: (a) cắm JP1 với I2C_NUM_1 riêng (khuyến nghị) hay treo bus
   chung GPIO7/8; (b) nguồn LED chiếu lấy `VCC3V3` JP1 chân 4 hay `BOOST_5V` chân 2 (nếu 5 V thì phải bật
   GPIO20 **và đo** vì chia áp hồi tiếp gợi ý ~2,6 V); (c) dòng 4 LED có vượt ngân sách DCDC1 của AXP2101 khi
   màn 4,3" + đèn nền 39 mA đang chạy không.
2. **Không có Bluetooth.** Toàn bộ nhánh BT SPP của ReaderPlus (`connectWIFI`, `settingBLE_ThresholdPositive`)
   phải bỏ; app điện thoại (nếu có) chuyển sang portal HTTP hoặc qua server. Cần xác nhận không có quy trình
   nhà máy nào đang phụ thuộc BT.
3. **Đích cloud.** Google Apps Script vẫn sống (`legacy/sheet`, `status: legacy_active`) nhưng sản phẩm mới nên
   POST thẳng Engineer Server theo hợp đồng trong `system/contracts/` (chưa có contract cho reader —
   `products.yaml › reader.payload.contract: null`). Nhớ luật server: **route mới không được là POST catch-all
   nuốt** → dùng đường đã có hoặc PUT.
4. **Registry.** Thêm sản phẩm vào `system/products.yaml` trước khi mở cây code (luật #2 CLAUDE.md gốc):
   khoá đề xuất `rapid4p`, `chip: esp32p4`, `status: dev`, `id_prefix` (TODO), `channels.optical_slots: 4`,
   `firmware.dir: firmware/rapid4p`, `image_name: "rapid4p_v{ver}.bin"`, `embeds_image_tag: true`,
   `tag_prefix: fw/rapid4p/`, `endpoints: [engineer_server]`. Chạy `python tools/registry_check.py`.
5. **Nguồn pin.** Rapid4P cầm tay chạy pin hay để bàn cắm USB? Nếu pin → cần driver AXP2101 (đọc % pin,
   sạc, tắt nguồn mềm) — chưa ai viết; board không có ADC pin.
6. **Hướng cầm máy.** `BOARD_LCD_ROTATION 270` = USB-C bên phải; đổi 90 nếu cơ khí Rapid4P đặt cổng bên trái.
   Chưa có mắt người xác nhận chiều xoay + chạm trên board này (README-P4 §7).
7. **Nâng slave C5 lên 2.12.x** chỉ bằng OTA qua SDIO, không đường cứu — quyết trước khi ra lô.
8. **Bảng partition riêng** (§6.3) — đổi bảng = nạp USB một lần, OTA không tự chuyển; chốt trước máy đầu tiên.
9. **Tần số calib/threshold:** giữ ngữ nghĩa 0–3000 và ngưỡng mặc định 600 (ReaderPlus) hay 500
   (`POSITIVE_THRESHOLD` define chưa dùng / reader v2.6.6 dùng 500)? Phải hỏi người sở hữu thuật toán.

---

## 6. Đề xuất cây `firmware/rapid4p/`

### 6.1 Kế thừa nguyên (copy có ghi nguồn)

```
components/esp_lcd_st7102/           main/boards/{board.h, board_esp32p4_43lcd.h}   docs/HARDWARE-PINOUT.md
main/core/{nvs_store, wifi_mgr, captive_dns, diagnostics, system_info, task_profile.h}
main/network/{ota_client, ota_boot_validation, ota_boot_validation_policy, http_dl}
main/input/{touch.c, button.c}       main/ui/fonts/*                                 main/ui/ui_wifi_setup.c
scripts/{build,flash}_p4_43lcd.bat, p4_readlog.py     sdkconfig.defaults, sdkconfig.defaults.p4-43lcd
```

### 6.2 Viết mới

```
main/rapid4p.h                 enum trạng thái máy, event bits, TAG
main/app_main.c                boot §2.3 (không audio/WS)
main/sensor/{tca9548.c, tcs34725.c, slot_led.c, sensor_bus.c}
main/app/{measure.c, calib_store.c, ui_flow.c}
main/ui/{display.c (rút gọn từ vimate: HW init + queue + backlight + icon WiFi), ui_reader_*.c, strings.c}
main/net/result_upload.c       JSON → Engineer Server, hàng đợi offline
partitions/partitions.rapid4p.csv
CLAUDE.md                      luật riêng phần (kế thừa AGENTS.md §2–4 của vimate-p4)
```

### 6.3 Bảng partition đề xuất (16 MB, app nhẹ hơn nhiều: không sr/opus/mp4)

```
nvs        0x9000   0x6000
otadata    0xf000   0x2000
phy_init   0x11000  0x1000
ota_0      0x20000  0x300000    (3 MB — app LVGL + WiFi remote ước ~1,5–2 MB)
ota_1               0x300000
assets     data spiffs 1M       (font CJK, logo, icon)
results    data spiffs 2M       (log kết quả offline; hoặc ra thẻ SD)
```

### 6.4 Checklist bring-up Rapid4P (theo tiêu chuẩn "xong" của cây P4)

1. `BUILD_EXIT=0`, nạp, log ≥ 60 s không `task_wdt`/`Guru Meditation`/`stack overflow`.
2. Log có `DSI rotate: logical 800x480 -> panel 480x800`, `Touch ST7123 init OK`, `diag heap internal` ổn.
3. `I2C1 probe: TCA9548 0x70 OK` + 4× `TCS34725 id=0x44/0x4D` sau từng kênh mux.
4. Bật từng LED slot → mắt thấy; đọc lux tăng rõ so với tắt (số đo ghi vào README).
5. Chu trình đo 4 slot ×3 vòng ≈ 34 s **không làm UI khựng** (touch vẫn ăn trong lúc đo).
6. Calib Max/Min 4 slot lưu NVS, còn sau reset; format xoá được.
7. Portal SoftAP nhập WiFi + ID máy + ngưỡng; STA lên `Got IP` (2,4 GHz).
8. POST kết quả lên server test (`server/scripts/localtest.ps1`) đúng contract; mất mạng → xếp hàng, có mạng
   gửi bù.
9. OTA từ Engineer Server sang slot kia + rollback khi bản mới không xác nhận boot.
