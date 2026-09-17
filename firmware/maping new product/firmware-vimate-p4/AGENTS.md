# firmware-vimate-p4 — Hướng dẫn cho agent (nguồn chính của cây P4)

Cây này là **nhánh bring-up song song** cho board FBT **ESP32-P4C5 + LCD 4.3" ST7102
MIPI-DSI + camera SC2336**. Production vẫn là `../firmware-vimate/` (ESP32-S3) và
`../AGENTS.md` vẫn là luật chung của repo; file này chỉ **thêm** ràng buộc cho thư mục
`firmware-vimate-p4/`. Khi hai file mâu thuẫn về phạm vi sản phẩm → `../AGENTS.md`
thắng; về phần cứng P4 → file này thắng, và phải nêu mâu thuẫn ra.

Nhật ký kỹ thuật đầy đủ (mọi số đo, log, bẫy, theo mục §) là `README-P4.md`. File
này cố ý ngắn: **luật + sự thật phần cứng + chỉ mục**. Đừng chép README-P4 vào đây.

## 0. Phạm vi và ranh giới — đọc trước

- Chỉ sửa trong `firmware-vimate-p4/`. **Không đụng** `../firmware-vimate/` (cây S3
  production), kể cả "sửa tiện tay". Nếu một sửa chữa đúng cho cả hai cây, ghi vào
  README-P4 §8 để người sở hữu merge ngược, không tự làm.
- Mọi khác biệt so với S3 đi qua **knob** trong `main/boards/board_esp32p4_43lcd.h`
  hoặc `#if BOARD_*` với **mặc định giữ nguyên hành vi S3** (khai `#ifndef` default
  trong `main/boards/board.h`). Bảng README-P4 §8 là danh sách khác biệt — cập nhật khi
  thêm.
- Không commit trừ khi người dùng yêu cầu; trước đó build `BUILD_EXIT=0`. Không đưa
  credential vào repo: `partitions/partitions.csv` bản dev đang ghi mật khẩu WiFi nhà
  dạng chữ thường (README-P4 §7) — không sao chép nó đi đâu.
- Kết luận về phần cứng phải có **bằng chứng**: log UART, số đo, hoặc schematic qua
  `docs/HARDWARE-PINOUT.md`. Ba project demo của hãng trong `ESP-IDF 5/` chỉ là tham
  khảo LCD/camera — đã dính bẫy chép cờ Kconfig không tồn tại từ đó (§7 bảng, dòng 5).
- Không thêm task FreeRTOS, dependency, setting, tính năng nếu chưa qua cổng §5.
- Sau mỗi thay đổi có ý nghĩa: ghi kết quả (số đo + dòng log chứng minh) vào đúng mục
  README-P4, cập nhật §1 bảng trạng thái và §8 bảng khác biệt. Không tạo README mới.

## 1. Điều hướng nhanh — đọc hẹp

Đọc file "đọc trước" và cộng sự trực tiếp của nó; `rg` đúng tên symbol thay vì quét
cả thư mục. File lớn (`display.c` ~4000 dòng, `audio_pipeline.c` ~1500) → đọc theo
khoảng dòng quanh symbol.

