# 2026-09-20 — Rapid4P 2.8": tối ưu thao tác cho NÔNG DÂN (găng tay, tay to) trên 3 nút cơ

Chạm: `firmware/rapid4p/` (UI, nút, NVS, bíp, dev console), `system/brand/tokens.json` (`lcd-2.8in.softkey`).
Kế hoạch: `~/.claude/plans/ph-t-tri-n-th-m-phi-n-adaptive-rainbow.md` (đã duyệt). Tiếp nối
`2026-09-19-rapid4p-bien-the-s3-28lcd.md` (biến thể S3 + softkey đầu tiên, commit `0b7eadb`).

## Vì sao

Người dùng cuối là nông dân: đeo găng (chạm điện dung vô dụng), tay to (đè 2 nút, nhấn đôi), nhìn màn 2.8" từ xa ngoài trời,
không đọc nhiều chữ. Khảo sát code: luồng đo 4–10 nhấn, không nhớ lựa chọn, không phản hồi khi nhấn, màn ngủ khi đang đo,
không đếm ngược, kết quả không có tổng kết to, Cài đặt/Cân chỉnh/Xoá vào được bằng 1 nhấn, nhấn đôi ĐỎ ở "Đặt ống" = huỷ đo,
nhấn để đánh thức màn cũng kích hành động, mặc định EN và ZH/TW rơi về EN.

## Quyết định (với người dùng)

| # | Quyết định |
|---|---|
| Q1 | Vỏ máy sẽ gắn **loa nhỏ** → module bíp `audio/beep.c` qua ES8311 + PA có sẵn trên ES3N28P (chỉ S3, `CONFIG_RAPID4P_BEEP`). |
| Q2 | Màn chính: **XANH Bắt đầu · ĐỎ Đo lại (mẫu+ống lần trước, NVS) · TRẮNG GIỮ 1,5 s = Cài đặt**; Cân chỉnh chuyển vào Cài đặt; Kết quả ĐỎ "Xong" → chính. |
| Q3 | Ngôn ngữ mặc định **Tiếng Việt**; ẩn Trung/Đài tới khi có font CJK (`R4P_LANG_SELECTABLE`); NVS lang ≥ ZH → VI. |

## Đã làm (firmware/rapid4p)

- **Nút cơ** (`input/button.c`): nhấn lúc màn ngủ chỉ đánh thức (`wake_only`, đọc `display_is_sleeping()` trước khi đánh thức);
  giữ lặp mỗi 400 ms kèm `R4P_EVT_BTN_REPEAT` (BIT14); `app_main` xử 3 nút bằng `else if` (giữ > tap, XANH > ĐỎ > TRẮNG) → đè
  2 nút chỉ nhận 1. API `ui_reader_on_key_ex(key, hold, repeat)`.
- **Softkey 2.8"** (`ui/ui_reader.c`): bỏ chấm tròn; vạch màu nút 4 px (`UI_SK_BORDER`) + nhãn 18 px `fit_font`; nhãn `~` = cần
  giữ → ô 2 dòng "giữ"/nhãn (`hold_lbl()`); chạm ô `SHORT_CLICKED` = tap, `LONG_PRESSED` = giữ, ngưỡng chạm giữ =
  `BOARD_BTN_HOLD_MS` (`lv_indev_set_long_press_time`); ô nháy 150 ms khi nhấn nút cơ (`sk_flash`); **khoá phím 350 ms sau
  `show()`** (`KEY_LOCK_US`); lặp chỉ có tác dụng ở ngưỡng/danh sách và khi màn chưa đổi (`s.hold_state`).
- **Bảng `apply_key` mới**: Chính XANH Bắt đầu / ĐỎ Đo lại (`redo_last()`, ẩn khi chưa có) / TRẮNG giữ Cài đặt; Đang đo ĐỎ **giữ**
  Dừng; Kết quả ĐỎ Xong → Chính; Cân chỉnh XANH → Cài đặt, TRẮNG **giữ** Xoá; WiFi ĐỎ Đổi mã (`ui_wifi_setup_toggle_card()` mới);
  màn mới **`UI_MEASURE_ERROR`** (Thử lại / Quay lại) thay cho kẹt ở Đang đo khi `MEASURE_PHASE_ERROR`.
