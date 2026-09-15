# 2026-07-26 — Mobile: ẩn bảng slot ở Home khi chart đang chạy

## Vấn đề

Trên layout mobile, Home lúc chạy run xếp theo thứ tự DOM: status → controls → temps compact →
**bảng slot 10 hàng** → chart. Đo ở 390×844: bảng chiếm `y 433..1036` (~600 px), chart bị đẩy
xuống tận `y 1127`, trang dài `1825 px`. Muốn nhìn đường cong phải cuộn qua trọn một bảng mà
ở giai đoạn này **không còn là form nữa** — nó chỉ còn là legend, mà Highcharts đã tự in legend
`#1`–`#10` ngay dưới chart rồi.

## Sửa

Một rule trong `data/style.css`, đặt cạnh phần "Slot naming card (Home)":

```css
@media (max-width: 819px), (max-height: 599px) {
  #screen-home.active:has(#homeChartCard:not(.hide)) #namingCard { display: none; }
}
```

Ba quyết định trong đó:

1. **Chỉ ẩn khi chart đang lên** (`:has(#homeChartCard:not(.hide))`), **KHÔNG ẩn lúc đặt tên**.
   Ẩn luôn là hỏng chức năng: đặt tên bằng điện thoại vừa quét QR chính là đường vào bình thường
   của một run. Cùng một card `#namingCard` phục vụ **hai** giai đoạn (form đặt tên → legend), rule
   phải phân biệt bằng trạng thái chart chứ không bằng id.
2. **Media query = phần bù CHÍNH XÁC của breakpoint desktop** (`min-width: 820px` **and**
   `min-height: 600px`). Viết `(max-width: 819px), (max-height: 599px)` để **điện thoại xoay ngang**
   (844×390 — rộng nhưng thấp, vẫn là layout mobile) cũng được tính là mobile. Chỉ gác `max-width`
   là bỏ sót đúng ca này (cùng cái bẫy đã ghi ở mục breakpoint trong CLAUDE.md).
3. **Không cần `!important`**: selector có 2 id nên đặc hiệu hơn mọi rule class đang chạm card này.
   (`.hide` thì vẫn cần `!important` như cũ — đó là utility 1 class.)

## Đo thật (mock `--full`, probe CDP)

| Màn | Giai đoạn đặt tên | Giai đoạn chart |
| --- | --- | --- |
| mobile 390×844 | bảng `314..999`, Confirm hiện, Start khoá ✓ | bảng **HIDDEN**, chart card `433..1178`, trang `1825 → 1208 px` |
| landscape 844×390 | bảng `258..926`, 10 hàng, Confirm hiện, Start khoá ✓ | bảng **HIDDEN**, chart card `343..642` |
| desktop 1280×860 | bảng `417..1087` ✓ | bảng **vẫn hiện** `448..1054`, chart `107..852` ✓ |

Desktop không đổi gì — ở đó bảng nằm ở **cột trái** dưới các card status (không đè lên chart), nên
nó vẫn là legend hữu ích.

## Bẫy khi đo

Probe chạy 4 kích thước liên tiếp trên **cùng một mock**: sau mỗi run máy dừng ở `finished` và
`white` chỉ có tác dụng từ đó, nên kích thước kế tiếp dễ đo nhầm khi máy còn kẹt giữa run. Hàng
landscape ở lần chạy đầu ra "naming stage" mà chart lại đang hiện = **trạng thái rác**, không phải
kết quả. Phải ép về `idle` (bấm `white` tới khi `/home` báo `idle`) rồi mới `green` → `waitamp`
mới đo được đúng giai đoạn đặt tên.