| Khu vực | Đọc trước | Sở hữu |
|---|---|---|
| Pinout + knob board | `main/boards/board_esp32p4_43lcd.h`, `docs/HARDWARE-PINOUT.md` | Mọi chân, địa chỉ I2C, sample rate, góc xoay, cỡ emoji, knob AFE/mic, tư liệu chân chưa có driver |
| Chọn board / default knob | `main/boards/board.h`, `main/Kconfig.projbuild` (`choice VIMATE_BOARD`) | Nhánh `CONFIG_VIMATE_BOARD_P4_43LCD`, `#ifndef` mặc định cho mọi knob |
| Boot / runtime | `main/app_main.c` | Thứ tự init: NVS → diag → `display_init` → `audio_pipeline_init` → button → `touch_init` → WiFi → WS → telemetry; máy trạng thái thiết bị; neo TLS `g_vimate_tls_anchor` |
| Màn hình | `main/ui/display.c` + `display.h` | LDO DPHY → DSI → DPI, reset cứng GPIO22, 2 frame buffer + vsync, khối `DSI_ROTATE` (PPA), display task + hàng đợi `sched_q`, khối mặt robot `BOARD_FACE_GIF_FULLSCREEN`, icon WiFi + Bluetooth, đèn nền |
| Mặt robot (emoji) | `main/ui/face.c` + `face.h`, `main/util/gif/lvgl_gif.c`, `tools/build_face_gifs.py` | Bảng 32 clip + chế độ LOOP/ONCE/HOLD + map cảm xúc/trạng thái (face.c); decode opaque ×2 theo hộp bẩn (lvgl_gif); dựng asset từ `docs/01_gif/` vào `spiffs_face_image/` → partition `emo_spiffs` 3,875 MB (README-P4 §6.5b) |
| Touch | `main/input/touch.c` | ST7123 giao thức reg16 (`BOARD_TOUCH_USE_REG16`), map native → logical theo knob |
| Audio | `main/audio/audio_pipeline.c`, `vimate_es8311.c`, `audio_afe.c`, `opus_codec.c` | ES8311 + ES7210 qua `esp_codec_dev`, ESP-SR AFE (VAD/WakeNet/AGC), Opus, bộ ghi chẩn đoán `wcap_*` |
| WiFi / provisioning | `main/core/wifi_mgr.c`, `captive_dns.c`, `main/ui/ui_wifi_setup.c` | C5 qua `esp_hosted`/`esp_wifi_remote`, SoftAP + captive portal, mã QR |
| Server | `main/network/ws_client.c`, `main/protocol/envelope.c` | WebSocket, khung MCP `self.edu.*` |
| Thẻ nhớ / cache | `main/store/course_media_cache.c` | SDMMC slot 0 + LDO kênh 4, fallback HTTP |
| MP4 | `main/media/mp4_player.cpp` | `esp_extractor` + `av_render`, vẽ qua `display_panel_blit()` |
| Cấu hình build | `sdkconfig.defaults`, `sdkconfig.defaults.p4-43lcd`, `partitions/partitions.p4-43lcd.csv`, `main/idf_component.yml`, `CMakeLists.txt` gốc, `main/CMakeLists.txt` | Profile P4, **bảng partition riêng** (emo 3,875 MB, asset/lesson 1 MB), ghim version, workaround `-L` không bọc nháy |
| Chẩn đoán | `main/core/diagnostics.c`, `tools/wake_dump_to_wav.py` | Dòng `diag heap` / `diag rotate`, dump PCM `WD:` (tắt mặc định — `audio_pipeline_capture_enable(true)`) |
| Driver panel | `components/esp_lcd_st7102/` | Vendor ST7102 (không có trên registry) |

## 2. Phần cứng — sự thật không thương lượng

Nguồn: `docs/HARDWARE-PINOUT.md` (dựng từ schematic thật, có § tương ứng) + số đo trên
board ghi ở README-P4. Số chân cụ thể **luôn** lấy từ board header, không chép từ đây.

- **MCU** ESP32-P4 silicon **rev v1.3** (bootloader in `chip revision: v1.3`) → esp-sr
  phải dùng `lib/esp32p4_less_v3`. 2 core RISC-V 360 MHz, flash 16 MB, **PSRAM 32 MB**
  (XIP). **RAM nội là thứ hiếm nhất**: lúc chạy đủ AFE còn ~53 KB, min 31 KB khi nối WS.
- **Radio**: không có WiFi/BT trên P4. WiFi qua **ESP32-C5** trong module, ESP-Hosted
  **SDIO slot 1** (GPIO14–19, reset slave GPIO54), slave FW 2.7.0 (host 2.12). C5 **hai
  băng**: associate 5 GHz được nhưng DHCP không xong (15/09) → firmware khoá 2,4 GHz
  (`BOARD_WIFI_BAND_2G_ONLY`, README-P4 §5.6); AP provisioning phải cùng kênh STA.
  **Không có BLE** (`ble_wifi_prov.c` là stub). Không có UART tới C5 → nâng slave chỉ
  bằng OTA qua SDIO, **không có đường cứu**.