- **Đang đo**: `remain_lbl` "còn ~N s" `F_HERO` (deadline ước lượng lại mỗi `SLOT_START` từ thời gian trung bình các bước; bước đầu
  1,6 s), "Đừng mở nắp", `display_note_user_activity()` mỗi khe → màn không ngủ. **Kết quả**: dòng tổng kết `F_HERO` "n/N DƯƠNG
  TÍNH" đỏ / "ÂM TÍNH N/N" xanh, ô khe nhuốm 30 % màu kết quả. Cài đặt 6 mục (thêm Cân chỉnh). Token mới `UI_MEASURE_ROW_H`,
  `UI_BAR_MEAS_W`, `UI_SK_BORDER`.
- **NVS** (`app/calib_store.c`): `last_sick`/`last_sample` (u32, giá trị + 1; 0 = chưa có), `calib_store_set_last()` gọi trong
  `start_measure()`, không ghi lại nếu không đổi; `lang` mặc định VI, ≥ ZH về VI khi `!R4P_HAVE_CJK_FONT` (cờ chuyển lên
  `rapid4p.h`). Chuỗi mới VI/EN/ZH: `STR_REDO_LAST, LAST_RUN, DONE, HOLD, RETRY, TOGGLE_CARD, REMAINING ("còn ~%d s" — font
  không có ≈), POSITIVE_COUNT, ALL_NEGATIVE, DONT_OPEN, MEASURE_ERROR_HINT, SETTINGS_SHORT`.
- **Bíp** (`audio/beep.[ch]`, ~250 dòng, rút từ `vimate_es8311.c`): I2S0 TX 24 kHz (STEREO slot + SLOT_BOTH + bit_shift + MCLK
  ×256), `esp_codec_dev` 1.6.2 (`^1.3.0`, rules esp32s3) open channel=1, PA GPIO1 active-low điều khiển TAY quanh mỗi tiếng
  (20 ms im → sin fade 5 ms → 20 ms im → PA off); task `beep` + queue 6; `beep_key` (2 kHz 40 ms) trong `apply_key`, `beep_done`
  (1,2 + 1,6 kHz) ở DONE/CALIB_DONE, `beep_error` (400 Hz 400 ms) ở ERROR, `BEEP_BOOT` khi init OK. Không thấy 0x18 → WARN, no-op.
  Board S3 thêm lại knob `BOARD_AUDIO_*`, `BOARD_SPK_SAMPLE_RATE`, `BOARD_BEEP_VOLUME 80`; `board.h` mặc định
  `BOARD_AUDIO_USE_ES8311 0`. P4: `beep.h` inline rỗng, component không kéo (`Skipping optional dependency`).
- **Dev console S3 qua USB-JTAG**: `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` (`sdkconfig.defaults.s3_28lcd`; bo không ra UART0)
  → `esp_console_new_repl_usb_serial_jtag`; lệnh `btn green|red|white [hold|rep]` đẩy bit vào event group (test cả đường
  app_main); `scripts/keytest.py COM20 "btn red"` gõ lệnh + in log.

