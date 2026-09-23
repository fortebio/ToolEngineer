# 2026-09-23 — Rail bung ra không còn che nội dung (nội dung co theo)

Báo của chủ dự án kèm ảnh tab Hiệu chuẩn: *"sửa lỗi khi phóng ra không giữ tab lại và ko autoscale"*.

## Triệu chứng

Rê chuột vào thanh điều hướng → rail bung 64 → 248px và **phủ lên** nội dung. Trên tab Hiệu chuẩn,
184px chênh đó nuốt đúng những thứ đang cần đọc: thanh mục con `Lô pha | Bộ ống | Ngưỡng` chỉ còn
`ộ ống · Ngưỡng`, mã lô mất mấy ký tự đầu (`…2-01`), chip lọc `Đang pha` cụt. Nội dung không co lại
theo rail nên không có cách nào thấy lại ngoài việc nhấc chuột ra.

## Vì sao trước đây làm vậy

`_buildDesktop` inset nội dung **cố định `_slim` (64px), một lần, không bao giờ đổi** rồi cho rail
nằm đè trong `Stack`. Chủ ý là để bung/thu rail KHÔNG relayout nội dung → `fl_chart` (tab Lịch sử,
Giám sát) không phải dựng lại mỗi lần chuột đi ngang. Cái giá thì đúng như ảnh báo về.

## Sửa

`home_shell.dart` — nội dung thành `AnimatedPositioned` chạy **cùng nhịp** với rail:

- hằng dùng chung `_railAnim` (220ms) + `_railCurve` (`easeOutCubic`) cho cả hai, lệch nhịp là thấy
  sọc nền lọt giữa rail và nội dung suốt lúc animate;
- `contentLeft = wide && pushes ? _open : _slim`;
- **`pushes`** = `c.maxWidth - _open >= _minContentWidth` (560), tính trong `LayoutBuilder` đã có sẵn.

Ngưỡng 560 là chỗ đánh đổi: cửa sổ hẹp mà vẫn đẩy thì nội dung bị ép xuống ~500px, bảng số đo
ống × nồng độ vỡ cột — tệ hơn hẳn việc bị che tạm trong lúc rê chuột. Dưới ngưỡng, rail quay về
kiểu phủ lên như cũ.

Đánh đổi chấp nhận: tab có đồ thị relayout trong 220ms lúc bung/thu. Đo trên bản web không thấy
giật; nếu sau này thấy giật ở `fl_chart` thì hướng xử lý là cho rail một nút ghim (bung = trạng
thái, không phải hover) chứ đừng quay lại kiểu phủ lên.

## Đã kiểm

Bản web + `mock-server.js` (vai root, thấy đủ tab), tab Hiệu chuẩn:

- **1280×800** (còn 1032 > 560 → đẩy): rail bung, `Lô pha | Bộ ống | Ngưỡng`, 5 chip lọc, nút
  `+ Lô mới` và cả thẻ lô `B2609A` hiện đủ, không cắt ký tự nào.
- **760×800** (còn 512 < 560 → phủ lên): rail bung đè lên nội dung như bản cũ — đúng nhánh dự phòng.
- `flutter analyze lib/` 10 info có sẵn (không mới); `flutter test` 261 pass, vẫn 1 đỏ có sẵn ở
  `widget_test.dart` (tìm tab `Cloud` đã bỏ từ lâu, không liên quan).

⚠️ Chưa thử trên **Windows desktop** (máy dev này không build được) — bố cục dùng chung một nhánh
code với web nên rủi ro thấp, nhưng nhịp animate trên desktop chưa ai nhìn tận mắt.

---

# Cùng ngày — nút THU/MỞ ở chân rail (chốt cuối)

Đi qua ba bản trong một ngày, ghi lại cả đường đi vì hai bản đầu đã bị bác:

1. **Nút GHIM ở đầu rail** (`_PinButton`) — giữ rail bung cố định. Chủ dự án duyệt lúc đầu.
2. **Enum `_RailMode` ba nấc** (`auto` / `open` / `closed`) + thêm nút thu/mở ở chân rail theo mẫu
   sidebar hubOTA (`04.FBT-OTA`, xem `Sidebar.jsx` › `sidebar__toggle`).
3. **CHỐT: bỏ nút ghim, chỉ còn nút thu/mở ở chân rail.** *"bỏ pin đi tôi cần thu phóng ở dưới"* —
   hai nút cho một việc là rối. Trạng thái rút về **một `bool _collapsed`**.

