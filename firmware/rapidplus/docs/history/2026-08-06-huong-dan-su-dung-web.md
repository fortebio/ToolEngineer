# 2026-08-06 — Hướng dẫn sử dụng web (file Word + ảnh chụp theo quy trình thật)

Sinh tài liệu hướng dẫn cho **người vận hành** máy RPL: `docs/manual/HDSD-Web-FBT-RAPID.docx`,
22 ảnh chụp giao diện thật đi đúng chuỗi trạng thái máy chạy, kèm thao tác phần cứng.

## Sản phẩm

| File | Vai trò |
| --- | --- |
| `docs/manual/huong-dan-su-dung-web.md` | Nguồn nội dung (tiếng Việt, chuỗi UI giữ tiếng Anh) |
| `docs/manual/img/*.png` | 22 ảnh, sinh lại được |
| `docs/manual/HDSD-Web-FBT-RAPID.docx` | Bản Word để in/phát hành |
| `tools/manual_screenshots.js` | Lái mock đi hết quy trình rồi chụp từng bước |
| `tools/make_manual.py` | Markdown (tập con) → .docx |

Dựng lại: `node tools/manual_screenshots.js` (tự bật/tắt mock, ~5 phút) rồi
`python tools/make_manual.py`.

## Vì sao phải sửa mock trước khi chụp

Ảnh in ra cho khách hàng thì **phải là chữ máy thật in**. Mock đã trôi khỏi `fillStatus()` /
`fillActions()` ở 5 chỗ, và trôi theo hướng khó thấy — không có gì đỏ, chỉ là chữ khác:

1. Màn `idle`: nút trắng để **rỗng**, firmware trả **`"QR / Web"`**. Đây là nút *duy nhất*
   đưa người dùng vào được web, mà ảnh lại cho thấy nó vô tác dụng.
2. Pha `heater` chỉ có một bộ chữ cho **cả hai nhánh**, in `"Lysis heating"` ngay giữa một
   run chỉ khuếch đại. `fillStatus` tách `epreheating80` và `eheating67` — nay mock cũng vậy
   (thêm cờ `_amp_flow`).
3. `amplification` thiếu **bộ đếm vòng** (`"~19 min left - round 65/120"`).
4. `finished`: `"Errors"` thay vì `"Errors Table"`.
5. Nhiệt độ không theo nhánh: màn **"Name the samples"** — lúc **chưa hề gia nhiệt** — hiện
   nắp **105 °C**; khối khuếch đại hiện 63 °C thay vì setpoint thật (`amplifTemp`, mặc định
   **65.8 °C**).

Thêm: khi phát lại run thật bằng `--slots`, kết quả P/N/S nay **suy từ chính đường cong**
(cùng cửa sổ baseline phút 2–6 mà chart dùng) thay vì bảng cứng — bảng cũ gắn **P** cho một
kênh phẳng, tức tài liệu dạy đọc kết quả bằng một ví dụ sai.

## Hai lỗi thật lộ ra nhờ việc này

**1. Hàng nút tràn ngang ở 320px / font 130%** (`data/style.css`). Chỉ lộ sau khi mock trả
đúng `"QR / Web"`: ba nhãn `"Lysis"` / `"Amplification"` / `"QR / Web"` không vừa một hàng,
tràn **+7px**, và theo đúng vết đã ghi trong CLAUDE.md thì Chrome nới layout viewport →
người dùng thấy **"giao diện lệch sang phải"**. `flex: 1` không cứu được: flex item không co
dưới min-content. Sửa: `.btn-row { flex-wrap: wrap }` — cùng khuôn đã dùng cho `.card-head`
và `.wifi-item`. `tools/test_no_hscroll.js` từ đỏ về xanh.

> Guard trước đó **xanh nhờ mock sai**. Một test double trôi khỏi bản gốc không chỉ cho ảnh
> sai — nó **tắt** luôn guard đang canh đúng chỗ đó.

**2. Extension của Edge chèn vào ảnh chụp.** `--user-data-dir` dùng lại giữa các lần chạy
nên nhặt extension đi kèm Edge; một cái chèn popup *"Customize modifications…"* và **đổi màu
chữ trang** ngay trong ảnh. Sửa ở **cả hai** script chụp (`ui_screenshot.js` và
`manual_screenshots.js`): xoá profile trước khi chạy + `--disable-extensions`. Lỗi này âm
thầm — nó làm người soát thiết kế đánh giá một trang mà ứng dụng chưa từng render.

## Sửa CLAUDE.md: EventSource **không** tự hồi sau 503

Mục "Nhiều client cùng xem" ghi *"`EventSource` của browser tự retry nên có người rời là vào
được ngay"*. **Sai.** Đo bằng thực nghiệm (endpoint trả 503 trong 6 s rồi chuyển 200):
browser gửi **đúng một** request, `readyState = 2 (CLOSED)` suốt 18 s sau khi endpoint đã
khoẻ; reload thì vào ngay. Theo HTML spec, status khác 200 làm UA *"fail the connection"* —
auto-reconnect chỉ dành cho đứt giữa chừng/EOF, không cho lỗi HTTP. Người thứ 3 **phải tải
lại trang**; tài liệu người dùng ghi đúng như vậy.

## Hai bẫy khi viết harness

- **Chờ *client* thấy phase, đừng chờ *máy*.** Chip đỏ đổi nghĩa theo trạng thái: ở `idle`
  client viết lại thành `ampname` (cổng đặt tên), chỗ khác gửi `red` trần. Bấm trước khi
  frame SSE đầu về là gửi nhầm lệnh → máy **gia nhiệt thẳng**, bỏ qua bước đặt tên, **không
  lỗi gì cả**. Nay mọi cú bấm đi qua `press(id, fromPhase)` gác trên `curPhase` của trang.
