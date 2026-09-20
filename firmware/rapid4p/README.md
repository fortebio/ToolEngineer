# Forte Rapid4P — firmware (RAPID READER 5 SLOT, hai board)

Thiết bị đọc test nhanh **5** ống cùng lúc (TCS34725 ×5 sau mux TCA9548A; số khe = `BOARD_SENSOR_SLOTS`, 4 → 5 ngày 2026-09-17),
màn cảm ứng, WiFi, gửi kết quả về Engineer Server và cập nhật OTA. Kế thừa luồng người dùng của
`FBT-ReaderPlus-1.0` (Arduino ESP32, 4 ống, 3 nút cơ). **Một mã nguồn, HAI board** (2026-09-19):

| Board (`scripts\build.bat <board>`) | Phần cứng | Khoá sản phẩm / kho OTA | Trạng thái |
|---|---|---|---|
| `p4_43lcd` (mặc định) | ESP32-P4C5 + LCD 4.3" ST7102 MIPI-DSI 800×480 + touch ST7123 (board tham chiếu `firmware-vimate-p4`) | `rapid4p` · hw `P4C5-43` | đã nạp board rev v1.3 (2026-09-17), chưa có bo cảm biến |
| `s3_28lcd` | AI-IoT VN **ES3N28P**: ESP32-S3 N16R8 + LCD 2.8" ILI9341 SPI 320×240 + touch FT6236G (pinout `firmware-vimate` s3-28lcd) | `rapid4p-s3` · hw `S3-28` | đã nạp bo 2026-09-19 (LCD/touch/boot sạch), chưa kiểm bằng mắt; chân bo cảm biến là ĐỀ XUẤT |

**Phiên bản:** `v0.1.0` (`main/rapid4p.h`, chung hai board) · ESP-IDF 5.5.1/5.5.4.

## Luồng người dùng

```
START ─ĐO─► Chọn mẫu (Tôm thẻ/Tôm sú/Cá rô phi/Heo/Nước) ─► Chọn bệnh (PC/EHP/EMS/WSSV/TPD)
      ─► Đặt ống vào 5 khe ─ĐO─► Đang đo (3 vòng × 5 khe, ~42 s ước tính) ─► Kết quả 5 khe (0–3000, +/−)
START ─► Cân chỉnh: mỗi khe đọc Cao nhất rồi Thấp nhất → NVS (5 khe)
START ─► Cài đặt: Ngôn ngữ (VI/EN/ZH/TW) · WiFi (SoftAP + portal: SSID, mật khẩu, Mã máy, Token) ·
         Cập nhật (OTA) · Threshold (5 bệnh, mặc định 600)
```
Nút vật lý: **ĐO** = xác nhận/đo (P4: BTN3 GPIO0); **BOOT** (P4 GPIO35 / S3 GPIO0) tap = quay lại, giữ 5 s = xoá WiFi +
khởi động lại. S3 2.8": 3 nút cơ của vỏ máy (dưới).

### 2.8" — 3 nút cơ XANH · ĐỎ · TRẮNG (tối ưu nông dân, 2026-09-20)

Nông dân đeo găng, tay to, nhìn từ xa → mọi việc làm bằng 3 nút cơ dưới màn (GPIO 47/48/41; chạm màn chỉ là phụ, cho kỹ thuật viên):

- **Đo lần đầu**: XANH *Bắt đầu* → chọn mẫu (ĐỎ ▼ / TRẮNG ▲ / XANH *Chọn*) → chọn ống → *Đặt ống vào 5 khe, đậy nắp* → ĐỎ **ĐO**
  → *Đang đo* (đếm ngược "còn ~N s", "Đừng mở nắp", bíp 2 tiếng khi xong) → *Kết quả* ("2/5 DƯƠNG TÍNH" đỏ / "ÂM TÍNH 5/5" xanh)
  → ĐỎ *Xong*.
- **Đo lại cùng loại** (máy nhớ mẫu + ống lần trước): màn chính ĐỎ **Đo lại** → *Đặt ống* → ĐỎ **ĐO** — 2 nhấn.
- **Kỹ thuật viên**: màn chính **giữ TRẮNG 1,5 s** → Cài đặt (Ngôn ngữ · WiFi · Cập nhật · Threshold · Cân chỉnh · Quay lại).
  Dừng đo = **giữ ĐỎ**; xoá cân chỉnh = **giữ TRẮNG** + hộp thoại (XANH Huỷ · ĐỎ Xác nhận).