- **LCD** ST7102 MIPI-DSI 2 lane 520 Mbps, DPI 37.8 MHz; panel **native DỌC 480×800**,
  video mode, **không xoay được bằng phần cứng**. LDO nội **kênh 3 @ 2.5 V** cấp DPHY —
  bắt buộc acquire trước `esp_lcd_new_dsi_bus()`. UI chạy **logical 800×480**
  (`BOARD_LCD_ROTATION 270`): LVGL vẽ 800×480, `display.c` xoay vùng bẩn bằng **PPA**
  vào frame buffer DPI ẩn rồi đổi khung đúng vsync (2 fb).
- **GPIO22 = `LCD_RST` = `TP_RST`** — một net, chỉ có RC → panel **không reset theo
  MCU**. `display.c` reset cứng trước khi mở DSI; **không module nào khác** được giữ hay
  toggle chân này (`BOARD_TOUCH_RST_GPIO` cố tình = -1).
- **Đèn nền** GPIO6 = EN SY7200, pull-up → **sáng mặc định** khi chưa cấu hình. LEDC PWM.
- **Touch** ST7123 I2C `0x55`, INT GPIO23, toạ độ native dọc; map sang logical bằng
  `BOARD_TOUCH_SWAP_XY / MIRROR_X / MIRROR_Y` suy từ `BOARD_LCD_ROTATION`.
- **MỘT bus I2C** GPIO7 SDA / GPIO8 SCL cho **7 thiết bị**: touch 0x55, ES8311 0x18,
  ES7210 0x40, AXP2101 0x34, IMU 0x6A, MCP4725, SCCB camera. Lấy handle bằng
  `i2c_master_get_bus_handle()` trước, chỉ `i2c_new_master_bus()` khi chưa có (mẫu ở
  `display.c`, `touch.c`, `vimate_es8311.c`).
- **MỘT bus I2S** cho **hai codec**: MCLK 13, BCLK 12, WS 10, DIN 11 (ES7210 → ESP),
  DOUT 9 (ESP → ES8311). ⇒ **thu và phát PHẢI cùng sample rate** (24000; lệch là
  `I2S_IF: ... record conflict sample_rate`). `ASDOUT` ES8311 không nối → **ADC ES8311
  vô dụng, mic chỉ qua ES7210** (`BOARD_AUDIO_USE_ES7210_ADC 1`). PA GPIO3 active HIGH,
  ampli NS4150B ăn `AXP_VSYS`. ES7210: MIC1/MIC2 = 2 mic MEMS (đã kiểm cả hai sống),
  MIC3 = AEC reference cứng từ DAC_OUT (**chưa lấy được** qua TDM của driver), MIC4 trống.
- **Thẻ nhớ** SDMMC **slot 0 IOMUX** (CLK 43, CMD 44, D0–D3 39–42), nguồn **LDO nội
  kênh 4** — không bật là `send_op_cond 0x107`; khe trống cũng `0x107` (15/09: cắm thẻ
  SDHC 4 GB là lên, 4-bit 20 MHz). Không card-detect. Không xung đột với C5 (slot 1)
  nhưng **chung một host SDMMC** → lệnh SD xếp hàng sau lưu lượng C5. Driver chỉ DMA
  nhiều block khi buffer căn 64 B → `course_media_cache.c` bọc diskio bằng bounce
  buffer + `CONFIG_FATFS_VFS_FSTAT_BLKSIZE=16384`; đo 363 KB/s ghi / 761 KB/s đọc
  (README-P4 §7).
- **Nút**: BOOT GPIO35 (chung net DTR của CH343 — chỉ nhận xung, không nút giả), BTN3
  GPIO0 (chưa có code). **LED** WS2812B GPIO34 — cần RMT/SPI, chưa có driver, không
  phải GPIO bật/tắt.
- **PMIC** AXP2101 0x34, IRQ GPIO21: board **không có chân đo pin, không chân giữ
  nguồn** — mọi thông tin pin/sạc phải hỏi PMIC; chưa có driver.
- **`BOOST_ON` GPIO20** là tiên quyết cho RS485 / MCP4725 / USB host; chia áp hồi tiếp
  gợi ý `BOOST_5V` có thể chỉ ~2.6 V — **đo trước khi tin**. MCP4725 VIH 3.5 V > bus
  3.3 V. IMU CS thả nổi. (`HARDWARE-PINOUT.md` §13.8–11.)
