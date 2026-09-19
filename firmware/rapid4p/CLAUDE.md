# CLAUDE.md — firmware/rapid4p (Forte Rapid4P — RAPID READER **5** SLOT, ESP-IDF, **hai board**)

> **Số khe = `BOARD_SENSOR_SLOTS`** (board header) — 5 từ 2026-09-17 (`docs/plan/rapid4p-5-slot.md` ở gốc repo). Mọi mảng,
> bố cục LCD, chuỗi `%d`, payload `slots:N`, dashboard, registry `optical_slots` suy từ đó; KHÔNG hard-code 4/5.
> **Hai board từ 2026-09-19** (một mã nguồn): `p4_43lcd` = ESP32-P4C5 + LCD 4.3" DSI (khoá `rapid4p`) ·
> `s3_28lcd` = ES3N28P ESP32-S3 + LCD 2.8" SPI (khoá **`rapid4p-s3`**, `variant_of` trong registry — kho OTA tách).
> Khoá `rapid4p` chưa phát hành; đổi tên thương mại = quyết định Q1 còn mở.

> Luật toàn hệ thống ở `CLAUDE.md` gốc. File này: **luật + sự thật phần cứng + chỉ mục** của
> cây rapid4p. Khảo sát gốc và mọi quyết định: `firmware/maping new product/MAPPING-Rapid4P.md`.
> Cây tham chiếu (chỉ đọc, KHÔNG sửa): `firmware/maping new product/firmware-vimate-p4/` —
> `AGENTS.md` + `README-P4.md` ở đó là nhật ký bring-up của board P4 (mọi số đo); cùng cây có
> `main/boards/board_esp32s3_28lcd.h` + nhánh ILI9341/FT6236 trong `display.c`/`touch.c` = nguồn board S3.
> Học từ dự án ngoài (xiaozhi-esp32 P4/DSI/AXP2101/ML307, CrossInk quy trình/heap/simulator):
> `firmware/maping new product/THAM-KHAO-xiaozhi-CrossInk.md` §3 = danh sách việc đề xuất; cây đã
> chép ở `…/tham-khao/` (MIT, chỉ đọc — chép code sang phải giữ dòng bản quyền).

## 0. Trạng thái (2026-09-19)

- **Build**: cả hai board `BUILD_EXIT=0` trên ESP-IDF **5.5.1** (5.5.4 cũng được): `build_p4_43lcd/rapid4p.bin`
  **2,08 MB** · `build_s3_28lcd/rapid4p-s3.bin` **2,00 MB** / slot 3 MB.
- **P4 đã nạp máy thật 2026-09-17** (COM7, ESP32-P4 rev **v1.3**, chưa có bo cảm biến): log 60 s sạch, DSI/touch/WiFi
  qua C5/portal chạy, 13 màn đã xem qua webcam. Mọi thứ thuộc `sensor/*`, `app/measure.c` vẫn chưa có bằng chứng phần cứng.
- **S3 2.8" CHƯA nạp máy** (2026-09-19 mới build): LCD/touch/UI thang nhỏ/chân cảm biến ĐỀ XUẤT đều chờ bo ES3N28P.
  Tiêu chuẩn "xong" §4.
- Kế thừa từ vimate-p4 (đã chạy thật): `components/esp_lcd_st7102`, `boards/`, `core/{nvs_store,
  system_info, captive_dns, wifi_mgr}`, `ui/ui_wifi_setup.c`, `ui/fonts/*`, khối HW-init +
  xoay PPA của `ui/display_hw_dsi.c`, giao thức reg16 của `input/touch.c`, `scripts/readlog.py`; từ vimate (S3):
  nhánh SPI ILI9341 (`ui/display_hw_spi.c`), FT6236 (`input/touch.c`), `board_esp32s3_28lcd.h`, knob sdkconfig S3.
- Viết mới: `sensor/*`, `app/*`, `ui/ui_reader.c`, `ui/ui_strings.c`, `network/*`,
  `input/button.c`, `app_main.c`, `rapid4p.h`, partition, sdkconfig, scripts, `ui/display.c` (chung) + `display_hw.h`.

## 1. Điều hướng nhanh

