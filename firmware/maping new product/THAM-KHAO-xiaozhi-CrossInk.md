# Học từ xiaozhi-esp32 và CrossInk cho Rapid4P — bài học + mapping sang `firmware/rapid4p/`

> Ngày lập: 2026-09-17. Nguồn: đọc trực tiếp hai cây đã chép chọn lọc trong `tham-khao/`
> (xiaozhi `5d54beb7`, CrossInk `7a092e82` — bản kê + giấy phép ở `tham-khao/README.md`).
> Đường dẫn `xz:` = `tham-khao/xiaozhi-esp32/`, `ci:` = `tham-khao/CrossInk/`, `r4p:` = `firmware/rapid4p/`.
>
> Tài liệu này trả lời 3 câu: **(1)** hai dự án giải bài toán nào giống Rapid4P và giải thế nào,
> **(2)** cái gì chép được ngay / cái gì chỉ học ý, **(3)** thứ tự việc nên làm. Không lặp lại
> `MAPPING-Rapid4P.md` (phần cứng, quyết định §5) — đọc file đó trước.

---

## 0. Tóm tắt một phút

| | xiaozhi-esp32 | CrossInk |
|---|---|---|
| Là gì | Chatbot AI voice trên ESP32, **ESP-IDF ≥ 6.0.1** (bản mới nhất bỏ 5.x), C++17/23, LVGL 9.5, 138 board — **có 6 board ESP32-P4 MIPI-DSI + ESP-Hosted** giống board Rapid4P | Máy đọc sách e-ink ESP32-C3/S3, PlatformIO + Arduino, C++20, RAM 380 KB không PSRAM |
| Giống Rapid4P ở đâu | **Cùng SoC, cùng bus màn (DSI), cùng cách lên WiFi (C5/C6 qua SDIO), cùng LVGL/esp_lvgl_port, cùng bài OTA + NVS + captive portal**, có driver **AXP2101** (Rapid4P chưa có), có driver **ML307 4G** (board P4 có ML307R-DL, chưa dùng) | Không giống phần cứng. Giống ở **cách vận hành dự án nhỏ mà chặt**: AGENTS.md, SCOPE.md, CHANGELOG, tài liệu kiến trúc, kỷ luật heap, simulator PC, kiểm cỡ firmware theo commit |
| Chép được ngay | `axp2101.cc` (41 dòng, reg map), `power_save_timer.cc` (ngủ → tắt máy), `backlight.cc` (fade), `ota.cc::ParseVersion` (so semver), ý tưởng `Settings` NVS dirty-commit | `lib/Memory/Memory.h` (`makeUniqueNoThrow`) chỉ cho C++; với C của Rapid4P → học **luật** trong `_claude/skills/heap-discipline/SKILL.md`; `scripts/check_firmware_size.py` + `firmware_size_history.py` |
| Không lấy | audio/AFE/Opus, WebSocket/MQTT/MCP, assets partition, emote (đã quyết bỏ ở MAPPING §0) | EPUB engine, e-ink renderer, web portal, Nearby, KOReader sync |

---

## 1. xiaozhi-esp32 — 12 điểm dùng được cho Rapid4P

### 1.1 Driver PMIC AXP2101 — lấp khoảng trống lớn nhất của Rapid4P

Board P4 có AXP2101 `0x34`, IRQ GPIO21, **không có chân ADC đo pin** (MAPPING §1.1) → % pin, đang sạc,
nhiệt độ, tắt máy mềm đều phải hỏi PMIC. Rapid4P **chưa có driver** (`r4p:CLAUDE.md` §2).

- `xz:main/boards/common/axp2101.{h,cc}` — 41 dòng, đủ dùng:

  | Reg | Ý nghĩa | Dòng |
  |---|---|---|
  | `0x01` bit[6:5] | hướng dòng pin: `1` = đang sạc, `2` = đang xả | `GetBatteryCurrentDirection()` |
  | `0x01` bit[2:0] == `0b100` | sạc xong | `IsChargingDone()` |
  | `0xA4` | % pin (0–100) | `GetBatteryLevel()` |
  | `0xA5` | nhiệt độ | `GetTemperature()` |
  | `0x10` \|= `0x01` | **tắt máy mềm** | `PowerOff()` |

