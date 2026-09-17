# firmware-vimate-p4 — bộ firmware VIMATE cho ESP32-P4

Cây firmware **tách riêng** cho board FBT ESP32-P4 + LCD 4.3" ST7102 MIPI-DSI.
`firmware-vimate/` (ESP32-S3, sản phẩm đang chạy) **không bị sửa một dòng nào** —
mọi thay đổi nằm gọn trong thư mục này.

Tính năng firmware **giữ nguyên**: toàn bộ UI, WebSocket, OTA, store, protocol,
emotion… là bản sao y hệt cây S3. Chỉ khác lớp phần cứng (panel/touch/audio/WiFi);
phần nào phần cứng chưa xác nhận thì **khoá runtime**, không xoá code.

---

## 1. Trạng thái — đã chạy thật trên board (UART CH343, COM47 → COM48 từ 12/09)

| Hạng mục | Trạng thái |
|---|---|
| Build `esp32p4` | ✅ EXIT=0, app **4,16 MB** / slot 4,5 MB (12% trống) — đã gồm MP4 player |
| Boot ổn định | ✅ **0 lần reboot, 0 panic** trong 45s theo dõi |
| LCD ST7102 MIPI-DSI 480×800 | ✅ `LCD hard reset GPIO22` → `Display HW ready 800x480`. Lệnh đọc ID đã bỏ (điểm treo không timeout, §6.2) |
| Màn NGANG 800×480 | ✅ **từ 12/09** — LVGL vẽ ngang, PPA xoay vùng bẩn vào fb DPI ẩn, vẫn 2 fb + vsync (§6.6). Cả màn 15,9 ms/PPA. **Chiều xoay + touch chưa có mắt người xác nhận** |
| Chống xé hình (DSI 2 fb, direct mode) | ✅ `DSI: avoid_tearing=1 num_fbs=2 direct_mode=1`, `flush=6` sau 60s (§6.1). **Mắt người chưa xác nhận** |
| Touch ST7123 (I2C 0x55) | ✅ `Touch reg16 OK (status=0x00, max points=5)`; 12/09: knob swap/mirror suy từ `BOARD_LCD_ROTATION` (§6.6) |
| LVGL + UI VIMATE | ✅ `display_setup_ui done` |
| Emoji cảm xúc | ✅ **15/09: "mặt robot" toàn màn** — 32 clip 400×240 ×2 = 800×480 từ `docs/01_gif` (bộ 128 khung/clip tối 15/09 → 1561 khung sau gộp < 40 ms, 3,9 MB), partition P4 `emo_spiffs` **5,375 MB** (`4052395/5173361`), trạng thái + cảm xúc + idle có clip riêng (§6.5b). Đo: decode **9–13 ms/khung** (vẽ thẳng RGB565, bỏ chia trong LZW), 0 wdt, boot_up → idle_normal đúng. **Mắt người + phiên nói chuyện chưa kiểm** (WiFi không lên). Bộ Noto §6.5 chỉ còn ở S3 |
| WiFi qua co-processor | ✅ **chạy được** — xem §5, chip là **C5** không phải C6. 15/09: khoá 2,4 GHz (5 GHz associate được nhưng không DHCP), canh gác DHCP 20 s, AP provisioning theo kênh STA (§5.6) |
| SoftAP provisioning | ✅ `GENU-Setup-53:C8` @ 192.168.4.1 — captive DNS, portal, 2 mã QR, danh sách WiFi hâm nóng sẵn (§5.5). 15/09: hết panic ở màn QR (tắt LVGL TJPGD) và tràn stack `captive_dns` (§5.6) |
| Portal trên trình duyệt thật | ✅ 31/31 kiểm tra đạt (Playwright + Edge, §5.5) |
| Captive portal tự bật trên điện thoại | ❔ **chưa test máy thật** (§7) |
| BLE provisioning | ⛔ P4 không có radio BT → tự rơi về SoftAP (§5) |
| Audio ES8311 + ES7210 | ✅ hai codec trả lời I2C, pipeline lên sạch (§4) |
| Chạm cảm ứng | ✅ **13/09**: map sau xoay đúng; quét 11 ms; nhấc tay → `home_select` median 55 ms (trước 0,8–1,4 s); vuốt/gõ lại nút xử lý đúng; còn chờ lock LVGL 23–158 ms ở 1/3 lần (§6.7) |
| WakeNet "Hi Lily" | ✅ model nạp từ flash, 20KB internal + 325KB PSRAM (§4) |
| Mic thu tín hiệu | ✅ **đo được 11/09** — nền 60–100 rms sau HPF, DC ≈ 0; nghe được beep của chính loa (§4) |
| Loa + amp NS4150B | ✅ beep 880Hz lúc boot, mic ghi rms ~1900–3000 / peak ~7500 đúng cửa sổ beep (§4) |
| TTS qua Opus | ✅ **12/09**: từng panic `pseudostack overflow` ở gói TTS đầu tiên (lỗi TLS của IDF trên P4, §4.5) — đã sửa, `SPK WRITE peak=16866` |
| Mic → ASR | ✅ **12/09**: ESP-SR AFE (tổng 2 mic → VAD WebRTC + WakeNet + AGC) chạy đủ 313/313 khung/10 s; VAD kết thúc lượt đúng (EOT 704 ms), STT trả câu tiếng Việt đầy đủ (§4.7) |
| WakeNet "Hi Lily" | ❔ trong AFE, ngưỡng 0,6, AGC WakeNet — **chưa có lần gọi thử trong phòng yên** (§4.7) |
| AEC ref cứng (ES7210 MIC3) | ⛔ không lấy được qua TDM của driver; AEC/BSS 2 mic trong AFE quá nặng cho lib `esp32p4_less_v3` (§4.7) |
| HPF mic 120Hz | ✅ **đã sửa** — trên P4 từng là passthrough vì bug esp-dsp `dsps_biquad_f32_arp4` (§4) |
| Đèn nền PWM (GPIO6) | ✅ LEDC, chỉnh được độ sáng + tự tắt khi rảnh |
| Camera SC2336 MIPI-CSI | ⛔ chưa làm (§7) |
| Thẻ nhớ SDMMC slot 0 | ✅ **15/09: có thẻ là lên** — SDHC 4 GB, 4-bit **20 MHz**, `SD verify read/write OK`, course cache mounted. Ghi 363 KB/s / đọc 761 KB/s sau diskio bounce 64 B + stdio 16 KB (trước 61 / 126 KB/s). `0x107` trước đây = khe trống (§7) |
| MP4 player | ✅ **build được** (workaround CMake, §2 bẫy 4), decoder siết về AAC/MP3/FLAC (§6.4). **Chưa phát thử** — cần thẻ SD có file |

Log boot rút gọn của bản đang nạp (khởi động bình thường, đã có WiFi):

```
I vimate.ui:    LCD hard reset GPIO22 truoc khi mo DSI
I vimate.ui:    Init MIPI-DSI ST7102 480x800 (2 lane @ 520 Mbps)
I vimate.ui:    DSI: avoid_tearing=1 num_fbs=2 direct_mode=1
I vimate.ui:    Display HW ready 480x800 (BL=100%)
I vimate.audio: I2C probe before-codec-open: ES8311 0x18=ESP_OK, ES7210 0x40=ESP_OK, TCA9555 0x00=ESP_ERR_NOT_FOUND
I vimate.audio: ES8311 duplex channels created 24000Hz 16-bit stereo slots
I vimate.audio: ES8311 codec_dev ready (vol=80%, 24000Hz es8311-out mono + es7210-in stereo-slots, i2c=0, pa=3 on=1)
I vimate.audio: Opus codec ready (enc 24000Hz, dec 24000Hz)
I vimate.audio: WakeNet ready model=wn9_hilili_tts words=Hi,Lily or Hi,莉莉 sr=16000 chunk=512
I vimate.audio: Audio pipeline ready
I vimate.main:  Touch: reuse I2C bus from ES8311 codec
I vimate.main:  Touch ST7123 init OK (SCL=8 SDA=7 addr=0x55)
I vimate.ui:    display_setup_ui done
I vimate.main:  Chip=ESP32-P4 rev103 MAC=80:F1:B2:D1:53:C8
I vimate.cache: SD LDO power ON 3.3V (on-chip chan=4)
I vimate.cache: SD SDMMC mount slot=0 CLK=43 CMD=44 D0=39 width=4
I transport:    Identified slave [esp32c5]
I transport:    Base transport is set-up, TRANSPORT_TX_ACTIVE
I vimate.wifi:  Got IP: 192.168.1.22
I vimate.main:  diag heap internal=99419 min=94403 psram=27565152 min=27330312 task_stack=1240 flush=6
```

Log khi vào chế độ cài đặt WiFi (SoftAP) nằm ở §5.5.

### Thông báo còn lại — đã kiểm, vô hại

| Log | Giải thích |
|---|---|
| `E lcd_panel: esp_lcd_panel_swap_xy(50): swap_xy is not supported` | `esp_lvgl_port` gọi swap_xy khi áp rotation; panel DPI không có API này. Ta để rotation = 0 nên không ảnh hưởng. |
| `E i2c.master: this port has not been initialized` | `touch.c` dò xem bus I2C đã tồn tại chưa trước khi tự tạo. Có sẵn từ cây S3. |
| `E system_api: 0 mac type is incorrect` | Đọc MAC WiFi trước khi WiFi lên; firmware fallback về base MAC (đã lấy đúng `80:F1:B2:D1:53:C8`). |

### ⚠️ Việc nên làm sớm: nâng firmware slave trên C5

```
W transport: Version mismatch: Host [2.12.0] > Co-proc [2.7.0]
             ==> Upgrade co-proc to avoid RPC timeouts
```

C5 đang chạy firmware ESP-Hosted slave **2.7.0**, host là **2.12.0**. Hiện vẫn
bắt tay và chạy SoftAP bình thường, nhưng Espressif cảnh báo có thể timeout RPC
khi tải nặng.

**Đường nạp duy nhất là OTA qua SDIO từ host** — đã tra schematic
(`docs/HARDWARE-PINOUT.md` §4): `C5_U0RXD/TXD` **không nối ra ngoài**, không có
cách nạp bằng UART. esp_hosted 2.12 có sẵn API
`esp_hosted_slave_ota_begin/write/end()` (ví dụ
`managed_components/espressif__esp_hosted/examples/host_performs_slave_ota/`),
firmware slave build từ `managed_components/espressif__esp_hosted/slave/` với
`idf.py set-target esp32c5` + `sdkconfig.ci.sdio`.

⚠️ **Không có đường cứu nếu image mới không bắt tay được**: OTA_END chỉ kiểm
magic + CRC, slave không bật rollback, và không có UART. Image build sai cấu
hình (ví dụ SDIO 1-bit thay vì 4-bit, hay đổi chân handshake) là mất WiFi vĩnh
viễn trên board đó. Vì thế **chưa tự làm** — cần người sở hữu board quyết, và nên
thử trên một board không phải board duy nhất.

---

## 2. Build

```bash
. $IDF_PATH/export.sh          # ESP-IDF 5.5.1
./scripts/build_p4_43lcd.sh
```

Tương đương:

```bash
idf.py -B build_p4_43lcd \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.p4-43lcd" \
  set-target esp32p4
idf.py -B build_p4_43lcd \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.p4-43lcd" build
```

Build dir `build_p4_43lcd/` tách hẳn, không đụng `build_s3_*` của cây S3.