- **Console** UART0 115200 qua **CH343** (số COM đổi theo lần cắm / cổng USB: COM47 ↔
  COM48 trong hai ngày 12–13/09 — luôn `python scripts/p4_readlog.py --list` hoặc dùng
  `auto`, không tin số cũ). USB-Serial-JTAG nội là console phụ.
- **Chân trống**: GPIO1, 5, 36, 54 — chỉ ở chân module, không ra header.
- **Chưa có driver / chưa bật**: camera SC2336 (SCCB chung bus I2C), AXP2101, WS2812,
  IMU, RS485, MCP4725, 4G ML307R. Số chân là **tư liệu** trong board header — không khai
  vào knob "sống" (`BOARD_LED_GPIO` giữ -1) để không lừa người sau.

## 3. Luật ràng buộc phần cứng (HAL của cây này)

1. **Mọi chân, địa chỉ, tần số, kích thước màn** đi qua macro `BOARD_*` — include
   `boards/board.h`, không bao giờ `board_esp32p4_43lcd.h` trực tiếp. Không số GPIO,
   `0x18`, `24000`, `800`/`480` trong module. Thiếu knob → **thêm knob vào board header**
   kèm nguồn (§ schematic hoặc số đo + ngày), không hard-code tại chỗ.
2. **Hai hệ toạ độ màn**: `BOARD_LCD_H_RES/V_RES` = logical (LVGL, 800×480);
   `BOARD_LCD_NATIVE_W/H` = panel (480×800). Code UI chỉ biết logical. Chỉ khối
   `DSI_ROTATE` và `display_panel_blit()` trong `display.c` được biết native.
3. **Đổi hướng màn = đổi một knob** `BOARD_LCD_ROTATION` (0/90/270). H/V_RES và knob
   touch suy ra từ nó bằng `#if` — không sửa `touch.c`/`display.c` để "xoay".
4. **LVGL chỉ được gọi** trong display task hoặc dưới `display_lock()/display_unlock()`.
   Module khác muốn đổi UI → gọi API `display_set_*` / `display_show_*` (chúng đi qua
   hàng đợi). Không `lv_*` từ ISR, task audio, callback WiFi. **Task input (touch/btn)
   không được chờ lock** trên đường lấy mẫu — taskLVGL giữ lock 20–160 ms mỗi khung
   render; input phải ở prio cao hơn LVGL (`BOARD_TOUCH_TASK_PRIO` 7 > 6) và đẩy phản
   hồi UI qua `display_schedule` (README-P4 §6.7).
5. **GPIO22 chỉ `display.c`**, reset một lần trước DSI; sau đó không toggle (màn chớp).
6. **I2C**: get-handle trước, tạo bus chỉ khi chưa có; cùng `i2c_master_bus_handle_t`
   cho mọi thiết bị kể cả SCCB camera sau này. Không tạo bus hai lần.
7. **Audio**: không đổi sample rate một chiều; RX ≥3 mic phải mở `bits=32, channel=2`
   (esp_codec_dev với `channel=4,16-bit` → `slot_mask=0xF` → I2S read timeout); PA bật
   muộn/tắt sớm đã có trong `vimate_es8311.c`; AEC của AFE chỉ khi loa phát.
8. **Knob mới** phải có `#ifndef` default trong `board.h` = hành vi S3, và một dòng
   trong README-P4 §8.
9. **Cấu hình Kconfig** chỉ qua `sdkconfig.defaults*` (ASCII thuần, không dấu). Không
   sửa `sdkconfig` sinh ra. Sửa defaults phải xoá `sdkconfig` (script đã tự làm).
10. **Không tin demo hãng hơn schematic**; không tin ghi chú cũ hơn log mới — hai ghi
    chú sai đã bị lật (SDIO chung thẻ nhớ, "AGC clip" do tool dựng WAV lệch 1 byte).

## 4. Luật tài nguyên

1. **RAM nội**: buffer > 4 KB → `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`; RAM nội chỉ
   khi DMA/ISR/IRAM bắt buộc và nêu lý do. Mục tiêu giữ `diag heap internal` ≥ ~45 KB
   lúc ổn định; min < 30 KB là phải cắt.
2. **Cache/DMA**: L2 cache line P4 = **64 B** (`CONFIG_CACHE_L2_CACHE_LINE_64B`, 128 làm
   SDIO chết). Buffer cho DMA/PPA/DSI: `heap_caps_aligned_alloc(64, ...)` +
   `esp_cache_msync()` sau khi CPU ghi. Không cast con trỏ lệch.