- Khởi tạo đầy đủ (bật rail LDO, dòng sạc, ngưỡng tắt, TS pin) xem `xz:main/boards/kevin/box-2/kevin_box_board.cc:20-45`
  (lớp `Pmic : Axp2101`): `0x90/0x93` bật ALDO, `0x61–0x64` dòng/áp sạc, `0x24` ngưỡng Vsys tắt 3,2 V
  (mặc định 2,6 V — "kill battery"), `0x50` TS pin external. **Board P4 dùng ALDO1/3/4 + DCDC1** (MAPPING
  §1.1) — đối chiếu schematic trước khi bật/tắt rail, đừng chép nguyên các giá trị của board khác.
- Mapping: viết `r4p:main/core/pmic_axp2101.c` (C, bus I2C chung GPIO7/8 qua `i2c_master`), knob
  `BOARD_PMIC_I2C_ADDR 0x34`, `BOARD_PMIC_IRQ_GPIO 21` vào `boards/board_esp32p4_43lcd.h`; hiện % pin
  lên header của `ui_reader.c`; `PowerOff()` gắn vào giữ nút BOOT 5 s (đang là hành động khác — xem
  `input/button.c`) hoặc mục "Tắt máy" trong Settings.

### 1.2 Touch ST7123: có component chính thức của Espressif

xiaozhi khai `espressif/esp_lcd_touch_st7123: ^1.0.2` (`xz:main/idf_component.yml:140`) và M5Stack Tab5
dùng nó (`xz:main/boards/m5stack/tab5/m5stack_tab5.cc:184-211`): đọc reg `0x0000` = *touch firmware
version* để phân biệt ST7121/ST7123 rồi chọn driver panel tương ứng.

- Rapid4P đang dùng driver **tự viết reg16** kế thừa vimate-p4 (`r4p:main/input/touch.c`) — **đã chạy
  thật**, có 3 bẫy đã xử (đọc đủ 7×max_points byte, chờ STATUS nibble thấp = 0, không toggle reset).
- Khuyến nghị: **giữ driver riêng** (bằng chứng phần cứng quý hơn), nhưng khi gặp lỗi touch lạ thì mở
  component chính thức so trình tự handshake. Nếu sau này muốn giảm code: component này cắm thẳng vào
  `lvgl_port_add_touch()` của esp_lvgl_port, bỏ được phần `lv_indev` tự viết.

### 1.3 Cấu hình ESP32-P4 rev < 3 + ESP-Hosted — Rapid4P đã làm đúng, đây là xác nhận

`xz:main/boards/m5stack/tab5/README.md` và `xz:main/boards/waveshare/esp32-p4-wifi6-touch-lcd/config.json`:
biến thể cho chip Rev < 3 phải có `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` + `CONFIG_ESP32P4_REV_MIN_100=y`;
nạp nhầm bản P4X vào chip rev 1.x → `bootloader.bin requires chip revision in range [v3.0 - v3.99]`.
`CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y` để pool SDIO của ESP-Hosted không ăn hết RAM nội trên IDF 6.

→ `r4p:sdkconfig.defaults:14-15,50` **đã có cả ba** (chip Rapid4P rev v1.3). Khác biệt còn lại so với
`xz:sdkconfig.defaults.esp32p4`: xiaozhi bật `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` — Rapid4P **cố ý
không bật** vì `esp_wifi_remote` RPC NO_MEM → boot-loop (`sdkconfig.defaults:35`). Giữ của Rapid4P.
xiaozhi dùng `COMPILER_OPTIMIZATION_PERF`, Rapid4P dùng `SIZE` (app 1,97/3 MB) — chỉ đổi khi đo thấy UI
giật. Khi nào nâng lên **IDF 6** (xiaozhi đã bỏ 5.x): P4 rev 1 và rev 3 đều được hỗ trợ, ESP-SR không
liên quan Rapid4P.

### 1.4 Lớp `Board` — HAL theo lớp ảo + luật "không đổi chân board đã phát hành"