- **Vùng chạm to hơn** (gợi ý người dùng "tăng chỗ cảm ứng thao tác lên to hơn tí"): footer 52 → **56**, ô softkey 44 → **50**,
  nút danh sách 44 → **56** cao (2 hàng × 56 + 10 = 122 ≤ content 144), nút hộp thoại 44 → 52 (modal 164), ± ngưỡng 56×64 → 64×72;
  **mọi nút `lv_obj_set_ext_click_area(UI_EXT_CLICK)`** (2.8": 3 px, 4.3": 8 px) → khe hở giữa các nút cũng nhận chạm. Content
  148 → 144: `UI_MEASURE_ROW_H` 28 → 26, QR màn WiFi 128 → 124 (≥ QR_MIN 96). Token `UI_BTN_LIST_H`, `UI_MODAL_BTN_H`,
  `UI_SK_CELL_H`, `UI_EXT_CLICK` có ở cả hai thang (4.3" giữ giá trị cũ).

## Kết quả kiểm

- Build: `scripts\build.bat s3_28lcd` → `rapid4p-s3.bin` 2,04 MB, `BUILD_EXIT=0`; `scripts\build.bat p4_43lcd` → `rapid4p.bin`
  2,09 MB, `BUILD_EXIT=0`. Không warning mới ở `main/` (chỉ deprecated `lv_obj_add_flag` cũ).
- Nạp S3 COM20 (bản trước bíp): boot `settings: … lang=0 last=-`, `Button init … XANH=GPIO47 DO(RED)=GPIO48 TRANG=GPIO41`,
  `man 0`; console USB-JTAG trả lời `help` (ui/btn/heap).
- **2026-09-21** (sau khi cắm lại USB, bản có bíp + vùng chạm to): boot `r4p.beep: ES8311 ok: I2S0 MCLK4 BCLK5 WS7 DOUT8 PA GPIO1`,
  `dev console USB-JTAG san sang`. Kịch bản `keytest.py` (giả lập bit nút, chưa có bo cảm biến): TRẮNG tap ở Chính = không · giữ →
  Cài đặt (`man 7`) · ĐỎ ×4 + XANH → Cân chỉnh (`man 6`) · TRẮNG tap = không · giữ → hộp thoại · XANH huỷ · XANH → Cài đặt · `btn red rep`
  (lặp sau khi đổi màn) bị bỏ · ĐỎ ×5 + XANH → Chính · `ui prepare` rồi ĐỎ sau 150 ms → `key 1 bo (khoa phim sau doi man)` · ĐỎ → Đang đo
  (`man 4`) → `slot 1 vong 1 loi` → màn lỗi (`man 13`) · XANH Thử lại → `man 3` · ĐỎ Quay lại → `man 0` · giữ ĐỎ khi đang đo → `man 3`.
  **Bug bắt được**: panic `LoadProhibited` trong `lv_obj_is_in_widget_tree` ← `sk_flash_end` (timer nháy ô giữ con trỏ ô đã bị xoá khi
  đổi màn; LVGL 9.6 `lv_obj_is_valid` deref con trỏ) → sửa: `s.sk_flash_timer` huỷ trong `content_clear`/`softkeys_clear`.
- Camera: sau khi người dùng cắm lại, **UGREEN FineCam 4K CM973 enumerate** (DSHOW 2). `scripts/uishot.py` = gõ lệnh console +
  chụp + tự cắt LCD. **Rà 13 màn 2.8" qua ảnh** → sửa: (1) tiêu đề "Bước 1/3 · Chọn Mẫu…", "Kết Quả · Ống: …", "CÀI ĐẶT GIÁ TRỊ · …"
  bị "…" → 2.8" bỏ "Bước n/3", tiêu đề "bệnh · mẫu", `set_title` tự co 14 px; (2) lưới: "Cá Rô Phi"/"Cân chỉnh" co 14 px lẻ tẻ,
  "Chứng Dương: 570" tràn → `mk_btn` giữ 18 px + xuống 2 dòng khi từ dài nhất vừa (`longest_word_w`), danh sách ngưỡng dùng mã
  PC/EHP/EMS/WSSV/TPD; (3) ô khe 57 px: "Thấp Nhất 0" và "✓ Âm tính" tràn → cân chỉnh = số cao (18) trên số thấp (14 mờ) + chú
  giải "ô: Cao Nhất / Thấp Nhất" ở dòng đầu, kết quả chỉ icon ✓/⚠ 18 px; (4) hộp thoại bỏ icon thùng rác để "Xác nhận" 18 px;
  (5) màn Cập nhật: câu "Chưa có WiFi…" 18 px tràn, icon văng khỏi màn → icon trên + câu 14 px wrap; (6) `btn` console đánh thức
  màn (màn ngủ 5 phút làm ảnh đen). Đã xác nhận bằng ảnh: Chính (ô "giữ/Cài đặt" 2 dòng), Cài đặt 6 mục, Cân chỉnh, hộp thoại,
  Đặt ống, Đang đo ("còn ~24 s"), Kết quả ("ÂM TÍNH 5/5"), ngưỡng + sửa ngưỡng, Ngôn ngữ (2 mục), Cập nhật, WiFi (ĐỎ "Đổi mã"
  lật thẻ ① → ②, XANH quay lại → Cài đặt).

- **UX pass 2** (2026-09-21, cùng ngày, theo ảnh): mục đang chọn trong danh sách **tô nền teal + chữ tối** (viền mỏng
  không thấy từ xa); màn chính 2.8": gợi ý 18 px theo màu nút ("Nhấn nút XANH để bắt đầu" / "Lần trước: …"), bỏ dòng
  IP·version (chuyển sang màn Cập nhật "Firmware v… · IP: …"); Đặt ống: "Đặt ống vào 5 khe, đậy nắp rồi nhấn nút ĐỎ";
  ô softkey rỗng không vạch + mờ 50 %; màn Đo lỗi dòng to "Khe n" (tiêu đề đã nói "Lỗi cảm biến"). Đã xem qua camera:
  danh sách mẫu (Tôm Thẻ tô teal), Chính, Đặt ống, Cập nhật, Đo lỗi — bo đã nằm trong VỎ MÁY (ảnh nghiêng 20°).
- **Phương án tool debug LCD localhost**: `firmware/rapid4p/docs/plan/2026-09-21-lcd-debug-tool.md` (đề xuất A: lệnh console
  `screen` dump framebuffer RLE+base64 qua USB-JTAG → `scripts/lcdtool.py` web localhost với ảnh màn, 3 nút ảo, `ui N`, log;
  B giả lập LVGL trên PC để sau).

- **Tool debug LCD localhost — phương án A ĐÃ LÀM** (2026-09-21): firmware lệnh console `screen` (`lv_snapshot_take` RGB565 →
  RLE 3 byte/run → base64 → `SCR … SCR-END crc32`; 320×240 nén còn ~25 KB, < 1 s qua USB-JTAG; `CONFIG_LV_USE_SNAPSHOT=y`; stack
  REPL 4 → 16 KB vì snapshot vẽ trong task console → Double exception). PC: `scripts/lcdtool.py` (pyserial + numpy + opencv, không
  thư viện ngoài): luồng đọc cổng tách khối SCR khỏi log, web `http://127.0.0.1:8791/` (ảnh ×2 tự làm mới, 3 nút ảo tap/hold/rep,
  phím 1/2/3, danh sách `ui N`, ô lệnh, log, Lưu PNG, Chụp bộ màn), `--shot`/`--gallery`/`--cmd`. Đã chạy: Shift+3 trên web →
  `btn white hold` → máy sang Cài đặt, ảnh cập nhật đúng pixel. Bộ 13 màn `docs/history/img/lcd/` + ảnh tổng
  `img/2026-09-21-rapid4p-28-all-screens.png`. Bẫy: decode RGB565 nhân 255 trên uint8 numpy tràn → ảnh đen (tính uint32).
  Một lần ảnh màn Cập nhật trong gallery bị lệch footer (4 ô, chữ nhiễu) ngay sau `ui wifi` (SoftAP đang lên) — chụp lại 3 lần
  không tái hiện; theo dõi.

Ảnh camera (UGREEN, `uishot.py`, sau khi sửa): `img/2026-09-21-rapid4p-28-{chinh,dat-ong,dang-do,ket-qua}.jpg`.

## Bẫy mới

- `serial.Serial(port)` (pyserial mặc định kéo DTR+RTS) lên cổng USB-JTAG → chip vào trạng thái không trả lời (console im,
  `esptool` "No serial data received", cổng vẫn hiện) → phải rút/cắm USB. Luôn tắt `dtr/rts` trước `open()` (jtaglog.py,
  keytest.py). Ghi ở `firmware/rapid4p/CLAUDE.md` §5.
- Comment C chứa `BOARD_AUDIO_*/BOARD_SPK_*` → `*/` đóng comment sớm → lỗi lạ ở dòng sau.
- LVGL 9.6 `lv_obj_is_valid()` deref con trỏ → timer giữ con trỏ widget phải huỷ trước khi xoá widget (xem CLAUDE.md §5).
- Heredoc Bash nuốt `\\` lần nữa (anchor `printf("…\\n")` không khớp; `2.8\\"` trong script) → mọi script patch ghi bằng Write tool.

## Còn mở

1. Rút/cắm lại USB bo S3 → nạp bản có bíp (`scripts\flash.bat s3_28lcd COM20`) → chạy kịch bản `keytest.py`: START TRẮNG tap
   (không vào Cài đặt) / TRẮNG hold (vào) · ĐỎ ở Đặt ống rồi ĐỎ ngay (không Dừng) · ĐỎ hold ở Đang đo (Dừng) · không cảm biến →
   `UI_MEASURE_ERROR` → XANH Thử lại · sau một lần đo màn chính hiện "Lần trước:" + ô ĐỎ "Đo lại" · Cài đặt có Cân chỉnh · giữ
   TRẮNG ở Cân chỉnh → hộp thoại. Log mong đợi: `key … bo (khoa phim sau doi man)` khi nhấn đôi.
2. Gắn loa → nghe `BEEP_BOOT`; không loa/codec → chỉ WARN. Chỉnh `BOARD_BEEP_VOLUME`/`AMPLITUDE` theo tai.
3. Nối 3 nút cơ 47/48/41; kiểm wake-only với `CONFIG_RAPID4P_SCREEN_SLEEP_SEC` 20.
4. Web dashboard `POST /api/control?btn=…` chưa biết 3 nút màu (vẫn measure/back) — thêm khi cần.