3. **RTC/LP RAM không vào heap** (`ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP=n`) — SIMD của
   esp-sr đọc `0x5010xxxx` là `Load access fault`.
4. **Không thêm task** nếu ghép được vào task có sẵn (icon WiFi dùng heartbeat task).
   Nếu bắt buộc: stack theo `main/core/task_profile.h`, ghi core, đo high-water mark,
   ghi số vào README-P4.
5. **Stack**: main task 10240 (TJPGD cấp 4 KB trên stack). Mảng cục bộ > 256 B phải giải
   thích. Không `std::string`/`printf` float trong task audio.
6. **TLS trên P4** (`_Thread_local`, micro-opus pseudostack): neo `g_vimate_tls_anchor`
   trong `app_main.c` là **bắt buộc** — bỏ là PT_TLS lệch 0x48 B → Opus panic. Thêm biến
   TLS mới phải kiểm lại `readelf -l` PT_TLS.
7. **Cấm** `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` (esp_wifi_remote RPC NO_MEM boot-loop).
   `LWIP_MAX_SOCKETS=10`.
8. **CPU core 1**: AFE `LOW_COST`, format `"M"`; `HIGH_PERF`, `"MMR"`/`"MMNR"`, AEC/BSS
   liên tục đã đo là **quá nặng** cho `esp32p4_less_v3` (`Ringbuffer FEED full`,
   `task_wdt IDLE1`) — đừng thử lại mà không có số mới. Bố trí core 1 (13/09):
   `spk_dec` 7, `afe_feed` 7 (`BOARD_AUDIO_FEED_TASK_PRIO`), task AFE 6
   (`BOARD_AUDIO_AFE_CORE/PRIO`), `afe_fetch` 4, `img_worker` 3. I2S RX DMA chỉ 60 ms
   (= 1 lần đọc, RAM không cho nâng) nên `afe_feed` phải chạy ngay khi có mẫu; đo bằng
   `AFE out 10s: fetch=/313 feed=/167` — feed thiếu = tràn I2S, không phải AFE chậm.
9. **Flash**: app slot 4.5 MB còn ~15 % (15/09, sau khi bỏ embed 13 GIF Noto); P4 dùng
   `partitions/partitions.p4-43lcd.csv`: `emo_spiffs` **5,375 MB** chứa 32 clip mặt robot
   3,9 MB (1561 khung, tool fail nếu > 90 %), `asset_spiffs`/`lesson_spiffs`/`model`
   512 KB (`srmodels.bin` 291 KB — thêm model phải mở lại). Đổi bảng = nạp USB. Mỗi
   dependency mới phải nêu số byte (`idf.py size-components`).
10. **Mỗi cấp phát mới** = một dòng "vì sao không stack/static/tái dùng + cỡ xấu nhất".
    Không tuyên bố "nhanh hơn/nhẹ hơn" nếu không nêu cơ chế và số đo trước/sau.

## 5. Cổng trước khi thêm tính năng / task / dependency / setting

Trả lời theo thứ tự, dừng ở câu đầu tiên không đạt:

1. Có phục vụ **VIMATE Edu trên board P4** không? (Không thêm chức năng ngoài sản phẩm;
   RS485/4G/IMU/DAC chỉ khi có yêu cầu sản phẩm.)
2. **Chi phí**: RAM nội, PSRAM, flash, CPU core 1 — **đo** (`diag heap`, `size`), không đoán.
3. Làm được bằng **knob/API có sẵn** không? (`display_set_*`, board knob, profile server.)
4. **Giữ build S3 nguyên** không? (knob default.)
5. Dependency mới: ship lib cho `esp32p4`? có `-L` không bọc nháy (README-P4 §2 bẫy 4)?
   có hợp ESP-IDF 5.5.1 không (`esp_lvgl_port` ghim `~2.8.0`)?

Không qua → nói lý do cụ thể và đề xuất cách trong phạm vi. Quyết định rồi nói vì sao,
không đưa danh sách lựa chọn.

## 6. Build, nạp, đọc log — máy Windows, chỉ cmd.exe