| Khu vực | Đọc trước | Sở hữu |
|---|---|---|
| **Chọn board** | `CMakeLists.txt` (gốc), `main/boards/board.h`, `main/Kconfig.projbuild`, `sdkconfig.defaults{,.p4_43lcd,.s3_28lcd}` | `-DR4P_BOARD=` → profile sdkconfig, `SDKCONFIG` trong build dir, `DEPENDENCIES_LOCK` theo target, tên `project()` = khoá sản phẩm; Kconfig choice mỗi target 1 board; `board.h` `#ifndef` mặc định mọi knob mới |
| Pinout + knob P4 | `main/boards/board_esp32p4_43lcd.h`, `docs/HARDWARE-PINOUT.md` §1–14 | mọi chân/địa chỉ/tần số; khối `BOARD_SENSOR_*` / `BOARD_SLOT_LED_*` (JP1, **đề xuất** — chưa có schematic bo IF); `BOARD_PRODUCT_KEY "rapid4p"`, `BOARD_HW_VERSION "P4C5-43"` |
| Pinout + knob S3 2.8" | `main/boards/board_esp32s3_28lcd.h`, `docs/HARDWARE-PINOUT.md` §15 | LCD SPI3/ILI9341, touch FT6236 I2C0, BOOT GPIO0; **chân bo cảm biến ĐỀ XUẤT** từ 10 GPIO trống (I2C1 41/40, LED 2/9/14/21/38, PWM 39, ĐO 47); `BOARD_PRODUCT_KEY "rapid4p-s3"`, `BOARD_HW_VERSION "S3-28"` |
| **Kiến trúc HW ghép bo có sẵn** | `docs/HARDWARE-ARCHITECTURE.md` | 4 bo: P4C5 · **`Rapid4P-IF`** (bo giao tiếp, MỚI) · bo LED · bo cảm biến (ReaderPlus/ReaderMax, netlist đọc từ KiCad + EasyEDA). Quyết định D1–D6 (bỏ LDD-1200L → R + MOSFET từ 5 V; 1 nguồn 5 V), 5 phép đo §7 phải làm trước khi vẽ bo IF, bring-up 4 khe trước rồi rev 5 khe. Bo S3 cũng cần bo IF tương đương |
| Boot | `main/app_main.c` | thứ tự init, main task (event bits → ui_reader), SNTP TZ `ICT-7`, diag 60 s |
| Kiểu chung | `main/rapid4p.h` | `R4P_FW_VERSION` (registry đọc regex ở đây, chung 2 khoá), `R4P_PRODUCT_KEY`/`R4P_HW_VERSION` = `BOARD_*`, enum bệnh/mẫu/ngôn ngữ, event bits, `g_r4p_cfg` |
| Màn hình chung | `main/ui/display.c`, `display_hw.h` | đèn nền LEDC ch0/timer0, ngủ màn, `lvgl_port_init` (core UI prio 6), hàng đợi + display task; `display_init()` = LEDC → lvgl_port → `display_hw_init` → BL 100 % → queue |
| Màn hình HW P4 | `main/ui/display_hw_dsi.c` | LDO DPHY → reset GPIO22 → DSI → DPI 2 fb → `lvgl_port_add_disp_dsi` → PPA xoay 270°; mọi chú thích "vì sao" giữ nguyên văn |
| Màn hình HW S3 | `main/ui/display_hw_spi.c` | SPI3 40 MHz → ILI9341 (BGR, invert) → `lvgl_port_add_disp` 2 buffer × `BOARD_LCD_DRAW_BUF_LINES` 40 dòng RAM nội DMA, swap_bytes, xoay MADCTL (swap_xy) |
| Màn hình app | `main/ui/ui_reader.c`, `ui_strings.[ch]` | máy trạng thái 13 màn (theo ReaderPlus). Bố cục header · content · footer = thanh hành động (Quay lại/Huỷ LUÔN trái, hành động chính LUÔN phải teal, phá huỷ đỏ giữa + hộp thoại `confirm_show`). **Mọi kích thước qua token `UI_*` của `ui_theme.h`** — 2 thang: 4.3" (56/96/72/12, chữ 24/18/14, số khe 48) và 2.8" (`UI_SCALE_SMALL`: 32/52/44/6, chữ 18/14, số khe 24 tự hạ qua `fit_font_from`); 2.8" header chỉ icon WiFi + số chờ gửi, IP/version xuống màn chính. Token màu/font = bản C của `system/brand/tokens.json` (`typography.scales`); `ui_theme_brand_bar()`, `mk_btn(icon, txt)` tự co font theo token, `mk_chip` |
| Logo | `main/ui/ui_logo.c` | Logo FORTE BIOTECH **vẽ vector** (5 `lv_draw_triangle` + 2 label) theo toạ độ file chuẩn 2000×1780 — không dùng PNG. `ui_logo_create(parent, h, with_text)`; chữ chỉ đọc được khi h ≥ ~90 (2.8": mark 56 px không chữ). Góc trái header mọi màn mark `HEADER_LOGO_H` |
| Chạm | `main/input/touch.c` | 2 nhánh `#if BOARD_TOUCH_USE_ST7123` (reg16) / `BOARD_TOUCH_USE_FT6236` (reg8: 0x02 số điểm, 0x03..0x06 XY, RST xung 10/300 ms) → cùng `lv_indev` (đọc trong LVGL task); xoay native→logical qua `BOARD_TOUCH_SWAP_XY/MIRROR_*` |
| Nút | `main/input/button.c` | BOOT (tap/giữ 5 s), ĐO — GPIO theo board (`BOARD_BTN_*`, −1 = không có) |
| Cảm biến | `main/sensor/{sensor_bus,tca9548,tcs34725,slot_led}.c` | I2C riêng `BOARD_SENSOR_I2C_NUM` (tái dùng bus nếu đã có), mux 0x70 kênh 0..N−1, TCS 0x29 ×N (AGC riêng từng slot), LED enable ×N + PWM LEDC ch1 chung |
| Đo / calib | `main/app/measure.c`, `calib_store.c` | task đo core 1 prio 4 (~34 s/chu trình), map 0..3000, ngưỡng, NVS |
| Mạng | `main/network/{engineer_api,ota_client,result_upload}.c` | Bearer token từ NVS, `/ota/check?product=<khoá board>&hw=<board>` + esp_https_ota + rollback guard, POST kết quả + hàng đợi offline NVS |
| **Web dashboard** | `main/network/dashboard.c`, `main/web/dashboard.html` (nhúng `EMBED_TXTFILES`), `scripts/dash_mock.py` | Học theo Rapid+ nhưng ESP-IDF: `esp_http_server` :80, client **poll `GET /api/state` 1 s**, 3 tab, nút web = nút vật lý (`POST /api/control?btn=measure\|back`), `POST /api/threshold`. Lazy start khi STA có IP; `dashboard_suspend/resume` do `wifi_mgr_start/stop_provisioning` gọi. Độc lập màn hình (dùng chung 2 board). Test không cần máy: `python scripts/dash_mock.py 8790` |
| Dev console | `main/core/dev_console.c`, `scripts/uicmd.py` | REPL UART0 `r4p> `: `ui`/`btn`/`heap`. `CONFIG_RAPID4P_DEV_CONSOLE` — TẮT ở bản phát hành |
| WiFi | `main/core/wifi_mgr.c` (+ `captive_dns.c`, `ui/ui_wifi_setup.c`) | chỉ `esp_wifi.h` → chạy cả remote (P4 qua C5) lẫn native (S3); SoftAP `FBT-Rapid4P-XX:XX` + portal (ô **Mã máy**, **Token**). Màn `ui_wifi_setup.c`: 4.3" 2 thẻ QR cạnh nhau (QR 200); **2.8" MỘT thẻ bố cục hàng** (QR 128 trái, chữ phải), tự hiện thẻ ② khi có client bám AP, chạm thẻ để đổi; chip header chỉ icon. Chuỗi vẫn tiếng Việt cứng |
| Build | `CMakeLists.txt`, `main/CMakeLists.txt`, `main/idf_component.yml`, `sdkconfig.defaults*`, `partitions/partitions.rapid4p.csv`, `scripts/build.bat` | ghim lvgl `~9.6.0`, esp_lvgl_port `~2.8.0`; P4: esp_hosted `2.12.*`, esp_wifi_remote `0.14.*` (`rules: target == esp32p4`); S3: `esp_lcd_ili9341 ^2.0.0` (`rules: target == esp32s3`) |

## 2. Phần cứng — sự thật

### 2.1 P4C5 + LCD 4.3" (từ cây vimate-p4, đã đo)

- ESP32-P4 rev v1.3, flash 16 MB, PSRAM 32 MB. **Không radio**: WiFi qua ESP32-C5 (ESP-Hosted
  SDIO slot 1), **không BT/BLE**, khoá 2,4 GHz (5 GHz không DHCP được).
- LCD ST7102 native **dọc 480×800**, UI logical **800×480** (`BOARD_LCD_ROTATION 270`, PPA).
  **GPIO22 = LCD_RST = TP_RST**: chỉ `display_hw_dsi.c` toggle, một lần trước DSI.
- Đèn nền GPIO6 pull-up → sáng mặc định; LEDC timer 0 / ch 0. **LED slot dùng timer 1 / ch 1.**
- Bus I2C chung GPIO7/8 có 7 thiết bị; touch 0x55. **Bo cảm biến đi I2C_NUM_1 riêng (JP1
  GPIO26/27 — đề xuất)**; đổi sang bus chung chỉ cần sửa `BOARD_SENSOR_I2C_*`.
- Nút: BOOT GPIO35 (chung DTR CH343 — chỉ xung), BTN3 GPIO0. WS2812 GPIO34, AXP2101 0x34 IRQ 21
  **chưa có driver** (pin/sạc chưa đọc được).
- JP1 còn trống sau khi gán cảm biến 5 khe: GPIO48/49/50 (GPIO47 = LED khe 5, đề xuất).
- **Bo LED + bo cảm biến đã có** (thế hệ ReaderPlus/ReaderMax, ngoài repo `01. EngineerHub/03.RapidReaderMax/02.HardwarePCB/`):
  I2C 4 dây GND-3V3-SCL-SDA (TCA9548A 0x70, pull-up 10 K trên bo), LED 5 dây VOUT+ chung + LIGHT1..4 sink, pitch khe
  13,545 mm; bo main ESP32 cũ nuôi chúng bằng 12 V + LDD-1200L + S8050 — P4C5 **không có** các khối đó →
  `docs/HARDWARE-ARCHITECTURE.md`.

### 2.2 ES3N28P + LCD 2.8" (từ vimate s3-28lcd, đã chạy thật ở dự án đó; Rapid4P CHƯA nạp)

- ESP32-S3 N16R8: flash 16 MB QIO 80 MHz, PSRAM 8 MB **Octal** 80 MHz, WiFi 2,4 GHz nội (BT tắt), CPU 240 MHz.
- LCD ILI9341 SPI3 (SCLK12/MOSI11/MISO13/CS10/DC46/BL45, RST nối reset hệ thống), native dọc 240×320 →
  logical **320×240** bằng MADCTL (`SWAP_XY 1`, mirror 0/0), BGR + invert + swap_bytes.
- Touch FT6236G I2C0 SCL15/SDA16 0x38, INT17, **RST18 phải xung LOW 10 ms → HIGH 300 ms** trước khi nói I2C.
  Chạm: swap theo màn, mirror_x theo màn, **không** mirror_y (vimate đo trên board).
- Nút: chỉ BOOT GPIO0. ĐO = GPIO47 (ĐỀ XUẤT, nút ngoài). LED đơn GPIO42 (chưa driver). Không PMIC/pin/SD.
- GPIO trống: 2, 9, 14, 21, 38, 39, 40, 41, 47, 48 → cảm biến I2C1 SDA41/SCL40, LED 2/9/14/21/38, PWM 39
  (**ĐỀ XUẤT** — chưa có schematic/header thật, `docs/HARDWARE-PINOUT.md` §15). Tránh 3 (strap), 19/20 (USB),
  26–37 (flash/PSRAM), 43/44 (UART0), 45/46 (strap, đã dùng cho BL/DC).
- Console UART0 (43/44) + USB-Serial/JTAG phụ; `readlog.py auto` KHÔNG tìm được (chỉ tìm CH343 của P4) → ghi COM.

## 3. Luật riêng cây này

1. Mọi chân/địa chỉ/tần số qua `BOARD_*` (include `boards/board.h`). Thiếu knob → thêm vào
   board header kèm nguồn (schematic § hoặc số đo + ngày) **và `#ifndef` mặc định trong `board.h`** để board kia không vỡ.
2. **LVGL chỉ trong LVGL task / display task hoặc dưới `display_lock()`.** Task đo, mạng, nút →
   `display_schedule(cb, arg)`. Callback sự kiện LVGL muốn đổi màn → `show_async()` (không xoá
   widget đang phát sự kiện trong chính callback của nó).
3. Task đo (`measure.c`) **không bao giờ** gọi lv_*; báo tiến độ qua callback → ui_reader bọc
   `display_schedule` với bản sao `measure_result_t` (malloc, free trong apply).
4. Khoá payload/NVS (`"PC"`, `"EHP"`, `thr_PC`, `cal_min`…) **không đổi, không dịch** —
   `r4p_sick_name()`; chuỗi hiển thị qua `ui_strings.c`.
5. Payload lên Engineer Server **không dùng tên mảng** `result`/`CT_value`/`record_out`/
   `amplification` (server bắt 10 phần tử cho Rapid+). Dùng `slot_*` N phần tử + `slots: N`
   (server `validate` kiểm `len == slots`; hợp đồng `system/contracts/ingest-rapid4p.schema.json`).
6. **Không có bí mật trong repo/firmware**: token server nhập qua portal → NVS `api_token`, không
   log giá trị. `sdkconfig.defaults*` ASCII thuần.
7. `R4P_FW_VERSION` đổi = đổi ở `rapid4p.h` + tag `fw/rapid4p/vX.Y.Z` **và `fw/rapid4p-s3/vX.Y.Z`** (hai khoá, cùng số)
   **+ `project(… VERSION x.y.z)` trong `CMakeLists.txt`** (không chữ `v`); chạy `python tools/registry_check.py`.
   Vì sao: ảnh ESP-IDF tự mang `esp_app_desc_t` ở offset `0x20` (`version[32]` @`0x30`, `project_name[32]` @`0x50`,
   magic `0xABCD5432`) — `project_name` = **khoá sản phẩm theo board** (`rapid4p` / `rapid4p-s3`, CMake đặt theo
   `R4P_BOARD`); server dùng nó làm thẻ nhận dạng ảnh (`server/docs/plan/ota-quan-ly-may-nhieu-san-pham.md` §2.7).
   Kiểm nhanh: `python -c "b=open('build_s3_28lcd/rapid4p-s3.bin','rb').read();print(b[0x30:0x50],b[0x50:0x70])"`.
8. Sửa `sdkconfig.defaults*` → build.bat tự xoá `build_<board>/sdkconfig` (so mtime). Không sửa `sdkconfig` sinh ra.
   Knob chung vào `sdkconfig.defaults`, knob theo chip/board vào `sdkconfig.defaults.<board>`.
9. **Kích thước UI qua token `UI_*` (`ui_theme.h`), không ghi px trong `ui_reader.c`/`ui_wifi_setup.c`.** Thêm token =
   thêm vào CẢ HAI thang; đổi số ở thang 4.3" phải giữ đúng giá trị đã duyệt qua webcam 2026-09-17.
10. **Thêm board mới** = header `boards/board_<x>.h` + mục Kconfig choice (`depends on IDF_TARGET_*`) + `sdkconfig.defaults.<x>`
    + `display_hw_<x>.c` nếu panel khác + khoá registry riêng (`variant_of: rapid4p`) + dòng `build.bat`. Không sửa driver chung.

## 4. Build / nạp / log (Windows, cmd.exe — ESP-IDF tại `C:\Espressif`)

```
scripts\build.bat                    → p4_43lcd, BUILD_EXIT=0/1 (build dir build_p4_43lcd/)
scripts\build.bat s3_28lcd [COMx]    → S3 2.8", build_s3_28lcd/rapid4p-s3.bin (+ nạp)
scripts\build.bat COMxx              → (tương thích cũ) p4_43lcd + nạp
scripts\flash.bat [board] COMxx
python scripts\readlog.py --list | auto 60 boot.log   (auto = CH343 bo P4; bo S3 ghi COM; thả DTR/RTS trước khi mở cổng)
python scripts\uicmd.py COM7 "ui settings" "btn do"   (dev console: ui <0..12|tên|confirm> · btn do|boot · heap · help)
```
**Xem màn LCD từ máy dev (không cần người chạm)**: dev console `core/dev_console.c`
(`CONFIG_RAPID4P_DEV_CONSOLE`, tắt ở bản phát hành) + webcam qua **ffmpeg dshow** — trình duyệt
tích hợp của Claude CHẶN camera (`NotAllowedError`), ffmpeg thì được:
`ffmpeg -f dshow -video_size 1280x720 -vcodec mjpeg -i video="Integrated Webcam" -ss 1.2 -frames:v 1 -update 1 -vf "crop=640:260:440:140,scale=1280:-1" shot.jpg`
(crop theo vị trí máy trên bàn 2026-09-17 — chỉnh lại). Vòng lặp: `uicmd ui N` → chụp → xem.
Từ Git Bash: `MSYS_NO_PATHCONV=1 cmd.exe /c "scripts\build.bat s3_28lcd"`. Lần đầu mỗi target cần mạng để
component manager kéo component (~2 phút); lock riêng `dependencies.lock.<target>` (track git).
Script tự đọc id bản ESP-IDF đang chọn từ `C:\Espressif\esp_idf.json`; muốn bản khác đặt `R4P_IDF_ID=esp-idf-<hash>`.
Bản đó phải có toolchain của target (xem `targets` trong `C:\Espressif\idf-env.json`; box ADM 5.5.1 có cả s3 + p4).

**Tiêu chuẩn "xong" khi có board P4**: `BUILD_EXIT=0` → nạp → log ≥ 60 s không `task_wdt`/`Guru
Meditation`/`stack overflow`; có `DSI rotate: logical 800x480 -> panel 480x800`, `Touch ST7123
init OK`, `sensor bus I2C1`, `slot n (mux ch): TCS34725 OK` ×N, `measure task san sang, N/N` (N=5),
`diag heap internal=`; chu trình đo 34 s mà chạm vẫn ăn.
**Tiêu chuẩn "xong" bo S3 2.8"** (chưa đạt — chưa có bo): `board=rapid4p_s3_ili9341_lcd28`, `Init SPI ILI9341 panel 240x320 … logical 320x240`,
`Touch FT6236 chip id=0x64`, `Touch FT6236 init OK`, `Button init BOOT=GPIO0 DO=GPIO47`, `sensor bus I2C1 SDA=41 SCL=40`,
`TCA9548 … ESP_ERR_INVALID_STATE` / `0/5` (chưa cắm bo con — đúng), SoftAP `FBT-Rapid4P-XX:XX` hoặc STA Got IP, `r4p.dash: dashboard http://<ip>/`,
log 60 s sạch; chạm đúng vị trí (không lệch trục — nếu lệch chỉnh `BOARD_TOUCH_SWAP_XY/MIRROR_*`); 13 màn + màn WiFi xem qua webcam:
5 ô vừa một hàng, số 4 chữ số không tràn ô 57 px, nút footer 80/100/116 px không tràn chữ, QR 128 px quét được.

## 5. Bẫy đã biết

| Triệu chứng | Nguyên nhân → xử |
|---|---|
| `unknown type name 'r4p_lang_t'` ở mọi file | header tên `strings.h` trong `ui/` (INCLUDE_DIRS) che `<strings.h>` hệ thống → include vòng. **Không đặt tên header trùng libc** (đã đổi `ui_strings.h`) |
| `ESP_ERR_WIFI_NOT_CONNECT undeclared` | cần `#include "esp_wifi.h"` |
| `lv_obj_remove_flag` deprecated (LVGL 9.6) | dùng `lv_obj_set_scrollable()` v.v.; `ui_wifi_setup.c` kế thừa vẫn còn — vô hại |
| Task chờ bit `g_r4p_events` (vd `R4P_EVT_WIFI_UP`) không bao giờ thức | main task nhận bit bằng `xClearOnExit=pdTRUE` → task khác chờ cùng bit bị nuốt. Bit sự kiện chỉ MỘT người tiêu thụ (main task); task khác **poll trạng thái** (`wifi_mgr_is_connected()`), như `ota_client.c` |
| Hàng đợi offline làm NVS đầy | NVS 24 KB (~20 KB dùng được, entry 32 B): `RQ_CAP 8` × `RQ_MAX_JSON 1100` (5 khe) ≈ 9 KB + WiFi blob/calib/token — đủ, không tăng RQ_CAP. Muốn nhiều hơn → chuyển sang partition `results` (SPIFFS) |
| Đổi số khe → mất calib (blob NVS `cal_min/cal_max/led_pwm` đổi kích thước) | `calib_store.c::load_slot_blob()` đọc blob cũ theo mọi kích thước 1..N−1 khe, mở rộng, ghi lại (log `blob 4 khe -> mo rong 5 khe`). Đã chạy thật 2026-09-17 |
| Nút `LV_STATE_DISABLED` thành khối xám sáng, mất chữ | theme mặc định LVGL đè style disabled; state không lan xuống label con. Dùng `btn_set_disabled()` (đổi màu tay + bỏ `CLICKABLE`) |
| Tiêu đề header `LV_LABEL_LONG_DOT` vẫn xuống 2 dòng | DOT chỉ cắt khi thiếu **cao**; label cao tự động thì wrap. Đặt `lv_obj_set_size(title, UI_TITLE_W, UI_TITLE_H)` |
| Icon header / dấu "—" hiện **ô vuông** | font `lv_font_vimate_*` chỉ có Latin + Việt (0x20–0x24F, 0x1E00–0x1EFF, `…`, `→`): KHÔNG có dải symbol 0xF000 (`LV_SYMBOL_*`) và không có U+2014. Nhãn chỉ ASCII+icon → Montserrat (`F_ICON*`); chuỗi hiển thị dùng `-` / `·`, không dùng `—` |
| Chữ tràn khỏi nút (vd "THIẾT LẬP RAPID" 24 px trong nút 200 px) | `mk_btn()` đo `lv_text_get_size` rồi hạ font `F_BODY→F_SMALL→F_TINY` (`fit_font`), cuối cùng mới wrap. Nút cần glyph to (−/+) chỉnh font của child label sau khi tạo. Số khe 2.8" dùng `fit_font_from({F_HERO,F_BODY,F_SMALL}, "3000", inner, -2)` |
| Màn WiFi hiện 2 nút quay lại, che dòng bước 2 | màn `ui_wifi_setup` là static + flex column; `build_wifi` phải xoá nút cũ (`s.wifi_back_btn`) và gắn `LV_OBJ_FLAG_IGNORE_LAYOUT` trước khi `lv_obj_align` |
| Nạp báo `bootloader.bin requires chip revision in range [v3.1 - v3.99] (this chip is revision v1.3)` | IDF ≥ 5.5.3 tách P4 rev <3.0 / ≥3.0 thành hai nhánh loại trừ, mặc định `REV_MIN=v3.1`. `sdkconfig.defaults.p4_43lcd` đã đặt `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` + `CONFIG_ESP32P4_REV_MIN_100=y` (board rev v1.3). **Đừng `--force`** — bootloader/app build cho rev 3.x khác phần cứng thật |
| `E i2c.master: this port has not been initialized` ngay trước `sensor bus I2C1` | log thăm dò của `i2c_master_get_bus_handle()` trong `sensor_bus_init()` khi I2C1 chưa ai tạo → code tự tạo bus, vô hại |
| `TCA9548 select chN loi ESP_ERR_INVALID_STATE` ×N, `0/N cam bien` | `ESP_ERR_INVALID_STATE` là mã **NACK** của driver `i2c_master` IDF 5.5 (`i2c_master.c:101`) = không có thiết bị 0x70 trên bus = chưa cắm bo cảm biến. Không phải bug thứ tự init |
| `transport: Version mismatch: Host [2.12.0] > Co-proc [2.7.0]` | firmware ESP-Hosted trong C5 cũ hơn host; hiện WiFi vẫn chạy. Nếu gặp RPC timeout → nâng slave C5 (xem AGENTS.md vimate-p4) |
| **build.bat thoát mã 2 không in gì** (2026-09-19) | `echo … (a ^| b)` trong khối `if ( … )` của cmd: dấu `)` trong echo ĐÓNG khối → `exit /b 2` chạy vô điều kiện. Không dùng ngoặc đơn trong echo bên trong khối `if (…)` |
| `"/*" within comment [-Werror=comment]` | ghi `sensor/*`, `display_hw_*.c` trong comment C → viết "thư mục sensor", `display_hw_<x>.c` |
| Hai board đè `sdkconfig`/lock của nhau | `CMakeLists.txt` gốc đặt `SDKCONFIG=${CMAKE_BINARY_DIR}/sdkconfig` (idf.py đọc lại từ `build_<board>/project_description.json` sau lần configure đầu — `tools/idf_py_actions/tools.py::get_sdkconfig_filename`) + `DEPENDENCIES_LOCK dependencies.lock.${IDF_TARGET}` (lock có trường `target:`). Build dir cũ `build/` + `sdkconfig` gốc là rác — xoá được |
| REQUIRES theo board không ăn (`esp_lcd_st7102.h: No such file`) | REQUIRES đọc ở pha early-expansion, `CONFIG_*` chưa có → điều kiện theo `idf_build_get_property(target IDF_TARGET)` (có ở pha đó); SRCS thì theo `CONFIG_RAPID4P_BOARD_*` được. `set(COMPONENTS main)` để component vendor P4-only trong `components/` không vào build S3 |
| Cần đọc netlist bo thiết kế ở ngoài repo (`01. EngineerHub/…`) | KiCad: MCP `kicad` › `extract_schematic_netlist` + `list_labels_in_schematic` (tool KHÔNG gộp nhãn cục bộ vào net → đối chiếu bằng danh sách nhãn). EasyEDA Pro `.eprj` = **SQLite**: `documents.dataStr` = `base64` + gzip của định dạng dòng JSON (`["COMPONENT",…]`, `["PAD_NET",comp,pad,net]`), `devices`/`attributes` map UUID → mã linh kiện; đọc **PCB** (`PAD_NET`) tin hơn sheet. PDF datasheet: `pypdf` trong venv IDF (`C:\Espressif\python_env\idf5.5_py3.11_env`), `python` hệ thống không có pip |
| Bẫy phần cứng P4 (panel kẹt sau reset MCU, SDIO chết cache line 128 B, TJPGD tràn stack, DHCP 5 GHz…) | `firmware/maping new product/firmware-vimate-p4/AGENTS.md` §7 — vẫn đúng cho cây này |

## 6. Chưa làm (theo thứ tự)

> Nguyên tắc: số khe = `BOARD_SENSOR_SLOTS`, không hard-code 4; kích thước UI = token, không hard-code px.

1. **Nạp bo ES3N28P 2.8"** (`scripts\build.bat s3_28lcd COMx`) → tiêu chuẩn §4 bo S3: LCD/touch/xoay, 13 màn + WiFi qua webcam,
   chỉnh `BOARD_TOUCH_*`/token 2.8" theo mắt; soak 60 s; xác nhận header thật của board → sửa chân cảm biến ĐỀ XUẤT.
2. ~~Schematic bo cảm biến 5 slot~~ → **đã có bo LED + bo cảm biến 4 khe** (2026-09-18): làm theo `docs/HARDWARE-ARCHITECTURE.md`
   — đo 5 số liệu §7 → chốt D1–D6 → vẽ bo **Rapid4P-IF** (P4 JP1; bo S3 cần bản tương đương) → bring-up `BOARD_SENSOR_SLOTS 4` → rev hai bo lên 5 khe.
3. P4: soak > 60 s; chu trình đo 34 s khi có bo con; màn WiFi chuỗi vẫn tiếng Việt cứng (chưa theo `ui_strings`, đi cùng portal web).
4. Font CJK (107 chữ, `lv_font_conv --symbols`) → bật `R4P_HAVE_CJK_FONT` trong `ui_strings.h`.
5. Web dashboard: chưa test trên máy thật (PC dev khác mạng) — mới test `scripts/dash_mock.py` (5 khe). Khi có WiFi: kiểm ĐO/Quay lại từ web,
   heap sau 10 phút poll, portal vẫn mở được (cùng cổng 80).
6. Hợp đồng JSON `system/contracts/ingest-rapid4p.schema.json` (P2) + thử POST vào `server/scripts/localtest.ps1` cho cả hai khoá.
7. Driver AXP2101 (% pin, sạc) nếu Rapid4P P4 chạy pin; WS2812/LED GPIO42 báo trạng thái.
8. Ngưỡng mặc định 600 (ReaderPlus) hay 500 (reader v2.6.6) — hỏi người sở hữu thuật toán.
9. Nhúng thẻ `FBTIMG1` vào .bin khi server bắt buộc cho kho `rapid4p`/`rapid4p-s3`.
