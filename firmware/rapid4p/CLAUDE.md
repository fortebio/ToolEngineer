# CLAUDE.md — firmware/rapid4p (Forte Rapid4P — RAPID READER **5** SLOT, ESP32-P4, ESP-IDF)

> **Số khe = `BOARD_SENSOR_SLOTS`** (board header) — 5 từ 2026-09-17 (`docs/plan/rapid4p-5-slot.md`). Mọi mảng,
> bố cục LCD, chuỗi `%d`, payload `slots:N`, dashboard, registry `optical_slots` suy từ đó; KHÔNG hard-code 4/5.
> Khoá sản phẩm vẫn `rapid4p` (chưa phát hành; đổi tên thương mại = quyết định Q1 còn mở).

> Luật toàn hệ thống ở `CLAUDE.md` gốc. File này: **luật + sự thật phần cứng + chỉ mục** của
> cây rapid4p. Khảo sát gốc và mọi quyết định: `firmware/maping new product/MAPPING-Rapid4P.md`.
> Cây tham chiếu (chỉ đọc, KHÔNG sửa): `firmware/maping new product/firmware-vimate-p4/` —
> `AGENTS.md` + `README-P4.md` ở đó là nhật ký bring-up của chính board này (mọi số đo).
> Học từ dự án ngoài (xiaozhi-esp32 P4/DSI/AXP2101/ML307, CrossInk quy trình/heap/simulator):
> `firmware/maping new product/THAM-KHAO-xiaozhi-CrossInk.md` §3 = danh sách việc đề xuất; cây đã
> chép ở `…/tham-khao/` (MIT, chỉ đọc — chép code sang phải giữ dòng bản quyền).

## 0. Trạng thái (2026-09-17)

- **Build**: `BUILD_EXIT=0`, ESP-IDF **5.5.4** (5.5.1 cũng build được), app **1,97 MB / slot
  3 MB** (37 % trống).