- **Build**: `scripts\build_p4_43lcd.bat` (tự xoá `sdkconfig` khi defaults mới hơn, tự
  `set-target` lần đầu, in `BUILD_EXIT=0/1`). `scripts\build_p4_43lcd.bat COMxx` =
  build rồi nạp. Chỉ nạp: `scripts\flash_p4_43lcd.bat COMxx` (`COMxx` lấy từ
  `p4_readlog.py --list`). Redirect log ra file phải đặt `PYTHONIOENCODING=utf-8`
  (esp-sr `movemodel.py` in ký tự Unicode → cp1252 chết, README-P4 §2 bẫy 6).
- **Bộ mặt robot**: sửa/thêm GIF trong `docs/01_gif/` → `python tools/build_face_gifs.py`
  (gộp khung < 40 ms, in bảng size, fail khi vượt ngân sách) → build + nạp (SPIFFS image
  đi cùng `flash`). Không sửa tay `spiffs_face_image/`. Khung nhanh hơn 40 ms không có
  ý nghĩa trên máy: decode 9–13 ms + blit + PPA; muốn mượt hơn phải đo `face 10s:` trước.
- **Đọc log**: `python scripts\p4_readlog.py auto 60 boot.log` (`auto` tự tìm cổng
  CH343 — số COM đổi theo lần cắm, COM47 ↔ COM48; `--list` để xem; reset qua DTR/RTS;
  `--no-reset` để bắt tiếp). Ghi log vào scratchpad, không vào repo.
