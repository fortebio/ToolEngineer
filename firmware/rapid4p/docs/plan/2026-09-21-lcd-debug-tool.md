# Phương án: tool localhost debug màn hình Rapid4P (không cần camera)

Trạng thái: **A ĐÃ LÀM** (2026-09-21, người dùng chốt "làm phương án A"): firmware `screen` + `scripts/lcdtool.py`
(web `:8791`, `--shot`, `--gallery`), bộ 13 màn `docs/history/img/lcd/`. Việc 4 (HTTP `/api/screen.bmp`) chưa làm. B để sau. Mục tiêu: sửa UI/UX lặp nhanh, xem đúng từng pixel, bấm 3 nút
XANH/ĐỎ/TRẮNG từ PC, không phụ thuộc camera/ánh sáng/góc chụp (camera UGREEN vẫn giữ để kiểm "mắt thật").

## Bài toán

Hiện nay vòng sửa UI = build (~40 s) → nạp (~20 s) → `uishot.py` (console + webcam, xoay/cắt ảnh) → xem ảnh mờ/lệch góc.
Camera phụ thuộc tay người cầm bo (đã lệch 20° khi bo vào vỏ), autofocus, ngủ màn; không đo được px; không xem được khi
bo đang ở xưởng. Cần một tool trên `http://127.0.0.1:<port>/` cho cả người lẫn Claude (Read ảnh PNG).

## Hai hướng

| | **A. Mirror màn thật qua console USB-JTAG** (đề xuất làm trước) | B. Giả lập LVGL trên PC (SDL/Emscripten) |
|---|---|---|
| Ý tưởng | Firmware thêm lệnh console `screen` → chụp framebuffer LVGL (`lv_snapshot`, RGB565 320×240) → nén RLE → in base64 qua USB-JTAG; tool PC giải mã thành PNG, hiện trên trang localhost cùng 3 nút ảo (`btn …`), danh sách `ui N`, log | Biên dịch `ui_reader.c`/`ui_theme.h`/font/LVGL cho PC (Emscripten → chạy trong trình duyệt localhost; hoặc SDL), stub `measure`, `calib_store`, `wifi_mgr`, `display_schedule`… |
| Đúng pixel | **Có** (chính bo vẽ) | Có (cùng LVGL/font) nhưng driver/panel/swap_bytes không tham gia |
| Cần bo | Có (COM20) | Không |
| Bấm nút / luồng thật (đo, lỗi cảm biến, NVS, bíp) | **Có** — đúng máy trạng thái thật | Chỉ khi stub đủ; đo giả |
| Công | ~150 dòng firmware + ~250 dòng Python/HTML | ~1–2 ngày: tách lớp HAL cho UI, stub 8 module, build system thứ hai; giữ đồng bộ về sau |
| Rủi ro | RAM chụp 150 KB (PSRAM có 8 MB, OK); tốc độ USB-JTAG (~200–500 KB/s → 1 khung < 1 s sau RLE) | Lệch với máy thật (rotation, swap byte, font hinting); tốn công bảo trì |
| Dùng cho | Rà UI/UX, tài liệu ảnh màn, test luồng nút, xem bo ở xa (qua RDP) | Thiết kế bố cục mới nhanh, CI chụp màn không cần bo |

**Đề xuất: làm A trước** (đủ cho mục tiêu "sửa UI/UX theo ảnh thật" + debug luồng nút), B để sau nếu muốn CI/không bo.
Hướng A còn tái dùng cho **P4 4.3"** (console UART0 `uicmd.py`, framebuffer 800×480 RGB565 = 750 KB → RLE + base64,
~3–5 s/khung qua UART 921600; hoặc đường HTTP khi có WiFi).

## Thiết kế hướng A

### Firmware (`main/core/dev_console.c` + `main/ui/display.c`)

1. `sdkconfig.defaults`: `CONFIG_LV_USE_SNAPSHOT=y` (LVGL 9.6 `lv_snapshot_take(obj, LV_COLOR_FORMAT_RGB565)`; buffer
   cấp bằng `lv_draw_buf_create` → PSRAM qua `lv_malloc`/`heap_caps` tuỳ port; 320×240×2 = 153,6 KB).
2. Lệnh console **`screen [raw]`**: chạy trong LVGL task (`display_lock()` hoặc `display_schedule` + semaphore chờ xong),
   chụp `lv_screen_active()` (cả overlay hộp thoại vì cùng screen), rồi in:
   ```
   SCR 320 240 RGB565 RLE
   <base64 từng dòng 76 ký tự>
   SCR-END <crc32>
   ```
   RLE: cặp `(count u8, pixel u16)` — UI phẳng nén còn ~10–30 KB. `raw` = không nén (đo tốc độ).
   Không chặn UI quá 100 ms: chụp (memcpy) trong LVGL task, mã hoá + in trong task console.
3. Lệnh có sẵn dùng lại: `btn green|red|white [hold|rep]` (đã đánh thức màn), `ui N`, `heap`.
4. Tuỳ chọn (khi có WiFi): `GET /api/screen.bmp` trên dashboard `:80` — cùng hàm chụp, trả BMP 16-bit; tool PC tự chọn
   đường HTTP nếu bo có IP.

### Tool PC (`scripts/lcdtool.py`, Python trong venv IDF, không thư viện ngoài pyserial + Pillow/OpenCV)