- Chống nhầm: nhấn khi màn tắt chỉ sáng màn; 350 ms sau khi đổi màn phím bị bỏ (nhấn đôi); 2 nút cùng lúc nhận 1; mỗi phím bíp
  ngắn (loa qua ES8311, `CONFIG_RAPID4P_BEEP`) + ô softkey nháy. Mặc định tiếng Việt; Trung/Đài ẩn tới khi có font CJK.

## Cấu trúc

```
main/
├── app_main.c            boot orchestrator + main task (event bits)
├── rapid4p.h             version, enum bệnh/mẫu/ngôn ngữ, event bits, cấu hình server (khoá/hw lấy từ board)
├── boards/               board.h (chọn board + #ifndef mặc định) · board_esp32p4_43lcd.h · board_esp32s3_28lcd.h (MỌI chân/knob)
├── core/                 nvs_store · system_info · wifi_mgr (+captive_dns) · dev_console · task_profile.h
├── network/              engineer_api (token/URL) · ota_client (/ota/check + rollback) · result_upload (POST + hàng đợi offline) · dashboard (web :80)
├── ui/                   display (chung: backlight/ngủ/queue) · display_hw_dsi | display_hw_spi (panel theo board) ·
│                         ui_theme.h (2 thang 4.3"/2.8") · ui_reader (13 màn) · ui_strings · ui_wifi_setup · ui_logo · fonts/
├── input/                touch (ST7123 reg16 | FT6236 reg8 → lv_indev) · button (BOOT + ĐO)
├── sensor/               sensor_bus (I2C riêng) · tca9548 · tcs34725 (AGC + DN40 lux) · slot_led
├── app/                  measure (task đo/calib) · calib_store (NVS: calib, ngưỡng, ngôn ngữ, LED)
└── web/dashboard.html    nhúng vào ảnh (EMBED_TXTFILES)
components/esp_lcd_st7102/   driver panel vendor (chỉ vào build P4)
partitions/partitions.rapid4p.csv   ota_0/ota_1 3 MB, assets 1 MB, results 2 MB (16 MB, chung 2 board)
sdkconfig.defaults + sdkconfig.defaults.<board>   knob chung + knob theo board
dependencies.lock.<target>   lock component manager riêng mỗi target
scripts/                  build.bat [board] [COM] · flash.bat [board] COM · readlog.py (UART0/CH343) · jtaglog.py (USB-JTAG) · uicmd.py · dash_mock.py
docs/HARDWARE-PINOUT.md   pinout P4C5 từ schematic thật (§1–14) + board ES3N28P 2.8" (§15)
```

## Xem màn từ PC (không camera)

```
python scripts/lcdtool.py COM20            # http://127.0.0.1:8791/ — ảnh màn thật, 3 nút ảo, chọn màn, log
python scripts/lcdtool.py COM20 --shot x.png
python scripts/lcdtool.py COM20 --gallery out/
```
Firmware lệnh console `screen` dump framebuffer (RLE + base64) qua USB-JTAG (S3) / UART0 (P4); tool giải mã thành PNG.

## Build / nạp

```bat
scripts\build.bat                    :: p4_43lcd → build_p4_43lcd\rapid4p.bin, BUILD_EXIT=0/1
scripts\build.bat s3_28lcd           :: S3 2.8"  → build_s3_28lcd\rapid4p-s3.bin
scripts\build.bat s3_28lcd COM5      :: build + nạp
scripts\build.bat COM48              :: (tương thích cũ) p4_43lcd + nạp
python scripts\readlog.py auto 60 boot.log     :: auto = CH343 của bo P4 (UART0)
python scripts\jtaglog.py COM20 40 boot.log    :: bo S3 cắm USB native (USB-Serial/JTAG)
```
Mỗi board một build dir + `sdkconfig` riêng trong đó. Chi tiết, luật, bẫy: `CLAUDE.md`. Khảo sát và quyết
định thiết kế: `../maping new product/MAPPING-Rapid4P.md`; biến thể 2.8": `docs/history/2026-09-19-rapid4p-bien-the-s3-28lcd.md` (gốc repo).

## Hợp đồng với Engineer Server

- `GET /ota/check?device=&ver=&product=<rapid4p|rapid4p-s3>&hw=<P4C5-43|S3-28>` → tải `url` khi `ver` khác bản đang chạy
  (khoá + hw = `BOARD_PRODUCT_KEY`/`BOARD_HW_VERSION` của board; ảnh `.bin` mang `project_name` = khoá).
- `POST /rapid4p/results` JSON (xem `main/network/result_upload.h`): `id_device`, `sick`,
  `sample`, `slot_value[N]`, `slot_result[N]`, `slot_positive[N]`, `slots: N`, `threshold`, `time` giờ VN…
- Token Bearer: nhập qua portal SoftAP → NVS; không có trong firmware.