- **Đã nạp máy thật lần đầu 2026-09-17** (COM7, ESP32-P4 rev **v1.3**, chưa có bo cảm biến):
  log 60 s sạch (không `task_wdt`/`Guru`/`stack overflow`), `DSI rotate` + `Touch ST7123 init OK`,
  chạm chuyển màn được, WiFi qua C5 lên, portal `FBT-Rapid4P-XX:XX` + scan chạy. Cảm biến
  `0/4` (đúng — chưa cắm bo con). Mọi thứ thuộc `sensor/*`, `app/measure.c` vẫn chưa có bằng
  chứng phần cứng.
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
| Màn hình app | `main/ui/ui_reader.c`, `ui_strings.[ch]` | máy trạng thái 13 màn (theo ReaderPlus). **Bố cục 2026-09-17**: header 56 · content 328 căn giữa · **footer 96 = thanh hành động** (Quay lại/Huỷ LUÔN trái, hành động chính LUÔN phải teal, phá huỷ đỏ giữa + hộp thoại xác nhận `confirm_show`). Token màu/font/kích thước ở **`ui_theme.h`** = bản C của `system/brand/tokens.json` (màu logo gốc; đổi JSON trước rồi chép sang, không hard-code); thanh tiến độ dùng `ui_theme_brand_bar()` gradient thương hiệu; `mk_btn(icon, txt)` tự co font; chip trạng thái `mk_chip`; icon Montserrat, chữ vimate |
| Logo | `main/ui/ui_logo.c` | Logo FORTE BIOTECH **vẽ vector** (5 `lv_draw_triangle` + 2 label) theo toạ độ file chuẩn 2000×1780 — không dùng PNG (repo chỉ có bản 208×178 mờ; không bật decoder/partition assets). `ui_logo_create(parent, h, with_text)`; chữ chỉ đọc được khi h ≥ ~90. **Góc trái header mọi màn** (cả WiFi) đặt mark 40 px không chữ (`HEADER_LOGO_H`/`HEADER_TITLE_X` trong `ui_theme.h`); màn chính thêm logo lớn 120 px có chữ |
| Chạm | `main/input/touch.c` | ST7123 reg16 → `lv_indev` (đọc trong LVGL task, không task riêng) |
| Nút | `main/input/button.c` | BOOT GPIO35 (tap/giữ 5 s), ĐO GPIO0 |
| Cảm biến | `main/sensor/{sensor_bus,tca9548,tcs34725,slot_led}.c` | I2C_NUM_1 riêng, mux 0x70 kênh 0..N−1, TCS 0x29 ×N (AGC riêng từng slot), LED enable ×N (GPIO28/29/30/45/**47**) + PWM LEDC ch1 chung |
| Đo / calib | `main/app/measure.c`, `calib_store.c` | task đo core 1 prio 4 (~34 s/chu trình), map 0..3000, ngưỡng, NVS |
| Mạng | `main/network/{engineer_api,ota_client,result_upload}.c` | Bearer token từ NVS, `/ota/check` + esp_https_ota + rollback guard, POST kết quả + hàng đợi offline NVS 16 bản |
| **Web dashboard** | `main/network/dashboard.c`, `main/web/dashboard.html` (nhúng `EMBED_TXTFILES`), `scripts/dash_mock.py` | Học theo Rapid+ (`firmware/rapidplus/docs/architecture/05-web-dashboard.md`) nhưng ESP-IDF: `esp_http_server` :80, client **poll `GET /api/state` 1 s** (không SSE), 3 tab Trang chủ/Kết quả/Cài đặt, nút web = nút vật lý (`POST /api/control?btn=measure\|back` → `ui_reader_on_*_button`), `POST /api/threshold`. **Lazy start** khi STA có IP; `dashboard_suspend/resume` do `wifi_mgr_start/stop_provisioning` gọi (portal cùng cổng 80). Máy là nguồn sự thật, client không giữ trạng thái. Test không cần máy: `python scripts/dash_mock.py 8790` |
| Dev console | `main/core/dev_console.c`, `scripts/uicmd.py` | REPL UART0 `r4p> `: `ui`/`btn`/`heap` (+ crash/reboot/mem-dump của esp_hosted). `CONFIG_RAPID4P_DEV_CONSOLE` — TẮT ở bản phát hành |
| WiFi | `main/core/wifi_mgr.c` (+ `captive_dns.c`, `ui/ui_wifi_setup.c`) | STA 2,4 GHz qua C5, SoftAP `FBT-Rapid4P-XX:XX` + portal (thêm ô **Mã máy**, **Token**). Màn `ui_wifi_setup.c` theo `ui_theme.h` (2026-09-17): 2 thẻ QR **cạnh nhau** (QR 200 px; xếp dọc cũ bị ép 88 px), huy hiệu bước ①→✓, chip trạng thái header, footer riêng — ui_reader gắn Quay lại qua `ui_wifi_setup_footer()`. Chuỗi vẫn tiếng Việt cứng (cùng portal web) |
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
- JP1 còn trống sau khi gán cảm biến 5 khe: GPIO48/49/50 (GPIO47 = LED khe 5, đề xuất).

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
   `amplification` (server bắt 10 phần tử cho Rapid+). Dùng `slot_*` N phần tử + `slots: N`
   (server `validate` kiểm `len == slots`; hợp đồng `system/contracts/ingest-rapid4p.schema.json`).
6. **Không có bí mật trong repo/firmware**: token server nhập qua portal → NVS `api_token`, không
   log giá trị. `sdkconfig.defaults` ASCII thuần.
7. `R4P_FW_VERSION` đổi = đổi ở `rapid4p.h` + tag `fw/rapid4p/vX.Y.Z` **+ `project(rapid4p VERSION x.y.z)`
   trong `CMakeLists.txt`** (cùng số, không chữ `v`); chạy `python tools/registry_check.py` (venv
   `%LOCALAPPDATA%\fbt-localtest\venv` có pyyaml/jsonschema). Vì sao: ảnh ESP-IDF tự mang
   `esp_app_desc_t` ở offset `0x20` (`version[32]` @`0x30`, `project_name[32]` @`0x50`, magic `0xABCD5432`
   — đã đọc từ `build/rapid4p.bin`: `rapid4p` / `0.1.0`); server dự kiến dùng nó làm thẻ nhận dạng ảnh
   (`server/docs/plan/ota-quan-ly-may-nhieu-san-pham.md` §2.7) nên hai số lệch = server đặt tên file
   sai bản. Kiểm nhanh: `python -c "b=open('build/rapid4p.bin','rb').read();print(b[0x30:0x50],b[0x50:0x70])"`.
8. Sửa `sdkconfig.defaults` → xoá `sdkconfig` (script tự làm). Không sửa `sdkconfig` sinh ra.

## 4. Build / nạp / log (Windows, cmd.exe — ESP-IDF tại `C:\Espressif`)

```
scripts\build.bat            → BUILD_EXIT=0/1 (build dir build/)
scripts\build.bat COMxx      → build + nạp
scripts\flash.bat COMxx
python scripts\readlog.py --list | auto 60 boot.log   (thả DTR/RTS trước khi mở cổng)
python scripts\uicmd.py COM7 "ui settings" "btn do"   (dev console: ui <0..12|tên|confirm> · btn do|boot · heap · help)
```
**Xem màn LCD từ máy dev (không cần người chạm)**: dev console `core/dev_console.c`
(`CONFIG_RAPID4P_DEV_CONSOLE`, tắt ở bản phát hành) + webcam qua **ffmpeg dshow** — trình duyệt
tích hợp của Claude CHẶN camera (`NotAllowedError`), ffmpeg thì được:
`ffmpeg -f dshow -video_size 1280x720 -vcodec mjpeg -i video="Integrated Webcam" -ss 1.2 -frames:v 1 -update 1 -vf "crop=640:260:440:140,scale=1280:-1" shot.jpg`
(crop theo vị trí máy trên bàn 2026-09-17 — chỉnh lại). Vòng lặp: `uicmd ui N` → chụp → xem.
Từ Git Bash: `MSYS_NO_PATHCONV=1 cmd.exe /c "scripts\build.bat"`. Lần đầu cần mạng để
component manager kéo lvgl/esp_lvgl_port/esp_hosted/esp_wifi_remote (~2 phút).
Script tự đọc id bản ESP-IDF đang chọn từ `C:\Espressif\esp_idf.json` (id là hash riêng
từng máy); muốn bản khác đặt `R4P_IDF_ID=esp-idf-<hash>`. Bản đó phải có toolchain **esp32p4**
(xem `targets` trong `C:\Espressif\idf-env.json` — máy `Admin` chỉ 5.5.4 có, 5.5.1 chỉ s3).

**Tiêu chuẩn "xong" khi có board**: `BUILD_EXIT=0` → nạp → log ≥ 60 s không `task_wdt`/`Guru
Meditation`/`stack overflow`; có `DSI rotate: logical 800x480 -> panel 480x800`, `Touch ST7123
init OK`, `sensor bus I2C1`, `slot n (mux ch): TCS34725 OK` ×N, `measure task san sang, N/N` (N=5),
`diag heap internal=`; chu trình đo 34 s mà chạm vẫn ăn.

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
| Tiêu đề header `LV_LABEL_LONG_DOT` vẫn xuống 2 dòng | DOT chỉ cắt khi thiếu **cao**; label cao tự động thì wrap. Đặt `lv_obj_set_size(title, 520, 30)` |
| Icon header / dấu "—" hiện **ô vuông** | font `lv_font_vimate_*` chỉ có Latin + Việt (0x20–0x24F, 0x1E00–0x1EFF, `…`, `→`): KHÔNG có dải symbol 0xF000 (`LV_SYMBOL_*`) và không có U+2014. Nhãn chỉ ASCII+icon → `lv_font_montserrat_18` (bật trong `sdkconfig.defaults`); chuỗi hiển thị dùng `-` / `·`, không dùng `—` |
| Chữ tràn khỏi nút (vd "THIẾT LẬP RAPID" 24 px trong nút 200 px) | `mk_btn()` đo `lv_text_get_size` rồi hạ font 24→18→14, cuối cùng mới wrap. Nút cần glyph to (−/+) chỉnh font của child label sau khi tạo |
| Màn WiFi hiện 2 nút quay lại, che dòng bước 2 | màn `ui_wifi_setup` là static + flex column; `build_wifi` phải xoá nút cũ (`s.wifi_back_btn`) và gắn `LV_OBJ_FLAG_IGNORE_LAYOUT` trước khi `lv_obj_align` |
| Nạp báo `bootloader.bin requires chip revision in range [v3.1 - v3.99] (this chip is revision v1.3)` | IDF ≥ 5.5.3 tách P4 rev <3.0 / ≥3.0 thành hai nhánh loại trừ, mặc định `REV_MIN=v3.1`. `sdkconfig.defaults` đã đặt `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` + `CONFIG_ESP32P4_REV_MIN_100=y` (board rev v1.3). **Đừng `--force`** — bootloader/app build cho rev 3.x khác phần cứng thật |
| `E i2c.master: this port has not been initialized` ngay trước `sensor bus I2C1` | log thăm dò của `i2c_master_get_bus_handle()` trong `sensor_bus_init()` khi I2C1 chưa ai tạo → code tự tạo bus, vô hại |
| `TCA9548 select chN loi ESP_ERR_INVALID_STATE` ×N, `0/N cam bien` | `ESP_ERR_INVALID_STATE` là mã **NACK** của driver `i2c_master` IDF 5.5 (`i2c_master.c:101`) = không có thiết bị 0x70 trên bus = chưa cắm bo cảm biến. Không phải bug thứ tự init |
| `transport: Version mismatch: Host [2.12.0] > Co-proc [2.7.0]` | firmware ESP-Hosted trong C5 cũ hơn host; hiện WiFi vẫn chạy. Nếu gặp RPC timeout → nâng slave C5 (xem AGENTS.md vimate-p4) |
| Bẫy phần cứng (panel kẹt sau reset MCU, SDIO chết cache line 128 B, TJPGD tràn stack, DHCP 5 GHz…) | `firmware/maping new product/firmware-vimate-p4/AGENTS.md` §7 — vẫn đúng cho cây này |

## 6. Chưa làm (theo thứ tự)

> **Thay đổi lớn đang đề xuất**: nâng lên **5 khe** — kế hoạch, quyết định cần chốt (tên/khoá sản
> phẩm, GPIO LED thứ 5, khe chuẩn), kiểm kê chỗ phụ thuộc số khe và lộ trình ở
> `docs/plan/rapid4p-5-slot.md`. Nguyên tắc: số khe = `BOARD_SENSOR_SLOTS`, không hard-code 4.


1. Schematic bo cảm biến **5 slot** → chốt chân JP1 (LED 5 = GPIO47 đề xuất), pull-up, nguồn LED (VCC3V3 hay BOOST_5V — đo!).
2. ~~Nạp board thật~~ (đã nạp 2026-09-17, log đạt §4 trừ phần cảm biến). ~~Xác nhận chiều
   xoay/chạm bằng mắt~~ (đã xem 13 màn qua webcam 2026-09-17: xoay đúng, chạm ăn, đã sửa
   5 lỗi bố cục). Còn: soak > 60 s; chu trình đo 34 s khi có bo con; màn WiFi chuỗi vẫn tiếng
   Việt cứng (chưa theo `ui_strings`, đi cùng portal web).
3. Font CJK (107 chữ, `lv_font_conv --symbols`) → bật `R4P_HAVE_CJK_FONT` trong `ui_strings.h`.
3b. Web dashboard: máy đã có WiFi (log `r4p.dash: dashboard http://192.168.0.100/`) nhưng PC dev ở mạng khác
   (192.168.1.x) nên **chưa test trên máy thật** — mới test qua `scripts/dash_mock.py` (5 khe)
   (`python scripts/dash_mock.py 8790` → http://127.0.0.1:8790/). Khi có WiFi: log `r4p.dash: dashboard
   http://<ip>/`; kiểm ĐO/Quay lại từ web, heap sau 10 phút poll, portal vẫn mở được (cùng cổng 80).
4. Hợp đồng JSON `system/contracts/ingest-rapid4p.schema.json` (P2) + thử POST vào
   `server/scripts/localtest.ps1`.
5. Driver AXP2101 (% pin, sạc) nếu Rapid4P chạy pin; WS2812 báo trạng thái.
6. Ngưỡng mặc định 600 (ReaderPlus) hay 500 (reader v2.6.6) — hỏi người sở hữu thuật toán.
7. Nhúng thẻ `FBTIMG1` vào .bin khi server bắt buộc cho kho `rapid4p`.
