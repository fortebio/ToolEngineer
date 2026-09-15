# 2026-08-02 — "Tràn chữ bảng Result" + "Setting lệch sang phải" trên máy hẹp (OPPO Reno15 F)

## Hai triệu chứng, MỘT lỗi

Người dùng báo hai thứ tưởng rời nhau. Thực ra cả hai là **trang bị tràn ngang**. Đo ở 320px:

```
BEFORE  overflow=+47  clientWidth=320  innerWidth=367  nav: width=367  header=320
AFTER   overflow=  0  clientWidth=320  innerWidth=320  nav: width=320  header=320
```

Khi document tràn ngang, **Chrome trên mobile nới layout viewport ra bằng bề rộng nội dung** —
`innerWidth` thành 367. Thanh nav dưới (`position: fixed; width: 100%`) giãn theo thành 367 trong
khi header vẫn 320. Nav và nội dung lệch nhau, trang trượt ngang được → người dùng đọc ra là
**"giao diện lệch sang phải"**. Sửa hết tràn là hết cả hai triệu chứng.

## Vì sao chưa từng thấy

Mọi lần soát UI đều chạy ở **390px, font mặc định** — đúng ô sạch duy nhất. Ma trận đo:

| viewport × font | Home | Result | Setting |
|---|---|---|---|
| 320 × 100% | 0 | +25 | +47 |
| 320 × 130% | +6 | +67 | +142 |
| 360 × 100% | 0 | 0 | **+7** |
| 360 × 130% | 0 | +27 | +103 |
| 390 × 100% | 0 | 0 | 0 |
| **412 × 130%** | 0 | 0 | **+50** |
| **820 × 100%** (desktop) | — | **+90** | — |

OPPO/Samsung có mục "cỡ chữ / cỡ hiển thị". Bật lên là hỏng cả trên máy rộng hơn — và **desktop
820px đang tràn 90px ngay ở font mặc định**, chưa ai báo.

## Lỗi 1 — `1fr` trần trong grid

`grid-template-columns: 1fr` là viết tắt của **`minmax(auto, 1fr)`**, và cái hỏng là **minimum
`auto`**, không phải maximum `1fr`:

- Track có min sizing `auto` lấy base size = **minimum contribution lớn nhất** của item. Free
  space âm nên `1fr` không bao giờ được áp → track đứng nguyên ở base size.
- `min-width: auto` trên grid item cũng chỉ giải ra content-based minimum khi item nằm trong track
  có min sizing `auto` — nên **`width: 100%` sẵn có trên `.set-card` là vô dụng**.

Đo được track **350.719px trong khung 288px**. Con số đó = 119.2 (icon 40 + chev 18 + 2 gap +
padding + border) + **231.5** = min-content của `.set-card-desc`, vốn `white-space: nowrap`.

Hai thứ **không** cứu được: `overflow: hidden` không giảm min-content của một box, và
`min-width: 0` trên `.set-card-body` chỉ kẹp *used size*, không kẹp cái contribution mà flexbox
đẩy ngược lên container.

**Hệ quả phụ đáng chú ý**: `text-overflow: ellipsis` ở `.set-card-desc` **chưa bao giờ chạy** (đo
`fits 301/301`) vì lưới luôn cấp đủ chỗ cho chuỗi. Bản vá biến ba dòng khai báo chết đó thành có
tác dụng thật.

**Sửa**: `1fr` → `minmax(0, 1fr)` ở **7 track**. `.temp-grid.one` và `#screen-result.active` vốn
đã đúng — style.css **đã ghi chú chính xác lý do này** cho `#screen-result.active`, chỗ khác sót.

| dòng | rule | ghi chú |
|---|---|---|
| 145 | `.temp-grid.four` | Home tràn +6 ở 320/130% |
| 397 | `.set-grid` | thủ phạm chính |
| 461-463 | `.f-arr` ×3 | tiềm ẩn — card LED/PID/Other đang comment, bật lại là dính |
| 698 | `.temp-grid.four` desktop 4 cột | tiềm ẩn |
| 716 | `.set-grid` desktop | **KHÔNG tiềm ẩn — xem dưới** |

### Trên desktop lỗi này ẩn dưới dạng LỆCH CỘT, không phải tràn

Ở 1280px lưới hai cột không bao giờ tràn nên `scrollWidth` sạch, trong khi `1fr 1fr` âm thầm thổi
track 1 lên min-content của thẻ rộng nhất và ném phần thừa cho track 2. Đo ở font 150%:

