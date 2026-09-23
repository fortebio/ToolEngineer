# 2026-09-23 — Nhãn QR để quản lý bộ ống chuẩn ĐẠT

Yêu cầu chủ dự án: *"thêm QR để in quản lý thông tin cho các ống hiệu chuẩn pass"*.

Bộ ống rời tủ lạnh là mất dấu: túi zip chỉ có chữ viết tay, không tra được nó thuộc lô nào, đường
chuẩn bao nhiêu, hết hạn khi nào; máy đọc lệch cũng không truy ngược được đã dùng ống nào. Tab Hiệu
chuẩn nay in được **nhãn có mã QR** cho từng bộ ĐẠT.

## Nội dung mã QR — TEXT rời, không phải URL

```
FBTCAL1|<mã bộ>|<lô>|<hạn>|<slope>|<intercept>|<R²>|<LOD>|<ống>|<ngưỡng>
FBTCAL1|B2609A-S01|B2609A|2026-12-20|12.3456|41.2|0.99871|8.4|300/3,200/2,100/3,0/3|2026-09-v1
```

Vì sao không nhúng URL: kho lạnh không có mạng, và `/calib/sets/{id}` đòi Bearer token nên điện thoại
quét ra link cũng chỉ nhận 401. Payload mang đủ thứ cần để dùng ống offline; mã bộ trong đó gõ thẳng
vào ô lọc mục "Bộ ống" là ra hồ sơ đầy đủ. Tiền tố `FBTCAL1` **phiên bản hoá** — nhãn đã in nằm trên
túi hàng tháng, đổi ý nghĩa cột thì phải lên `FBTCAL2`, không sửa tại chỗ.

`|` trong mã lô (người gõ tay) được đổi thành `/` trước khi ghép — lọt một dấu là máy quét đọc lệch
hết các cột sau nó.

## Chỉ bộ ĐẠT mới in được

`calibPrintableSets()`: `verdict != FAIL` **và** `status ∈ {stored, issued}`. Bộ FAIL / đã huỷ / đã
dùng hết không có nút nhãn, và nút in cả danh sách nói rõ bỏ qua bao nhiêu bộ vì lý do gì. Nhãn đẹp
dán trên túi FAIL là mời người ta dùng nhầm một vật chuẩn không đạt — đây là vật chuẩn cho cả fleet.

Nút **Nhãn QR** KHÔNG gác `canWriteCalib`: in nhãn không đổi gì trong hồ sơ, mà người đi dán túi
trong kho thường chỉ có quyền xem.

## Bản in: HTML tự chứa, in qua iframe

`services/calib_label.dart` dựng **tờ A4** (2 cột × 5 hàng = 10 nhãn/tờ, mỗi nhãn 99×57 mm khi in):
QR 24 mm + mã bộ + ống + slope/R²/LOD + lô/ngưỡng + HSD. QR vẽ bằng **SVG inline**, gộp module tối
liền nhau theo hàng (run-length) cho file khỏi phình; mức sửa lỗi **Q (25%)** vì nhãn đi tủ lạnh, ướt
và xước là chuyện thường. Trang không tải gì từ mạng (test khoá điều này) — máy kho không có internet
vẫn in được.

Không dùng plugin in (`printing`/`pdf`): app chạy cả desktop lẫn web, mọi máy đều có trình duyệt biết
in A4 với hộp thoại quen thuộc. `util/printable.dart` tách hai bản:

- **desktop**: ghi `%TEMP%\fbt_rapid_in\nhan_ong_chuan_<stamp>.html` (kèm BOM UTF-8, thiếu nó bản in
  tiếng Việt có máy ra "Ã´ng chuáº©n") rồi `cmd /c start "" <file>`.
- **web**: nhét vào **iframe ẩn**, trang tự gọi `window.print()` trong `onload` của nó.

⚠️ Bản đầu dùng `window.open` và **hỏng**: pop-up bị chặn thì `dart:html` KHÔNG trả `null` như kiểu
`WindowBase` hứa mà ném `Attempting to use a null window opened in Window.open` — gặp ngay trong khung
xem trước của Claude. Iframe không phải pop-up nên không ai chặn; hỏng nữa thì lùi về tải file xuống
Downloads và snack chỉ người dùng mở file rồi Ctrl+P.

## Thư viện

Thêm **`qr: ^4.0.0`** (thuần Dart, không plugin nền tảng) — bộ mã hoá QR dùng CHUNG cho SVG bản in và
`widgets/qr_view.dart` (CustomPainter) trong ô xem trước, nên cái hiện trên màn đúng bằng cái ra giấy.

## Đã kiểm

- `test/calib_label_test.dart` — **12 test**: lọc bộ được in, thứ tự cột payload, LOD rỗng khi chưa
  tính được, `|` trong mã lô, ô định vị QR, SVG (mm + gộp rect), số nhãn × số bản, tự in + `@page A4`,
  **không có URL ngoài**, mã lô chứa `<script>` không chèn được thẻ.
- **Quét thử bằng máy quét thật**: sinh `qr.svg` từ code, vẽ lên canvas trong trình duyệt rồi giải bằng
  `jsQR` → đọc ra **đúng từng ký tự** payload. Đây là thứ test Dart không chứng minh được (SVG có thể
  lật hàng/cột mà vẫn "đúng" với chính nó).
- Giao diện trên bản web + `mock-server.js` (đã thêm route `/calib/*` giả, có sẵn bộ PASS/FAIL/đã huỷ):
  bộ ĐẠT có nút "Nhãn QR", bộ FAIL không có; hộp thoại xem trước hiện QR + thông tin + nội dung mã;
  "In nhãn QR" của cả danh sách báo đúng "in 1 bộ ĐẠT, bỏ qua 1 bộ"; bấm In thì hộp thoại in của
  trình duyệt bật lên.
- `flutter analyze lib/ test/` không error/warning mới; `flutter test` 261 pass (vẫn 1 đỏ có sẵn ở
  `widget_test.dart`, không liên quan).

⚠️ **Chưa chạy thử đường desktop** (`cmd /c start`): máy dev này chưa dựng được bản Windows. Cần kiểm
khi có máy build desktop.

## Còn có thể làm

- Máy quét cầm tay/điện thoại trả về payload → app tự tra bộ (cần plugin camera, chưa làm).
- In kèm mã vạch 1D cho máy quét cũ chỉ đọc Code-128.