> **Trên máy Windows đang dùng, lệnh bash ở trên KHÔNG chạy.** `export.sh` không
> đặt được `IDF_PATH` (kết quả: `idf.py: command not found`), và gọi
> `idf_cmd_init.bat` từ Git Bash thì nó tự chối:
> *"This .bat file is for Windows CMD.EXE shell only."*
> Phải chạy qua **cmd.exe** thật:
>
> ```bat
> set IDF_TOOLS_PATH=C:\Espressif
> call C:\Espressif\idf_cmd_init.bat esp-idf-29323a3f5a0574597d6dbaa0af20c775
> cd /d "...\firmware-vimate-p4"
> idf.py -B build_p4_43lcd -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.p4-43lcd" build
> ```
>
> Từ 13/09/2026 ba bước đó nằm sẵn trong repo (đường dẫn tương đối, không hard-code
> máy nào): `scripts\build_p4_43lcd.bat` (build, in `BUILD_EXIT=0/1`, tự xoá `sdkconfig`
> khi defaults mới hơn — bẫy 2), `scripts\flash_p4_43lcd.bat COMxx` (chỉ nạp),
> `python scripts\p4_readlog.py auto 60 boot.log [--no-reset]` (đọc UART; `auto` tự tìm
> cổng CH343 vì số COM đổi theo lần cắm, `--list` để xem; reset qua DTR/RTS). Từ Git Bash
> gọi `MSYS_NO_PATHCONV=1 cmd.exe /c "scripts\build_p4_43lcd.bat"` (không có
> `MSYS_NO_PATHCONV` thì MSYS đổi `/c` thành `C:\` và cmd mở shell tương tác); script tự
> xoá biến `MSYSTEM` vì `export.bat` của ESP-IDF từ chối chạy khi thấy nó. Biến
> `IDF_TOOLS_PATH` / `VIMATE_IDF_ID` đổi được khi máy khác cài ESP-IDF chỗ khác. Đã kiểm
> 13/09: build `BUILD_EXIT=0`; `p4_readlog.py auto 35` bắt được boot sạch (0 panic, đủ
> dòng `DSI rotate` / `Touch ST7123 init OK` / `opus decode self-test` /
> `Identified slave [esp32c5]` / `Got IP` / `WakeNet ON`).

### Năm cái bẫy đã gặp — đọc trước khi sửa cấu hình

1. **`sdkconfig.defaults*` phải là ASCII thuần.** `kconfgen` đọc file config bằng
   locale hệ thống (cp1252 trên Windows). Chữ có dấu như "ọ" (UTF-8 `E1 BB 8D`)
   làm build chết: `UnicodeDecodeError: 'charmap' codec can't decode byte 0x8d`.
   Comment trong 2 file sdkconfig viết **không dấu**; giải thích đầy đủ ở file này.

2. **Sửa `sdkconfig.defaults` mà không xoá `sdkconfig` thì KHÔNG có tác dụng.**
   ESP-IDF chỉ dùng defaults để *sinh* `sdkconfig` lần đầu; sau đó bỏ qua im lặng.
   Đã dính một lần: đổi `CONFIG_ESP_MAIN_TASK_STACK_SIZE` mà build ra y hệt, board
   vẫn crash. `scripts/build_p4_43lcd.sh` giờ tự xoá `sdkconfig` khi defaults mới hơn.

3. **`esp_lvgl_port` ghim `~2.8.0`.** Bản 2.9.0 đổi callback DPI theo ESP-IDF 5.5.2
   (`on_refresh_done` → `on_frame_buf_complete`), máy đang chạy 5.5.1 nên lỗi
   `'esp_lcd_dpi_panel_event_callbacks_t' has no member named 'on_frame_buf_complete'`.
   Chỉ nâng lên `^2.9` **sau khi** nâng ESP-IDF ≥ 5.5.2.

4. **Đường dẫn repo có dấu cách** (`FBT- ESP-IDF 5 4.3LCD+SC2336 ESP P4`) —
   **ĐÃ XỬ LÝ 11/09/2026, không cần dời repo.** Ba component `esp_extractor`,
   `esp_audio_codec`, `esp_audio_effects` khai
   `target_link_libraries(lib PRIVATE "-L ${CMAKE_CURRENT_SOURCE_DIR}/lib/...")`
   không bọc nháy; CMake ghi nguyên chuỗi vào response file của `ld`, `ld` tách
   theo dấu cách: `cannot find 4.3LCD+SC2336`. Chỉ cần **khai báo** là đã lỗi.

   Hai cách thử **không** ăn thua trước khi tới cách đúng:
   - Tên ngắn 8.3 (`C:\Users\ADM\DOWNLO~1\FBT-ES~1.3LC\...`) — `idf.py`/CMake
     chuẩn hoá về đường dẫn dài trước khi sinh build, lỗi y nguyên.
   - Chỉ bỏ cờ `-L` — `esp_extractor`/`esp_audio_codec` thì được (chúng link
     `.a` bằng đường dẫn tuyệt đối qua `add_prebuilt_library`), nhưng
     `esp_audio_effects` đặt tên target prebuilt theo **tên thư mục**
     (`espressif__esp_audio_effects`) rồi lại link `esp_audio_effects` trơn →
     thành `-lesp_audio_effects`, **phải** có thư mục tìm kiếm.

   Cách đúng, nằm trong `CMakeLists.txt` gốc sau `project()`: gỡ chuỗi `-L <dir>`
   khỏi `LINK_LIBRARIES`/`INTERFACE_LINK_LIBRARIES` của 3 target đó (kể cả dạng
   `$<LINK_ONLY:...>` mà PRIVATE trên static lib sinh ra) và đưa `<dir>` vào
   `target_link_directories()` của ELF cuối — CMake tự bọc nháy. Không đụng
   `managed_components` (component manager ghi đè + kiểm hash). Build in ra:
   `-- vimate: __idf_espressif__esp_audio_effects (INTERFACE_LINK_LIBRARIES): '-L <dir>' -> target_link_directories: ...`

5. **`CONFIG_ESP32P4_SELECTS_REV_LESS_V3` KHÔNG tồn tại trên ESP-IDF 5.5.1.**
   Ba project demo của hãng đều đặt cờ này nên tôi chép theo — build báo
   `warning: unknown kconfig symbol 'ESP32P4_SELECTS_REV_LESS_V3' assigned to 'y'`,
   tức dòng đó **bị bỏ qua im lặng**. Đã gỡ. Board chạy silicon **P4 rev v1.3**
   (bootloader in `chip revision: v1.3`) nên esp-sr phải nạp `lib/esp32p4_less_v3`,
   và trên 5.5.1 nó tự chọn đúng nhờ điều kiện `IDF_VERSION_PATCH < 3`:
   `-- TARGET_LIB_PATH is set to: esp32p4_less_v3`.
   ⚠️ **Khi nâng ESP-IDF**: lên 5.5.3+ điều kiện tự động hết hiệu lực → phải tự
   thêm `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` (lúc đó symbol mới tồn tại), nếu
   không esp-sr nạp nhầm thư viện của rev ≥ v3. Lên 6.0 thì esp-sr báo
   `FATAL_ERROR`, không hỗ trợ P4 rev < v3.

6. **Redirect log build ra file → `movemodel.py` của esp-sr chết `UnicodeEncodeError:
   'charmap'`** (nó in `─` × 40; stdout không phải console thì Python dùng cp1252).
   Chạy `PYTHONIOENCODING=utf-8` (hoặc `PYTHONUTF8=1`) trước `cmd.exe /c
   scripts\build_p4_43lcd.bat`; chạy thẳng trong cửa sổ cmd thì không gặp. (15/09)

---

## 3. Nạp firmware

```bash
./scripts/build_p4_43lcd.sh COM47     # build + flash + monitor
# hoặc chỉ nạp:
idf.py -B build_p4_43lcd -p COM47 -b 460800 flash monitor
```

Bản đồ flash (16MB, từ `build_p4_43lcd/flash_args`):

| Offset | Ảnh |
|---|---|
| `0x2000` | bootloader |
| `0x8000` | partition table |
| `0xf000` | ota_data_initial |
| `0x20000` | `vimate-fw.bin` (app, slot 4.5MB) |
| `0x920000` | asset_spiffs (3MB) |
| `0xc20000` | lesson_spiffs (2MB) |
| `0xe20000` | emo_spiffs (512KB) |
| `0xea0000` | srmodels (WakeNet, 1MB) |

### 3.1 Debug UART — đã kiểm

| Mục | Giá trị |
|---|---|
| Cổng trên máy | **USB-Enhanced-SERIAL CH343** (`VID_1A86 PID_55D3`) — cầu USB-UART rời, **không** phải USB-Serial-JTAG nội của P4. Số COM đổi theo lần cắm: `COM47` tới 12/09, `COM48` sau khi cắm lại — tra Device Manager, không tin số cũ |
| Console chính | `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`, UART**0**, **115200** 8N1 |
| Console phụ | `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y` (vẫn bật, dùng được nếu cắm cổng USB native của P4) |
| Chân UART0 | mặc định của P4 — **không đụng** chân nào đang dùng (I2C 7/8, nút 35, SDIO 14–19 + 54) |

Cấu hình **giống hệt cây S3**, không phải sửa gì. Log ROM + bootloader + app đều
ra đủ ở 115200 (đã bắt log thành công nhiều lần qua cổng này).

Đọc log không cần `idf.py monitor`:

```bash
idf.py -B build_p4_43lcd -p COM47 monitor      # có giải mã backtrace
# hoặc bất kỳ terminal nào ở 115200 8N1
```

Nếu **không thấy gì** trên COM47: kiểm tra baud (115200, không phải 74880/921600)
và đúng cổng CH343 chứ không phải cổng Bluetooth ảo — máy này còn COM3/4/5/10/11/22/23
đều là Bluetooth/SOL, không phải thiết bị.

Nếu màn đen: tìm `MIPI DPHY LDO acquire lỗi` hoặc `esp_lcd_new_dsi_bus lỗi` trong
log — firmware **cố ý không panic** khi LCD lỗi (vẫn boot để giữ WS/OTA cứu máy).

**⚠️ Bẫy cổng COM (13/09/2026): mở cổng với DTR/RTS kéo lên = chip đứng im, màn đen.**
pyserial (và nhiều terminal) mặc định kéo cả DTR lẫn RTS khi mở cổng. Qua mạch
auto-reset của CH343 trên board này, trạng thái đó **giữ chip không chạy** (UART câm,
màn đen, không boot) cho tới khi thả. Đo trực tiếp:

| Mở cổng | Kết quả |
|---|---|
| DTR=1, RTS=1 (mặc định pyserial) | 0 byte trong 10 s, chip đứng — **màn đen** |
| DTR=0, RTS=0 | chip boot lại (`rst:0x1 (POWERON)`) nếu đang bị giữ |
| DTR=0, RTS=0 lần nữa | chạy tiếp, **không reset** — đúng ý "chỉ nghe" |

Bản đầu của `scripts/p4_readlog.py --no-reset` mở kiểu mặc định → mọi lần "bắt tiếp"
đều 0 byte, và người dùng báo "màn hình đen" ngay giữa lúc test chạm. Đã sửa:
`open_port()` thả DTR/RTS **trước** `open()`; reset = xung RTS 150 ms như esptool. Ai
dùng terminal khác (PuTTY, `idf.py monitor`) mà thấy màn đen + im lặng: kiểm DTR/RTS.

---

## 4. Audio — ĐÃ CHẠY (từ 10/09/2026)

Trước đây khối này bị khoá vì repo chưa có schematic. `docs/HARDWARE-PINOUT.md`
lấp đầy chỗ trống đó, và audio đã lên trên board thật.

### Cấu hình phần cứng

Board dùng **một bus I2S nuôi hai codec** — khác các bo S3 chỉ có ES8311:

```
                    ┌── ES8311 (0x18) ── NS4150B ── loa J1        [phát]
ESP32-P4  I2S0 ─────┤
                    └── ES7210 (0x40) ←─ 2 mic MEMS + AEC ref     [thu]
```

| Tín hiệu | GPIO |
|---|---|
| MCLK | 13 |
| BCLK (chung 2 codec) | 12 |
| LRCK (chung 2 codec) | 10 |
| DOUT → ES8311 `DSDIN` | 9 |
| DIN ← ES7210 `SDOUT1` | 11 |
| `PA_CTRL` → NS4150B `STD` | 3 (HIGH = bật ampli) |

**`ASDOUT` của ES8311 KHÔNG nối** ⇒ ADC của nó vô dụng, toàn bộ đường thu phải
đi qua ES7210. Vì thế `BOARD_AUDIO_USE_ES7210_ADC` **bắt buộc = 1**; để 0 là mic
câm hoàn toàn dù codec vẫn báo init OK.

Đường code ES7210 **đã có sẵn** trong `vimate_es8311.c` (bo GENU v6 cũng dùng cặp
codec này) — chỉ cần khai đúng macro, không phải viết driver mới. Khác biệt duy
nhất: GENU v6 điều khiển ampli qua IO expander TCA9555, board P4 dùng GPIO thẳng
⇒ **không** khai `BOARD_AUDIO_TCA9555_I2C_ADDR` / `_PA_TCA9555_EXIO` để
`vimate_es8311.c` rơi vào nhánh GPIO.

### Một lỗi thật đã sửa: hai đầu I2S lệch sample rate

Lần bật đầu tiên, driver báo thẳng:

```
E I2S_IF: Current mode record conflict sample_rate 16000 with peer mode sample_rate 24000
```

Hai codec treo **chung một BCLK/LRCK** nên chỉ có một tần số khả dĩ. Header đang
để `BOARD_MIC_SAMPLE_RATE 16000` + `BOARD_SPK_SAMPLE_RATE 24000` (giá trị
placeholder từ hồi chưa có schematic) ⇒ đường thu không lên. Đã đặt cả hai =
**24000**, khớp bo GENU v6 và khớp Opus encoder đang chạy 24000Hz.

### WakeNet

Bật cùng bộ model với cây S3 (`CONFIG_SR_WN_WN9_HILILI_TTS`). esp-sr 2.4.6 có
`lib/esp32p4` nên chạy được; `srmodels.bin` 291 KB nằm gọn trong phân vùng
`model` 1 MB.

```
I MODEL_LOADER: Successfully load srmodels
MC Quantized wakenet9: wakenet9l_tts1h8_Hi,Lily or Hi,莉莉_3_0.633_0.639
I vimate.audio: WakeNet ready model=wn9_hilili_tts words=Hi,Lily or Hi,莉莉 sr=16000 chunk=512
I vimate.audio: WakeNet heap: internal=250387->230383 largest=106496->94208 PSRAM=27556412->27231452
```

Giá phải trả: **20 KB internal + 325 KB PSRAM**. WakeNet chạy 16 kHz trong khi
I2S chạy 24 kHz — pipeline tự hạ mẫu, giống hệt bo GENU v6.

### Log khi lên đúng

```
I vimate.audio: I2C probe before-codec-open: ES8311 0x18=ESP_OK, ES7210 0x40=ESP_OK, TCA9555 0x00=ESP_ERR_NOT_FOUND
I vimate.audio: ES8311 duplex channels created 24000Hz 16-bit stereo slots
I ES7210: Enable ES7210_INPUT_MIC1
I ES7210: Enable ES7210_INPUT_MIC2
I vimate.audio: Mic gain 80% -> 28.0dB (es7210)
I vimate.audio: ES8311 codec_dev ready (vol=80%, 24000Hz es8311-out mono + es7210-in stereo-slots, i2c=0, pa=3 on=1)
I vimate.audio: Opus codec ready (enc 24000Hz, dec 24000Hz)
I vimate.audio: WakeNet ready model=wn9_hilili_tts ...
I vimate.audio: Audio pipeline ready
```

`TCA9555 0x00=ESP_ERR_NOT_FOUND` là **bình thường** trên board này — không có IO
expander, và code chỉ dò cho vui.

Bus I2C dùng chung đúng như thiết kế: `I (3117) vimate.main: Touch: reuse I2C bus
from ES8311 codec` — touch không init lại bus.

### Kiểm tra mic + loa không cần kích hoạt — ĐÃ ĐO (11/09/2026)

Trước đây không đo được: `mic_task` ngủ hoàn toàn khi chưa READY + WS, còn log
`WakeNet input rms=` chỉ chạy trong lượt nghe. Giờ bản **diag**
(`CONFIG_VIMATE_DIAG_ENABLE`, tắt ở mọi profile production S3) có **mic idle
probe** trong `audio_pipeline.c`: khi rảnh, 20s đầu đọc liên tục rồi 1 frame/s,
in mỗi 2s → 10s:

```
I vimate.audio: mic idle probe: frames=34 zero_reads=0 rms=71..198 peak=578 dc=-7 hpf_rms=60..190
```

Cách đọc: `rms` cố định ~0 = mic câm; `rms` vài chục–trăm dao động = mic sống;
`dc` lớn cố định = lệch DC; `hpf_rms` = cái VAD thật sự nhìn thấy.

Kết hợp `BOARD_AUDIO_BOOT_TEST_BEEP 1` (beep 880Hz 450ms ở ~4.6s, khối có sẵn
trong `app_main.c`) thì mic nghe lại loa của chính nó qua không khí → **một lần
đọc UART xác nhận cả hai đường**:

```
I (4634) vimate.main:  Speaker test tone start
I (5103) vimate.main:  Speaker test tone done
I (5485) vimate.audio: mic idle probe: frames=34 ... rms=58..2963 peak=7464 dc=7
```

Số đo (phòng làm việc yên, gain mic 80% = 28dB, 2 mic sum-bão-hoà):

| Đại lượng | Giá trị |
|---|---|
| Nền raw | rms 55–140, peak 400–700 |
| Nền sau HPF 120Hz | rms **60–100**, nhảy ngắn 150–220 |
| DC | ≈ 0 (±20) |
| Beep loa 880Hz @ 90% | rms 1900–3000, peak ~7500 |
| Sự kiện tần số thấp (chạm bàn) | raw 1146 / dc 254 → sau HPF **214** |

Beep đã tắt lại (`BOARD_AUDIO_BOOT_TEST_BEEP 0`); bật 1 khi cần kiểm lại.

### Bug thật: HPF mic là passthrough trên P4 — ĐÃ SỬA

Lần đo đầu, `hpf_rms` **bằng đúng** `rms` ở 26/26 giá trị. Không tin, thêm
self-test vào probe: cho sóng 30Hz A=10000 + 1kHz A=1000 qua HPF, mong 30Hz bị
cắt ~24dB (rms 7106 → ~830). Kết quả:

```
mic HPF self-test: rms_in=7198 rms_out=7198
```

Nguyên nhân trong esp-dsp 1.8.0 (và **master upstream vẫn vậy**): trên P4 với
`CONFIG_DSP_OPTIMIZED`, `dsps_biquad_f32` trỏ tới assembly
`dsps_biquad_f32_arp4`. Vòng lọc đúng, nhưng epilogue trả về `mv a0, a6` — a6
**chưa bao giờ được gán** → `esp_err_t` là rác (đo được `0xa0000000`). Mọi `.S`
arp4 khác đều `li a0,0`; chỉ hai file biquad (`f32` + `sf32`) bị. Code của ta
kiểm `!= ESP_OK` rồi `return` **trước khi chép** `out` → `pcm`, nên cả frame đi
qua nguyên vẹn dù log "Mic HPF enabled". Bản S3 (`_aes3`, `movi.n a2, 0`) không
dính.

Sửa: `MIC_HPF_BIQUAD` = `dsps_biquad_f32_ansi` khi `CONFIG_IDF_TARGET_ESP32P4`
(1440 mẫu/60ms, chi phí không đáng kể). Sau sửa:

```
mic HPF self-test: rms_in=7198 rms_out=848 (mong ~7106 -> ~830)
mic HPF self-test: dsps_biquad_f32_arp4 ret=0xa0000000 (RAC — vi the P4 dung ban ansi)
```

Self-test chạy mỗi lần boot bản diag — nếu esp-dsp có sửa, dòng `ret=` sẽ về 0.

### Sàn VAD theo số đo

`BOARD_MIC_VAD_RMS_MIN` mặc định 50 nằm **dưới nền** 60–100: khi server hạ
ngưỡng theo profile ("quiet" −10, "far" −24, "child_soft" −26), VAD coi ồn nền
là giọng và không bao giờ thấy 550ms im lặng để kết thúc lượt. Đặt **120** cho P4
(trên nền, dưới ngưỡng mặc định 160 để profile giọng nhỏ/xa còn tác dụng). genu-v6
đặt 420 vì nền bo đó cao hơn — không chép sang.

### 4.5 Panic `pseudostack overflow` của Opus — lỗi TLS ESP-IDF 5.5.1 trên P4 — ĐÃ SỬA (12/09/2026)

Triệu chứng: gói TTS thật đầu tiên (hoặc gói mic đầu tiên khi bấm nghe) →

```
FATAL ERROR: pseudostack overflow at .../esphome__micro-opus/opus-staged/src/opus_decoder.c:386
abort() → panic → reboot           (encoder: opus_encoder.c:1757)
```

lặp 100 %, nên loa chưa bao giờ kêu và mic chưa bao giờ gửi được. Không phải lỗi
Opus. micro-opus giữ 2 con trỏ pseudostack trong biến `_Thread_local` (`.tbss`), và
app này **không có biến TLS nào có giá trị khởi tạo** (`.tdata` rỗng). Trên P4:

- `sections.ld` chèn `. = ALIGN(_esp_pmp_align_size)` (SPIRAM_RODATA +
  PRE_CONFIGURE_MEMORY_PROTECTION) **vào trong** `.flash.tdata` → section rộng 0x48
  byte toàn đệm, NOBITS, không mang cờ TLS.
- Linker đặt **PT_TLS bắt đầu ở `.flash.tbss`** (`readelf -l`: vaddr 0x483dba80 trong
  khi `_thread_local_data_start` = 0x483dba38); compiler sinh `tp+0`, `tp+4`.
- FreeRTOS (`port.c` `uxInitialiseStackTLS`) lại chép 0x48 byte "tdata" (rác — NOBITS)
  rồi mới đặt bss = 0 SAU đó, tp trỏ đầu vùng chép.

→ mọi biến TLS đọc lệch 0x48 byte: `global_stack` là rác ≠ 0 → Opus tưởng đã cấp
pseudostack → kiểm tràn thấy tràn → `CELT_FATAL`. Xtensa (S3) không dính vì không
có đệm PMP.

**Sửa**: `app_main.c` khai `__attribute__((used)) _Thread_local int g_vimate_tls_anchor = 1;`
→ `.flash.tdata` thành PROGBITS mang cờ TLS, PT_TLS bắt đầu đúng chỗ, `global_stack`
offset 0x58 khớp cách port xếp. Bản diag tự kiểm trong `spk_task`:
`opus decode self-test trong spk_task: 480 mau (tls_anchor=1)` — dòng này trước sửa
panic ngay. **Không xoá biến neo.** Đây là lỗi nền tảng: bất kỳ `_Thread_local` nào
trên P4 với IDF 5.5.1 + SPIRAM_RODATA đều lệch nếu app không có `.tdata`.

### 4.6 Ghi âm chẩn đoán qua UART + kết quả (12/09/2026)

Bản diag có bộ ghi PCM tự kích (`audio_pipeline.c` `wcap_*`), dump base64
`WD:BEGIN tag=… / WD:… / WD:END` qua UART (~10 s/bản), tool
`tools/wake_dump_to_wav.py <log> <out.wav> [tag]` dựng WAV + số đo:

| tag | Nội dung | Kích |
|---|---|---|
| `idle` | 2 s đầu vào WakeNet 16 k lúc rảnh | 15 s sau `WakeNet ON` |
| `wake` | 2,5 s đầu vào WakeNet, pre-roll 0,5 s | ≥3 khung liên tiếp rms ≥ 600 (bỏ tiếng gõ màn) |
| `uplink` | 3 s mono 24 k sau HPF — đúng thứ đang opus lên server | `Mic ON` |
| `afe` | 3 s đầu ra AFE 16 k (đường P4 mới) | tiếng to bền hoặc `Mic ON` |

Kết quả 3 bản ghi 12/09 (phòng làm việc, người dùng cách máy ~50 cm):

- **Giọng vào mic quá nhỏ**: "Hi Lily" đỉnh −13,6 dBFS; câu nói thường đỉnh
  −19 dBFS, rms 300–900; nền 50–250 → SNR ~10 dB. WakeNet `det=0` dù `loud=43`
  khung/10 s; server STT trả `，，`.
- Có **vệt đơn âm ~2,3 kHz cố định** (nhiễu điện) trong cả 3 bản; ù 100–300 Hz.
- VAD RMS (ngưỡng server 120) thấp hơn nền → lượt nghe không bao giờ thấy im lặng →
  `Listen timeout 12000ms`.
- Loa: `SPK WRITE #700 peak=16866` — TTS phát bình thường.

### 4.7 Tận dụng phần cứng mic — ESP-SR AFE ĐÃ CHẠY, AEC ref cứng KHÔNG (12/09/2026)

Người dùng yêu cầu dùng hết phần cứng. Board có ES7210 **4 kênh ADC**: MIC1/MIC2 =
2 mic MEMS, **MIC3 = AEC reference cứng** từ `DAC_OUT` ES8311 (schematic §6), MIC4
trống; P4 dual-core 360 MHz. Kết quả sau một ngày thử trên board:

| Mục | Kết quả |
|---|---|
| 2 mic MEMS | ✅ **cả hai sống** — log `TDM ch rms` từng kênh: MIC1 ≈ 75–180, MIC2 ≈ 170–260 lúc rảnh, cùng dao động khi có tiếng |
| MIC3 ref cứng qua TDM | ⛔ `mic_selected` 4 mic → driver bật TDM (reg12=0x02) nhưng ES7210 **không ghép 4 kênh** vào khung I2S 64 BCLK: word 16-bit → 2 mic MSB-aligned trong 2 slot 32-bit, nửa thấp = 0; word 32-bit (ghi reg11 thẳng) → nửa thấp = nhiễu LSB. Muốn ref cứng phải chuyển CẢ ES8311 + ES7210 + I2S sang khung PCM/DSP bằng ghi thanh ghi thô — để sau |
| AFE `"MMNR"`/`"MMR"` (AEC + BSS 2 mic) | ⛔ lib `esp32p4_less_v3` không kịp: HIGH_PERF fetch 220/312 khung/10 s + `task_wdt IDLE1`; LOW_COST + AEC bật khi loa phát → `Ringbuffer FEED full`, fetch 195/312 |
| AFE `"M"` (tổng bão hoà 2 mic → VAD WebRTC + WakeNet + AGC WakeNet) | ✅ **đang dùng**: 313/313 khung/10 s, không watchdog, RAM nội −14…−23 KB, PSRAM −400 KB |

Cấu hình cuối trong `board_esp32p4_43lcd.h`: `BOARD_AUDIO_AFE_RUNTIME 1`,
`BOARD_AUDIO_AFE_FORMAT "M"`, `HIGH_PERF 0`, `AGC 1`, `VAD_MODE 3`, `LINEAR_GAIN 1.0`,
`BOARD_MIC_GAIN_DB_OFFSET 5.0` (PGA ES7210 28 → 33 dB), `ES7210_TDM4 0`,
`ASR_UPLINK_RAW 0` (ASR nhận output AFE, upsample 16→24 → Opus). Code TDM/4 kênh
(`es8311_codec_read_frames`, `audio_afe_feed_frames24`, thí nghiệm word length) giữ
sau knob để ai làm tiếp ref cứng có sẵn đường.

Vòng hội thoại đã chạy trọn trên board: `Mic ON → AFE VAD speech start → AFE VAD EOT
silence 704ms → STT: "Chỉ sau một ngày ra mắt, Astra đã tạo ra một làn sóng…" → TTS`.
VAD WebRTC thay VAD RMS là cái sửa được vụ "Listen timeout 12000ms" (§4.6).

**Năm bẫy đã gặp trên đường:**

1. `audio_afe.c` không include `boards/board.h` → knob không có tác dụng, AFE tạo
   "MMR" trong khi feed 4 kênh → thêm include.
2. Mở RX `channel=4, 16-bit`: esp_codec_dev 1.4 tự điền `channel_mask=0xF` rồi đưa
   thẳng vào `slot_mask` I2S STD (chỉ hợp lệ 1/2/3) → LL P4 rơi vào `default`, không
   slot RX nào bật → `i2s_channel_read` timeout mãi. Cách đúng theo `es7210.c`:
   `bits=32, channel=2`.
3. **RTC/LP RAM trong heap**: khi RAM nội còn ~100 KB, WakeNet (trong AFE) nhận buffer
   ở 0x50108xxx; lệnh SIMD `dl_esp32p4_sr_pointwise_conv1d_*.S` đọc vùng đó →
   `Load access fault` (MTVAL=0x50108cc0) → panic. `CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP=n`
   trong `sdkconfig.defaults.p4-43lcd` — mất 31 KB heap, không tái phát.
4. `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` (thử để đỡ RAM nội) làm `esp_wifi_init`
   qua esp_wifi_remote (RPC 0x216) trả `ESP_ERR_NO_MEM` → không lên WiFi, boot-loop 30 s.
   Đã bỏ + ghi chú trong defaults.
5. "Voice-activated" của đường AFE (VAD speech 6 khung → tự mở lượt nghe) + VAD nhạy
   trong phòng có người nói → mở lượt liên tục, WS rớt. Khoá sau `VOICE_ACTIVATED_ENABLE`
   (=0, cùng lý do S3 13/06).

**Và một bẫy của chính công cụ chẩn đoán** (§4.6): tool dựng WAV bỏ dòng base64 bị log
chen ngang → mất 57 byte (lẻ) → mọi mẫu int16 sau đó lệch 1 byte → nhìn như **nhiễu
trắng full-scale**. Suýt kết luận sai "AGC clip", "linear gain tràn số", "I2S rác".
Sửa tool (ghép mảnh / chèn đúng 57 byte 0) thì mọi bản ghi đều sạch: giọng F0 154–173
Hz, đỉnh −7…−17 dBFS, đầu ra AFE giống mic thô.

**Chưa kiểm**: "Hi Lily" trong phòng yên (mọi lần ghi trước đều lúc phòng có người
nói chuyện, `speech` 250–313/313 khung; lúc yên VAD về 0). RAM nội: 53 KB, min 31 KB
lúc kết nối WS — sát; cần theo dõi khi có ảnh bài học + TLS.

### WakeNet: ngưỡng và cách chỉnh

`BOARD_WAKE_GAIN_PCT / _DET_THRESHOLD / _DETECTION_AGGRESSIVE` giữ mặc định
(genu-v6: 150 / 0.60 / 1). Cần giọng thật — "Hi Lily" ở 1m và 3m, đọc
`WakeNet input rms=` + tỉ lệ bắt được — mới có số để chỉnh. Thiết bị phải READY
(kích hoạt trong app) thì WakeNet mới chạy.

---

## 5. WiFi + BLE

**ESP32-P4 không có radio WiFi lẫn Bluetooth.**

### WiFi — qua co-processor **ESP32-C5** (ESP-Hosted, SDIO) — ĐÃ CHẠY

Phát hiện khi bring-up: co-processor trên board là **ESP32-C5**, không phải C6
như giả định ban đầu. Board báo thẳng trong log:

```
E transport: Identified slave [esp32c5] != Expected [esp32c6]
assert failed: verify_host_config_for_slave transport_drv.c:500
```

Dây SDIO đi đúng pinout mặc định của ESP-IDF cho P4 và enumerate hoàn hảo:

```
CLK=18  CMD=19  D0=14  D1=15  D2=16  D3=17  RESET_SLAVE=54   (4-bit, 40MHz)
```

Ba điều chỉnh để chạy được (đều đã nằm trong repo):

1. **`esp_hosted` 2.12.\* thay vì 1.4.\*** (`main/idf_component.yml`). Bản 1.4.7 chỉ
   cho SDIO khi slave là ESP32 hoặc C6; chọn C5 là nó ép sang SPI, mà board đi SDIO.
   Bản 2.12 có `ESP_HOSTED_CP_TARGET_ESP32C5` trong `ESP_HOSTED_PRIV_SDIO_OPTION`.

2. **Chọn slave C5 + SDIO** (`sdkconfig.defaults.p4-43lcd`):
   ```
   CONFIG_SLAVE_IDF_TARGET_ESP32C5=y
   CONFIG_ESP_HOSTED_CP_TARGET_ESP32C5=y
   CONFIG_ESP_HOSTED_P4_DEV_BOARD_NONE=y   # không dùng preset dev-board (chân 47-53)
   CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y
   ```

3. **Buffer transport lấy từ PSRAM**:
   ```
   CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y
   ```
   Không có dòng này thì ESP-Hosted chết ngay trước `app_main`:
   ```
   E HS_MP: mempool create failed: no mem
   assert failed: sdio_mempool_create sdio_drv.c:258 (buf_mp_g)
   ```
   mempool xin một khối liên tục `(20 + 11) × 1536 ≈ 48KB`, align 64, cap
   `MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA`. Cấu hình RAM của ta rất chặt tay
   (`SPIRAM_MALLOC_RESERVE_INTERNAL=96KB`, phần còn lại cho TLS/LVGL/lwip).
   Trên P4, GDMA đọc được PSRAM qua cache nên đẩy sang PSRAM là hợp lệ và giữ
   được RAM nội; component tự fallback về RAM nội nếu PSRAM không cấp được.

Toàn bộ `wifi_mgr.c`, `ws_client.c`, `ota_client.c` **không phải sửa** — API
`esp_wifi_*` được `esp_wifi_remote` map sang C5.

### BLE — không khả dụng trên P4

`main/core/ble_wifi_prov.c` bọc trong `#if defined(CONFIG_BT_NIMBLE_ENABLED)`.
Trên P4 nó biên dịch thành stub trả `ESP_ERR_NOT_SUPPORTED`. `app_main.c` vốn đã
xử lý sẵn: rơi về **provisioning qua SoftAP**. Không mất tính năng cấu hình WiFi,
chỉ mất đường BLE. Nếu cần BLE: dùng chính C5 (log báo nó có `BLE only` +
`HCI over SDIO`) — cần thêm việc, chưa làm.

**Icon Bluetooth trên màn (13/09/2026).** Bên trái icon WiFi ở góc trên phải: rune
Bluetooth vẽ bằng một `lv_line` 6 điểm (không cần font biểu tượng), xám = tắt/không có
radio, xanh = đang quảng bá chờ app, xanh đậm + 2 chấm = điện thoại đã nối GATT.
Trạng thái đẩy **theo sự kiện** từ `ble_wifi_prov.c` (`ble_prov_state_t`,
`display_set_ble_state()`), không poll như icon WiFi — vì heartbeat task chưa chạy
trong lúc provisioning (main task `vTaskDelete` trước `telemetry_start()`). Trên P4 stub
trả `BLE_PROV_STATE_UNAVAILABLE` nên icon **luôn xám**; các trạng thái xanh chỉ sáng
trên S3 (hoặc khi P4 có BLE qua C5 HCI-over-SDIO). Đã nạp 13/09: boot sạch 45 s, 0 panic,
`display_setup_ui done`; app +1 KB (0x42b090).

### 5.2b Lỗi SDIO chết khi có máy kết nối — ĐÃ SỬA (cache line 128B → 64B)

Triệu chứng trên board: điện thoại join AP, **nhận IP 192.168.4.2**, rồi ~350 ms
sau ESP-Hosted tự reboot host:

```
I RPC_WRAP: SoftAP mode: station connected with MAC Addr 0a:94:54:45:fd:db
I esp_netif_lwip: DHCP server assigned IP to a client, IP is: 192.168.4.2
E H_SDIO_DRV: sdio_write_task: 0: Failed to send data: 258 70 70
E H_SDIO_DRV: sdio_write_task: 1: Failed to send data: 258 70 70
E H_SDIO_DRV: Unrecoverable host sdio state
I os_wrapper_esp: Restarting host
```

`258` = `0x102` = `ESP_ERR_INVALID_ARG`. Truy vết:
`sdio_write_task` → `hosted_sdio_write_block` → `sdio_write_toio` →
`sdmmc_io_write_blocks` → `card->host.check_buffer_alignment()` trả false.

`sdmmc_host_check_buffer_alignment()` (ESP-IDF 5.5.1) bắt buffer đưa cho SDMMC
phải căn **đúng cache line** của vùng nhớ chứa nó:

```c
alignment = cache line (L1 = 64B cho RAM nội, L2 cho PSRAM)
is_aligned = (ptr % alignment == 0) && (size % alignment == 0)
```

ESP-Hosted cấp **mọi** buffer transport với `HOSTED_MEM_ALIGNMENT_64` — căn 64.
Ta lại đặt `CONFIG_CACHE_L2_CACHE_LINE_128B=y` (chép từ demo của hãng), nên
buffer PSRAM căn 64 **không bao giờ** thoả `ptr % 128 == 0`.

Chỉ nổ khi có trạm kết nối vì gói bắt tay/RPC đi đường buffer khác; gói dữ liệu
qua giao diện AP mới dùng buffer 64-căn này.

**Sửa**: `CONFIG_CACHE_L2_CACHE_LINE_64B=y` — cả RAM nội lẫn PSRAM cùng yêu cầu
64, đúng mức ESP-Hosted cấp. 64B cũng **là mặc định của ESP-IDF** khi L2 cache
256KB (128B chỉ mặc định khi L2 = 512KB), nên đây là quay về mặc định chứ không
phải cấu hình lạ.

---

### 5.3 Luồng trình duyệt khi phát SoftAP — ĐÃ SỬA

**Trước đây** chỉ có 2 handler `/generate_204` và `/hotspot-detect.html`, **không có
DNS** và **không có redirect**. Trên máy thật, hậu quả:

- Điện thoại vừa join AP là bắn HTTP probe tới `connectivitycheck.gstatic.com` /
  `captive.apple.com` / `www.msftconnecttest.com`. DHCP không cấp DNS, cũng chẳng
  có DNS server nào → **hỏng ngay ở bước resolve** → hệ điều hành không bao giờ bật
  cửa sổ đăng nhập. Android còn báo *"Internet có thể không khả dụng"* rồi tự nhảy
  về 4G, cắt luôn kết nối tới thiết bị.
- Người dùng buộc phải tự gõ `192.168.4.1`.
- Mọi URL khác (Windows `/connecttest.txt` + `/ncsi.txt`, iOS
  `/library/test/success.html`, Firefox `/canonical.html`, `/favicon.ico`, hay gõ
  nhầm đường dẫn) trả **404 trần**.
- `httpd` chỉ được **2 socket** và `lru_purge_enable` mặc định = `false`. Trình
  duyệt mở song song nhiều kết nối keep-alive (trang + favicon) → hết socket →
  httpd **từ chối** kết nối mới → `fetch('/scan')` treo, trang kẹt ở
  *"Đang quét..."*. Đây là lỗi hay gặp nhất.

**Bây giờ**, 4 thay đổi:

1. **DNS hijack** — `main/core/captive_dns.c` (file mới): UDP :53, mọi truy vấn A
   đều trả IP của AP. Truy vấn AAAA/HTTPS/SRV trả NOERROR + 0 answer (đúng chuẩn,
   để điện thoại bỏ IPv6 và quay sang IPv4 ngay thay vì chờ timeout).
2. **DHCP cấp DNS + option 114** — `prov_setup_dhcps_dns()` trong `wifi_mgr.c`:
   bật cờ `OFFER_DNS`, trỏ DNS về chính AP, và quảng cáo
   `ESP_NETIF_CAPTIVEPORTAL_URI` (RFC 8910) = `http://192.168.4.1/`. iOS 14+ và
   Android 11+ đọc thẳng option này, mở portal không cần đoán qua probe.
3. **404 → 302** — `httpd_register_err_handler(HTTPD_404_NOT_FOUND, ...)` chuyển
   hướng mọi đường dẫn lạ về trang cấu hình. Trả `ESP_OK` để httpd **giữ** kết nối
   (trả `ESP_FAIL` sẽ đóng socket và trình duyệt báo lỗi thay vì đi theo redirect).
4. **Nới socket** — `max_open_sockets` 2 → 4, bật `lru_purge_enable`, timeout
   recv/send 5s → 15s (vì `/scan` chạy `esp_wifi_scan_start` blocking vài giây), và
   `CONFIG_LWIP_MAX_SOCKETS` 6 → 10 trong `sdkconfig.defaults.p4-43lcd`
   (4 httpd + 1 ctrl + 1 DNS + dự phòng).

`/generate_204` và `/hotspot-detect.html` **cố ý** trả HTML 200 thay vì 204 rỗng /
chữ `Success` — cả Android lẫn iOS coi đó là "bị chặn" và bật cửa sổ đăng nhập.

Log xác nhận khi bật AP:

```
I vimate.wifi: Open AP GENU-Setup-53:C8 started (no password)
I vimate.dns:  Captive DNS listening on :53
I vimate.wifi: Provisioning portal san sang tai http://192.168.4.1/
I esp_netif_lwip: DHCP server started on interface WIFI_AP_DEF with IP: 192.168.4.1
```

`prov_setup_dhcps_dns()` có stop/start DHCP server, nên nó nằm **sau** chốt chặn
`if (s_prov_httpd) return ESP_OK;` — `wifi_mgr_start_provisioning()` được gọi lại
lúc runtime (`app_main.c` khi mất WiFi), chạy lại sẽ cắt lease của điện thoại đang
kết nối.

> Chưa test bằng điện thoại thật (làm vậy phải ngắt WiFi của máy đang dùng).
> Firmware đã lên đủ dịch vụ; bạn join `GENU-Setup-53:C8` bằng điện thoại là
> trang cấu hình phải **tự bật**.

### 5.4 Màn provisioning dựng lại theo CrossInk — 2 mã QR

Tham chiếu: [CrossInk](https://github.com/uxjulia/CrossInk),
`src/activities/network/CrossPointWebServerActivity.cpp` →
`renderServerRunning()` và `startAccessPoint()`.

**CrossInk làm gì ở chế độ hotspot**

| Thành phần | CrossInk |
|---|---|
| AP | `WiFi.softAP("CrossPoint-Reader", nullptr, ch 1, 4 máy)` — mạng **mở** |
| DNS | `DNSServer::start(53, "*", apIP)` + `setErrorReplyCode(NoError)` — bắt mọi tên miền |
| mDNS | `MDNS.begin("crosspoint")` → `http://crosspoint.local/` làm URL chính |
| 404 | AP mode → **302 về `/`**, *trừ* URI bắt đầu bằng `/api/` (giữ 404 thật cho XHR) |
| Màn hình | **HAI QR**: ① `WIFI:T:nopass;S:<ssid>;;` để vào hotspot ② URL để mở trang |

Ý chính đáng học: **QR thứ nhất bỏ hẳn bước mò WiFi và gõ tên mạng** — camera
iOS/Android đọc chuỗi `WIFI:` theo
[spec zxing](https://github.com/zxing/zxing/wiki/Barcode-Contents#wi-fi-network-config)
và hỏi thẳng "Tham gia mạng …?".

**Ta áp dụng lại toàn bộ** trong `main/ui/ui_wifi_setup.c` (viết lại hẳn):

- Bỏ màn chữ cũ (file cũ ghi *"KHÔNG dùng QR widget — QR widget LVGL có vấn đề
  render trên config cũ"*; đó là hạn chế thời S3, LVGL 9.5 trên P4 480×800
  render tốt, `CONFIG_LV_USE_QRCODE=y` đã bật sẵn).
- Hai thẻ QR xếp dọc, mỗi thẻ: tiêu đề bước → QR → chữ bên dưới.
- QR tự co theo màn (`min(H,V)/2`, kẹp 96…200 px) nên vẫn dùng được nếu merge
  ngược về board S3 nhỏ.
- Bật `lv_qrcode_set_quiet_zone(true)` — thiếu vùng lặng là nhiều camera không bắt được mã.

**Ba chỗ cố ý làm khác CrossInk**

1. **QR URL mang thẳng IP `http://192.168.4.1/`**, không phải tên `.local`.
   CrossInk để `crosspoint.local` trong QR và IP làm chữ phụ — làm ngược thì hợp
   lý hơn: QR là thứ để **quét**, nên nó cần chắc chắn nhất, còn tên `.local`
   phụ thuộc mDNS. Trên iOS `.local` được phân giải bằng multicast DNS nên
   **không đi qua** DNS server của ta, `captive_dns.c` không đỡ được.
2. **Không thêm component mDNS.** Với DNS hijack sẵn có, mọi tên miền đã trỏ về
   AP rồi; mDNS chỉ thêm một component + task mà lợi ích trùng lặp.
3. **Escape SSID trong chuỗi `WIFI:`.** Spec bắt buộc chèn `\` trước
   `\ ; , : "`. SSID của ta là `GENU-Setup-53:C8` — **có dấu hai chấm**, không
   escape là điện thoại đọc sai tên mạng. CrossInk không escape vì SSID của họ
   không có ký tự đặc biệt.

Ngoài ra lấy nguyên quy tắc 404 của CrossInk: `prov_uri_is_api()` giữ **404 JSON
thật** cho `/api/*`, `/scan`, `/save`, `/status`; mọi đường khác mới 302. Nếu
redirect cả đường JSON thì `fetch()` nhận HTML rồi `r.json()` ném lỗi parse khó
hiểu thay vì báo đúng "không tìm thấy".

Màn QR cũng được dời xuống **sau** khi HTTP server đã chạy, để người dùng không
quét mã rồi gặp trang chưa sẵn sàng:

```
I vimate.dns: Captive DNS listening on :53
I esp_netif_lwip: DHCP server started on interface WIFI_AP_DEF with IP: 192.168.4.1
I vimate.ui:  WiFi setup screen: SSID=GENU-Setup-53:C8 portal=http://192.168.4.1/
I vimate.wifi: Provisioning portal san sang tai http://192.168.4.1/
```

---

### 5.5 Đại tu provisioning + SoftAP — ĐÃ SỬA, ĐÃ KIỂM TRÊN BOARD

Rà lại toàn bộ luồng cài đặt WiFi. Bốn lỗi dưới đây là **lỗi thật, đo được trên
board hoặc trên trình duyệt thật**, không phải suy đoán từ đọc mã.

#### A. Co-processor C5 tự nối lại mạng cũ giữa lúc đang cài đặt ⚠️ nặng nhất

Xoá sạch NVS của host rồi khởi động: máy vào provisioning đúng như mong đợi,
**nhưng ~8 giây sau vẫn nối vào mạng cũ**:

```
W (3300) vimate.main:  No WiFi configured → enter provisioning mode
I (7010) vimate.wifi:  Open AP GENU-Setup-53:C8 started
I (7177) rpc_req:      Scan start Req
W (7219) rpc_rsp:      Hosted RPC_Resp [0x21e], resp code [12294]   <- 0x3006 ESP_ERR_WIFI_STATE
I (15461) RPC_WRAP:    ESP Event: Station mode: Connected            <- KHONG AI YEU CAU
I (18671) vimate.wifi: Got IP: 192.168.1.90
```

Lặp lại 3 lần, không có điện thoại nào kết nối. Host **không hề** gọi
`esp_wifi_connect()`, và trong log RPC cũng không có lệnh nào như vậy gửi sang.
Nghĩa là **C5 tự nối bằng bản ghi trong flash của chính nó** — thứ mà
`esp_wifi_set_config()` đã ghi xuống từ lần cài đặt trước, và bản ghi đó **sống
sót qua việc xoá NVS của host**.

Hậu quả:
- `esp_wifi_scan_start()` bị từ chối `ESP_ERR_WIFI_STATE` vì STA đang bận nối
  → **danh sách WiFi trong trang cấu hình rỗng**;
- AP phải nhảy sang kênh của router cũ → **đá văng mọi điện thoại đang cài đặt**;
- "quên WiFi" (giữ lâu nút BOOT) **không thực sự quên**.

Sửa, trong `wifi_mgr.c`:
1. `esp_wifi_set_storage(WIFI_STORAGE_RAM)` ngay sau `esp_wifi_init()` — firmware
   này tự quản credential (`nvs_store_get/set_wifi` + `configure_sta` mỗi lần
   khởi động), nên bản sao của driver chỉ là dư thừa. **Chỉ cái này KHÔNG đủ**:
   nó chặn ghi mới, không xoá được bản ghi đã nằm sẵn.
2. `prov_forget_stale_sta_config()` — tạm bật lại `WIFI_STORAGE_FLASH`, ghi đè
   một `wifi_config_t` rỗng lên STA, rồi trả về RAM. Gọi **trước**
   `esp_wifi_start()`; sau khi start thì C5 đã kịp bắt đầu nối rồi.
   Chỉ làm khi host **không còn** creds — provisioning bật lúc chạy (mất WiFi
   tạm thời) thì phải giữ config để STA tự nối lại được.

Kết quả sau khi sửa:

```
W (3303) vimate.main:  No WiFi configured → enter provisioning mode
I (6163) vimate.wifi:  Don credential cu trong co-processor: ESP_OK
I (7072) vimate.wifi:  Open AP GENU-Setup-53:C8 started
I (13641) vimate.wifi: scan: 24 ban ghi -> 15 mang
I (13641) vimate.wifi: da ham nong danh sach WiFi (15 mang, lan thu 1)
```

Không còn `Station mode: Connected` ngoài ý muốn.

#### B. Quét WiFi làm AP tắt ~6 giây — đúng lúc điện thoại vừa vào

`/scan` cũ quét **blocking mỗi lần được gọi**. Chỉ có một radio, nên trong lúc
quét, radio rời kênh của AP: điện thoại đang bám thấy thiết bị "chết", DHCP
renew trượt, kết nối HTTP treo. Và cú quét đó rơi đúng vào lúc điện thoại vừa
mở trang — thời điểm mong manh nhất.

**Đo trên board: một vòng quét mất ~6 giây** (7.7s → 13.6s), không phải ~1s như
ước lượng ban đầu. RPC qua SDIO sang C5 đắt hơn nhiều so với WiFi chạy tại chỗ.

Sửa:
- **Cache 30 giây.** `/scan` trả cache; chỉ `?force=1` (nút "Quét lại") mới quét thật.
- **Hâm nóng ở nền** (`prov_prewarm_task`): quét sẵn lúc chưa ai kết nối, thử lại
  8 lần × 3s vì ngay sau khi AP lên radio còn bận. Task riêng chứ không gọi
  thẳng — màn QR không được phép đợi một việc chỉ để tối ưu.
- **Chặn khi đang thử credential** (`candidate_is_active()`): lúc đó STA cần
  toàn quyền radio.
- **Dwell 40–80ms/kênh** thay vì 120ms mặc định.
- **Gộp SSID trùng**, giữ bản mạnh nhất: router mesh/dual-band phát cùng tên trên
  nhiều kênh nên danh sách cũ hiện 3–4 dòng trùng tên. Thực đo: 24 bản ghi → 15 mạng.
- Trang báo trước "thiết bị tạm ngắt vài giây, đừng tắt" khi bấm Quét lại.

#### C. `receive_request_body()` treo vô hạn → khoá cả portal

```c
if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;   /* vòng lặp vô hạn */
```

`esp_http_server` chỉ có **một** task xử lý. Một client gửi POST dở dang rồi im
lặng sẽ giữ task đó mãi, và **mọi máy khác mất trang cài đặt**. Đã chặn ở 2 lần
chờ (đồng hồ reset mỗi khi có dữ liệu chạy về), `recv_wait_timeout` hạ 15s → 8s,
nên trường hợp xấu nhất là ~24s rồi bị cắt.

#### D. Trang web hỏng ngầm — bắt được bằng trình duyệt thật, không bằng biên dịch

Trình biên dịch C chỉ thấy trang HTML là **một chuỗi ký tự**; nó không soát được
gì bên trong. Nên trang được trích ra khỏi `wifi_mgr.c` rồi chạy trên Edge thật
với một server giả đóng vai firmware (`scripts/` không chứa bộ này — nó nằm ở
scratchpad phiên làm việc, xem cuối mục). Bắt được:

1. **Thuộc tính SVG không đặt nháy đứng ngay trước `/>`.**
   `<circle cx=12 cy=12 r=3/>` → bộ phân tích HTML đọc giá trị là `"3/"` và bỏ cả
   thuộc tính. Con ngươi của icon con mắt **không vẽ ra**. Tệ hơn:
   `fill=${...}/>` → `fill="currentColor/"`, và cái này **trình duyệt không báo
   lỗi gì cả** — mọi vạch sóng lặng lẽ tô đen như nhau, chỉ báo cường độ mất
   sạch ý nghĩa. Phải có khoảng trắng: `r=3 />`.
2. **Nhảy bố cục làm hụt cú chạm nút gửi.** Người dùng gõ mật khẩu ngắn → chạm
   "Kiểm tra và kết nối" → cú chạm làm ô nhập mất tiêu điểm → lỗi hiện ra → nút
   **tụt xuống một dòng** → ngón tay nhả ra ở chỗ không còn nút nữa → phải chạm
   lần hai. Sửa: `.fe { min-height: 21px }` — thẻ báo lỗi luôn giữ chỗ sẵn, chỉ
   đổi chữ, không bao giờ ẩn/hiện.

#### E. Màn hình thiết bị giờ có trạng thái

Trang web **không bảo đảm** báo được kết quả: lúc STA thử router ở kênh khác, AP
nhảy kênh theo và điện thoại rớt ngay giữa lúc đang poll `/status`. Màn hình
thiết bị là kênh phản hồi duy nhất còn chắc chắn, nên `ui_wifi_setup.c` được
viết lại thành máy trạng thái:

| Trạng thái | Màn hình |
|---|---|
| `WAITING` | 2 mã QR + "Đang chờ điện thoại kết nối" |
| `CLIENT` | bước 1 đổi thành "đã vào WiFi thiết bị — xong", nhắc quét mã 2 |
| `VALIDATING` | ẩn QR, hiện spinner + "điện thoại có thể tạm rớt — đó là bình thường" |
| `ERROR` | mã lỗi dịch sang tiếng Việt **kèm bước tiếp theo**; 8s sau tự trả lại QR |
| `SUCCESS` | "Kết nối thành công" + địa chỉ IP |

Nối vào bằng `mirror_status_to_screen()` trong `set_provision_status()` — **không**
dùng `wifi_mgr_set_provision_observer()` vì `ble_wifi_prov.c` đã chiếm chỗ quan
sát duy nhất đó (trên bản S3).

Số máy đang bám AP lấy từ `WIFI_EVENT_AP_STACONNECTED/STADISCONNECTED` →
`ui_wifi_setup_set_client_count()`. Không có mốc này thì màn hình đứng yên suốt,
người dùng không biết bước 1 đã xong hay chưa và thường quét lại mã 1 liên tục.

#### F. Sửa theo skill ui-ux-pro-max

Tra `--domain ux` cho từng hạng mục rồi sửa:

- **Tương phản.** 3 cặp màu **không đạt** ngưỡng WCAG 4.5:1 cho chữ thường, đã thay:
  | Chỗ dùng | Cũ | Tỉ số | Mới | Tỉ số |
  |---|---|---|---|---|
  | chữ mờ trên nền | `0x6b7280` | 4.1:1 ✗ | `0x4b5563` | 6.3:1 ✓ |
  | tiêu đề trên nền | `0xb45309` | 4.2:1 ✗ | `0x92400e` | 6.0:1 ✓ |
  | chữ "thành công" | `0x16a34a` | 3.3:1 ✗ | `0x166534` | 5.3:1 ✓ |
- **Vùng chạm ≥ 44px.** Nút "Quét lại" cũ chỉ ~31px. Giờ mọi nút/ô nhập ≥ 44px,
  dòng mạng WiFi 52px — có test tự động khẳng định.
- **Không dùng emoji làm icon.** `🔒` và `↻` thay bằng SVG inline (5 icon, nét 2px
  đồng bộ). Emoji phụ thuộc font hệ điều hành, mỗi máy hiện một kiểu, không tô
  màu theo giao diện được.
- **Điều khiển đúng ngữ nghĩa.** Dòng mạng WiFi từ `<div onclick>` thành
  `<button aria-pressed>` — bàn phím dùng được, trình đọc màn hình đọc được.
- **Nhãn gắn với ô nhập** (`for`/`id`), lỗi gắn liền ô (`aria-describedby` +
  `aria-invalid`), thông báo là `role=status`, lỗi là `role=alert` + chuyển tiêu điểm.
- **Nút hiện/ẩn mật khẩu** (`aria-pressed`, đổi cả `aria-label`).
- **Kiểm tra khi rời ô**, nhưng **xoá lỗi ngay khi gõ đủ dài** — để lỗi cũ nằm đó
  trong lúc người dùng đã sửa xong là vô lý.
- **Chỉ báo 3 bước** thay vì không có tiến trình nào.
- **Giao diện tối** (`prefers-color-scheme`), tính tương phản riêng cho từng chế độ.
- **Tôn trọng `prefers-reduced-motion`** (tắt animation xương giả).
- **Mỗi lỗi kèm bước tiếp theo**, không chỉ báo hỏng.

#### G. Vặt

- `esp_wifi_set_ps(WIFI_PS_NONE)` khi provisioning — mặc định `MIN_MODEM` cho
  radio ngủ giữa các beacon, thêm độ trễ cho mọi request và làm DHCP/DNS thỉnh
  thoảng trượt. Provisioning chỉ vài phút nên đổi điện năng lấy độ phản hồi.
- `/favicon.ico` → **204** thay vì rơi vào 404→302 rồi tải cả trang HTML về làm
  icon (tốn băng thông và ăn một socket trong số 4).
- Tên SSID được đổ bằng `textContent`, **không** nối chuỗi HTML — tên mạng do
  người lạ đặt, một AP tên `<img onerror=...>` hoàn toàn hợp lệ. Có test khẳng định.

#### Đã kiểm những gì

| Hạng mục | Cách kiểm | Kết quả |
|---|---|---|
| Build | `scripts/build_p4_43lcd.sh` | `EXIT=0`, app 3,01 MB / 4,5 MB |
| Cú pháp JS của trang | `node --check` trên phần `<script>` trích ra | OK |
| Mọi `$('id')` JS gọi đều có trong HTML | script đối chiếu | OK |
| Token CSS định nghĩa ↔ sử dụng | script đối chiếu | khớp 1:1, 10/10 |
| Luồng trang (chọn mạng, mật khẩu, lỗi, thành công, mạng mở) | Playwright + Edge + server giả | **31/31 đạt** |
| Không tràn ngang ở 375px | Playwright | thừa 0px |
| Giao diện sáng / tối | ảnh chụp | đạt |
| Thiết bị vào provisioning | UART COM47 | 1 boot, 0 panic |
| Hâm nóng danh sách WiFi | UART | `24 ban ghi -> 15 mang`, lần thử 1 |
| Không còn tự nối mạng cũ | UART | không còn `Station mode: Connected` |

**Chưa kiểm:** đường đi trên điện thoại thật của bản cuối (quét QR → captive
portal tự bật → gửi → xem trạng thái trên màn hình thiết bị). Bộ test Playwright
phủ logic trang, nhưng **không** phủ việc iOS/Android có tự bật cửa sổ captive
portal hay không — cái đó phụ thuộc DNS hijack + option 114 + handler probe,
chỉ máy thật mới trả lời được.

> ⚠️ **Mật khẩu WiFi nằm trong NVS dưới dạng chữ thường.** Lúc gỡ lỗi mục A, tôi
> đọc vùng NVS ra file và mật khẩu WiFi hiện nguyên văn (namespace `vimate`, key
> `wifi_cfg`). File dump đã xoá ngay. Bảng phân vùng dev (`partitions.csv`) không
> có NVS encryption; `partitions.prod.csv` **có** (`nvs_key ... encrypted`). Ai
> lấy được flash của board dev là đọc được mật khẩu WiFi nhà.

---

### 5.6 Phiên ghi log 15/09 tối — 4 lỗi thật, đã sửa và kiểm trên board

Người dùng đổi WiFi sang mạng "FBT" và yêu cầu ghi log xem lỗi. Ba lần ghi liên tiếp
lộ ra bốn lỗi độc lập, sửa lần lượt, lần ghi cuối 150 s: 0 panic, `Got IP` sau 3 s,
WS ready 24 s, AFE 313/313, WakeNet ON.

1. **`Stack protection fault` trong `vimate_main` ngay sau `QR size=88`** (màn WiFi
   setup). `addr2line`: `lv_tjpgd.c:82 decoder_info` ← `lv_image_decoder_get_info`.
   Hàm này khai `uint8_t workb[TJPGD_WORKBUFF_SIZE]` (4 KB) trên stack; frame được
   cấp **ngay khi vào hàm** dù nguồn không phải file .jpg, và LVGL gọi nó cho **mọi**
   `lv_image_set_src` (kể cả canvas của `lv_qrcode`). `vimate_main` 6144 B không đủ
   (đây cũng là lý do main task từng phải lên 10240, §8 cuối). Firmware giải JPG bằng
   `esp_new_jpeg` (`util/jpeg_to_image.c`), không bao giờ đưa đường dẫn .jpg cho LVGL
   → **tắt `CONFIG_LV_USE_TJPGD`** trong profile P4 (app −4 KB). Không nâng stack.
2. **`set_config AP: ESP_ERR_INVALID_ARG`** (3/4 lần rơi về SoftAP) → "Provisioning
   start fail … reboot sau 30s". Xảy ra khi STA **đang associate** (kể cả "associate
   mà không có IP"): một radio, AP kênh 1 khác kênh STA → C5 từ chối. Sửa
   `wifi_mgr_start_provisioning`: `esp_wifi_sta_get_ap_info` → AP dùng kênh của STA;
   vẫn INVALID_ARG thì `esp_wifi_disconnect` + 300 ms rồi thử lại kênh 1.
3. **Associate xong không bao giờ có IP**: `Station mode: Connected` rồi im 53 s →
   60 s sau app mới rơi về provisioning (mà lỗi 2 chặn). Log mới in
   `STA associated ssid=FBT ch=36` — **kênh 36 = 5 GHz**: C5 hai băng, vào FBT ở
   5 GHz thì DHCP không xong (3/3 lần), mọi mạng 2,4 GHz trước đó đều Got IP. Hai lớp:
   - **canh gác DHCP** (`WIFI_DHCP_WAIT_MS` 20 s, esp_timer): CONNECTED mà chưa GOT_IP
     → `esp_wifi_disconnect` để đường retry (associate lại → DHCP lại) chạy;
   - **khoá 2,4 GHz** `BOARD_WIFI_BAND_2G_ONLY 1` (P4; S3 mặc định 0):
     `esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY)` — chỉ nhận **sau**
     `esp_wifi_start()` (gọi sau init: `ESP_ERR_WIFI_NOT_STARTED`), gọi lại trước mỗi
     reconnect; CONNECTED với `channel > 14` thì ngắt ngay. Kết quả: `band 2.4 GHz
     only: ESP_OK` → `ch=8` → `Got IP: 192.168.1.23` sau 3,2 s. Muốn 5 GHz phải thử
     lại sau khi nâng slave C5 (2.7.0 → 2.12).
4. **`A stack overflow in task captive_dns`** ngay dòng `Captive DNS listening` (task
   3072 B, 1040 B buffer cục bộ + `ESP_LOGI`/vfprintf + `lwip_recvfrom` trên RISC-V).
   Buffer sang PSRAM (sống suốt provisioning, free khi thoát), stack 3072 → 3584, log
   `stack HWM` lúc dừng. Lưu ý: chuỗi `grep` kiểm crash phải có cả `stack overflow`
   (mẫu cũ bỏ sót → hai lần "crash=0" giả).

Số đo phiên cuối (WS + TLS + AFE + tải ảnh bìa khoá học): `diag heap internal=48535
min=25911` — free đạt mốc ≥ 45 KB, **min 25,9 KB dưới sàn 30 KB** (13/09: 28,0 KB,
§9.2); đáy rơi lúc TLS + tạo AFE + 3 lần `image cache miss — HTTP fallback` bìa khoá
học chồng nhau, chưa quy được cho thay đổi nào trong ngày (bộ mặt/SD không cấp RAM
nội). Mặt: `connect_success` chạy được 11 khung thì Home phủ lên → tick 1 s pause
(đúng thiết kế) — mặt chỉ thấy khi không có màn Home/app đè.

## 6. Màn hình: hướng, chống xé hình, panel kẹt, MP4 player

Panel native **dọc 480×800**, DPI video mode — **không xoay được bằng phần cứng**
(`esp_lcd_panel_swap_xy` báo *not supported*, dòng lỗi đó ở boot là của port gọi,
vô hại). Từ 12/09/2026 firmware chạy **ngang 800×480** (§6.6); bản bring-up 10–11/09
chạy dọc. Đổi qua lại bằng một knob:

```c
#define BOARD_LCD_ROTATION 270   /* board_esp32p4_43lcd.h: 0 = dọc, 90 / 270 = ngang */
```

`BOARD_LCD_H_RES/V_RES` giờ là kích thước **logical** (LVGL), suy ra từ knob đó;
`BOARD_LCD_NATIVE_W/H` = panel. UI VIMATE thiết kế cho màn ngang (S3: 320×240,
480×320) nên 800×480 là bố cục đúng của nó (home 6 cột, bottom bar, ảnh bài học
`sz=800x480` server đã gửi đúng ngay lần đầu).

### 6.1 Chống xé hình — ĐÃ BẬT (11/09/2026)

Ghi chú cũ ở đây và trong `display.c` nói *"cần tín hiệu TE (0x35)"* — **sai**.
TE chỉ dành cho panel command mode. Panel này chạy DPI video mode: host tự biết
cuối khung từ ISR DMA, `esp_lvgl_port` 2.8 làm đúng việc đó:

- `dpi_cfg.num_fbs = 2` → driver DPI cấp 2 frame buffer 768KB trong PSRAM.
- `lvgl_port_add_disp_dsi(..., .avoid_tearing = true)` → port lấy 2 fb qua
  `esp_lcd_dpi_panel_get_frame_buffer()`, đăng ký `on_refresh_done` thay cho
  `on_color_trans_done`, LVGL vẽ **thẳng vào fb**.
- `.direct_mode = true` → chỉ vẽ vùng bẩn, LVGL tự chép vùng bẩn của khung trước
  sang fb kia (`refr_sync_areas`). `full_refresh` sẽ rasterize cả 480×800 mỗi
  khung — đắt hơn nhiều.
- Flush cuối: `esp_lcd_panel_draw_bitmap()` với con trỏ fb của chính driver →
  driver chỉ `msync` + đổi `cur_fb_index`; ISR cuối khung mới chuyển link-list DMA
  → đổi đúng ranh giới khung. Port chờ `on_refresh_done` rồi mới báo LVGL xong.

Lợi phụ: **bớt 768KB PSRAM** (không còn 2 buffer LVGL riêng) và bỏ memcpy
768KB/khung từ buffer LVGL sang fb. Đo lúc 60s: PSRAM trống 27,57MB (bản 10/09:
27,21MB) dù đã cõng thêm MP4.

Log xác nhận: `DSI: avoid_tearing=1 num_fbs=2 direct_mode=1`. Nếu `num_fbs`
mà = 1, `get_frame_buffer(…, 2, …)` fail → `LVGL display add failed` ngay lúc boot.

**Chưa có mắt người xác nhận hết xé** — cần nhìn màn khi cuộn danh sách / chạy GIF
cảm xúc. Tắt nhanh để so sánh: `#define BOARD_LCD_DSI_AVOID_TEARING 0` trong
`board_esp32p4_43lcd.h`.

### 6.2 Panel kẹt sau reset MCU — ĐÃ SỬA (11/09/2026)

Sáng 11/09 board **không boot được**: task `main` quay vô hạn, `task_wdt` bắn
mỗi 5s, không một dòng log display nào. Nạp lại **bin hôm trước** (đã chạy tốt)
— **treo y hệt**. Vậy không phải firmware; `addr2line` chỉ vào:

```
mipi_dsi_hal_host_gen_read_short_packet   mipi_dsi_hal.c:210
mipi_dsi_host_ll_gen_is_read_fifo_empty   mipi_dsi_host_ll.h:690
```

Tức lệnh **đọc** DCS (driver ST7102 đọc ID `0x04` để in log) chờ panel trả lời
BTA mà panel không trả lời, và HAL của IDF **poll không timeout**. Ba mảnh ghép:

1. `LCD_RST` (GPIO22) trên board chỉ có RC — R70 10K lên 3V3 + C95 1uF. Panel
   **chỉ tự reset khi cấp nguồn**; reset MCU (nạp firmware, watchdog, nút RST1)
   để nguyên panel ở trạng thái của lần chạy trước. Board header từng ghi
   *"display.c reset một lần trước khi mở DSI"* — **không có dòng code nào như
   thế**, ghi chú sai.
2. Driver ST7102 đọc ID **trước** khi gọi `esp_lcd_panel_reset()`, nên panel kẹt là
   boot nào cũng kẹt, không firmware nào qua được.
3. Hàng chục lần nạp lại hôm trước đều qua vì panel tình cờ còn ở trạng thái đáp
   được; đến một lần nào đó (DSI bị cắt clock giữa video mode) thì không.

Sửa hai chỗ:

- `display.c`: xung reset cứng GPIO22 (10ms thấp, chờ 120ms) **trước** khi acquire
  LDO DPHY / mở DSI bus. Log: `LCD hard reset GPIO22 truoc khi mo DSI`.
- `components/esp_lcd_st7102/esp_lcd_st7102_mipi.c` (component local, không phải
  managed): **bỏ lệnh đọc ID**. Nó chỉ để in `LCD ID: 80 A0 FB`, nhưng là điểm treo
  không lối thoát — trái với nguyên tắc đã đặt trong `display.c` là *"LCD lỗi vẫn
  phải boot để giữ WS/OTA"*. Ghi DSI không cần BTA nên bảng init vẫn gửi bình
  thường; panel chết thì màn đen nhưng thiết bị vẫn lên mạng.

### 6.3 Bộ đếm flush trong diag

Trước đây display **đứng hình là không thể phát hiện qua log** (không có đếm
khung; flush chờ vsync không về thì LVGL treo im lặng, task_wdt không canh task
đó). Giờ `display.c` đếm `LV_EVENT_FLUSH_FINISH` — với avoid_tearing, sự kiện này
chỉ phát sau khi DMA đã đổi khung, tức "khung đã lên panel thật". Diag in mỗi 60s:

```
I vimate.main: diag heap internal=... psram=... task_stack=1240 flush=6
```

`flush` đứng yên trong khi UI đáng ra đang đổi (đồng hồ, GIF cảm xúc) = display
treo. API: `display_flush_count()`.

### 6.4 MP4 player — ĐÃ BẬT, CHƯA PHÁT THỬ (11/09/2026)

Bật cho ngang genu-v6 (bo S3 gần nhất, cũng SD + ES8311/ES7210). Bốn dependency
và version giống hệt S3; cả bốn đều ship lib cho `esp32p4`
(`esp_h264` còn có HAL phần cứng P4). Đường link qua được nhờ workaround §2 bẫy 4.

Ba điều chỉnh riêng cho P4:

- **Siết decoder audio** về đúng profile ghi trong Kconfig
  `VIMATE_MP4_PLAYER_ENABLE` (AAC/MP3/FLAC + PCM). Bật hết như genu-v6 thì
  `libesp_audio_codec.a` chiếm 475KB, app còn **6%** slot; siết xong còn 12%
  (555KB), ngang genu-v6 (520KB). Encoder không cần tắt: `mp4_player.cpp` không gọi
  `esp_audio_enc_register_default()` nên linker không kéo vào. Decoder MJPEG mềm
  cũng tắt — P4 có JPEG codec cứng.
- **Hoán vị byte có điều kiện.** Player luôn swap RGB565 (đúng cho panel SPI/i80
  big-endian của S3, `BOARD_LCD_SWAP_BYTES 1`); panel DPI nhận little-endian như
  LVGL → trên P4 màu sẽ sai. Giờ `swap_bytes = ... && BOARD_LCD_SWAP_BYTES`.
- **Vẽ qua `display_panel_blit()`** thay vì `esp_lcd_panel_draw_bitmap()` trực
  tiếp. Với 2 fb + direct mode, `draw_bitmap` bằng buffer ngoài chỉ memcpy vào fb
  **đang hiển thị**; flush kế tiếp của LVGL đổi sang fb kia là video biến mất
  (chớp). Hàm mới ghi vào **cả hai** fb rồi `esp_cache_msync()`; board SPI/i80 vẫn
  gọi `draw_bitmap` như cũ. Caller giữ `display_lock()` nên LVGL không vẽ chồng.

Chi phí tĩnh: **~1MB PSRAM** — board chạy XIP từ PSRAM
(`CONFIG_SPIRAM_XIP_FROM_PSRAM`), app phình 443KB vượt một ranh giới map nên pool
giảm đúng 1MB (29824K → 28800K); **~25KB RAM nội** do
`CONFIG_ESP_H264_DECODER_IRAM=y` đặt `libtinyh264` vào IRAM (cây S3 cũng `=y`,
giữ để đồng nhất; đây là đòn bẩy nếu sau này thiếu RAM nội).

**Chưa phát thử được**: player chỉ chạy khi server gửi `video_control_play`, và
file phải nằm trên thẻ SD — thẻ chưa mount (§7). Boot sạch, không init gì lúc
khởi động.

---

### 6.5 Emoji cảm xúc: Google Noto Animated Emoji thay bộ thỏ (11/09/2026)

Ảnh chụp màn Sẵn sàng cho thấy mặt thỏ "Chuppy" 128px bị **phóng 6,25× nearest-neighbour
và cắt hai mép**: nhánh `#else` của `EMOJI_TARGET_PX` trong `display.c` lấy cạnh LỚN
(800) trên màn dọc 480×800, ảnh 800×800 tràn khỏi widget 480 rộng. Người dùng chọn thay
cả 13 cảm xúc bằng emoji Unicode chuẩn.

**Nguồn:** Noto Emoji Animation của Google — https://googlefonts.github.io/noto-emoji-animation/
— giấy phép **CC BY 4.0** (ghi nguồn là đủ, dùng thương mại được). Tải thẳng từng file,
không cần clone repo: `https://fonts.gstatic.com/s/e/notoemoji/latest/<codepoint>/512.gif`
(đã kiểm 21/21 mã server có thể gửi đều có bản động).

**Tool:** `tools/fetch_noto_gifs.py` (Pillow + urllib). Mặc định 160 px / 6 khung / 32
màu, một bảng màu chung cho mọi khung, alpha 1-bit, disposal 2, delay ≥150 ms; ghi thẳng
vào `spiffs_emo_image/<key>.gif`, in bảng size, **fail** nếu tổng > 440 KB hoặc file
> 460 KB; `--preview x.png` xuất contact sheet để xem trước; `--all21` sinh thêm 8 cảm xúc
còn thiếu (muốn firmware dùng thì còn phải thêm vào `GIF_EMBED_FILES` + `s_embedded_gifs[]`).

| key | emoji | key | emoji | key | emoji |
|---|---|---|---|---|---|
| neutral | 🙂 U+1F642 | happy | 😊 U+1F60A | sad | 😢 U+1F622 |
| angry | 😠 U+1F620 | confused | 😕 U+1F615 | surprised | 😮 U+1F62E |
| sleepy | 😴 U+1F634 | embarrassed | 😳 U+1F633 | thinking | 🤔 U+1F914 |
| relaxed | 😌 U+1F60C | funny | 😂 U+1F602 | delicious | 😋 U+1F60B |
| loving | 😍 U+1F60D | | | | |

**Vì sao chỉ 160 px / 32 màu.** Bộ GIF nằm ở **cả hai nơi**: SPIFFS `emo_spiffs` 512 KB
(dùng được ~448 KB) *và* embed trong app (`main/CMakeLists.txt` `GIF_EMBED_FILES`). Noto
có gradient tô bóng nên GIF nặng gấp 3–6 lần bộ thỏ. Đo với bảng màu chung:

| Cấu hình | KB/file | ×13 |
|---|---|---|
| 240px / 8 khung / 64 màu | ~90 | ~1200 KB |
| 200px / 6 khung / 32 màu | ~40 | ~530 KB |
| **160px / 6 khung / 32 màu** | **23–31** | **346 KB** ✅ |

Bảng màu **cục bộ** từng khung (mặc định Pillow khi quantize riêng) làm file to gấp đôi
(240/8/64: 216 KB → 90 KB khi dùng bảng chung). Player on-device kẹp delay ≥150 ms nên
hơn 6–8 khung chỉ tốn chỗ. Nhìn contact sheet 32 màu: gradient còn mượt, chấp nhận được.
Muốn nét hơn: `--colors 64` (+25 %, sát 440 KB) — knob có sẵn. Về lâu dài Noto còn cấp
`lottie.json` (~22 KB/emoji, vector) nhưng cần `LV_USE_LOTTIE`/ThorVG + đổi widget.

**`display.c`:** thêm nhánh `#elif BOARD_LCD_USE_MIPI_DSI` — `EMOJI_TARGET_PX` = cạnh
NHỎ (480). `show_gif_locked` đã tự tính scale theo bề rộng nguồn nên 160 × 3 = 480,
hệ số nguyên, `antialias=false` như cũ. Nhánh `#else` của S3 không đổi.

**Kết quả build:** app 4,16 → 4,19 MB (+31 KB, còn 11 %), `emo_spiffs.bin` đóng gói được,
boot: `emo spiffs ready: 362444/474641 bytes used`. Cấu trúc 13 file đã kiểm bằng parser
riêng: GIF89a 160×160, GCT 32 màu, 6 khung, disposal 2, index trong suốt 0, không
interlace; khung 2–6 có LCT (Pillow luôn ghi cho khung nối thêm) — `gifdec.c:657-662`
đọc được.

**Cỡ mặt:** `BOARD_EMOJI_TARGET_PX` trong board header (12/09: 400 px trên màn ngang
480 cao, theo ý người dùng "bé hơn tí"; mặc định = cạnh nhỏ). GIF 160 px × 2,5 —
nearest-neighbour hàng xen 2/3 px, với emoji gradient không thấy.

**Chưa thấy trên máy:** emoji chỉ vẽ ở màn Sẵn sàng, thiết bị đang chờ kích hoạt
(`activation pending`, mã hiện trên màn). Kích hoạt trong app rồi nhìn: mặt 🙂 căn giữa,
không cắt mép, chuyển động ~6,7 fps. Bộ thỏ cũ sao lưu ở `tools/emo_backup_chuppy_128/`
(chép ngược vào `spiffs_emo_image/` + build là về như cũ).

---

### 6.5b "Mặt robot" toàn màn thay emoji Noto — ĐÃ NẠP, ĐO TRÊN BOARD (15/09/2026)

Người dùng đưa bộ 32 clip GIF mới vào `docs/01_gif/` (480×320, nền đen, hai mắt cyan
kiểu robot; `docs/02_png/` là 32 khung PNG gốc mỗi clip) và yêu cầu làm lại hệ thống
emoji quanh bộ này. Thiết kế thay đổi hẳn: **cả màn là mặt** (không còn emoji đặt giữa
nền kem), trạng thái máy cũng có clip riêng (boot, kết nối, nghe, nghĩ, nói, rảnh).

**Bộ clip (32):** `boot_up`, `connect_success/fail`, `idle_normal/bored/look_around/
sleepy`, `listening`, `thinking`, `speaking`, 10 `emo_*` (normal, happy, sad, angry, cry,
confused, surprised, scared, shy, wink), 12 `sym_*` (alarm, reminder, timer, timer_digits,
countdown, celebrate, event, music, weather_sun/cloud/rain/snow). Bản sáng 15/09: 32
khung/clip, 9,7 MB. **Bản tối 15/09 (đang dùng): 128 khung PNG/clip, GIF 16–64 khung
(đã bỏ khung trùng), delay 30–1000 ms, 11 MB**, 1766 khung tổng.

**Asset — `tools/build_face_gifs.py` → `spiffs_face_image/` (3,9 MB, 1561 khung):**
- `--min-delay 40` (mặc định): khung nguồn ngắn hơn 40 ms **gộp** vào khung kế (bỏ
  khung, cộng delay → tổng thời lượng clip không đổi): 1766 → 1561 khung, 4,36 → 3,9 MB.
  40 ms = `BOARD_FACE_GIF_MIN_FRAME_MS` = mức máy decode kịp; để nguyên thì lvgl_gif kẹp
  lên sàn và clip chạy chậm. Sàn 50 ms → 2,9 MB / 1112 khung, 60 ms → 2,5 MB / 904.
- `verify()` so **tổng thời lượng** thay vì số khung (Pillow gộp khung trùng sau khi
  co ảnh và cộng delay — hợp lệ).
- canvas **400×240**, nội dung co LANCZOS về 360×240 (giữ 3:2, không cắt — bbox nội
  dung gốc chạm y = 26..308/320), hai lề 20 px đen. Máy nhân đôi pixel → **800×480 =
  đúng màn logical**, hệ số nguyên nên không có hàng xen.
- **16 màu**, một bảng màu chung cho cả clip (soi crop phóng 2× không thấy vân: nền
  đen + cyan + glow). 32 màu = 3,8 MB, **không vừa** partition.
- Pillow 12 ghi `disposal=1` + delta: khung 2..N chỉ là hộp khác biệt, có LCT 17 màu và
  index trong suốt cho pixel không đổi — `gifdec.c` đọc đúng (LCT + tindex, đã parse
  lại 32 file). Giữ nguyên delay từng khung (70–910 ms), sàn 40 ms.
- Tool in bảng size, **fail** nếu tổng > 90 % partition hoặc file > 460 KB;
  `--preview x.png` contact sheet.

**Partition riêng cho P4 — `partitions/partitions.p4-43lcd.csv`** (chọn qua
`sdkconfig.defaults.p4-43lcd`), bản 2 (tối 15/09, cho bộ 128 khung): `emo_spiffs`
512 K → **0x560000 (5,375 MB)**, lấy từ `asset_spiffs` 3 M → 512 K (pack premium: chỉ
mount, chưa có sync), `lesson_spiffs` 2 M → 512 K (cache ảnh bài học, có SD + tải theo
nhu cầu) và `model` 1 M → 512 K (`srmodels.bin` 291 KB = WakeNet hilili + NS/VAD webrtc;
thêm MultiNet thì phải mở lại — esptool báo lỗi ngay khi bin > partition, không im).
NVS/otadata/phy/ota_0/ota_1 giữ offset → token + WiFi sống qua reflash. Boot:
`emo spiffs ready: 4052395/5173361` (78 %). (Bản 1 sáng 15/09: 0x3E0000, asset/lesson
1 M, model 1 M — cho bộ 32 khung 2,87 MB.)
⚠ Đổi bảng partition = nạp USB một lần (`flash_p4_43lcd.bat` nạp cả bảng + 3 SPIFFS +
model).

**Firmware:**
- `main/ui/face.[ch]` **mới** — bảng 32 clip + chế độ chạy + luật chọn (không LVGL):
  - `LOOP`: idle_normal (chớp mắt 9 s), listening, thinking, speaking, sym_alarm/
    reminder/timer/timer_digits/celebrate/music/weather_sun/rain/snow.
  - `ONCE` (phản ứng: mắt thường → biểu cảm → mắt thường, rồi về mặt nền): 10 `emo_*`,
    idle_bored/look_around/sleepy, boot_up, connect_success (✓ giữ ~2 s trong clip).
  - `ONCE_HOLD` (đứng ở khung cuối): connect_fail (✗ suốt lúc ERROR), sym_countdown,
    sym_event, sym_weather_cloud.
  - **21 cảm xúc server → clip:** happy/laughing/delicious → emo_happy; funny/winking/
    cool/confident/silly → emo_wink; sad; angry; crying → emo_cry; loving/embarrassed/
    kissy → emo_shy; surprised; shocked → emo_scared; thinking → thinking; confused;
    relaxed → emo_normal; sleepy → idle_sleepy; **neutral → bỏ phản ứng cảm xúc** (giữ
    mặt nền).
  - **Trạng thái → mặt nền:** LISTENING → listening, THINKING → thinking, SPEAKING →
    speaking, ERROR → connect_fail; BOOT/READY/WiFi/OTA/chưa kích hoạt → idle_normal.
    **Clip vào trạng thái:** lần đầu BOOT → boot_up; vào READY từ boot/kết nối/OTA/lỗi
    (không phải từ nghe-nghĩ-nói) → connect_success.
  - **Idle (READY rảnh):** cứ 15–30 s ngẫu nhiên chen look_around 50 % / bored 30 % /
    sleepy 20 % (sleepy chỉ khi rảnh ≥ 45 s) rồi về idle_normal; đồng hồ màn chờ 60 s
    vẫn thay mặt như cũ (mặt pause, hiện lại khi có chạm). Knob `BOARD_FACE_IDLE_*`.
- `display.c` khối `#if BOARD_FACE_GIF_FULLSCREEN` (knob P4 = 1, S3 mặc định 0 → đường
  emoji cũ nguyên): ba lớp **reaction > overlay (sym_*) > base**; `apply_state` /
  `apply_emotion` gọi `face_on_*_locked`; clip ONCE xong → `done_cb` (LVGL task) →
  `apply_face_done` (display task, kèm id để bỏ qua nếu clip đã bị thay) → về base.
  Widget `emoji_gif` phủ cả màn, nền đen, blit RGB565 1:1. `lv_timer` 1 s: chen idle +
  **pause decode khi màn khác che** (Home/bài học/popup/quiz/đồng hồ…). Thưởng sao:
  overlay `sym_celebrate` dưới lớp sao (lớp đen 70 % → 30 %). API mới
  `display_show_face("sym_music")` / `NULL` cho biểu tượng tính năng; alarm/nước/đếm
  ngược **chưa nối** vì panel của chúng phủ kín màn (nối = dựng lại màn đó quanh mặt).
- `util/gif/lvgl_gif.[ch]`: `lvgl_gif_create_ex(opts)` — **opaque** (RGB565 không alpha,
  MỘT buffer 800×480 = 768 KB PSRAM thay 3 buffer RGB565A8), **scale ×2** ngay lúc
  publish (ghi 32-bit = 2 pixel), chỉ chuyển **hộp bẩn** (rect khung trước ∪ khung này)
  và `lvgl_gif_last_dirty()` → display.c `lv_obj_invalidate_area()` đúng vùng thay vì
  `set_src` (= vẽ + PPA xoay cả 800×480). `set_loop/done_cb/pause`, cờ `completed` để
  ONCE_HOLD không tự chạy lại khi hiện lại. `take_stats()` cho log `face 10s:`.
  S3 (`lvgl_gif_create`) không đổi hành vi.
- `util/gif/gifdec.c`: `dispose()` disposal 0/1 không vẽ lại rect khung trước nữa
  (caller luôn `gd_render_frame` ngay sau `gd_get_frame` → canvas đã có; đo −27 %/khung).
  **Tối 15/09 (bộ 128 khung, khung 40 ms cần decode < ~15 ms):** (1) hook
  `gd_GIF.render_hook` — lvgl_gif opaque vẽ **thẳng index → LUT palette → RGB565 ×2**
  vào buffer ra lúc `gd_render_frame`, bỏ hẳn canvas ARGB trung gian (384 KB ghi +
  đọc mỗi khung) và vòng chuyển màu riêng; disposal 2 tô nền qua cùng hook; (2) vòng
  LZW (`read_image_data`) tính x,y một lần rồi lùi pixel thay vì `%`/`/` cho mỗi pixel
  (chuỗi LZW ghi ngược, p giảm 1 mỗi bước). Kết quả: **24–27 → 9–13 ms/khung**
  (`boot_up xong frames=28 decode=12387 us/khung`, `idle_normal 9053 us/khung`). Tick
  lv_timer = sàn/4 = 10 ms để khung 50 ms không thành 60. S3 (không hook) không đổi.
- Cache PSRAM: nạp **trước cả 32 file** sau `display_setup_ui`, mỗi bước một file rồi
  nghỉ 80 ms qua esp_timer (`FACE_PRELOAD_GAP_MS`). Lần đầu để display task tự xếp
  hàng liên tục → **`task_wdt IDLE0` ×2 và boot chậm 10 s** (display task prio 6 đè
  IDLE0 + vimate_main 5); có nghỉ: `preload xong, cache 2872 KB PSRAM` ở 16 s, boot
  `display_setup_ui done` 4,3 s như cũ, 0 wdt. `GIF_CACHE_N` 24 → 40.
- Không embed 13 GIF Noto vào app nữa (`main/CMakeLists.txt` `if(NOT P4)`): app
  **4,19 → 4,03 MB, slot còn 15 %** (trước 7 %). `emotion_sync_init()` idempotent
  (display.c mount emo_spiffs sớm, main task gọi lại không lỗi).

**Số đo trên board (log 15/09 21:50, bộ 128 khung + decoder tối ưu):**
- 0 `task_wdt` / panic trong 100 s; `boot_up (once)` 4,24 s → `boot_up xong frames=28
  decode=12387 us/khung dirty=99100 px` → `idle_normal (loop)` 6,60 s.
- `face 10s: idle_normal frames=56–60 decode+publish=9,0–9,3 ms/khung dirty≈152 K px`
  (idle_normal mới 16 khung 40–920 ms ≈ 6 fps). Khung 40 ms ⇒ ~25–30 % core 0 lúc clip
  nhanh (listening/speaking/emo), trước tối ưu sẽ là 60 %+.
- `diag rotate full=2 areas=496` / 60 s — vẫn chỉ hộp bẩn. PSRAM còn 20,97 MB sau
  preload 3915 KB (20,4 s, nghỉ 80 ms/file). RAM nội 99,5 KB (chưa WS/TLS).

**Số đo bản sáng 15/09 (bộ 32 khung, để so):**
- 0 `task_wdt` / panic trong 75 s; `boot_up (once)` 4,12 s → `idle_normal (loop)` 6,96 s
  (clip 2,03 s + done → base: đường ONCE → base chạy đúng).
- `face 10s: idle_normal frames=11 decode+publish=24–27 ms/khung dirty≈185 K px/khung`
  (trước tối ưu 33–37 ms). Khung 70–100 ms của emo/listening/speaking ⇒ ~25–35 % core 0
  lúc có clip nhanh; idle_normal 910 ms/khung ⇒ ~3 %. Bộ Noto cũ: lock LVGL 95–110 ms
  mỗi khung 150 ms (§6.7) — nặng hơn.
- `diag rotate full=2 areas=114` sau 60 s: chỉ 2 lần xoay cả màn (2 khung đầu), còn lại
  hộp bẩn — đường `invalidate_area` hoạt động.
- PSRAM sau preload còn 22,09 MB; RAM nội 99,7 KB (chưa có WS/TLS — WiFi không lên,
  xem dưới) nên **chưa so được với mốc 53 KB**; bộ mặt không cấp gì trong RAM nội
  ngoài `lvgl_gif_t` ~120 B và `s_face`.
- `Tmr Svc free=88` (13/09: 156–204) — task timer FreeRTOS của `wifi_mgr`, không liên
  quan mặt; nên nâng `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH` 2048 → 3072 khi rảnh.

**Ghi nhận thêm 15/09 (WiFi, ngoài phạm vi mặt):** 1/5 lần boot, khi rơi về SoftAP sau
60 s không nối được STA, C5 trả `set_config AP: ESP_ERR_INVALID_ARG` (RPC 0x21c resp
258) → "Provisioning start fail … reboot sau 30s"; 4 lần khác `Open AP GENU-Setup-53:C8
started` bình thường. Nghi C5 còn đang trong lượt connect/scan STA lúc nhận set_config
AP (§5.5 đã thấy `scan start fail: ESP_ERR_WIFI_STATE`). Chưa điều tra.

**Chưa kiểm được trên máy trong phiên này:** WiFi "BizGenie" không nối (4 lần
`STA disconnected`, rơi về SoftAP `GENU-Setup-53:C8` sau 60 s) — đường WiFi trên C5
không đổi, khả năng AP không trong tầm; nên **chưa thấy** listening/thinking/speaking,
emo_* theo server, connect_success/fail, celebrate. Cần: nối WiFi (portal
192.168.4.1) rồi nói *Hi Lily* → log phải có `face: listening (loop)` → `face: thinking`
→ `face: emo_<x> (once)` (nếu server gửi emotion) → `face: speaking (loop)` → `face:
idle_normal`; rút mạng → `face: connect_fail (once+hold)`; có mạng lại → `face:
connect_success (once)` → idle. Mắt người: boot thấy hai vạch sáng nở thành hai mắt, sau
2 s mắt chớp; ~15–30 s rảnh mắt liếc/ngáp; đồng hồ 60 s; USB-C bên phải.

**Cách sửa bộ mặt:** thay GIF trong `docs/01_gif/` (cùng tên) → `python
tools/build_face_gifs.py` → build + nạp (SPIFFS image đi cùng `flash`). Thêm clip mới =
thêm dòng `FACES[]` + enum trong `face.[ch]` (bảng phải cùng thứ tự enum).

---

### 6.6 Xoay ngang 800×480 bằng PPA — ĐÃ CHẠY (12/09/2026)

**Vì sao không dùng cách "đúng sách".** Ba đường có sẵn đều hỏng một thứ:

| Cách | Hỏng gì |
|---|---|
| `lv_display_set_rotation()` | LVGL 9.5 **không tự xoay** nữa (driver phải làm); chỉ có *matrix rotation* mà SW renderer không hỗ trợ — chỉ VG-Lite |
| `esp_lvgl_port` `sw_rotate` (có PPA) | chỉ xoay ở **partial mode**; với DSI + direct mode nó `draw_bitmap(0,0,800,480)` lên panel 480 rộng — sai, và mất chống xé hình |
| `esp_lv_adapter` (demo hãng) | thay cả tầng port, đổi API lock/touch, chưa chắc hợp IDF 5.5.1 |

**Cách đang chạy** (`display.c`, khối `DSI_ROTATE`):

1. Port cấp **2 buffer LVGL logical 800×480** trong PSRAM (`avoid_tearing=false`
   để port không đưa fb DPI cho LVGL), `direct_mode=true` → LVGL chỉ vẽ vùng bẩn,
   tự sync 2 buffer của nó (`refr_sync_areas`).
2. `dsi_rot_flush_cb` thay flush của port (đổi trong lúc giữ `lvgl_port_lock`,
   như ST77922 — flush mặc định ở cấu hình này take semaphore NULL). Mỗi flush
   gom vùng bẩn; ở flush cuối khung (`lv_display_flush_is_last`) xoay **hợp của
   vùng bẩn khung N-1 và N** từ buffer LVGL vào **fb DPI đang ẩn** bằng PPA
   (`ppa_do_scale_rotate_mirror`, RGB565, blocking). Hợp 2 khung là đủ vì fb ẩn
   đang giữ khung N-2 — đúng lý luận `sync_areas` của LVGL. 2 khung đầu + khi
   danh sách >32 vùng: xoay cả màn.
3. `esp_lcd_panel_draw_bitmap(panel, 0,0,480,800, fb_ẩn)` — con trỏ nằm trong fb
   nên driver chỉ `msync` + đổi `cur_fb_index`; ISR cuối khung mới chuyển DMA →
   đổi khung đúng ranh giới. Rồi chờ `on_refresh_done` (đăng ký đè cb của port,
   timeout 100 ms có log) trước khi báo LVGL xong → fb vừa rời màn không bị ghi
   đè khi còn đang quét.
4. `display_panel_blit()` (MP4) xoay vào **cả hai** fb qua cùng `dsi_rot_put()`.

Chiều xoay — PPA quay **ngược** kim đồng hồ (`driver/ppa.h`). `BOARD_LCD_ROTATION`
theo quy ước LVGL (panel bị xoay x° theo kim đồng hồ so với dọc):

| Knob | Nội dung quay | PPA angle | logical(lx,ly) → native(px,py) | Tư thế cầm |
|---|---|---|---|---|
| **270** (đang dùng) | theo kim đồng hồ | `_270` | `(479-ly, lx)` | cạnh trên panel dọc ở bên **trái**, USB-C bên phải — như ảnh 11/09 |
| 90 | ngược | `_90` | `(ly, 799-lx)` | USB-C bên trái |

Touch (`touch.c` map native → logical: swap → mirror_x → mirror_y) suy từ cùng
knob trong board header: 270 → `SWAP_XY=1, MIRROR_Y=1`; 90 → `SWAP_XY=1, MIRROR_X=1`.
Chạm lệch trục (ấn trái ra phải) = mirror sai; lệch 90° = swap sai — chỉnh knob
touch trong header, không sửa `touch.c`.

**Số đo** (log boot + `diag rotate` mỗi 60 s):

```
I vimate.ui:   DSI rotate: logical 800x480 -> panel 480x800, goc 270, PPA, 2 fb + vsync
I vimate.ui:   DSI rotate: ca man 800x480 qua PPA mat 15873 us
I vimate.main: diag rotate full=2 areas=280 full_max=15873us     (sau 120 s)
```

- Cả màn 768 KB qua PPA: **15,9 ms** (~48 MB/s, nghẽn ở PSRAM) — chỉ xảy ra 2
  khung đầu và khi đổi cả màn; còn lại xoay vùng bẩn (GIF 480×480 ≈ 5 ms).
- Tốn thêm **~2 MB PSRAM** (2 buffer LVGL 768 KB + descriptor PPA) so với đường
  dọc dùng thẳng fb: PSRAM trống 25,4 MB, ổn định giữa hai tick 60 s.
- 120 s: 270 flush, 280 vùng xoay, 0 timeout vsync, không task_wdt.
- Dự phòng: PPA đăng ký lỗi → xoay bằng CPU (đúng nhưng cả màn cỡ 40 ms).

**Chưa có mắt người xác nhận**: (1) hình đúng chiều — sai thì đổi knob 270 ↔ 90;
(2) chạm đúng chỗ; (3) không xé khi GIF chạy. Emoji Noto 160 px × 3 = 480 vẫn vừa
cạnh ngắn (§6.5).

---

### 6.7 Chạm cảm ứng: độ nhạy driver + UI/UX cho ngón tay — ĐÃ NẠP, CHỜ TAY NGƯỜI (13/09/2026)

Người dùng: "kiểm tra lại độ nhạy màn hình, cần thiết kế UI/UX để tối ưu chạm".
Màn 4.3" 800×480 ≈ 217 ppi → **1 mm ≈ 8,5 px**; chuẩn mục tiêu chạm 48 dp (Android)
≈ 9 mm ≈ **77 px**, khoảng cách ≥ 8 dp ≈ 13 px (ui-ux-pro-max, mục Touch). Đọc
`touch.c` + hit-test trong `display.c` thấy 4 lý do "kém nhạy", đều sửa trong đợt này:

| # | Trước | Sau |
|---|---|---|
| 1 | Quét I2C **30 ms** + cửa `held > 30 ms` → tap nhanh chỉ lọt 1 mẫu, `held = 30` → **bỏ im lặng** | `TOUCH_POLL_MS 10`, bỏ cửa thời gian, đòi ≥ 2 mẫu liên tiếp (`TOUCH_MIN_SAMPLES`) loại "ma". Rảnh chỉ đọc 1 byte `TOUCH_INFO` (~0,25 ms bus), 35 byte điểm chỉ khi đang chạm (max points = 5) → ≤ 10 % bus I2C |
| 2 | Hit-test đúng mép pixel widget | `hit_in(obj, x, y, slop)`, `TOUCH_HIT_SLOP 10` px mỗi cạnh cho nút Home, bar, quiz, uống nước, mũi tên khoá học (ô Home kề nhau → slop 0) |
| 3 | Mục tiêu nhỏ hơn 48 dp: Home 58 px (6,8 mm), bar 52 (6,1), quiz 54 (6,4), mũi tên 48 (5,6) | Home **68**, bar **64/62** (`HOME_BAR_H`, `HOME_BAR_RESERVED_H` = +14), quiz **72** (gap 14), uống nước **64**, mũi tên **64** — cộng slop ≈ 8,5–10 mm |
| 4 | Không phản hồi lúc chạm — mọi thứ chỉ xảy ra khi nhấc tay | `display_touch_feedback(x, y, pressed)`: lúc DOWN tìm nút dưới ngón (`pressable_at`, cùng thứ tự ưu tiên với touch.c) → `LV_STATE_PRESSED`; style pressed đặt lúc tạo (bar xanh nhạt, Home xanh đậm, quiz tím nhạt, ô Home xám 60 %, uống nước tối 30 %); nhấc tay trả lại. Không cấp phát, không timer |

Bản diag ghi mỗi lần chạm: `Touch DOWN native=(px,py) logical=(x,y) int=…` và
`Touch UP held=… ms samples=… moved=(dx,dy) int=…` (`-> BO (qua ngan)` khi < 2 mẫu) —
để đối chiếu **map toạ độ sau xoay** (§7 vẫn "chưa mắt người xác nhận") và xem INT
(GPIO23) có giữ mức khi chạm không (nếu có → đời sau dùng ngắt thay poll).

**Map toạ độ sau xoay — đã xác nhận bằng tay người (13/09):** native (479,22) →
logical (22,0) = góc trên-trái; native x≈0…50 → bar dưới y≈430–475; Home/quiz/nước/lịch
đều trúng đúng ô. Không phải đổi `BOARD_TOUCH_MIRROR_*`.

#### "Nhấn chậm" — phân bố RTOS task sai + một `vTaskDelay(600)` (đo 13/09, 35 lần chạm)

Người dùng: "kiểm tra lại phân bố RTOS task, đang phản hồi chậm khi nhấn". Log cho
đường đi một lần chạm bar dưới:

| Đoạn | Trước | Nguyên nhân | Sau |
|---|---|---|---|
| Mẫu khi ngón đang đè (phiên AI, emoji chạy) | 1–3 mẫu / 200–380 ms → **14 tap bị bỏ** | task chạm prio **5 < taskLVGL 6** cùng core 0, lại `display_lock()` trong `display_touch_feedback` → bị khối render emoji đè ~200 ms | prio **7** (`BOARD_TOUCH_TASK_PRIO`, S3 giữ 5); feedback đẩy qua hàng đợi display, không lock; tap `samples=1` nhưng DOWN→UP ≥ 40 ms vẫn nhận (`TOUCH_MIN_HELD_MS`) |
| Nhấc tay → biết trúng nút | 21–216 ms | 4 hit-test lấy lock LVGL | median 1 ms; khi trúng lúc render: 23–158 ms (xem dưới) |
| `Mic OFF` → `→ abort` | **600 ms mọi lần** | `ui_image.c clear_previous_image_buffers()` ngủ `vTaskDelay(600)` **vô điều kiện** (chừa flush SPI 600 ms của S3) — nằm trên `stop_active_program_for_navigation()` | chỉ ngủ khi có buffer; `BOARD_UI_IMAGE_UNHOOK_MS` = **40** trên P4 (default 600 S3) → **4 ms** |
| Nhấc tay → gửi `home_select` | **800–1400 ms** | cộng dồn | **median 55 ms, p90 222, max 666** (89 lần) |
| Server trả `Server command: HOME` | — | — | +10 ms — server không phải nút thắt |

Sau sửa: 89 lần chạm, quét đều **~91 mẫu/s** (chu kỳ 11 ms, tap 55 ms vẫn 5 mẫu),
1 tap bị bỏ (thật sự < 40 ms). **Phần còn lại**: `Touch hit-test cho lock LVGL` > 20 ms
ở 31/89 lần, median 81, p90 127, max 158 ms — taskLVGL giữ lock lúc render (bóng đổ
SW của bar/ô Home + emoji GIF 400 px). Đây là trần độ trễ hiện tại, không còn là RTOS;
muốn xuống nữa: bỏ shadow SW, cache khung GIF đã scale, hoặc
`LV_DRAW_SW_DRAW_UNIT_CNT=2` (§7). 2/67 lần `Mic OFF → abort` 400–530 ms khi thoát bài
học có ảnh (image worker đang giữ `s_image_lock`) — hiếm, để đó.

#### Ba lỗi UX lộ ra từ log, đã sửa cùng đợt

1. **Vuốt bị hiểu là tap** (`moved=(14,-171)` trên lịch → "tap ngoài ngày → về Home";
   `(53,175)` trong phiên AI → mở lượt nghe; `(73,103)` → chọn khoá học). Giờ lệch
   > `TOUCH_SWIPE_PX` 34 = vuốt: chỉ khoá học (ngang → trang) và lịch (dọc → cuộn) nhận,
   còn lại "Vuốt … — bỏ qua", không tính vào double-tap. Đã kiểm: 6 vuốt đúng, 2 lật
   trang khoá học.
2. **Double-tap → Home bắn nhầm**: 17/89 lần chạm là `Double-tap → VỀ HOME`, **tất cả**
   là gõ lại cùng một nút (bar/nước/khoá học) sau 140–430 ms — trẻ gõ dồn khi chưa thấy
   phản hồi và bị đá về Home ngay sau khi vừa mở app. Giờ: tap thứ hai trúng **nút**
   (`display_touch_target_at`) thì không phải double-tap — cùng nút (≤ 40 px) → bỏ qua
   (chống dội, log "Tap lặp cùng nút"), nút khác → tap thường. Double-tap trên **chỗ
   trống** vẫn về Home (spec 001 FR-015). ⚠️ Đây là nới spec — chủ sản phẩm xem lại.
3. **INT GPIO23 không phải mức**: đợt quét I2C 10 ms → `int=0` ở 35/35 DOWN; đợt có cổng
   INT (bỏ đọc I2C 2/3 chu kỳ) → `int=1` ở đa số DOWN dù đang chạm ⇒ INT là xung quanh
   mỗi báo cáo (hoặc bị xoá khi host đọc). Cổng chỉ làm DOWN trễ ≤ 20 ms → tắt
   (`BOARD_TOUCH_INT_ACTIVE_LOW 0`, code giữ). Muốn dùng INT thật: ngắt cạnh xuống đánh
   thức task — chưa làm.

## 7. Chưa làm

- **Camera SC2336 MIPI-CSI.** Code tham chiếu ở
  `ESP-IDF 5/Screen and camera code/.../main/app_video.c`. SCCB dùng chung GPIO7/8
  với touch. Cần `espressif/esp_video` + `CONFIG_CAMERA_SC2336*`.
- **Nhìn màn xác nhận chống xé hình** (§6.1) — code đã bật, log đã đúng, chỉ
  thiếu mắt người.
- **Nhìn màn xác nhận chiều xoay ngang + chạm** (§6.6) — sai chiều thì đổi
  `BOARD_LCD_ROTATION` 270 ↔ 90.
- **Nhìn mặt robot + phiên nói chuyện với mặt mới** (§6.5b) — cần WiFi lên; kiểm
  listening/thinking/speaking, emo_* theo server, connect_success/fail, celebrate.
- **Nối `sym_alarm/reminder/timer/countdown` vào màn hẹn giờ/uống nước/đếm ngược**
  (§6.5b) — panel hiện tại phủ kín màn nên phải dựng lại chúng quanh mặt (nền đen,
  thẻ chữ nhỏ); API `display_show_face()` đã có.
- **`Tmr Svc free=88`** (§6.5b) — nâng `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH`
  2048 → 3072 rồi đo lại.
- **LVGL 2 draw unit.** `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2` cần
  `CONFIG_LV_OS_FREERTOS=y` (LVGL báo *"OS support is required when more than one
  SW rendering units are enabled"*). Bật cả hai nhanh hơn khi rasterize, nhưng đổi
  mô hình khoá của LVGL so với cây S3 → phải soak lại. Đang để 1 unit cho giống S3.
- **Thẻ nhớ — ĐÃ MOUNT (15/09/2026, có thẻ thật).** ~~SDIO của P4 đã dành cho
  ESP-Hosted~~ — **khẳng định đó SAI**: ESP-Hosted đi SDIO **slot 1**, khe TF đi
  **slot 0** IOMUX. `send_op_cond 0x107` các ngày trước đúng là **khe trống**: cắm
  thẻ SDHC 4 GB (FAT32) là lên ngay, cả 4-bit:

  ```
  I vimate.cache: SD LDO power ON 3.3V (on-chip chan=4)
  I vimate.cache: SD SDMMC mount slot=0 CLK=43 CMD=44 D0=39 width=4 freq=20000kHz
  Name: asdfg  Type: SDHC  Speed: 20.00 MHz (limit: 20.00 MHz)  Size: 3840MB  SSR: bus_width=4
  W vimate.cache: SD free=3534220 KB / total=3923968 KB
  I vimate.cache: SD verify read/write OK: /sdcard/sd_test.txt
  I vimate.cache: SD diskio bounce 32 sector (16 KB PSRAM can 64) tren pdrv 0
  I vimate.cache: SD bench (...): write 256 KB 703 ms = 363 KB/s, read 256 KB 336 ms = 761 KB/s
  I vimate.cache: SD course cache mounted
  ```

  Ba việc đã làm để có số trên (trước: mount ở 400 kHz, ghi **61 KB/s**, đọc
  **126–298 KB/s**):
  1. `BOARD_SD_MMC_FREQ_KHZ 20000` (board header) — mount lần 1 ở 20 MHz
     (SDMMC_FREQ_DEFAULT), fail thì `course_media_cache.c` hạ 400 kHz rồi 1-bit.
  2. **diskio bounce buffer** (`course_media_cache.c`, `sd_diskio_install_bounce`):
     driver sdmmc trên P4 chỉ DMA nhiều block khi buffer căn **64 B** và cỡ bội 64
     (`sdmmc_host_check_buffer_alignment`), còn lại chép **từng sector một lệnh** qua
     buffer tạm 512 B — thẻ rẻ trễ ~4 ms/lệnh nên tốc độ bus vô nghĩa. Buffer qua
     FATFS gần như không bao giờ căn (stdio newlib, FIL buf, mảng cục bộ, malloc PSRAM
     thường). Thay `ff_diskio_impl_t` của volume: chưa căn → gom ≤ 32 sector qua buffer
     PSRAM căn 64 (một CMD18/25) rồi memcpy. FF_FS_REENTRANT=1 nên một buffer/volume.
  3. `CONFIG_FATFS_VFS_FSTAT_BLKSIZE=16384` (chỉ profile P4): buffer stdio cho file
     FAT mặc định 1 KB → mỗi `fread` là lệnh 2 sector (181 KB/s dù đã có bounce);
     16 KB = một cụm (allocation_unit 16 KB) → 761 KB/s. Buffer cấp từ PSRAM
     (malloc > 1 KB), tối đa 8 file × 16 KB.

  Còn chậm hơn lý thuyết 10 MB/s: ~20 ms cho mỗi lệnh 16 KB, nghi (a) thẻ "asdfg"
  rẻ, (b) host SDMMC dùng chung với ESP-Hosted slot 1 nên lệnh SD xếp hàng sau lưu
  lượng C5 — chưa đo tách. Chưa thử 40 MHz (`SDMMC_FREQ_HIGHSPEED`): đổi knob rồi
  đọc dòng `SD bench` (bản diag chạy mỗi boot, ghi/xoá 256 KB). Đủ cho cache khoá học
  (giới hạn bởi mạng) và MP4 bitrate thấp; **phát thử MP4 thật vẫn chưa làm**.
- **Phát thử một file MP4 thật** (§6.4) — thẻ đã mount, cần server gửi lệnh
  `video_control_play` (hoặc chép file vào thẻ và gọi thử).
- **Nâng firmware slave C5 lên 2.12.x** (xem §1) — OTA qua SDIO là đường duy
  nhất và **không có đường cứu**; cần người sở hữu board quyết.
- **Tinh chỉnh WakeNet bằng giọng thật** (§4) — mic/loa đã đo xong, chỉ còn số
  cho `BOARD_WAKE_GAIN_PCT` / `_DET_THRESHOLD`.
- **LED WS2812B (GPIO34).** Cần driver RMT/SPI, firmware chưa có. Số chân đã ghi
  vào `BOARD_WS2812_GPIO` làm tư liệu.
- **PMIC AXP2101 (0x34, IRQ GPIO21).** Board KHÔNG có chân ADC đo pin — mọi thông
  tin pin/sạc phải hỏi PMIC qua I2C. Chưa có driver.
- **RS485 / IMU LSM6DS3TR-C / DAC MCP4725 / module 4G ML307R.** Chân đã có đủ
  trong `docs/HARDWARE-PINOUT.md`; chưa có code. Lưu ý §13.9: MCP4725 chạy VDD 5V
  nhưng bus I2C chỉ kéo 3.3V, ngưỡng VIH 3.5V ⇒ có thể không nhận lệnh.
- **Test captive portal trên điện thoại thật.** Bộ test Playwright (§5.5) phủ logic
  trang, nhưng **không** phủ được việc iOS/Android có tự bật cửa sổ đăng nhập hay
  không — cái đó phụ thuộc DNS hijack + DHCP option 114 + handler probe, chỉ máy
  thật mới trả lời.
- **NVS không mã hoá trên bảng phân vùng dev.** Mật khẩu WiFi nhà nằm dạng chữ
  thường trong `partitions.csv` (đã xác nhận bằng cách đọc flash). `partitions.prod.csv`
  có `nvs_key ... encrypted`. Cần quyết định: bật NVS encryption cho cả bản dev, hay
  chấp nhận và ghi rõ rằng board dev không được cầm mật khẩu thật.
- **`docs/PROGRESS.md` chưa nhắc tới cây này** — thêm một dòng khi nhánh P4 có mốc
  đáng ghi vào kế hoạch chung (chưa deploy, chưa vào release pipeline).

### Phạm vi: đã ghi nhận trong AGENTS.md gốc (13/09/2026)

`AGENTS.md` gốc từng chỉ nói **ESP32-S3**; nay có mục *"Firmware ESP32-P4 — nhánh
bring-up song song"* thừa nhận `firmware-vimate-p4/` là ngoại lệ có chủ ý, production
vẫn khoá cứng S3, và trỏ về `firmware-vimate-p4/AGENTS.md`. Người dùng yêu cầu việc này
(13/09) sau khi học cách CrossInk ràng buộc agent vào phần cứng.

### Hướng dẫn agent cho cây P4 — học từ CrossInk (13/09/2026)

Người dùng đưa `https://github.com/uxjulia/CrossInk` (firmware e-reader ESP32-C3/S3) để
học cách họ trói agent vào phần cứng. Điểm lấy được:

| CrossInk | Áp vào cây P4 |
|---|---|
| `AGENTS.md` = nguồn chính; `CLAUDE.md` chỉ trỏ | `firmware-vimate-p4/AGENTS.md` mới; `CLAUDE.md` gốc thêm một dòng trỏ |
| Bảng "Read first / Owns" theo khu vực; đọc hẹp | §1 của AGENTS.md P4: 13 khu vực → file đọc trước → sở hữu |
| "Hardware Constraints" + "Resource Rules" đánh số, kèm nghĩa vụ chứng minh ("không tuyên bố tối ưu nếu không nêu cơ chế", "sau khi sửa phải nói cách kiểm trên phần cứng") | §2 sự thật phần cứng (từ schematic + số đo), §3 luật HAL, §4 luật tài nguyên, §6 tiêu chuẩn "xong" |
| `scope-discipline`: cổng 4 câu trước khi thêm tính năng/dependency | §5 cổng 5 câu (Edu trên P4? chi phí đo? knob có sẵn? giữ S3? dependency ship P4/`-L`?) |
| `hal-and-abstractions`: đi qua HAL, không 800/480, không GPIO thô | skill `vimate-p4-hardware`: mọi số qua `BOARD_*`, hai hệ toạ độ, GPIO22 chỉ display.c, danh sách "đã đo — đừng thử lại" |
| `heap-discipline`: thứ tự quyết định stack → static → alloc-once → fallible; "phân mảnh mới giết máy" | skill `vimate-p4-memory`: thứ tự 7 bước với PSRAM/DMA 64 B/RTC RAM/TLS anchor, luật task, cách đo `diag heap` |
| Mỗi skill kết bằng checklist tự soát; neo tên bền, không số dòng | cả ba skill P4 có checklist; neo vào knob/API/dòng log |
| `.claude/CONTEXT.md` ngắn cho gotcha bền | không thêm file thứ ba: §7 AGENTS.md là chỉ mục bẫy một dòng → README-P4 giữ chi tiết |
| Build/verify có lệnh cụ thể và điều phải nhìn thấy | skill `vimate-p4-verify`: bảng dòng log phải thấy cho từng khối + câu "người dùng cần làm gì" |

Không chép: activity model, i18n `tr()`, `makeUniqueNoThrow` (C++), SdFat mutex — không
có tương đương trong cây C này. Kèm theo: workflow Windows đã có script bền trong repo
(`scripts/build_p4_43lcd.bat`, `flash_p4_43lcd.bat`, `p4_readlog.py`) thay vì nằm ở
scratchpad phiên làm việc; skill cũ `vimate-fw-module` (fork xiaozhi C++) được đánh dấu
lỗi thời để không bắn nhầm vào hai cây C native.

---

## 8. Toàn bộ khác biệt so với cây S3

Mọi thay đổi đều có knob mặc định **giữ nguyên hành vi S3**, nên merge ngược về
`firmware-vimate/` được mà không đổi build S3.

| File | Thay đổi |
|---|---|
| `components/esp_lcd_st7102/` | **Mới** — vendor driver ST7102 (không có trên registry). 11/09: bỏ lệnh đọc ID `0x04` (điểm treo không timeout, §6.2) |
| `main/boards/board_esp32p4_43lcd.h` | **Mới** — pinout board P4. Từ 10/09/2026 điền đầy đủ theo `docs/HARDWARE-PINOUT.md`: LCD RST 22 + BL 6, touch INT 23, audio I2S 9/10/11/12/13 + PA 3 + ES7210 0x40, SDMMC slot 0 (43/44/39-42) + LDO kênh 4. 11/09: `BOARD_MIC_VAD_RMS_MIN 120` theo số đo (§4), `BOARD_AUDIO_BOOT_TEST_BEEP` (0). 12/09: `BOARD_LCD_ROTATION 270`, H/V_RES logical suy từ knob, touch knob suy từ knob (§6.6) |
| `main/boards/board.h` | Thêm nhánh chọn board P4; default `BOARD_AUDIO_RUNTIME_ENABLE 1`, `BOARD_LCD_USE_MIPI_DSI 0` |
| `main/Kconfig.projbuild` | Thêm `VIMATE_BOARD_P4_43LCD`; mỗi board `depends on` đúng target |
| `main/ui/display.c` | Bảng init ST7102 + nhánh `#if BOARD_LCD_USE_MIPI_DSI` (LDO → DSI bus → DBI io → DPI panel) và `lvgl_port_add_disp_dsi()`. 11/09: reset cứng GPIO22 trước DSI (§6.2), `num_fbs=2` + `direct_mode` + `avoid_tearing` (§6.1), `display_panel_blit()` ghi cả 2 fb, bộ đếm `LV_EVENT_FLUSH_FINISH` (§6.3), nhánh `EMOJI_TARGET_PX` cho DSI = cạnh nhỏ (§6.5). 12/09: khối `DSI_ROTATE` — flush cb xoay vùng bẩn bằng PPA vào fb ẩn + vsync, `display_rotate_stats()` (§6.6) |
| `spiffs_emo_image/*.gif` | 11/09: 13 GIF Noto Animated Emoji 160px (CC BY 4.0) thay bộ thỏ Chuppy 128px; bộ cũ ở `tools/emo_backup_chuppy_128/` (§6.5) |
| `tools/fetch_noto_gifs.py` | **Mới** — tải + chuyển Noto emoji, kiểm ngân sách, preview (§6.5) |
| `main/ui/display.h` | 11/09: thêm `display_panel_blit()`, `display_flush_count()`; 12/09: `display_rotate_stats()`, `display_set_wifi_rssi()` |
| `main/ui/display.c` (icon WiFi) | 12/09: icon sóng WiFi góc trên phải — 4 vạch + dBm, ngưỡng −60/−67/−75, xám "--" khi mất mạng; cập nhật qua hàng đợi display |
| `main/ui/display.c` + `.h` (icon Bluetooth) | 13/09: rune BT bằng `lv_line` bên trái icon WiFi, `display_set_ble_state()`; xám/xanh/xanh + 2 chấm theo `ble_prov_state_t` (§5 BLE) |
| `main/core/ble_wifi_prov.c` + `.h` | 13/09: `ble_prov_state_t` + `ble_wifi_prov_state()`; bản NimBLE đẩy trạng thái khi quảng bá / nối / ngắt / stop; stub P4 trả `UNAVAILABLE` |
| `main/core/telemetry.c` | 12/09: heartbeat task poll RSSI 3 s/lần nuôi icon (không tạo task mới — RAM nội chật), heartbeat vẫn 20 s |
| `main/core/diagnostics.c` | 11/09: in `flush=` trong dòng `diag heap`; 12/09: dòng `diag rotate` |
| `main/media/mp4_player.cpp` | 11/09: swap byte chỉ khi `BOARD_LCD_SWAP_BYTES`; vẽ qua `display_panel_blit()` (§6.4) |
| `main/input/touch.c` | Knob `BOARD_TOUCH_USE_ST7123`, gộp với ST77922 thành `BOARD_TOUCH_USE_REG16` (cùng giao thức register 16-bit, chỉ khác địa chỉ I2C) |
| `main/input/touch.c` (độ nhạy) | 13/09: quét 10 ms, bỏ cửa `held > 30`, ≥ 2 mẫu, phản hồi PRESSED lúc DOWN, log diag DOWN/UP native+logical+INT (§6.7) |
| `main/input/touch.c` (độ trễ, UX) | 13/09: prio theo `BOARD_TOUCH_TASK_PRIO` (P4 = 7), tap `held ≥ 40 ms` dù 1 mẫu, vuốt tách khỏi tap (`TOUCH_SWIPE_PX`), gõ lại cùng nút ≠ double-tap, diag `hit-test cho lock LVGL` (§6.7) |
| `main/ui/ui_image.c` | 13/09: `clear_previous_image_buffers()` chỉ ngủ khi có buffer, thời gian `BOARD_UI_IMAGE_UNHOOK_MS` (default 600 = S3; P4 40) — bỏ 0,6 s mỗi chạm điều hướng (§6.7) |
| `main/boards/board.h` (13/09) | default `BOARD_UI_IMAGE_UNHOOK_MS 600`, `BOARD_TOUCH_INT_ACTIVE_LOW 0` |
| `main/ui/display.c` + `.h` (chạm) | 13/09: `hit_in()` slop 10 px, `pressable_at()` + `display_touch_feedback()` (qua hàng đợi, không lock) + `display_touch_target_at()`, style `LV_STATE_PRESSED` cho Home/bar/quiz/uống nước/ô Home/mũi tên; nút Home 68, bar 64, quiz 72, uống nước 64, mũi tên 64 (§6.7) |
| `main/core/ble_wifi_prov.c` | Bọc `#if defined(CONFIG_BT_NIMBLE_ENABLED)`; target không có BT → stub |
| `main/core/captive_dns.{c,h}` | **Mới** — DNS hijack cho captive portal (§5.3) |
| `main/core/wifi_mgr.c` | DHCP cấp DNS + option 114, 404→302 (trừ đường API), nới socket httpd (§5.3). Đại tu §5.5: dọn credential cũ trong C5 + `WIFI_STORAGE_RAM`, cache/gộp/hâm nóng danh sách quét, chặn `receive_request_body()` treo, `WIFI_PS_NONE`, `/favicon.ico`→204, gương trạng thái lên màn hình, đếm máy bám AP, viết lại trang portal |
| `main/ui/ui_wifi_setup.{c,h}` | Viết lại: 2 mã QR kiểu CrossInk (§5.4); §5.5 thành máy trạng thái 5 bước + sửa 3 cặp màu không đạt tương phản |
| `main/app_main.c` | Bọc `audio_pipeline_init()` bằng `#if !BOARD_AUDIO_RUNTIME_ENABLE` |
| `main/audio/audio_pipeline.c` | 11/09: `MIC_HPF_BIQUAD` = bản ANSI trên P4 (bug esp-dsp arp4, §4); mic idle probe + HPF self-test chỉ khi `CONFIG_VIMATE_DIAG_ENABLE`. 12/09: thống kê WakeNet 10 s, bộ ghi PCM `wcap_*` 4 tag (§4.6), `AFE_RUNTIME_ENABLE`/`ASR_UPLINK_RAW_MIC` lấy từ board knob, feed AFE 4 kênh, thống kê `AFE out 10s` (§4.7) |
| `main/audio/vimate_es8311.c` + `.h` | 12/09: ES7210 TDM 4 kênh (`es8311_codec_read_frames`, `es8311_codec_in_channels`), DAC 2 slot 32-bit + nhân đôi mono, PGA ref 0 dB, log `TDM ch rms` (§4.7) |
| `main/audio/audio_afe.c` + `.h` | 12/09: format/mode/linear_gain từ board knob, `audio_afe_feed_frames24()` ref cứng, bỏ ring ref mềm khi 4 kênh (§4.7) |
| `main/audio/opus_codec.c` + `.h` | 12/09: `opus_codec_decode_selftest()` (§4.5) |
| `main/app_main.c` | 12/09: `g_vimate_tls_anchor` — neo TLS sửa panic Opus (§4.5) |
| `tools/wake_dump_to_wav.py` | **Mới** 12/09 — dựng WAV từ dump UART + số đo (§4.6) |
| `main/store/course_media_cache.c` | Thêm nhánh `BOARD_SD_USE_SDMMC` **trước** guard "P4 ESP-Hosted owns SDIO" — guard đó dựa trên giả định sai (hosted đi slot 1, thẻ nhớ slot 0), nó chặn thẻ nhớ ngay dòng đầu (§7) |
| `docs/HARDWARE-PINOUT.md` | **Mới** — pinout đọc từ schematic thật, nguồn chuẩn cho mọi chân |
| `main/CMakeLists.txt` | Thêm `esp_lcd_st7102` vào `PRIV_REQS` (vô điều kiện — xem comment trong file), `esp_mm` và (12/09) `esp_driver_ppa` vào `REQUIRES` |
| `main/idf_component.yml` | Ghim `esp_lvgl_port ~2.8.0`; nâng `esp_hosted` lên `2.12.*`; 11/09: mở lại 4 dependency MP4 |
| `CMakeLists.txt` | `SUPPORTED_TARGETS esp32p4`; 11/09: workaround `-L <dir>` không bọc nháy của 3 component media (§2 bẫy 4) |
| `sdkconfig.defaults` | Viết lại cho P4 (ASCII); `ESP_MAIN_TASK_STACK_SIZE=10240`; `CACHE_L2_CACHE_LINE_64B` (§5.2b) |
| `sdkconfig.defaults.p4-43lcd` | **Mới** — profile board (C5/SDIO, LVGL, CPU 360MHz, `LWIP_MAX_SOCKETS=10`, SD cache, WakeNet); 11/09: `VIMATE_MP4_PLAYER_ENABLE=y` + tắt 9 decoder audio không thuộc profile + tắt MJPEG mềm; 12/09: `ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP=n` (§4.7 bẫy 3) |
| `scripts/build_p4_43lcd.sh` | **Mới** — tự xử lý bẫy `sdkconfig` cũ |
| `scripts/build_p4_43lcd.bat`, `flash_p4_43lcd.bat`, `p4_readlog.py` | **Mới** 13/09 — bản Windows/cmd.exe của workflow build → nạp → đọc UART (§2); đường dẫn tương đối, ASCII. `p4_readlog.py`: `auto`/`--list` tìm CH343, ghi file tức thì + tự mở lại cổng, **thả DTR/RTS trước khi mở** (§3.1 bẫy) |
| `AGENTS.md` (trong cây P4) | **Mới** 13/09 — luật cho agent: phạm vi, bảng đọc-trước, sự thật phần cứng, luật HAL/tài nguyên, cổng thêm tính năng, tiêu chuẩn "xong", chỉ mục bẫy, chỉ mục skill (§7) |
| `../.agents/skills/vimate-p4-{hardware,memory,verify}/SKILL.md` | **Mới** 13/09 — ba playbook + checklist tự soát học từ CrossInk (§7). `vimate-fw-module` cũ đánh dấu lỗi thời |
| `../AGENTS.md`, `../CLAUDE.md` | 13/09 — thêm mục "Firmware ESP32-P4 — nhánh bring-up song song" + dòng trỏ; production vẫn khoá S3 |
| `main/audio/audio_afe.c` | 13/09 (§9.9): `afe_perferred_core/priority` = `BOARD_AUDIO_AFE_CORE/PRIO` (mặc định 0/5 = lib = S3; P4 1/6) |
| `main/audio/audio_pipeline.c` | 13/09 (§9.9): `afe_fetch` prio 3→4; `afe_feed` prio = `BOARD_AUDIO_FEED_TASK_PRIO` (S3 6, P4 7); tự kích ghi PCM **tắt** — `audio_pipeline_capture_enable(true)` bật lại; dòng `AFE out 10s` thêm `fetch=/313 feed=/167 short=` |
| `main/audio/opus_codec.c`, `vimate_es8311.c` | 13/09 (§9.9): `opus downlink` chỉ gói #1/gói lạ; `SPK WRITE` 500 lần/lần; `TDM ch rms` sau `BOARD_AUDIO_DIAG_TDM_RMS` (mặc định 0), tính số nguyên |
| `main/boards/board.h` | 13/09: knob `BOARD_AUDIO_AFE_CORE/PRIO`, `BOARD_AUDIO_FEED_TASK_PRIO`, `BOARD_AUDIO_DIAG_TDM_RMS` (mặc định = S3) |
| `main/input/touch.c` | 13/09 (§9.9): dội chạm 80 ms; double-tap → Home chỉ khi cả hai tap gọn + chỗ trống; lịch "ngoài ngày" gửi `home_select("home")` |
| `main/ui/display.c` | 13/09 (§9.9): cache GIF cảm xúc trong PSRAM (24 slot, khoá key+size+mtime) |
| `main/ui/face.c` + `face.h` | **Mới 15/09** (§6.5b): bảng 32 clip mặt robot, chế độ LOOP/ONCE/ONCE_HOLD, map 21 cảm xúc + 11 trạng thái → clip, clip vào trạng thái, biến thể idle |
| `main/ui/display.c` (mặt robot) | 15/09 (§6.5b): khối `#if BOARD_FACE_GIF_FULLSCREEN` — reaction > overlay > base, `face_on_state/emotion_locked`, done → base, lv_timer 1 s (idle + pause khi bị che), preload cache có nghỉ 80 ms, thưởng sao → `sym_celebrate`, `display_show_face()`; không tham chiếu `_binary_*_gif` khi P4 |
| `main/ui/display.h` | 15/09: `display_show_face(const char *key)` (no-op ngoài P4) |
| `main/util/gif/lvgl_gif.c` + `.h` | 15/09 (§6.5b): `lvgl_gif_create_ex(opts)` opaque + scale + hộp bẩn, `set_loop/set_done_cb/pause/last_dirty/take_stats`, cờ `completed`; `lvgl_gif_create` giữ hành vi S3 |
| `main/util/gif/gifdec.c` + `gifdec.h` | 15/09: `dispose()` disposal 0/1 không vẽ lại rect khung trước (caller luôn render ngay sau get_frame); tối 15/09: hook `render_hook/render_user` trong `gd_GIF` (gd_render_frame + disposal 2), vòng LZW bỏ `%`/`/` mỗi pixel |
| `partitions/partitions.p4-43lcd.csv` (bản 2) | tối 15/09: `emo_spiffs` 0x560000, `asset_spiffs`/`lesson_spiffs`/`model` 512 K |
| `tools/build_face_gifs.py` | tối 15/09: `--min-delay` gộp khung ngắn (mặc định 40), `verify()` theo tổng thời lượng, budget 0x560000 |
| `spiffs_face_image/*.gif` | tối 15/09: dựng lại từ bộ 128 khung → 1561 khung, 3,9 MB |
| `main/boards/board.h` | 15/09: knob `BOARD_FACE_GIF_FULLSCREEN` (0), `BOARD_FACE_GIF_SCALE` (2), `BOARD_FACE_GIF_MIN_FRAME_MS` (40), `BOARD_FACE_IDLE_VARIATION_MIN/MAX_S` (15/30), `BOARD_FACE_IDLE_SLEEPY_AFTER_S` (45) |
| `main/boards/board_esp32p4_43lcd.h` | 15/09: bật `BOARD_FACE_GIF_FULLSCREEN 1` + các knob mặt |
| `partitions/partitions.p4-43lcd.csv` | **Mới 15/09**: bảng partition P4 — `emo_spiffs` 0x3E0000, `asset_spiffs`/`lesson_spiffs` 1 M; `sdkconfig.defaults.p4-43lcd` trỏ tới |
| `CMakeLists.txt` gốc + `main/CMakeLists.txt` | 15/09: P4 đóng gói `spiffs_face_image/` vào `emo_spiffs`, không embed 13 GIF Noto (app −160 KB); thêm `ui/face.c` |
| `spiffs_face_image/*.gif`, `tools/build_face_gifs.py` | **Mới 15/09**: 32 clip 400×240 16 màu (2,87 MB) dựng từ `docs/01_gif/` (9,7 MB gốc của người dùng) |
| `main/store/emotion_sync.c` | 15/09: `emotion_sync_init()` idempotent (`esp_spiffs_mounted`) |
| `main/core/wifi_mgr.c` | 15/09 (§5.6): canh gác DHCP 20 s sau CONNECTED; `wifi_apply_band_lock()` (2,4 GHz, sau start + trước reconnect, ngắt nếu ch > 14); AP provisioning lấy kênh STA, INVALID_ARG → disconnect + thử lại |
| `main/core/captive_dns.c` | 15/09 (§5.6): buffer 1040 B sang PSRAM, stack 3072 → 3584, log HWM khi dừng |
| `main/boards/board.h` + `board_esp32p4_43lcd.h` | 15/09: knob `BOARD_WIFI_BAND_2G_ONLY` (mặc định 0, P4 1) |
| `sdkconfig.defaults.p4-43lcd` | 15/09: `# CONFIG_LV_USE_TJPGD is not set` (§5.6 lỗi 1) |
| `main/store/course_media_cache.c` | 15/09 (§7 thẻ nhớ): mount thang 4-bit@`BOARD_SD_MMC_FREQ_KHZ` → 4-bit@400 → 1-bit@400; **diskio bounce buffer** 32 sector căn 64 thay `ff_diskio_impl_t` của volume; `sd_benchmark()` 256 KB mỗi boot khi `CONFIG_VIMATE_DIAG_ENABLE` |
| `main/boards/board.h` + `board_esp32p4_43lcd.h` | 15/09: knob `BOARD_SD_MMC_FREQ_KHZ` (mặc định 400 = probing; P4 20000) |
| `sdkconfig.defaults.p4-43lcd` | 15/09: `CONFIG_FATFS_VFS_FSTAT_BLKSIZE=16384` (buffer stdio file FAT = một cụm) |
| `main/network/ws_client.c` | 13/09 (§9.9): timeout gửi WS 500 ms → 3 s (text) / 1 s (opus) — ghi trả 0 là esp_websocket_client abort kết nối |
| `main/core/system_info.c`, `diagnostics.c`, `store/course_media_cache.c` | 13/09 (§9.7): P4 đọc base MAC thẳng; `diag stack low` bỏ IDLE*/ipc*; bỏ dòng `SD SPI mount try` khi SDMMC |

### Vì sao phải nâng main task stack lên 10240

Board panic ngay sau `touch_init`:

```
Guru Meditation Error: Core 0 panic'ed (Stack protection fault).
Detected in task "main" at 0x480773ac   →  lv_tjpgd.c:decoder_info
```

`decoder_info()` của LVGL cấp `uint8_t workb[TJPGD_WORKBUFF_SIZE]` = **4096 byte
ngay trên stack** cộng struct `JDEC`, gọi từ `display_setup_ui()` →
`lv_image_decoder_get_info()` khi LVGL dò file ảnh `.jpg`. 4KB lớn hơn cả cái
stack 3584 mặc định nên tràn chắc chắn. 10240 là đúng giá trị cả 3 demo P4 chính
chủ của board đặt.

## 9. Phân tích log trọn phiên 13/09/2026 17:00 — kế hoạch tối ưu

Log ngoài (`Serial Debug 2026-09-13 170054.txt`, 222 s): POWERON → WiFi → WS → 2 bài
học → 5 lượt hội thoại → WS rớt/nối lại. Bản firmware = §6.7 (chạm prio 7, vuốt, chống
gõ lặp). Mọi số dưới đây trích từ dấu thời gian log, đối chiếu code.

### 9.1 Thời gian boot → sẵn sàng: 22,9 s

| Mốc (ms) | Việc | Mất | Nhận xét |
|---|---|---|---|
| 0–3 786 | ROM/bootloader/PSRAM test/DSI/codec/touch/`display_setup_ui` | 3,8 s | PSRAM memory test ~0,7 s (`esp_psram: SPI SRAM memory test OK` 2005) — tắt được bằng `CONFIG_SPIRAM_MEMTEST=n` cho bản production |
| 3 801–4 433 | SDMMC 3 lần thử (4-bit, 4-bit, 1-bit) → `0x107` | **0,6 s chặn main** | không có card-detect; 3 lần là thừa khi khe trống — 1 lần 4-bit + 1 lần 1-bit, hoặc mount trong task cache sau WS |
| 5 048–6 850 | ESP-Hosted: reset C5 qua GPIO54, SDIO init, chờ slave | 1,8 s | slave 2.7.0 cũ; C5 boot từ đầu mỗi lần vì P4 reset nó |
| 6 850–8 212 | tới `STA started` | 1,4 s | `esp_wifi_init/start` RPC — chưa rõ mất ở đâu, đáng đo |
| 8 237–15 464 | `esp_wifi_remote_connect` → Connected | **7,2 s** | quét đủ kênh + WPA; mạng mesh. Lưu BSSID+channel vào NVS → fast-connect (`sta.channel`, `bssid_set`) bỏ quét, tiết kiệm 2–5 s |
| 15 464–16 979 | DHCP | 1,5 s | mạng |
| 16 982–20 947 | SNTP `pool.ntp.org` | **4,0 s chặn** | chỉ cần "gần đúng" để TLS qua kiểm hạn cert: nạp thời gian đã lưu NVS/RTC ngay, SNTP chỉnh sau; chờ tối đa 1–2 s |
| 20 948–21 465 | OTA check HTTPS | 0,5 s | ok |
| 21 977–22 921 | WS TLS + hello | 0,9 s | ok |
| 25 930–26 179 | AFE + WakeNet nạp | 0,25 s | cố ý sau WS |

→ Có thể xuống ~12–14 s bằng 4 việc không đụng phần cứng: bỏ memtest PSRAM (bản
prod), SD không chặn, SNTP không chặn, WiFi fast-connect.

### 9.2 RAM nội — min 28,0 KB, dưới sàn 30 KB

Dòng thời gian `internal` (KB): boot 219 → trước WS 96 (largest 82) → sau WS 80,5 →
sau AFE 57,7 → heartbeat 24 s **42,3** → 27–223 s dao động 39,7–51,5; `min=28 383`
(107 s) rồi `min=28 031` (168 s).

Ai ăn: WS TLS (in 16 KB + out 4 KB cố định — `MBEDTLS_SSL_IN_CONTENT_LEN=16384`,
`OUT=4096`, `MBEDTLS_DYNAMIC_BUFFER` **tắt**); **mỗi ảnh tải HTTPS = thêm một TLS nữa**
(4 lần `Certificate validated` trong log: 23 616, 90 993, 124 882, 126 515 — mỗi lần
+20 KB buffer + handshake tạm), `http_dl.c` tạo client mới mỗi lần nên không tái dùng
phiên; AFE 12,8 KB; task `img_worker` 8 KB stack tạo/huỷ mỗi lượt (`worker ready` /
`idle exit`). Min 28 KB rơi đúng lúc bài học tải ảnh 800×480 qua HTTPS trong khi WS
TLS đang mở + AFE + LVGL.

→ (1) `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y` (+ `MBEDTLS_DYNAMIC_FREE_CONFIG_DATA`,
`MBEDTLS_DYNAMIC_FREE_CA_CERT`): buffer TLS cấp theo nhu cầu, trả sau handshake —
thường bớt 10–25 KB đỉnh; phải soak WS. (2) Một `esp_http_client` giữ keep-alive cho
media (hoặc prefetch cover ngay khi nhận home payload, tuần tự) → không bao giờ 2 TLS
tải ảnh chồng nhau. (3) Không tạo lại `img_worker`: giữ task thường trực, stack đo lại
(8 KB có thể dư).

### 9.3 CPU: AFE đang chạy trên core 0 (core UI)

`afe_perferred_core: 0, afe_perferred_priority: 5` (log AFE config) — `audio_afe.c`
không đặt → WakeNet/VAD/AGC chạy chung core với taskLVGL (6), touch (7), main/WS (5).
Hệ quả đo được: `AFE out 10s: fetch=276/313` lúc TTS + decode ảnh 800×480 (96 s), 298–
308 suốt lúc loa phát (mất 2–12 % khung); đồng thời render UI chậm hơn → chờ lock LVGL
95–109 ms khi chạm (85 847, 125 840). Trên core 1 chỉ có `afe_feed` (6), `spk_dec` (7),
`afe_fetch` (3), `img_worker` (3) — còn chỗ.

→ `cfg->afe_perferred_core = 1; afe_perferred_priority = 6`; `afe_fetch` lên 4 (đang
bằng `img_worker` 3 nên decode JPEG tranh đều với fetch). Đo lại `AFE out 10s` lúc TTS
và `hit-test cho lock` sau đổi.

### 9.4 Chạm — đường đi thật trong phiên này

| Chạm | UP → hành động | UP → `home_select` | Server trả | Màn mới |
|---|---|---|---|---|
| bar → Lịch (78 974) | 13 ms | 137 ms | +107 ms | — |
| bar → Hẹn giờ (85 751) | 107 ms (lock 95) | 235 ms | +110 ms | — |
| Home → về Home (84 237) | 8 ms | 78 ms | +17 ms | cover 204×288 sau **+830 ms** |
| khoá học 1 (89 945) | 16 ms | 109 ms | LESSON +427 ms | ảnh 800×480 **+1,5 s** (HTTPS 0,54 s + decode/hiển thị 0,73 s) |
| khoá học 2 (125 731) | 110 ms (lock 109) | 165 ms | LESSON +103 ms | ảnh 800×480 **+1,7 s** |

Phần firmware còn ăn: chờ lock LVGL (render) và **decode JPEG mềm** (`esp_new_jpeg`,
`img_worker` prio 3 core 1): 800×480 mất ~0,7 s tới khi hiện; cover 204×288 mất 0,5 s
từ `applied` → `previewed`. **ESP32-P4 có bộ giải mã JPEG cứng** (`esp_driver_jpeg`,
component `jpeg`): 800×480 ≈ vài ms, output RGB565 thẳng vào PSRAM DMA. Đây là thay đổi
cho lợi lớn nhất về "thấy ảnh nhanh": −0,5…−0,7 s mỗi ảnh và trả CPU core 1 cho AFE.

### 9.5 Log/diag tự làm nghẽn UART

~60 % dòng log là dump `WD:` (capture `afe` + `uplink` tự kích lúc 33 s vì ồn phòng
≥ 600 rms): 72 000 + 48 000 mẫu → ~250 KB base64 → **~22 s UART 115200 kín** (36–87 s),
mọi `ESP_LOG` khác xếp hàng sau (nhiều dòng in trễ, `Mic OFF` in sau `→ abort`). Cộng
`TDM ch rms` 3 s/lần, `opus downlink #1..8` 8 dòng mỗi lượt TTS, `SPK WRITE` 2 s/lần.
Đã xong việc phân tích mic (§4.7) → tắt auto-kích capture (giữ lệnh kích tay), gộp
`opus downlink` thành 1 dòng/lượt, `TDM ch rms` chỉ khi có cờ.

### 9.6 UX lộ ra

1. **Dội chạm 40 ms**: 110 390 UP → 110 430 DOWN (40 ms) → `Double-tap → VỀ HOME` ngay
   lúc vừa mở lượt nghe. Người không gõ hai lần cách 40 ms; đó là ngón nhấc-chạm lại
   (bounce). → DOWN mới < 80 ms sau UP = cùng một lần chạm (nối tiếp, không tính tap).
2. **Lịch học "tap ngoài ngày → về Home" chỉ ẩn lịch** (81 863), không gửi gì lên
   server → màn trống cho tới khi người dùng chạm lần nữa (84 048 → `đánh thức về HOME`).
   → gửi `home_select("home")` (như nút Home) hoặc hiện lại lưới Home cục bộ.
3. Tap (382,441) nằm trong vùng nút Home nhưng nút đang ẩn (lịch vừa đóng, Home chưa về)
   → rơi vào nhánh "chưa vào AI → HOME": kết quả đúng nhờ may.
4. `custom GIF loaded: …` mỗi lần đổi cảm xúc đọc lại 25–32 KB từ SPIFFS (3–4 lần/lượt);
   PSRAM còn 23 MB → cache toàn bộ 13 GIF trong PSRAM lúc boot.
5. WS rớt 209 s (`transport_poll_write(0)`, mạng) giữa lúc đang nghe → nối lại sau 6 s,
   server gửi lại 5 lệnh cấu hình + home; lượt nói mất. Ngoài firmware, nhưng nên hiện
   trạng thái "mất mạng" trên màn thay vì im lặng 6 s.

### 9.7 Dọn nhiễu (rẻ)

`E lcd_panel: esp_lcd_panel_swap_xy not supported` (không gọi swap_xy khi DSI),
`E system_api: 0 mac type is incorrect` (đọc MAC kiểu `ESP_MAC_BASE`/từ hosted), `SD SPI
mount try mosi=-1…` in trước SDMMC (dòng thừa), `diag stack low` cho IDLE0/1 và ipc
(ngưỡng chung 1 200 B không hợp task hệ thống 1 KB), `touch free=1016` (3 072 B đủ, có
thể nâng 3 584 để hết cảnh báo).

### 9.8 Thứ tự đề xuất (lợi / công)

| # | Việc | Lợi | Công | Rủi ro |
|---|---|---|---|---|
| 1 | AFE → core 1 prio 6, `afe_fetch` 4 | AFE không mất khung khi TTS; UI bớt chờ lock | 3 dòng | thấp — đo lại 10 phút |
| 2 | Tắt auto-capture + gộp log audio | UART thoáng, log đọc được, bớt CPU log | nhỏ | không |
| 3 | `MBEDTLS_DYNAMIC_BUFFER` | +10–25 KB RAM nội đỉnh | 1 dòng sdkconfig | vừa — soak WS 1 giờ |
| 4 | JPEG cứng P4 (`esp_driver_jpeg`) thay `esp_new_jpeg` | −0,5…−0,7 s mỗi ảnh, trả CPU core 1 | 1 ngày | vừa — format YUV/RGB565, căn 64 B |
| 5 | HTTPS keep-alive/prefetch cover | −0,3…−0,5 s mỗi ảnh, bớt 1 TLS chồng | nửa ngày | thấp |
| 6 | Boot: SD không chặn, SNTP không chặn, WiFi fast-connect, tắt PSRAM memtest (prod) | boot 22,9 → ~13 s | nửa ngày | thấp |
| 7 | UX: bounce 80 ms, lịch → Home thật, GIF cache PSRAM, báo mất mạng | — | nhỏ | thấp |
| 8 | Render: bỏ shadow SW bar/ô Home, cache khung GIF đã scale | lock LVGL 95–110 → mục tiêu < 40 ms | 1–2 ngày | vừa |
| 9 | Dọn nhiễu §9.7 | log sạch | nhỏ | không |

### 9.9 Đợt 1 (mục 1+2+7+9 + phát sinh) — ĐÃ NẠP, ĐO 3 phiên 13/09 18:00–19:00

Ba lần nạp/đo: boot 100 s, soak 300 s, soak 420 s (tay người: 2–3 lượt Hi Lily, mở
khoá học lúc loa nói, gõ dồn 4–5 lần vào nút bar, chạm ô ngày trong Lịch).

| Việc | Kết quả đo | Ghi chú |
|---|---|---|
| AFE → core 1 prio 6 (`BOARD_AUDIO_AFE_CORE/PRIO`) | log `afe_perferred_core: 1 / priority: 6` | nhưng **không** hết mất khung (xem dòng feed) |
| Đếm `feed=/167` thêm vào `AFE out 10s` | nghỉ: `313/167`; loa phát: `fetch 303–309, feed 162–164`, `short=0` | fetch mất đúng bằng feed mất → tràn **I2S RX** (DMA chỉ 60 ms = 1 lần đọc, không lề — không nâng được, RAM); nguyên nhân là `afe_feed` (6) bị `spk_dec` (7) + task AFE (6) chèn, không phải core 0 như §9.3 đoán |
| `afe_feed` → prio 7 (`BOARD_AUDIO_FEED_TASK_PRIO`) | boot 90 s: `313/313 feed=167/167` khi nghỉ; **chưa đo lúc loa phát** | đo ở phiên tay người kế tiếp; kỳ vọng feed=167 khi TTS |
| `afe_fetch` 3 → 4 | — | không thấy tác dụng riêng; giữ (trên `img_worker` 3) |
| Tắt tự kích ghi PCM, gộp `opus downlink`/`SPK WRITE`, `TDM ch rms` sau knob | 0 dòng `WD:`, log 420 s = 1 196 dòng (trước: ~60 % là dump) | `audio_pipeline_capture_enable(true)` khi cần ghi lại |
| Dội chạm 80 ms | gõ dồn 5 lần/1,2 s vào một nút → 1 lệnh, 4 dòng `Dội chạm (110–143 ms) — bỏ qua` | khoảng UP→DOWN thật của tay người: 34–78 ms |
| Double-tap → Home | soak 300 s: tap nghe 67 ms → 81 ms sau đặt lại ngón 231 ms trượt 30 px → **vẫn về Home giữa bài học** → thêm luật "cả hai tap gọn (≤180 ms, ≤15 px) + cùng chỗ ≤60 px + đều chỗ trống" | soak 420 s: 3 double-tap đều ở mép dưới màn bài học (y=468–479, nơi bar bị ẩn) — có thể là cố ý; **hỏi người dùng** |
| Lịch "ngoài ngày" → `home_select("home")` | 4 lần → server trả Home ngay | trước chỉ ẩn lịch |
| GIF cache PSRAM | `neutral/happy/thinking … -> cache PSRAM` mỗi mặt 1 lần; sau đó im | ~28–32 KB/mặt |
| Dọn nhiễu §9.7 | 0 dòng `E system_api`, `SD SPI mount try`, `diag stack low IDLE/ipc` | còn `afe_feed free=1192–1228` (4 KB) — theo dõi |
| `hit-test cho lock LVGL` | 21–102 ms khi gõ dồn lúc decode ảnh (trước 95–144) | còn spike → mục 4/8 |
| RAM nội | `min=27 895–29 083` lúc WS nối lại + tải ảnh | đúng §9.2 → mục 3 |

**Phát hiện mới — WS rớt là do SERVER đóng, không phải firmware.** 5 lần rớt trong 3
phiên, cùng một kịch bản: `TTS stop asks listen_after → listen start → Mic ON` rồi
**1,3–5 s sau** socket chết:
- soak 300 s (timeout ghi 500 ms): `Poll timeout … timeout_ms=500` (ghi trả 0);
- soak 420 s (timeout 1 s): `poll_write select error 104, errno = Connection reset by
  peer` (172 422) và `transport_poll_write(0) … errno=128` ENOTCONN (207 698) — tức
  **server gửi RST** trong lúc thiết bị đang stream opus của lượt nghe tự động sau TTS.

Firmware chỉ chịu phần "chết nhanh": esp_websocket_client 1.8 coi ghi trả 0 (poll
timeout) là lỗi → abort; timeout 500 ms làm WiFi mesh khựng 0,5 s cũng đứt. Đã nâng
3 s (lệnh) / 1 s (opus) — không cứu được RST nhưng không còn tự đứt vì khựng ngắn.
**Việc cần làm phía server** (`vimate.vn/ws/`): xem log handler khi nhận binary
frame trong phiên `listen_after` ngay sau TTS — khả năng handler ASR chưa mở lại
session mà đã nhận audio → exception → đóng socket. Firmware phía nối lại: 5 s
(`CONFIG_VIMATE_WS_RECONNECT_BASE_MS`) + TLS 0,9 s; có thể hạ base xuống 1,5 s cho P4.

Chưa làm trong đợt này: báo "mất mạng" trên màn đã có sẵn (`DEV_STATE_ERROR` "Đang
kết nối lại" ở WS DOWN) nên không thêm.