## Bản chốt làm gì

- `_RailFoot` (chân rail, cạnh dòng `v1.1.0-dev`) là nút DUY NHẤT đổi bề rộng.
- **Mở**: rail đứng nguyên 248px, nội dung co theo.
- **Thu**: 64px, nhưng **rê chuột vào vẫn bung TẠM** để đọc tên mục — hành vi có từ 2026-08-19, cố ý
  giữ lại (bản nháp `closed` chặn cả hover, bỏ luôn cùng nút ghim).
- Icon chỉ hướng SẼ ĐI: `»` khi đang thu (bấm là mở), `«` khi đang mở.
- Nhớ qua `rail_collapsed_v1`, **đọc bù hai khoá nháp cũ** (`rail_mode_v1=='open'`,
  `rail_pinned_v1==true`) để máy đang để rail mở sẵn không bị thu lại sau khi cập nhật.
- Xoá hẳn `_PinButton`, enum `_RailMode`, và hai khoá i18n `nav.pin`/`nav.unpin`.

## Đã kiểm (bản web + `mock-server.js`, 1280×800)

1. Máy chỉ còn khoá nháp `rail_pinned_v1=true` → rail lên **mở sẵn** (di cư đúng), không còn nút ghim
   ở đầu rail.
2. Bấm `«` → `collapsed=true`, rail thu, nội dung trả về full width.
3. Tải lại trang → vẫn thu (nhớ đúng).
4. Đang thu, rê chuột vào rail → **bung tạm**, nút chân đổi sang `»`.
5. Bấm `»` → `collapsed=false`; rời chuột hẳn sang nội dung, rail **đứng nguyên** 248px.
6. `flutter analyze lib/` 10 info có sẵn; `flutter test` 261 pass, vẫn 1 đỏ có sẵn ở `widget_test.dart`.

⚠️ Vẫn chưa thử trên **Windows desktop** (máy dev này không build được bản desktop).

## Bổ sung cùng ngày — TẠM ĐÓNG "rê chuột thì bung"

Yêu cầu tiếp: *"tạm đóng chức năng rê chuột"*. Bề rộng rail giờ CHỈ đổi bằng nút ở chân rail; đưa
chuột ngang qua không còn làm bố cục nhảy.

Làm bằng **cờ const** theo idiom sẵn có của file (`kShowFolderTab`), không xoá code:

```dart
const bool kRailHoverExpand = false;   // bật lại = true
```

- `_isWide` viết dạng `if (kRailHoverExpand) return ...` cho dễ đọc. (Tôi từng ghi ở đây rằng dạng
  `kRailHoverExpand && (...)` sẽ bị analyzer kêu `dead_code` — **đo lại thì KHÔNG**: cả hai dạng đều
  `No issues found`. Cứ chọn dạng nào đọc rõ hơn.)
- `MouseRegion.onEnter/onExit` **không `setState`** khi cờ đóng — chuột đi ngang qua rail mà dựng lại
  cả cây widget là phí, nhất là tab có `fl_chart`.
- **Tooltip tên mục trong `_RailItem` giờ là đường DUY NHẤT** đọc tên mục lúc rail thu (trước đây còn
  đường "rê vào cho nó bung"). Đã sửa 2 comment cũ nói ngược lại.

Kiểm trên bản web: đặt `rail_collapsed_v1=true` rồi tải lại → rê chuột vào GIỮA rail và đứng yên 1,2 s
→ icon sáng hover, tooltip hiện, **rail không bung** ✓. Bấm `»` ở chân → mở ra, rời chuột hẳn vẫn
đứng nguyên ✓. `flutter test` 261 pass (vẫn 1 đỏ có sẵn ở `widget_test.dart`).

## Mẹo lái bản web (bổ sung cho gotcha đã có)

`computer{left_click}` của browser tool **không tới canvas Flutter** trong phiên này (bấm 2 lần không
đổi gì). Cách chạy được: dispatch `PointerEvent` vào `flutter-view` — `pointermove` → chờ ~180ms →
`pointerdown(buttons:1)` → `pointerup`. Muốn thử **hover** (rail bung) thì chỉ cần chuỗi
`pointermove` đi dần vào mép trái rồi dừng lại; trạng thái hover giữ nguyên qua lệnh chụp màn hình.