`xz:main/boards/common/board.h`: lớp `Board` thuần ảo (`GetDisplay/GetBacklight/GetNetwork/
GetBatteryLevel/SetPowerSaveLevel/GetBoardJson`), mỗi board là một lớp con + đúng một `DECLARE_BOARD(...)`;
`xz:AGENTS.md` › *Required Rules*: **không bao giờ sửa chân của board đã có để chiều phần cứng khác —
thêm board/biến thể mới, vì định danh board ảnh hưởng tương thích OTA**.

- Rapid4P làm theo kiểu C: knob `BOARD_*` trong `boards/board_esp32p4_43lcd.h` (`r4p:CLAUDE.md` §3.1).
  Cùng tinh thần. Điều cần bổ sung là **luật OTA**: khi có bo cảm biến rev 2 đổi chân JP1 → tạo
  `board_esp32p4_43lcd_r2.h` + `hw` mới trong `system/products.yaml`, **không** sửa header cũ; server
  chỉ phát bản build đúng `hw` (registry đã có trường này).
- `GetBoardJson()`/`GetDeviceStatusJson()` gửi kèm khi check OTA (`xz:main/ota.cc`) — Rapid4P
  `network/engineer_api.c` nên gửi `hw`, `fw`, `chip_rev`, `flash`, `psram`, `wifi_rssi`, `battery` một
  lần khi boot để Engineer Server có "device status" (server đã có bảng máy).

### 1.5 `Application::Schedule()` + event bits + máy trạng thái có kiểm chuyển

`xz:main/application.h:25-38` định nghĩa `MAIN_EVENT_*` bits; mọi callback ngoài main task (nút, mạng,
timer) gọi `Schedule(std::function)` hoặc set bit; `SetDeviceState()` đi qua
`xz:main/device_state_machine.{h,cc}` — **bảng chuyển trạng thái hợp lệ**, chuyển sai bị từ chối + log.

- Rapid4P đã có đúng mẫu: `display_schedule(cb,arg)` + event bits trong `app_main.c` (`r4p:CLAUDE.md`
  §3.2–3.3). Thứ **chưa có** là bảng chuyển hợp lệ cho máy 13 màn của `ui_reader.c` — hiện chuyển
  bằng `show_async(screen)` tự do. Thêm `static const uint16_t s_allowed[R4P_SCREEN_COUNT]` (bitmask màn
  đích) + `ESP_LOGW` khi vi phạm: rẻ, bắt được lỗi "đang đo mà nhảy về Home" — đúng loại lỗi IEC 62304
  muốn thấy có kiểm soát.

### 1.6 `PowerSaveTimer` — từ "ngủ màn" tới "tắt máy"

`xz:main/boards/common/power_save_timer.{h,cc}`: `esp_timer` chu kỳ 1 s; sau `seconds_to_sleep` gọi
`OnEnterSleepMode` (board tắt đèn nền, hạ CPU bằng `esp_pm_configure{max, min 40 MHz, light_sleep}`);
sau `seconds_to_shutdown` gọi `OnShutdownRequest` (board gọi `pmic->PowerOff()`); mọi input gọi
`WakeUp()`; `CanEnterSleepMode()` của app chặn ngủ khi đang bận.

- Rapid4P có `display_set_sleep_timeout()` / `display_wake()` (`r4p:main/ui/display.h:43-46`) = nửa đầu.
  Nửa sau (tắt máy sau N phút không dùng khi chạy pin, **không tắt khi đang đo** — hỏi `measure.c`) là
  việc nên làm ngay sau khi có driver AXP2101 (§1.1). `esp_pm_configure` light-sleep trên P4 + ESP-Hosted
  cần thử thật (xiaozhi chỉ bật khi `cpu_max_freq != -1`, tức board tự chọn).

### 1.7 `Backlight` có fade

`xz:main/boards/common/backlight.{h,cc}`: `SetBrightness(target, permanent)` không nhảy thẳng mà chạy
`transition_timer_` từng bước `step_` tới đích; `RestoreBrightness()` lấy giá trị lưu NVS. `PwmBacklight`
LEDC 25 kHz, có `output_invert`.

