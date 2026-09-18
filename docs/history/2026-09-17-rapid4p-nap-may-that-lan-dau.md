# 2026-09-17 — Rapid4P: nạp board thật lần đầu (ESP-IDF 5.5.4, P4 rev v1.3)

Phần: `firmware/rapid4p/` (chỉ một phần, ghi ở gốc vì cây rapid4p chưa có `docs/history/`).

## Kết quả
- Xác nhận cây `firmware/rapid4p` đã là **ESP-IDF native** (không `platformio.ini`, registry
  `build_system: idf`). Build `BUILD_EXIT=0`, nạp COM7 (CH343) thành công, boot log 60 s đạt tiêu
  chuẩn §4 CLAUDE.md trừ phần cảm biến (chưa có bo con → `0/4`).
- Máy dev `Admin` chỉ có toolchain **esp32p4 trên ESP-IDF 5.5.4** (bản 5.5.1 cài kèm chỉ có s3).

## Thay đổi
- `sdkconfig.defaults`: thêm `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` + `CONFIG_ESP32P4_REV_MIN_100=y`.
  Lý do: IDF ≥ 5.5.3 mặc định `REV_MIN=v3.1` → bootloader từ chối nạp cho chip rev v1.3
  (`requires chip revision in range [v3.1 - v3.99]`). Trên 5.5.1 symbol thứ nhất không tồn tại
  (chỉ cảnh báo), symbol thứ hai vẫn áp.
- `scripts/build.bat`, `scripts/flash.bat`: bỏ ghim cứng id ESP-IDF của máy khác; tự đọc
  `idfSelectedId` từ `C:\Espressif\esp_idf.json`, vẫn cho override bằng `R4P_IDF_ID`.
- `dependencies.lock`: chỉ đổi `idf.version` 5.5.1 → 5.5.4, hash component giữ nguyên.
- `CLAUDE.md`, `README.md` của phần: cập nhật trạng thái + 4 bẫy mới (rev chip, log I2C thăm dò,
  NACK = `ESP_ERR_INVALID_STATE`, version mismatch ESP-Hosted host 2.12 > C5 2.7).

## Buổi chiều: xem UI qua webcam, sửa bố cục
- Thêm **dev console** UART0 (`main/core/dev_console.c`, Kconfig `RAPID4P_DEV_CONSOLE`, mặc định y)
  + `scripts/uicmd.py` (gửi lệnh, mở cổng không kéo DTR/RTS). Lệnh `ui <màn>`, `btn do|boot`, `heap`.
  Mục đích: lái UI từ máy dev để chụp màn qua webcam (ffmpeg dshow — browser pane của Claude chặn camera).
- Đã xem đủ 13 màn: xoay 270° đúng, chạm ăn. Sửa `ui_reader.c`:
  1. `mk_btn` tự hạ font khi nhãn rộng hơn nút ("THIẾT LẬP RAPID" bị cắt).
  2. Nhãn trạng thái header 14→18 px, dùng `lv_font_montserrat_18` (font vimate không có `LV_SYMBOL_*` → ô vuông).
  3. Màn chính / danh sách / thiết lập căn giữa dọc (trước để trống 1/4 dưới).
  4. Ô kết quả 140→156 px (chữ "Âm tính" bị cắt đáy); nút −/+ màn ngưỡng font 48 (trước như dấu chấm).
  5. Màn WiFi: chỉ 1 nút Back, `IGNORE_LAYOUT` (trước mỗi lần vào thêm 1 nút, che dòng bước 2).
  6. Màn Update offline: thông báo `STR_NO_WIFI`/`STR_NO_TOKEN` (3 ngôn ngữ), không chạy task OTA.
- `sdkconfig.defaults`: thêm `CONFIG_LV_FONT_MONTSERRAT_18=y`.
- Chuỗi hiển thị không dùng "—" (U+2014 không có trong font vimate).

## Làm lại UI/UX LCD (skill `ui-ux-pro-max` + webcam)
- Tra cứu: `--design-system "medical diagnostic device embedded touchscreen kiosk dark"` → Flat, nền tối tương phản
  cao, accent thương hiệu, nút chạm to; 5 luật UX áp dụng: touch ≥ 44 pt + cách ≥ 8 px, không truyền nghĩa bằng
  màu đơn thuần, tiến độ đa bước, xác nhận hành động phá huỷ, tương phản 4.5:1.