- **Ảnh điện thoại chụp cả trang là không dùng được để in**: 780×2900 px ép vừa trang giấy
  còn **~2 inch** ngang, chữ ~4pt. Ảnh mobile nay mặc định chụp **đúng một khung màn hình**;
  bản desktop của cùng bước mới là ảnh mang chi tiết.

## Bổ sung: tách hai kiểu giao diện, và lỗi iPad lộ ra từ đó

Tài liệu được viết lại **từ chương 3** để phục vụ hai nhóm người dùng: máy tính/iPad và
điện thoại. Đo bằng `tools/probe_layouts.js` trên 12 kích thước thật thì ranh giới **không
phải loại thiết bị mà là kích thước cửa sổ** — `min-width: 820px` **và** `min-height: 600px`:

| Thiết bị | Kích thước | Bố cục |
| --- | --- | --- |
| iPhone (dọc và ngang) | 375–844 × 390–932 | 1 cột |
| **iPad mini dọc** | 744 × 1133 | **1 cột** |
| iPad 10.9 / Pro 11 dọc | 820–834 × 1180–1194 | 2 cột |
| iPad ngang, laptop, desktop | ≥ 1180 | 2 cột |
| **Laptop thu nửa cửa sổ** | 640 × 800 | **1 cột** |

Hai dòng in đậm là lý do **không được chia tài liệu theo tên thiết bị**: người dùng iPad mini
đọc phần "iPad" sẽ thấy mô tả một màn hình không giống máy họ.

**Lỗi thật tìm được:** hàng 4 ô nhiệt độ (`.temp-grid.four`) được gác **cùng breakpoint 820px**
với bố cục 2 cột — nhưng ở 820px cột phải chỉ rộng ~370px, nên mỗi ô còn **53px** bề rộng bên
trong cho một giá trị rộng **78px**. Nhiệt độ **in tràn ra ngoài ô** trên **mọi iPad dựng dọc**,
và `scrollWidth` vẫn sạch nên không guard nào thấy. Đo được: tràn **+26px** ở 820, +16 ở 900,
+3 ở 1000, vừa từ ~1100. Giá trị rộng nhất máy hiện là `"105.3 C"` (nắp gia nhiệt) ≈ 91px.

Sửa: chuyển rule 4-ô-ngang sang `@media (min-width: 1280px)`; từ 820 đến 1279 dùng lưới **2×2**
sẵn có (đúng lưới điện thoại đang dùng). Sau khi sửa, đo với giá trị rộng nhất: **không tràn ở
mọi bề rộng**, dư 48–138px ở dải iPad.

Guard: thêm vào `tools/test_no_hscroll.js` — quét 6 kích thước 2 cột, **tự đặt giá trị rộng
nhất** rồi so bề rộng chữ với lòng ô. Negative test: gieo lại breakpoint 820 → guard đỏ đúng
3 mức (820/834/1024) và vẫn xanh ở 1180+, tức nó đo thật chứ không chép công thức.

## Bổ sung 2: hai con số sai vì tin mock thay vì tin firmware

**Nắp gia nhiệt là 75 °C, không phải 105 °C.** `define.h:373` đặt `HOTLID23_TEMP 75.0` cho cả
hai nắp (Top Left / Top Right); `PIDControl.cpp:233-234` dùng đúng macro đó. Mock lại phát
`top = 105.0` từ đầu, và vì tài liệu được viết **từ ảnh chụp mock**, con số 105 chui vào cả
phần cảnh báo an toàn lẫn bảng phụ lục — tức tài liệu dạy người vận hành một nhiệt độ máy
không bao giờ đặt tới. Nay mock có `HOTLID_SETPOINT = 75.0` kèm ghi chú rằng đây là **macro
biên dịch**, không phải key trong `/config` như hai setpoint kia. Đã chụp lại toàn bộ ảnh.

**Số vòng đo có HAI nguồn khác nhau**, và tài liệu chỉ nêu một: `define.h:201` mặc định
`amplification_time = 120` (**40 phút**), còn `JsonPara/pass_file_initial_Full.json` — file
cấu hình mock nạp và cũng là file provisioning — đặt **130** (**43,3 phút**, đúng bằng trần
validate). Tài liệu nay ghi cả hai và chỉ người dùng tra giá trị thật ở thẻ Profile.

Bài học chung của cả hai: **mock là bản sao, không phải nguồn sự thật.** Mỗi con số in ra cho
người vận hành phải truy được về `src/` hoặc về file cấu hình, không phải về ảnh chụp mock.
Các mốc còn lại đã đối chiếu và khớp: lysis 82 °C / 600 s, khuếch đại 65.8 °C, 20 s mỗi vòng,
opto preheat 900 s.

## Nội dung tài liệu

9 chương: giới thiệu + 3 nút (kèm **giữ nút**) → kết nối (QR/IP/SoftAP, đọc dòng địa chỉ trên
TFT) → màn Home → **quy trình Amplification 5 bước** → **quy trình đầy đủ có Lysis** → đọc
kết quả (P/N/S/E/B, CT, xem lại sau reboot ~8 s) → Setting (WiFi / Profile / Firmware) → xử
lý sự cố → phụ lục thông số.

Cảnh báo đưa lên đầu vì **nhãn web nói nhẹ hơn hậu quả thật**: nút TRẮNG nhãn `"Return"`,
nhưng ở màn đang gia nhiệt/đang đo nó rơi vào `ebuttonrestart` → *"Reboot in 1 second"* →
**mất trắng mẻ đang chạy**, không hỏi lại.