- Rapid4P: LEDC timer 0 / ch 0 GPIO6 (`r4p:main/ui/display.c`). Thêm fade 200–300 ms khi vào/ra ngủ màn
  cho đỡ "giật", và lưu độ sáng người dùng chọn vào NVS (`nvs_store.c`).

### 1.8 `Button` bọc component `espressif/button` (iot_button)

`xz:main/boards/common/button.h`: click / double / long / multiple-click / press-down/up, có chế độ
power-save (GPIO wake). Rapid4P `input/button.c` tự đọc GPIO (tap/giữ 5 s). Nếu sau này cần double-tap
hoặc giữ nhiều mốc, dùng iot_button thay vì mở rộng máy trạng thái tay — component đã lo debounce +
timer.

### 1.9 OTA: so phiên bản, header định danh, xác nhận app hợp lệ

`xz:main/ota.cc`: header `Device-Id` (MAC), `Client-Id` (UUID), `User-Agent` `<board>/<version>` (:63-69);
`ParseVersion()` (:408) tách `a.b.c` thành vector int rồi `IsNewVersionAvailable()` (:431) so từng phần;
`esp_ota_mark_app_valid_cancel_rollback()` chỉ gọi **sau khi check version thành công** (:268) — tức
"mạng còn sống" là điều kiện xác nhận bản mới; phản hồi server có thể mang **giờ server** để đặt đồng hồ
khi SNTP bị chặn.

- Rapid4P `network/ota_client.c:36` đã có `mark_app_valid`; `:93` quyết định nâng cấp bằng
  `strcmp(ver, R4P_FW_VERSION) != 0` — **khác là nạp**, kể cả server trả bản cũ hơn (hạ cấp ngoài ý muốn
  nếu registry lỡ trỏ sai). Đối chiếu 3 điểm: (a) so semver theo phần như `ParseVersion` và chỉ nạp khi
  **mới hơn** (hạ cấp phải là hành động chủ ý qua cờ `force` của server); (b) gắn `mark_app_valid` vào
  "check OTA lần đầu OK" hoặc "UI lên + touch OK" chứ không phải ngay đầu `app_main`; (c) server
  `/ota/check` trả thêm `server_time` → fallback cho `ICT-7` khi NTP không ra (hiện trường chặn UDP 123).

### 1.10 `Settings` — NVS theo namespace, ghi trễ

`xz:main/settings.{h,cc}`: mở handle theo namespace, `Set*` chỉ đặt `dirty_`, `~Settings()` mới
`nvs_commit`. Rapid4P `core/nvs_store.c` mỗi `nvs_store_set_*` là một chu kỳ `nvs_open → set → commit →
close` (`:21-27`, `:47-53`); `calib_store.c` lưu calib = 2 chu kỳ (`cal_max`, `cal_min`, `:60-61`), đặt
5 ngưỡng = 5 chu kỳ. Chưa phải vấn đề, nhưng khi thêm lưu độ sáng/ngủ màn/kết quả offline 16 bản thì gom
thành `nvs_store_begin()/commit()` giảm mòn flash và thời gian lưu; CrossInk cũng có luật "debounce
persistent writes" (§2.4).

### 1.11 ML307 4G — đường sẵn nếu hiện trường không có WiFi

Board P4 có ML307R-DL (UART TX 53 / RX 52 / DTR 51, nguồn GPIO4 — MAPPING §1.1) chưa có driver. xiaozhi:
`xz:main/boards/common/ml307_board.{h,cc}` trên component `78/esp-ml307 ~3.7.3` (AT modem, `NetworkTask`
riêng), `dual_network_board.{h,cc}` chuyển WiFi ↔ 4G theo NVS; enum `NetworkEvent` (`board.h:20-33`) có
`ModemErrorNoSim / RegDenied / InitFailed / Timeout` — mẫu tốt để UI Rapid4P báo lỗi mạng cụ thể thay vì
"Không có mạng". Chỉ làm khi có yêu cầu sản phẩm; nếu làm, đi theo `dual_network_board` (WiFi mặc định,
4G dự phòng), token/Bearer không đổi.

### 1.12 Ma trận build nhiều biến thể + test host