- `ui_reader.c` viết lại lớp trình bày (máy trạng thái/callback giữ nguyên): token màu/font; header 56 · content
  328 căn giữa · **footer 96 thanh hành động** (trái = Quay lại/Huỷ, phải = chính teal, giữa = phá huỷ đỏ);
  `mk_btn` có icon Montserrat + nhãn vimate tự co; `mk_chip` trạng thái (Cảm biến n/4 ✓/⚠, Mã máy, Chưa cân
  chỉnh); tiêu đề "Bước n/3"; kết quả = viền + icon ✓/⚠ + chữ; hộp thoại xác nhận `confirm_show` cho Xoá cân
  chỉnh (BOOT = Huỷ); ngưỡng: −/+ chạm ±10, giữ ±50, không còn số 0 đầu; ĐO khoá khi cảm biến 0/4 kèm lý do;
  Update offline ẩn thanh tiến độ; nhấn nút đổi màu 120 ms.
- Chuỗi mới: `STR_CONFIRM`, `STR_CLEAR_CALIB_ASK`, `STR_STEP`, `STR_SENSORS`, `STR_SENSOR_NONE_HINT` (3 ngôn ngữ).
- Dev console thêm `ui confirm` để chụp hộp thoại. Đã chụp lại 12 màn + hộp thoại qua webcam, đạt.
- Bẫy mới (CLAUDE.md §5): `LV_STATE_DISABLED` bị theme đè; `LONG_DOT` cần cao cố định.

## Web dashboard (học theo Rapid+ webDashboard.cpp)
- Học từ `firmware/rapidplus/docs/architecture/05-web-dashboard.md`: asset nhúng, lazy start khi có mạng,
  3 tab bottom-nav, chip nút = nút vật lý, máy là nguồn sự thật (backfill), dashboard/TLS loại trừ nhau.
- Rapid4P (ESP-IDF): `main/network/dashboard.c` — esp_http_server :80, **poll JSON 1 s thay SSE**,
  `GET /api/state` · `POST /api/control?btn=measure|back` · `POST /api/threshold`; `main/web/dashboard.html`
  nhúng EMBED_TXTFILES (19 KB, không lib ngoài, logo SVG từ `system/brand/tokens.json`, bộ nền sáng).
  `wifi_mgr_start/stop_provisioning` gọi `dashboard_suspend/resume` (portal cùng cổng 80).
- `scripts/dash_mock.py`: mock máy (idle → prepare → measuring 12 bước → result) để test UI trên trình duyệt;
  đã duyệt 3 tab + luồng đo + lưu ngưỡng ở 375 px. Chưa test trên máy thật (chưa có WiFi STA).
- Bẫy web: `display:flex` đè thuộc tính `hidden` → thêm `[hidden]{display:none!important}`.

## Nâng lên 5 khe (kế hoạch `docs/plan/rapid4p-5-slot.md` — P0 mặc định + P1 + P4)
- Mặc định (đổi được, chưa phát hành): giữ khoá `rapid4p`; LED khe 5 = GPIO47, mux kênh 4; 3 vòng; khe 5 = mẫu thường.
- **Số khe = `BOARD_SENSOR_SLOTS`** (board header): `R4P_SLOTS` thành bí danh, `R4P_PRODUCT_NAME` sinh
  "RAPID READER N SLOT"; chuỗi "N khe" 3 ngôn ngữ dùng `%d`; log measure/calib/LED in theo vòng.
- `calib_store.c::load_slot_blob()`: migration blob NVS khi số khe đổi (đọc blob cũ 1..N−1 khe, mở rộng) —
  chạy thật: `cal_min: blob 4 khe -> mo rong 5 khe (giu calib cu)`.
- `RQ_MAX_JSON` 900 → 1100. LCD: ô khe rộng `(760 − (N−1)·16)/N` (5 khe = 139 px), dòng phụ 14 px; tiêu đề
  header 480 → 430 px (chồng trạng thái khi có IP). Dashboard: đọc `sensors.total`, lưới `--n`; mock N=5.
- Registry `optical_slots: 5` + `payload.contract` = `system/contracts/ingest-rapid4p.schema.json` (mới);
  `registry_check` thêm kiểm `BOARD_SENSOR_SLOTS` == `optical_slots` (ĐẠT 0 lỗi); server `validate` kiểm
  `len(slot_*) == slots` (+ `SLOT_ARRAY_FIELDS` trong config, test_logic 23 passed).
- Nạp máy: `slot LED x5 en=28/29/30/45/47`, `0/5 cam bien`; webcam: 5 ô hiện đủ ở màn chuẩn bị/đang đo/kết
  quả/cân chỉnh; dashboard mock 5 khe OK. Máy đã có WiFi (`dashboard http://192.168.0.100/`) nhưng PC dev ở
  mạng 192.168.1.x → dashboard thật chưa test được từ PC.
- Máy dev `Admin`: `python` trên PATH là Python 3.11 của ESP-IDF **không có pip**; tool Python (registry_check,
  pytest) chạy bằng `C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe` (đã cài pyyaml/jsonschema/pytest/httpx).