- `python scripts/lcdtool.py COM20 [--port 8791]` → mở `http://127.0.0.1:8791/` (một file HTML nhúng, không CDN).
- Trang: **ảnh màn phóng ×2 (640×480) tự làm mới mỗi 1 s**, 3 nút to XANH/ĐỎ/TRẮNG (click = tap, giữ chuột ≥ 1,5 s =
  hold, nút phụ "rep"), thanh lệnh `ui <tên màn>` (danh sách 14 màn), ô gõ lệnh console tự do, khung log 200 dòng cuối
  (lọc `r4p.`), nút **Lưu PNG** (`out/<màn>_<thời gian>.png`) và **Chụp bộ 14 màn** (tự chạy `ui N` + chụp → thư mục
  `docs/history/img/` để đưa vào tài liệu).
- API nội bộ: `GET /screen.png`, `POST /cmd` (JSON `{cmd}`), `GET /log`. Một luồng nền đọc cổng liên tục (mở KHÔNG
  DTR/RTS — bẫy đã ghi), tách khối `SCR…SCR-END` ra khỏi log.
- Claude dùng: `curl http://127.0.0.1:8791/screen.png -o x.png` rồi Read → không cần camera; hoặc `lcdtool.py --shot`
  chụp một lần rồi thoát (thay `uishot.py`).

### Việc + ước lượng

| # | Việc | Công |
|---|---|---|
| 1 | Kconfig snapshot + `screen` console (chụp, RLE, base64, CRC) — build S3 + P4 | 2 h |
| 2 | `lcdtool.py`: serial thread + parser + HTTP server + trang HTML | 3 h |
| 3 | Chụp bộ 14 màn → so với ảnh camera, cập nhật `docs/history/img/` | 0,5 h |
| 4 | (tuỳ chọn) `GET /api/screen.bmp` dashboard + lcdtool tự chọn HTTP | 1 h |
| 5 | Docs: CLAUDE.md §4 (quy trình mới thay uishot), README | 0,5 h |

## Trạng thái việc (rà 2026-09-21 cuối ngày)

| # | Việc | Trạng thái |
|---|---|---|
| 1 | Kconfig snapshot + `screen` console | ✅ (stack REPL 16 KB) |
| 2 | `lcdtool.py` web + `--shot`/`--gallery` | ✅ + `--cam` đối chiếu máy thật (bổ sung theo yêu cầu) |
| 3 | Bộ màn vào docs | ✅ 13 màn PNG + `_cam.jpg`, 2 ảnh tổng |
| 4 | HTTP `/api/screen.bmp` | ⏸ chưa làm (tuỳ chọn, chỉ cần khi bo có WiFi và không cắm USB; P4 UART 115200 chậm 7–8 s/khung là lý do thứ hai) |
| 6 | Chạy với P4 4.3" (UART0 115200, khung 800×480) | ✅ 2026-09-21: hạn chờ giãn theo `enc`, `--baud`, `grab_ok`, tắt log khi dump |
| 5 | Docs | ✅ CLAUDE.md §1/§4/§5, README |

## Việc UI/UX còn lại sẽ làm bằng tool này (từ ảnh camera 2026-09-21)

- [ ] **Màu xanh lá trên panel**: `green-500 #1BD1A5` qua ILI9341 nhìn gần teal (ảnh đối chiếu) → người dùng xác nhận bằng mắt;
      nếu đúng, thêm token "xanh nút" thuần lục hơn cho LCD (`tokens.json` trước, đo tương phản).
- [ ] Kiểm bezel vỏ máy có che mép dưới softkey không (bo hiện đã tháo khỏi vỏ; ảnh trong vỏ trước đó chưa rõ) → nếu che, nâng
      footer lên `UI_FOOTER_BOTTOM_PAD`.
- [ ] Màn chính: cân nhắc bỏ tiêu đề "Rapid4P" trùng "RAPID READER 5 SLOT"; chip "Mã máy" chỉ hiện khi đã cấu hình.
- [ ] Đang đo: highlight khe đang đọc rõ hơn (nền amber nhạt thay vì chỉ viền).
- [ ] Kết quả: khe dương tính thêm nhãn "+"/"−" dưới icon nếu còn chỗ (không chỉ màu).
- [ ] Ngủ màn: hạ `CONFIG_RAPID4P_SCREEN_SLEEP_SEC` 300 → 120 cho máy chạy pin? (hỏi).
- [x] P4 4.3": rà 13 màn qua `lcdtool.py COM47` (UART0) 2026-09-21 → sửa 6 lỗi, ảnh `docs/history/img/lcd43/`,
      ghi `docs/history/2026-09-21-rapid4p-43-ra-ui-lcdtool.md` (gốc). Tool: hạn chờ giãn theo header, `--baud`, `grab_ok` chụp lại
      khi mất dòng base64; firmware `screen` tắt log lúc in.
- [ ] P4: 7–8 s/khung ở 115200 → nâng `CONFIG_ESP_CONSOLE_UART_BAUDRATE` 921600 (+ `--baud`, `readlog.py`/`uicmd.py`) nếu cần
      tương tác trên trang web; hoặc làm việc 4 (HTTP) khi bo có WiFi.

## Còn chờ phần cứng (từ kế hoạch nông dân 3 nút 2026-09-20)

- [ ] Nối 3 nút cơ 47/48/41 → kiểm wake-only (`CONFIG_RAPID4P_SCREEN_SLEEP_SEC` 20: nhấn lúc màn tắt chỉ sáng màn), giữ 1,5 s,
      lặp 400 ms (console `btn` đi tắt qua event group nên KHÔNG kiểm được wake-only/thời gian giữ).
- [ ] Gắn loa → nghe bíp boot/phím/đo xong/lỗi (log đã `beep: ES8311 ok`).
- [ ] Bo cảm biến → "Lần trước: … / Đo lại" ở màn chính (NVS đã lưu `last=PC`, nhưng ô ĐỎ chỉ hiện khi có cảm biến), đếm ngược
      thật (~25 s), màn không ngủ khi đo, kết quả dương tính thật (tô đỏ).