`config.json` mỗi board → `xz:scripts/build.py` (`--list-boards`, `--name <variant>`, ghi `sdkconfig_append`)
→ `Kconfig.projbuild` → `main/CMakeLists.txt`; `xz:scripts/tests/` là **unittest Python chạy trên host**
cho chính script build; `xz:_github/workflows/build.yml` là ma trận CI. Rapid4P hiện 1 biến thể
(`scripts/build.bat`). Khi có biến thể thứ hai (bo cảm biến r2, hoặc bản không pin), làm theo: một
`variants.json` + `build.bat <variant>` ghép `sdkconfig.defaults` + `sdkconfig.<variant>` — đừng nhân
bản `sdkconfig.defaults`.

### 1.13 Lớp display/LVGL (đọc thêm, chưa cần làm)

`xz:main/display/lvgl_display/`: `lvgl_theme` (token màu light/dark), `lvgl_font` + `dynamic_glyph_cache`
(glyph nạp động từ assets partition — `xz:docs/glyph-push.md`), `lvgl_psram_pool.h` (heap LVGL trong
PSRAM). Rapid4P font `lv_font_vimate_*` chỉ Latin/Việt, nhưng `ui_strings.c` **đã có bảng ZH/TW**
(`:115-178`) → chữ Hoa hiện đang không có glyph. Nếu cần ZH thật: cách rẻ nhất là bảng font riêng cho
ZH (LVGL font converter, chỉ các ký tự trong `s_zh`), cách "đúng" là glyph cache như xiaozhi.

---

## 2. CrossInk — 8 điểm về CÁCH LÀM (không phải phần cứng)

### 2.1 `AGENTS.md` là mẫu cho CLAUDE.md của một cây firmware

`ci:AGENTS.md` (canonical, `CLAUDE.md` chỉ trỏ vào) — cấu trúc: bảng *Area / Read first / Owns* →
*Runtime Boundaries* → *Directory & Generated-Asset Map* → *Target Selection* → *Core Rules* → *Resource
Rules* (12 luật đánh số) → *HAL Rules* → *C++ Gotchas* → *Error Handling* → *Lifecycle* → *UI/Input* →
*Build & Verification* → *Generated Files* → *Cache Format* → *Git Workflow* → *Changelog*.
`r4p:CLAUDE.md` đã có 3 phần đầu (điều hướng, phần cứng, luật). Thiếu và nên thêm: **Error Handling**
(log trước khi `return false`; `esp_restart()` chỉ cho OTA), **Resource Rules** (stack task đo/mạng,
không malloc trong callback LVGL, `static const` cho bảng), **Generated Files** (font `ui/fonts/*` sinh
từ đâu, đừng sửa tay).

### 2.2 `SCOPE.md` — in-scope / out-of-scope / "in-scope nhưng chưa làm được"

`ci:SCOPE.md` liệt kê rõ *Out-of-Scope* kèm lý do (pin, single-core) và mục *In-scope — Technically
Unsupported*. Rapid4P là thiết bị IVD thú y: một `SCOPE.md` ngắn ("máy đọc 4 slot + tải kết quả; không
làm: trình duyệt, media, phân tích tại máy vượt ngưỡng đã hiệu chuẩn…") vừa giữ sản phẩm gọn, vừa là
"intended use" mà IEC 62304 / hồ sơ kỹ thuật cần (`firmware/rapidplus-prod` đang đi hướng này).

### 2.3 `CHANGELOG.md` kiểu keep-a-changelog + `CONTEXT.md` bẫy đã gặp

`ci:CHANGELOG.md`: mọi bản có mục Added/Changed/Fixed/Removed, ngày phát hành. Rapid4P ghi
`docs/history/*.md` theo ngày (luật monorepo) — giữ, nhưng khi phát hành tag `fw/rapid4p/vX.Y.Z` nên có
`CHANGELOG.md` để người dùng/ERP đọc được. `ci:_claude/CONTEXT.md` = "Durable Context" chỉ chứa gotcha
tái dùng (vd POSIX TZ ngược dấu: `"UTC-1"` nghĩa là UTC+1 — Rapid4P dùng `ICT-7` đúng rồi). Tương đương
mục "Bẫy đã gặp" trong `r4p:CLAUDE.md`; nguyên tắc "ngắn, chỉ điều tái dùng, không nhật ký" đáng giữ.

### 2.4 Kỷ luật heap — áp dụng cho C của Rapid4P

`ci:_claude/skills/heap-discipline/SKILL.md` + `ci:lib/Memory/Memory.h`: thứ tự quyết định
*stack ≤ 256 B → `static constexpr` → cấp một lần trong `onEnter`, thả trong `onExit` → cấp động phải
null-check + log kích thước → raw malloc chỉ khi SDK nhận quyền sở hữu (ghi chú ai free)*; "fragmentation,
not total usage, is what kills"; `reserve()` trước `push_back`; **debounce ghi NVS** (không ghi mỗi lần
lật trang). Rapid4P (C, PSRAM 32 MB) không thiếu RAM nhưng **RAM nội** thì có hạn (ESP-Hosted, DSI fb,
LVGL) — `r4p:CLAUDE.md` §3.3 đã yêu cầu malloc bản sao `measure_result_t` rồi free trong apply; bổ sung:
mọi buffer > 4 KB cấp bằng `heap_caps_malloc(MALLOC_CAP_SPIRAM)`, buffer DMA/I2C bằng `MALLOC_CAP_DMA |
MALLOC_CAP_INTERNAL`, và in `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` trong diag 60 s
(hiện `dev_console` có `heap` — thêm cột largest block).