- **Git Bash không build được** bằng `export.sh` (không đặt `IDF_PATH`). Gọi script qua
  `MSYS_NO_PATHCONV=1 cmd.exe /c "scripts\build_p4_43lcd.bat"` — không có
  `MSYS_NO_PATHCONV` thì MSYS đổi `/c` thành `C:\` và cmd mở shell tương tác; script tự
  xoá biến `MSYSTEM` vì `export.bat` của ESP-IDF từ chối khi thấy nó. Heredoc bash có
  `\` hoặc CRLF làm hỏng file → sửa file bằng tool Edit/Write hoặc script `.py`.
- Build dir `build_p4_43lcd/` tách riêng; không đụng `build_s3_*`. Đường dẫn repo có dấu
  cách là chuyện đã xử lý — không dời repo, không dùng tên 8.3.
- Trước khi nạp: dừng mọi tiến trình giữ COM (readlog nền, monitor).
- **Tiêu chuẩn "xong"** (chi tiết skill `vimate-p4-verify`): `BUILD_EXIT=0` → nạp → log
  ≥ 60 s: 0 dòng `task_wdt` / `Guru Meditation` / `stack overflow` / `Stack protection` /
  `Ringbuffer FEED full`; có
  `DSI rotate: logical 800x480 -> panel 480x800`, `Touch ST7123 init OK`,
  `opus decode self-test ... 480 mau`, `AFE out 10s: fetch=31x`, `diag heap internal`
  ≥ ~45 KB; rồi nói người dùng cần nhìn/nghe gì trên board để xác nhận phần máy không
  tự kiểm được.
- Sau khi sửa: bin phải mới hơn nguồn; cây S3 `git status` không đổi; README-P4 cập nhật.

## 7. Bẫy đã biết — chỉ mục (chi tiết ở README-P4)

| Triệu chứng | Nguyên nhân → cách xử | README-P4 |
|---|---|---|
| `UnicodeDecodeError: 'charmap'` khi build | chữ có dấu trong `sdkconfig.defaults*` → ASCII | §2 bẫy 1 |
| Sửa defaults, build không đổi | `sdkconfig` cũ → xoá (script tự làm) | §2 bẫy 2 |
| `no member named 'on_frame_buf_complete'` | `esp_lvgl_port` 2.9 cần IDF 5.5.2 → ghim `~2.8.0` | §2 bẫy 3 |
| `ld: cannot find 4.3LCD+SC2336` / `-lesp_audio_effects` | `-L` không bọc nháy trong 3 component media → workaround `CMakeLists.txt` gốc | §2 bẫy 4 |
| `unknown kconfig symbol ESP32P4_SELECTS_REV_LESS_V3` | không tồn tại trên 5.5.1; tự chọn `less_v3` tới 5.5.2; **5.5.3+ phải thêm lại** | §2 bẫy 5 |
| `Stack protection fault` tại `lv_tjpgd.c` (bất kỳ task nào gọi `lv_image_set_src`, kể cả QR) | `decoder_info` cấp 4 KB stack ngay khi vào hàm → P4 **tắt `LV_USE_TJPGD`** (JPG đi `esp_new_jpeg`); main task 10240 là cách cũ | §5.6, §8 cuối |
| `set_config AP: ESP_ERR_INVALID_ARG` khi vào SoftAP | STA đang associate ở kênh khác → AP lấy kênh STA, fail thì disconnect + thử lại | §5.6 |
| `Station mode: Connected` mà không `Got IP` | associate 5 GHz (`ch=36`) → khoá 2,4 GHz + canh gác DHCP 20 s; kiểm dòng `STA associated ssid=… ch=` | §5.6 |
| `A stack overflow in task captive_dns` | buffer 1 KB trên stack 3 KB + log + lwip → PSRAM + 3584. Chuỗi grep crash phải có `stack overflow` | §5.6 |
| Màn đen sau reset MCU, `task_wdt` trong `mipi_dsi_hal_host_gen_read_short_packet` | panel không reset theo MCU + đọc ID DSI không timeout → reset cứng GPIO22, bỏ đọc ID | §6.2 |
| `I2S_IF: ... record conflict sample_rate` | mic/spk khác tần số trên cùng bus | §4 |
| HPF mic thành passthrough | esp-dsp `dsps_biquad_f32_arp4` trả rác trên P4 → bản ANSI | §4 |
| Opus `pseudostack overflow` (dec 386 / enc 1757) | PT_TLS lệch 0x48 B do padding PMP → `g_vimate_tls_anchor` | §4.5 |
| `i2s_channel_read` timeout khi RX 4 kênh | `channel=4,16-bit` → `slot_mask=0xF` → mở `32-bit×2` | §4.7 bẫy 2 |
| `Load access fault` MTVAL `0x50108xxx` trong `dl_esp32p4_sr_*` | RTC RAM trong heap → tắt | §4.7 bẫy 3 |
| `esp_wifi_init` RPC 0x216 `NO_MEM`, boot-loop 30 s | `SPIRAM_TRY_ALLOCATE_WIFI_LWIP` → bỏ | §4.7 bẫy 4 |
| `Ringbuffer FEED full`, `task_wdt IDLE1` | AFE HIGH_PERF / AEC+BSS 2 mic quá nặng → `"M"`, LOW_COST | §4.7 |
| Lượt nghe mở liên tục, WS rớt | voice-activated + VAD nhạy → `VOICE_ACTIVATED_ENABLE 0` | §4.7 bẫy 5 |
| WAV dump nghe như nhiễu trắng full-scale | tool bỏ dòng `WD:` 57 byte lẻ → sửa tool, **không phải lỗi audio** | §4.6 |
| SDIO chết khi có máy nối WiFi | cache line 128 B → 64 B | §5.2b |
| C5 tự nối mạng cũ lúc provisioning | credential còn trong C5 → `WIFI_STORAGE_RAM` + dọn | §5.5 A |
| SD `send_op_cond 0x107` | khe trống (đã xác minh 15/09: có thẻ là mount) hoặc LDO kênh 4 chưa bật | §7 |
| SD ghi/đọc vài chục–trăm KB/s dù bus 20 MHz | buffer không căn 64 B → driver chép từng sector một lệnh; stdio 1 KB → lệnh 2 sector. Giữ diskio bounce + `FATFS_VFS_FSTAT_BLKSIZE=16384`; đọc dòng `SD bench` | §7 |
| Emoji bị cắt hai mép | `EMOJI_TARGET_PX` lấy `max(H,V)` → nhánh DSI dùng `BOARD_EMOJI_TARGET_PX` (chỉ còn PNG dự phòng) | §6.5 |
| `task_wdt IDLE0` lúc boot, `CPU 0: vimate_disp`, boot chậm 10 s | display task (prio 6) tự `display_schedule` liên tục (preload 32 GIF) → đói IDLE0 + main; mọi chuỗi việc dài trong display task phải nghỉ qua esp_timer (`FACE_PRELOAD_GAP_MS`) | §6.5b |
| `UnicodeEncodeError: 'charmap'` từ `movemodel.py` khi build redirect ra file | stdout không phải console → `PYTHONIOENCODING=utf-8` | §2 bẫy 6 |
| Mặt robot đứng im / không đổi theo trạng thái | log phải có `face: <clip> (loop\|once\|once+hold)` mỗi lần đổi; thiếu file → `face: thieu file <key>.gif` (chạy lại `build_face_gifs.py`); bị che → tick 1 s pause, không phải lỗi | §6.5b |
| `idf.py: command not found` / "This .bat file is for Windows CMD.EXE shell only" | gọi từ Git Bash → chạy `scripts\build_p4_43lcd.bat` qua `cmd.exe` | §2 |
| **Màn đen + UART câm** ngay khi mở cổng COM để "chỉ nghe" | pyserial/terminal kéo DTR+RTS khi mở → mạch CH343 giữ chip đứng; thả cả hai trước khi mở (`p4_readlog.py open_port`) | §3.1 |
| Tap nhanh không ăn, không log gì | quét 30 ms + cửa `held > 30 ms` (đã bỏ 13/09: quét 10 ms, ≥ 2 mẫu hoặc ≥ 40 ms) | §6.7 |
| Chạm điều hướng trễ ~1 s | `ui_image_hide()` ngủ 600 ms vô điều kiện + task chạm prio 5 dưới LVGL 6 chờ lock → knob `BOARD_UI_IMAGE_UNHOOK_MS` 40, `BOARD_TOUCH_TASK_PRIO` 7, feedback qua hàng đợi | §6.7 |
| Task input mất mẫu khi UI đang render | không bao giờ `display_lock()` trong task input; mọi thứ cần LVGL đẩy qua `display_schedule` | §6.7 |
| "Double-tap → Home" nổ giữa bài học | trẻ gõ dồn / đặt lại ngón 40–80 ms sau UP; chỉ nhận double-tap khi cả hai tap gọn (≤180 ms, ≤15 px), cùng chỗ, đều chỗ trống; dội < 80 ms bỏ | §9.9 |
| WS rớt đúng 1–5 s sau `listen start` theo sau TTS | server gửi RST (`Connection reset by peer`/ENOTCONN) — **không phải** firmware; timeout ghi WS < 1 s chỉ làm đứt sớm hơn (đã nâng 3 s/1 s) | §9.9 |
| UART "kín" 20 s, log in trễ hàng chục giây | dump `WD:` ~250 KB base64 tự kích theo rms — nay tắt mặc định | §9.5 |

## 8. Skills — đọc khi việc khớp, không đọc hết lúc bắt đầu

| Skill (`../.agents/skills/`) | Đọc khi bạn đang... |
|---|---|
| `vimate-p4-hardware` | chạm `display.c`, `touch.c`, `main/audio/*`, board header, `sdkconfig.defaults*`, bất kỳ GPIO/I2C/I2S/DSI/SDMMC/PPA |
| `vimate-p4-memory` | cấp phát bộ nhớ, buffer DMA/PPA, thêm task, đổi stack, thêm dependency, thấy `diag heap` tụt |
| `vimate-p4-verify` | build, nạp, đọc log, viết câu "đã kiểm" cho người dùng, cập nhật README-P4 |

Skill là playbook lớp trên file này; xung đột → file này thắng, nêu xung đột. Skill
`vimate-fw-module` / `vimate-e2e-module` ở gốc viết cho fork xiaozhi C++ (`firmware/`,
không còn trong repo) — **không áp dụng** cho cây C native này; `vimate-server-module`
vẫn dùng cho phía Go.

## 9. Git

- `git status --short` trước khi sửa và trước khi báo kết quả. Cây `firmware-vimate-p4/`
  hiện **untracked toàn bộ**; hai thay đổi sẵn có ở cây S3 (`dependencies.lock`,
  `sdkconfig.defaults.s3-28lcd`) không phải của phiên này — giữ nguyên.
- `.gitignore` đã loại `build_*/`, `sdkconfig`, `managed_components/`, `*.bin`; khi
  stage kiểm lại không lọt log/WAV/PNG chẩn đoán (để ở scratchpad).
- Nhắn commit gợi ý: `<type>: <tóm tắt>` — `feat` / `fix` / `docs` / `refactor` /
  `chore` / `perf`. Không commit khi chưa được yêu cầu.