```
1fr 1fr              -> 496px vs 388px     (menu lệch hẳn, không có scroll ngang để lộ ra)
minmax(0,1fr) x2     -> 442px vs 442px
```

## Lỗi 2 — `fitNameColumn()` ghim px vào bảng

`fitNameColumn()` viết **inline px** lên header cell:
`th.style.width = Math.min(Math.max(w + 34 + 132, 210), 600) + "px"`.

Thực tế nó **luôn** dính sàn 210: `w` đo chữ bệnh, mà bệnh là `PC`/`EHP`/`EMS`/`WSSV`/`TPD` (~40px),
và slot chưa đặt tên là `<select>` **không có placeholder** nên `inp.value || inp.placeholder || ""`
đo chuỗi rỗng → `w = 0`. Đo được `thInline=210px` ở **mọi** ca mobile.

`210 + 2 × 3.2rem` = 312 trong card 254 → tràn. Ở font 130% hai cột kia thành 67px mỗi cột.
Ở desktop 820px cột Result chỉ rộng 212 → `210 + 2 × 5.5rem` = 344 → tràn 90px.

### Bản vá đầu của tôi bị bác — và bác đúng

Tôi định **kẹp** giá trị theo chỗ còn lại. Hai người kiểm độc lập cùng bác: *xoá hẳn*, vì
`table-layout: fixed` cộng hai cột value cỡ rem **đã** trả cho `col-name` đúng phần còn lại rồi —
**đó chính là cái kẹp, miễn phí, ở mọi bề rộng và mọi cỡ chữ**. Kẹp là tái tạo đúng cái pattern
"JS tính px rồi ghim" đã gây ra lỗi. Đo lại thì họ đúng:

| | hiện tại | sau khi XOÁ |
|---|---|---|
| 320 @100% | table 312 ⊄ 254 **TRÀN** | table 254 ⊆ 254 OK |
| 320 @130% | 344 ⊄ 234 **TRÀN** | 234 ⊆ 234 OK |
| 820 @100% desktop | 344 ⊄ 212 **TRÀN** | 212 ⊆ 212 OK |
| 390 @100% | th 218 | th 222 — **không đổi** |
| 1280 @100% | th 223 | th 231 — **không đổi** |

Xoá `fitNameColumn` + 3 call site + span `#nameMeasure`. **Thuần xoá, không thêm gì.**

## Guard: `node tools/test_no_hscroll.js`

Cần mock chạy sẵn (`--slots tools/slots.txt --reboot` để bảng Result có hàng).

Ma trận **320 / 360 / 390 / 412 px × font 100 / 130 % × 3 màn**, đọc `scrollWidth` trên trang thật
nên bắt được tràn từ **bất kỳ** nguồn nào, không chỉ hai nguồn lần này — nó chính là thứ tìm ra
`temp-box` mà tôi không nhìn ra. Assert riêng việc **nav không giãn quá viewport**, vì đó là nửa
nhìn thấy được của lỗi; một bản vá nửa vời có thể hết tràn mà vẫn để lại triệu chứng.

Thêm ca **1280 @150%** assert **hai cột Setting bằng nhau** — ở đó `scrollWidth` sạch nên chỉ
assertion này bắt được (negative test: `496 vs 388` → đỏ đúng một dòng, check tràn vẫn xanh).

Trên code chưa sửa guard đỏ **9 chỗ**; sau khi sửa xanh hết.

## Cố ý KHÔNG làm

- **Không đụng `.slot-table .sample-cell .sample-name { min-width: 4.5rem }`** — có người đề xuất
  hạ xuống 0, người kiểm đo lại và bác: 4.5rem đang mua ~1 ký tự chữ ở cỡ 16px touch, và padding
  phải 0.45rem của `td` đã hấp thụ phần thừa. Diff bằng 0 nhỏ hơn diff một giá trị.
- **Không đụng `.disease-sel { flex: 1 1 5rem }`** — bác vì lý do tương tự.
- **Không hạ cỡ chữ để chống tràn** — quy tắc 16px cho control cảm ứng (iOS zoom) là bất khả xâm phạm.
- **Không đụng `.bottom-nav`** — nó giãn là *hệ quả*, không phải nguyên nhân; hết tràn là hết giãn.