### 2.5 `ActivityManager` — một render task, một khoá, ngăn xếp màn hình

`ci:docs/activity-manager.md` + `ci:src/activities/ActivityManager.h`: trước đây mỗi màn hình tự tạo
render task 8 KB → RAM + race; sau refactor: **một** render task, **một** mutex toàn cục qua `RenderLock`,
ngăn xếp `push/pop/replace` áp dụng **trên main loop** (không phải trong callback), `startActivityForResult()/
setResult()` cho hộp thoại con, `onEnter()/onExit()` cấp/thả tài nguyên theo thứ tự ngược.
Rapid4P dùng LVGL task + `display_lock()` + `show_async()` — cùng nguyên lý. Điểm học: (a) hộp thoại xác
nhận `confirm_show` của `ui_reader.c` nên trả kết quả về màn gọi theo kiểu *result*, không đặt biến
toàn cục; (b) mỗi màn có `enter/exit` rõ để giải phóng timer/widget — hiện 13 màn nằm chung một file,
khi vượt ~2 000 dòng thì tách theo mẫu `activities/<flow>/`.

### 2.6 HAL + capability gating theo compile-time

`ci:include/AppCapabilities.h`, `DeviceCapabilities.h`, `ci:lib/hal/`: app chỉ gọi lớp HAL, phần cứng
thật nằm trong SDK; tính năng (touch, PSRAM, USB Drive, SDMMC) là **capability** bật theo env trong
`platformio.ini`, activity không `#if` tay. Rapid4P: `BOARD_*` là HAL chân; thêm lớp capability
(`BOARD_HAS_BATTERY`, `BOARD_HAS_4G`, `BOARD_HAS_SDCARD`) để `ui_reader.c` ẩn/hiện % pin hay mục 4G mà
không rải `#if CONFIG_…` — đúng lúc khi làm §1.1/§1.11.

### 2.7 Simulator trên PC + smoke test

`ci:docs/simulator.md`, `ci:scripts/run_simulator_smoke_test.py`: env `simulator` build native (SDL2),
FS giả `./fs_`, script tự chạy qua các flow chính. Nạp P4 chậm và không có bo cảm biến → **LVGL simulator
cho `ui_reader.c`** (LVGL 9 có port SDL; `measure.c` giả lập bằng bảng giá trị) sẽ cho duyệt bố cục 13
màn, 4 ngôn ngữ, hộp thoại xác nhận mà không cần máy. Đây là việc **lợi nhất về thời gian** trong cả
danh sách này; điều kiện: `ui_reader.c` không include gì ngoài `lvgl.h` + `ui_strings.h` + một header
giao diện đo (đã gần đạt vì luật §3.3 cấm gọi lv_* từ task đo).

### 2.8 Kiểm cỡ firmware theo commit + I18n sinh mã

