# 2026-09-21 — Rapid4P 4.3" (P4): rà lại 13 màn bằng `lcdtool.py` qua UART0, sửa 6 lỗi bố cục

Chạm: `firmware/rapid4p/` (UI chung hai thang, dev console, `scripts/lcdtool.py`). Tiếp nối
`docs/plan/2026-09-21-lcd-debug-tool.md` (tool debug LCD, việc còn lại "P4 4.3": chưa rà lại sau các đổi chung").

## Bối cảnh

Từ 2026-09-19 mọi sửa UI làm trên bo S3 2.8" (softkey, `mk_btn` 2 dòng, `set_title` co font…) và chỉ kiểm bằng camera trên
2.8". Bo P4 4.3" (COM47, CH343) còn chạy firmware cũ tiếng Anh từ 2026-09-17, chưa ai xem lại thang 4.3" sau các đổi chung.
Hôm nay: build `p4_43lcd` mới nhất → nạp → `lcdtool.py COM47` chụp framebuffer 800×480 từng màn (VI + EN) → sửa theo ảnh →
nạp lại → chụp lại. Không cần camera; camera UGREEN (DSHOW 2) chỉ dùng đối chiếu màn WiFi một lần (khớp).

## Tool: `lcdtool.py` chạy được với P4 qua UART0 115200

- Khung 800×480 RLE ~53 KB (màn thường) / 69 KB (màn WiFi có 2 QR) → base64 → **6,7–8,5 s/khung** ở 115200 (S3 USB-JTAG < 1 s).
  `grab()` cũ hết hạn cứng 6 s → "khong nhan duoc khung". Sửa: hạn chờ chỉ áp cho HEADER; nhận header (`SCR w h RGB565 RLE enc raw`)
  thì hạn giãn theo `enc × 4/3 ÷ (baud/10) × 1,3 + 2 s`. Thêm `--baud` (mặc định 115200), khung ảnh web không ép tỉ lệ 4:3 nữa
  (max-width 800 = 1× cho 4.3", 2,5× cho 2.8").
- **Ảnh lệch dải ngang** (`docs/history/img/lcd43/_loi-mat-dong-base64_09_wifi.png`): từ y≈96 xuống, mọi pixel dồn sang trái 50 px
  (lần 2: ~385 px), huy hiệu ① bị cắt làm hai nửa ở hai vị trí x. Không phải bố cục: camera đối chiếu cho thấy màn thật đúng.
  Nguyên nhân: **mất một dòng base64 giữa chừng** (RLE giải mã tuần tự nên mọi pixel sau dồn lên; tool có ghi
  `[lcdtool] RLE thieu N px` nhưng `--gallery` không in). Chỉ gặp ở màn WiFi ngay sau `ui wifi` (SoftAP/DNS/wifi đang log);
  6/6 lần chụp khi màn đã yên đều đủ 1208 dòng. Sửa hai phía: firmware `screen` tắt log (`esp_log_level_set("*", NONE)`)
  trong lúc in khối, khôi phục `CONFIG_LOG_DEFAULT_LEVEL` sau `SCR-END`; tool `grab_ok()` chụp lại tối đa 3 lần khi
  `frame_short > 0` (in ra console), `--gallery` chờ gấp đôi ở màn `wifi`. Sau sửa, gallery VI trọn bộ sạch.
- Đổi ngôn ngữ máy không cần chạm (4.3" không có softkey nhưng `apply_key` vẫn chạy): `ui language` → `btn green` = mục 0 (VI);
  `btn red` rồi `btn green` = EN. `uicmd.py COM47 "ui language" "btn green"`.

## Lỗi bố cục 4.3" thấy qua framebuffer → sửa (`ui_reader.c`, `ui_logo.c`, `ui_strings.[ch]`, `ui_theme.h`)

| # | Màn | Thấy | Sửa |
|---|---|---|---|
| 1 | Chính | Logo 120 px cắt chữ "BIOTEC\|" — object logo rộng theo tỉ lệ mark (134 px) còn nhãn "BIOTECH" font vimate 18 px rộng hơn chữ file gốc | `ui_logo.c`: mark vẽ theo **chiều cao** (`w = h·2000/1780` trong draw cb), object nới tới mép phải chữ (`lv_text_get_size`) |
| 2 | Chính, Cân chỉnh | "RAPID SETTING"/"THIẾT LẬP RAPID", "Clear calibration" co xuống **14 px** lẻ loi trong nút 72 px | luật 2 dòng `F_BODY` + `longest_word_w` của 2.8" (2026-09-21 sáng) bỏ `#if UI_SCALE_SMALL` → áp cả 4.3": "THIẾT LẬP / RAPID", "Xoá cân / chỉnh" 24 px 2 dòng (2×33+4 = 70 ≤ 72) |
| 3 | Kết quả, Đặt ống | Tiêu đề 430 px bị "…": "Result · Tube: Positive · P.Vanna…"; VI "Bước 3/3  ·  Chứng Dương  ·  Tôm Thẻ" cũng sẽ cắt | `set_title` co 24 → 18 ở cả hai thang (`fit_font_from`); 4.3" bỏ "Ống:", dấu `·` một khoảng trắng (như 2.8") |
| 4 | Cân chỉnh | Số ô khe 24 px (`F_BODY` ép cứng) trong ô 139×156 — nửa ô trống, khác màn Kết quả 48 px | 4.3" giữ `F_HERO` 48 px; 2.8" vẫn 18/14 (ô 57 px) |
| 5 | Sửa ngưỡng | −/+ icon Montserrat 24 px lọt thỏm trong nút 130×110; gợi ý "+/- 10 ⟳ +/- 50" khó hiểu | 4.3": dấu vẽ bằng **thanh** (`thr_glyph()`, token `UI_THR_GLYPH_W/T` 40/7 — "-" 48 px của vimate chỉ là gạch nối ngắn, thử rồi bỏ), thanh `clickable=false` để chạm về nút; gợi ý chuỗi `STR_TAP_HOLD_HINT_THR` "Chạm: ±10 · Giữ: ±50" (VI/EN/ZH) |
| 6 | Ngôn ngữ | Hai nút "Tiếng Việt"/"Tiếng Anh" giống hệt, không biết đang chọn gì | viền teal 3 px ở mục = `r4p_str_get_lang()` (viền, không tô — nền teal là con trỏ danh sách 2.8") |

Không đổi số token thang 4.3" (luật 9 CLAUDE.md). Bo S3 2.8" build lại `BUILD_EXIT=0`; hành vi 2.8" chỉ đổi ở #3 (co tiêu đề đã
có sẵn) và #6 (viền mục ngôn ngữ) — chưa chụp lại 2.8" (bo không cắm).

## Ảnh

- Bộ 13 màn VI sau sửa: `firmware/rapid4p/docs/history/img/lcd43/NN_<màn>.png` (800×480 đúng pixel) + EN 3 màn có nhãn dài
  (`00_start_en`, `06_calib_en`, `07_settings_en`); ảnh tổng `…/img/2026-09-21-rapid4p-43-all-screens.png`.
- Đối chiếu camera màn WiFi (framebuffer | máy thật, UGREEN đặt ngược): `…/img/2026-09-21-rapid4p-43-wifi-doi-chieu-camera.jpg`.
- Màn Đang đo (04) không chụp được (cần cảm biến; gallery bỏ qua như 2.8").

## Còn lại (chuyển vào `docs/plan/2026-09-21-lcd-debug-tool.md`)

- UART0 115200 làm mỗi khung 4.3" mất 7–8 s; muốn tương tác trên trang web thì nâng `CONFIG_ESP_CONSOLE_UART_BAUDRATE`
  921600 cho P4 (+ `--baud 921600`, `readlog.py`/`uicmd.py` cũng phải theo) hoặc làm việc 4 (HTTP `/api/screen.bmp`).
- Người cầm máy xác nhận nút −/+ vẽ thanh và viền ngôn ngữ nhìn ổn trên panel thật; màu `green-500` trên ST7102 (2.8" thấy gần teal).
