# 2026-09-22 — Tab Hiệu chuẩn: bố cục lại cho nhân viên không chuyên máy móc

Yêu cầu chủ dự án: *"tối ưu lại khu tab hiệu chuẩn để nhân viên không có chuyên môn máy móc dễ vận hành"*.
Màn cũ đúng nghiệp vụ nhưng viết cho kỹ sư: 5 thẻ ngang hàng, mỗi thẻ mở đầu bằng một đoạn hint dày đặc
biệt ngữ (`R²`, `LOD = 3,3·SD/slope`, `raw = slope·nM + intercept`, `{Green: N}`, "xếp hạng tổ hợp"), nút
ĐỌC cỡ nút phụ nằm lẫn trong một hàng `Wrap` 8 phần tử, và **không chỗ nào nói việc kế tiếp là gì**.

## Đã đổi

**1. Thanh tiến độ + "Việc tiếp theo" (mới, đầu màn).** Bốn việc — Nguyên liệu · Pha · Đo ống · Đóng bộ —
vạch xanh lá = xong, xanh đậm = đang làm. Ngay dưới là một ô bấm được nói **bằng lời thường** việc đang
chờ (`_nextAction`): *"Đo ống: còn 12/40 ống. Nối máy, đặt ống vào khe rồi bấm ĐỌC."* Bấm vào ô là cuộn
thẳng tới thẻ đó (`Scrollable.ensureVisible` + `GlobalKey` mỗi thẻ). Suy từ **dữ liệu lô**, không phải cờ
riêng: lot + khe → bước pha chưa tick → ô số đo trống → `n_sets`.

**2. Mỗi thẻ: một câu hướng dẫn + "Chi tiết kỹ thuật" gập lại.** `_card()` nhận thêm `tech:` và `done:`.
Toàn bộ công thức/ngưỡng/giao thức chuyển vào `ExpansionTile` đóng sẵn — nhân viên không phải đọc, kỹ sư
mở khi cần. `done:` vẽ ✓ xanh cạnh tiêu đề. Tiêu đề đổi sang việc phải làm: *"3. Đo ống"* (không phải
"Số đo thô"), *"4. Chọn bộ ống đạt"* (không phải "Xếp hạng tổ hợp & đóng bộ"), *"5. Bộ ống đã đóng"*.

**3. Thẻ 2 chỉ rõ bước kế tiếp.** Bước chưa tick đầu tiên có nền + viền trái màu chính, chữ đậm, mật độ
thưa hơn — người pha biết đang ở đâu mà không phải đọc hết 10 dòng.

**4. Panel đọc viết lại theo trình tự làm việc** (thay hàng `Wrap` cũ):
- Chưa nối: chip "Chưa nối máy" + khe + cổng + nút **Nối máy**, kèm câu *"Cắm cáp USB vào máy, bấm Nối máy
  rồi chọn cổng trong hộp thoại của trình duyệt."*
- Đã nối: một câu to (17 px) *"Đặt ống **300 nM · số 3** vào **khe 6**, rồi bấm ĐỌC."* — nồng độ/ống và
  khe in đậm màu chính, đọc được khi đứng cách máy.
- Nút **ĐỌC** cao **56 px**, full chiều rộng trên điện thoại (trước là nút thường trong `Wrap`, nhãn dài
  *"ĐỌC khe 6 → 300 nM · ống 3 (Enter)"* vừa là nhãn vừa là hướng dẫn). Enter / nút ĐỎ chuyển xuống dòng
  phụ nhỏ.
- Thanh tiến độ **n/40 ống** ngay dưới nút.

**5. Kết quả xếp hạng nói bằng lời thường.** Gợi ý bộ hiện *"Bộ #1 — 300 nM số 3, 200 nM số 5, …"*
(`_tubesPlain`, thay `300/3 · 200/5`) với dòng phụ *"ĐẠT · độ tuyến tính R² 0,9991 · phát hiện tới 4,2 nM"*.
Bảng "Top N tổ hợp" + thống kê blank gập vào *"Chi tiết kỹ thuật: 45 tổ hợp · 12 PASS"*. Không có bộ nào
đạt thì nói **phải làm gì** ("kiểm lại ống pha/khe đọc và đo lại, hoặc gọi kỹ sư") thay vì "Xem bảng dưới
để biết vì sao (R²/LOD)".

**6. Nhãn ô nhập bỏ biệt ngữ**: "Máy tham chiếu (SN)" → "Số máy (SN)", "Khe đọc (1–10)" → "Đặt ống vào khe
số", "Lưu số đo" → "Lưu số gõ tay" (nút phụ — đường chính là ĐỌC).

**7. Danh sách lô + phụ đề tab.** Mỗi dòng lô hiện **việc đang chờ** ở dòng đầu (*"Cần đo tiếp: còn 12/40
ống"*, *"Đo xong — cần chọn bộ ống đạt"*), số liệu truy vết lùi xuống dòng 12 px. Phụ đề tab bỏ "đo thô →
xếp hạng tổ hợp".

**8. Chạm.** Ô số đo cao ≥ 44 px (`contentPadding` dọc 8 → 13) để đổi ống đang đọc bằng ngón tay; nút ĐỌC
56 px; hàng bước pha dùng `ListTile` mặc định (48 px).

## Kiểm

`flutter analyze lib/` — 0 lỗi/cảnh báo ở hai màn calib (10 info còn lại là của flasher/serial console, có
từ trước). `flutter test test/calib_reader_test.dart` 20/20. Dựng web rồi xem thật trên
`http://127.0.0.1:8080/app/` ở **1280×900** và **375×812**: thanh tiến độ, ô "Việc tiếp theo", ✓ thẻ 1–3,
bảng 40 ô, thẻ 4/5 hiện đúng; mobile không trượt ngang (`scrollWidth == innerWidth == 375`).

## Không đổi (cố ý)

- Luồng dữ liệu, API, quyền ghi, thứ tự 5 thẻ — chỉ đổi cách trình bày.
- Mọi con số kỹ thuật vẫn còn nguyên, chỉ chuyển vào "Chi tiết kỹ thuật" — hồ sơ hiệu chuẩn phải tra được.
- Không thêm wizard nhiều trang: kỹ sư vẫn cần thấy cả lô một màn (sửa số đo cũ, xem lại bước pha).