`ci:scripts/check_firmware_size.py` (fail khi vượt ngưỡng) + `firmware_size_history.py` (ghi cỡ theo
commit): Rapid4P app 1,97 MB / slot 3 MB — thêm bước in cỡ `.bin` + % slot vào cuối `scripts/build.bat`
và fail ở 90 %. `ci:scripts/gen_i18n.py` sinh `I18nKeys.h`/`I18nStrings.cpp` từ `translations/*.yaml`
(`ci:lib/I18n/translations/vietnamese.yaml` có sẵn làm ví dụ): thay cho 4 mảng `s_vi/s_en/s_zh` viết tay
trong `ui_strings.c` — chỉ đáng làm khi số chuỗi > ~150 hoặc có người dịch ngoài.

---

## 3. Thứ tự việc đề xuất (ưu tiên theo giá trị / công)

| # | Việc | Nguồn học | Chạm file Rapid4P | Điều kiện |
|---|---|---|---|---|
| 1 | Driver AXP2101 (% pin, sạc, PowerOff) | §1.1 | `core/pmic_axp2101.c` (mới), `boards/board_esp32p4_43lcd.h`, header `ui_reader.c` | có máy, đo bằng USB-C + pin |
| 2 | Tắt máy sau N phút khi chạy pin, không tắt khi đang đo | §1.6 | `ui/display.c`, `app/measure.c` (cờ bận), `app_main.c` | sau #1 |
| 3 | LVGL simulator PC cho `ui_reader.c` | §2.7 | `sim/` (mới), tách include của `ui_reader.c` | không cần máy |
| 4 | OTA: so semver theo phần, `mark_app_valid` sau check OK, `server_time` fallback | §1.9 | `network/ota_client.c`, `engineer_api.c`, **server** `/ota/check` | chạm 2 phần → ghi `docs/history/` gốc |
| 5 | Bảng chuyển màn hợp lệ + log vi phạm | §1.5 | `ui/ui_reader.c` | không cần máy |
| 6 | In cỡ `.bin` / % slot + fail 90 % trong `build.bat`; largest-free-block vào diag | §2.8, §2.4 | `scripts/build.bat`, `core/dev_console.c`, `app_main.c` | không cần máy |
| 7 | Gom commit NVS khi lưu calib/ngưỡng | §1.10 | `core/nvs_store.c`, `app/calib_store.c` | không cần máy |
| 8 | Fade đèn nền + lưu độ sáng | §1.7 | `ui/display.c` | có máy |
| 9 | `SCOPE.md` + `CHANGELOG.md` + mục Error Handling/Resource Rules trong CLAUDE.md | §2.1–2.3 | `firmware/rapid4p/` | không cần máy |
| 10 | Capability macros (`BOARD_HAS_BATTERY/4G/SDCARD`) | §2.6 | `boards/`, `ui_reader.c` | cùng #1 |
| 11 | 4G ML307 qua `esp-ml307` + dual network | §1.11 | `network/` (mới), portal | **chỉ khi có yêu cầu sản phẩm** |
| 12 | Nâng ESP-IDF 6.x | §1.3 | `sdkconfig.defaults`, `idf_component.yml` | khi esp_hosted/lvgl_port ổn định trên 6.x; kiểm rev < 3 |

Không nằm trong danh sách: chuyển touch sang component chính thức (§1.2 — giữ driver đã chạy), iot_button
(§1.8 — chỉ khi cần double-tap), glyph cache ZH (§1.13 — chưa có yêu cầu).

---

## 4. Lưu ý giấy phép khi chép code

Cả hai MIT. Khi chép một hàm/file sang `firmware/rapid4p/` (vd reg map AXP2101): ghi ở đầu file
`/* Chép/chuyển từ xiaozhi-esp32 (MIT, © 2025 Shenzhen Xinzhi Future Technology) — tham-khao/xiaozhi-esp32/LICENSE */`
hoặc tương ứng `© 2025 Dave Allie` cho CrossInk. Component của Espressif (`esp_lcd_touch_st7123`,
`esp_lcd_st7123` trong Tab5) là Apache-2.0 — dùng qua `idf_component.yml`, không chép nguồn vào cây.
