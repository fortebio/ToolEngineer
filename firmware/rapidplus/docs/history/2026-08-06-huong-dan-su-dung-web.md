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

## Bổ sung 3: bản trực quan (Artifact) cho người vận hành

Bản Word là tài liệu tra cứu đầy đủ; thêm một bản **trực quan, một trang cuộn** cho người
đứng ở máy: `docs/manual/visual-guide.html` (nguồn) → `tools/build_visual_guide.py` →
`visual-guide.build.html` (**0,53 MB**, tự chứa).

- **Kế thừa hệ màu của chính sản phẩm** (`data/style.css`): cùng brand token, cùng màu nút
  xanh/đỏ/trắng, cùng màu badge P/N/S/E/B. Tài liệu vẽ màu khác là dạy một cái máy không tồn tại.
- **Bố cục = vòng lặp thao tác**: mỗi bước là một băng có rãnh "MÁY HIỆN" (chuỗi tiếng Anh
  nguyên văn) và "BẠN BẤM" (nút đúng màu thật), ảnh màn hình đặt cạnh.
- **Hero là dữ liệu thật**: 10 đường cong của một mẻ chạy thật, trừ nền đúng cửa sổ phút 2–6
  mà chart dùng, hai kênh dương tính vẽ đậm.
- Ảnh nhúng **WebP q90** thành data URI (CSP của Artifact chặn mọi host ngoài).

**Bản PDF A4** (`docs/manual/HDSD-truc-quan-FBT-RAPID.pdf`, 10 trang) dựng bằng
`node tools/print_guide_pdf.js` (Edge headless → `Page.printToPDF`, `preferCSSPageSize`,
`printBackground`, ép theme sáng). Khối `@media print` **dựng lại bố cục hai cột** thay vì
thừa hưởng: trang A4 chỉ rộng ~718 px CSS, dưới ngưỡng 820 px, nên nếu để mặc thì bản in rơi
về layout điện thoại và bỏ trống nửa trang cạnh mỗi ảnh. Một bước cao hơn nửa trang nên không
bao giờ có hai bước chung một tờ — vì vậy ảnh in **to hơn** bản web (19rem), cùng số trang mà
đỡ trống và chữ trên ảnh còn đọc được. `break-inside: avoid` giữ mỗi bước, mỗi thẻ, mỗi bảng
nguyên vẹn trong một tờ.

**Bốn lỗi phải sửa trong lúc làm, đều chỉ lộ ra khi render thật:**

1. **Hiệu ứng scroll-reveal làm mất trắng phần giữa trang.** `IntersectionObserver` chỉ kích
   khi phần tử lọt khung nhìn; kéo thanh cuộn nhanh hoặc nhảy giữa trang là các băng bước
   nằm nguyên ở `opacity: 0`. Ảnh render đầu tiên cho thấy **toàn bộ 5 bước trống trơn**.
   Đã **bỏ hẳn** — một quy trình vận hành không được phép render rỗng.
2. **Media query lọt vào selector list** (`:root[data-theme] .x, @media ... { }`) — CSS không
   hợp lệ, âm thầm huỷ rule.
3. **Va chạm class `.two`.** Nó vừa là lưới hai cột (`display: grid`) vừa là biến thể nhãn
   `.tag.two`, nên **mọi nhãn "Hai cột" biến thành grid container** và giãn hết bề rộng ô —
   chỉ nhìn ra khi soi trang PDF. Đổi lớp bố cục thành `.pair`. Đúng loại lỗi mà một tên lớp
   chung chung sinh ra: `.two` không nói nó là *cái gì có hai*.
4. **Kiểm thử mobile sai suốt nhiều vòng.** Nguồn artifact là **fragment** (không `<head>`),
   nên mở thẳng bằng `file://` thì không có thẻ viewport → Edge giả lập mobile ở **layout
   viewport 980px**, chữ bị `.hero{overflow:hidden}` cắt mà `scrollWidth` vẫn báo 0. Harness
   nay **bọc fragment đúng khung publish** trước khi đo. Bài học: kiểm bản dựng trong đúng
   khung nó sẽ chạy, đừng kiểm cái fragment.

## Bổ sung 4: nhánh Lysis, thuật ngữ, và nhận diện công ty

**Mock chưa hề mô phỏng nhánh Lysis** — nó gộp thẳng `heater → waitamp`, bỏ mất **ba màn hình
mà máy dừng lại chờ người**: `ewaitLysisTube` (đặt ống vào), `eheatLysis` (đếm ngược 10 phút),
`ewaitphase2` (lấy ống nóng ra). Vì tài liệu được viết từ ảnh chụp mock, bản trực quan ban đầu
chỉ có nhánh Amplification. Nay `run_state()` mô hình đủ ba state, `press()` có hai điểm dừng
chờ người, kèm chuỗi hiển thị lấy nguyên văn `fillStatus` và `busy = True` (firmware
`isBusy()` là **deny-by-default**, mọi state ngoài allowlist đều bận).

Hai bẫy khi chụp nhánh này:

- **`eheatLysis` báo `phase = "heater"`**, trùng với màn preheat trước nó — `fillStatus` cố ý
  như vậy, chỉ khác title. Harness chờ theo **title** (`waitTitle`) cho các màn đó; chờ theo
  phase thì timeout mãi mãi.
- **Nắp gia nhiệt phải ở nhiệt độ phòng suốt nhánh Lysis.** Mock để 40 °C, mâu thuẫn với chính
  chú thích của tài liệu. Firmware chỉ arm nắp ở `setPreheat67()` (nhánh khuếch đại);
  `setpid1startpreHeat80()` chỉ chạy heater1.

**Thuật ngữ** (theo yêu cầu): *giếng* → **kênh**, *mẻ* → **lần chạy**. Không thay máy móc — 63
chỗ dùng "mẻ" phần lớn đứng cạnh động từ "chạy", thay thẳng sẽ ra "lần chạy đang chạy". Danh
sách quy tắc đi từ cụm dài nhất tới ngắn nhất, rồi quét lại tìm cụm lặp.

**Nhận diện công ty** lấy từ `fortebio.tech`: tên pháp lý *Forte Biotech Pte. Ltd.*, slogan
*"Diagnostic. Wherever. Whenever."*, dòng định vị (LAMP tới tận ao nuôi, không cần lab/chuỗi
lạnh/kỹ thuật viên) và thông tin liên hệ — đưa vào hero và chân trang. **Bảng màu không lấy từ
web**: repo đã có token trích từ chính file logo và đã được kiểm tương phản.

> **Còn vênh, chưa giải quyết:** website ghi RAPIDPlus chạy **2 mẫu/lần**, firmware máy này có
> **10 kênh quang** (`OPTOCHANNELS 10`) và mã máy `RPL…`. Tài liệu bám firmware. Cần xác nhận
> RPL thuộc dòng nào trước khi phát hành ra ngoài.

## Bổ sung 5: cắt ảnh theo vùng, nền trắng, bỏ chương giao diện

**Khoảng trống là do ảnh, không phải do bố cục.** Một ảnh chụp nguyên màn điện thoại là
780×1688 đứng cạnh đoạn chữ cao ~330px — hai phần ba khung hình là viền máy và thẻ rỗng, và
phần rỗng đó chính là nửa dưới trống trơn của mỗi bước. Nay `manual_screenshots.js` có
`clipRect()`: chụp **đúng hợp của các thẻ mà bước đó nói tới** (`Page.captureScreenshot` với
`clip`), nên hình ngắn, rộng, và **mỗi bước một hình dạng khác nhau** — hết luôn cảm giác lặp.

Đo được: PDF **20 → 16 trang**, ảnh bước 09 từ 102 KB xuống 64 KB mà chữ trên ảnh **to hơn**
vì không còn phải thu nhỏ cả màn hình.

Ba điều rút ra khi chỉnh phân trang PDF (đều đo, không đoán):

- Nới ảnh in **14rem → 18rem** làm trang lấp từ **56% → 72%**. Vì một bước cao hơn nửa trang
  nên **không bao giờ có hai bước chung một tờ**; đã vậy thì ảnh nhỏ chỉ đổi lấy giấy trắng.
- Biến thể `.step.stack` (bước 04 dàn ngang) **tốn thêm một tờ khi in** — text full-width cộng
  một hàng hai hình cao hơn chính nội dung đó xếp hai cột. Nay `.stack` chỉ còn là hiệu ứng
  của bản màn hình; bản in giữ hai cột.
- `fs.rmSync` file tạm sau khi in **không được phép làm hỏng lượt chạy**: Edge còn giữ file,
  PDF thì đã ghi xong.

**Nền trắng**: `--paper` từ `#eef6f7` → `#ffffff`, thẻ chuyển sang `#f6fafb` để vẫn đọc ra là
khối lõm trên nền trắng.

**Bỏ hẳn chương "Hai kiểu giao diện" và mọi nội dung iPad** (theo yêu cầu). Việc bỏ chương làm
đánh số chương lùi lại đúng như cũ, nên tham chiếu `"xem mục 7.1"` trong mục 2.3 — thứ từng
sai khi thêm chương — **tự đúng trở lại**. Ảnh `02-home-idle-desktop` mồ côi theo chương đó
nên được đưa lên mở đầu chương *Màn hình Home*.

## Bổ sung 6: bảng lỗi cảm biến — và một mã không khớp giữa hai màn hình

Tài liệu **bỏ sót hoàn toàn** bảng lỗi (`GET /errors`, nút "Error table" ở thẻ Result, và
màn `escreenErrorResult` mà Home soi theo). Nay có mục riêng ở cả hai bản, kèm ảnh chụp thật:
harness đi thêm hai nhánh — bấm nút "Error table" ở thẻ Result, và bấm ĐỎ ở màn finished để
máy sang `errortable` rồi WHITE quay ra.

**Lỗi thật tìm được khi soi ảnh chụp:** máy in mã bằng `sprintf("%04d")` (`errorCheck.cpp`)
nên hiện **`0102`**, còn client in `String(s.code)` nên hiện **`102`**. `webDashboard.cpp`
chú thích rõ *"Same 4-digit encoding the TFT prints, so an operator can read one screen to the
other"* — tức ý đồ là phải khớp, nhưng client bỏ số 0 đầu. Cùng một mã đọc ra thành hai, đúng
thứ mà việc đọc chéo sinh ra để tránh.

- Sửa: `String(s.code).padStart(4, "0")`. **Không** dùng `("000" + s).slice(-4)` — cách đó
  **cắt cụt** mã dài hơn 4 chữ số.
- **Guard đang ghim chính giá trị sai**: `test_error_table.js` assert `firstCode === "102"`
  trong khi phần chú thích đầu file nói "mã 4 chữ số của máy". Guard đứng canh mà giữ nguyên
  bug. Nay assert `"0102"` kèm lý do. Chạy lại: **toàn bộ xanh** ở 390/320/1280 px.
- Guard này **không tự bật mock** (khác các guard khác) — phải chạy
  `python tools/sse_test_server.py --reboot` trước, nếu không nó đỏ với
  `Cannot read properties of null` vì trang không tải được.

> `data/script.js` đã đổi → cần nạp lại firmware (`pio run -e esp32dev -t upload`) thì máy
> thật mới hiện mã 4 chữ số.

## Bổ sung 7: chương Setting, và thanh nav in đè giữa ảnh chụp

Bản trực quan **thiếu hẳn thẻ Setting** (bản Word đã có ở mục 7). Nay có chương "Cấu hình máy"
với 6 ảnh mới (menu ở hai cỡ màn, WiFi Connect, WiFi Saved, Profile, Firmware) và ba điều mà
người vận hành phải biết trước khi chạm vào: đổi WiFi **bắt buộc khởi động lại máy** (máy chỉ
đổi mạng lúc boot), **gõ sai mật khẩu không mất mạng cũ** (trial-then-commit), và **máy không
kiểm tra được tệp `.bin` có bị cắt cụt hay không** — đường brick thật duy nhất.

**Thanh nav in đè ngang giữa hình.** Ảnh toàn trang chụp bằng `captureBeyondViewport: true`:
Chrome render cả trang, nhưng `.bottom-nav` là `position: fixed` nên nó **neo theo viewport
860px**, không theo trang. Trang nào cao hơn thì thanh nav bị vẽ **cắt ngang giữa hình** — nó
đang nằm đúng trên slot #4–#5 của bảng kết quả và bảng lỗi.

Hai lần vấp khi sửa, cả hai đều là "sửa xong trông vẫn sai":

1. Đặt `position: static` cho nó chảy về đáy thật — hết đè, **nhưng thanh nav ra nửa chiều
   rộng và lệch trái**. `position: fixed` cũng chính là thứ cho nó chiều rộng đầy; bỏ đi thì nó
   rơi về `max-width` của một breakpoint hẹp hơn.
2. Bù `width: 100%` vẫn lệch, vì thanh nav căn giữa bằng `left: 50%` **và**
   `transform: translateX(-50%)`. `left` hết tác dụng khi static, **`transform` thì không** —
   nó vẫn kéo thanh sang trái nửa bề rộng. Phải xoá cả `transform` lẫn `left`.

Kèm theo: `document.body.paddingBottom = 0` (khoảng chừa cho thanh nav thành dải trắng thừa
dưới đáy khi thanh nav đã chảy vào luồng), và **khôi phục lại hết** sau khi chụp — cùng một
tab chụp tiếp mấy chục ảnh nữa.

## Ba lần thất bại im lặng của chính harness

Đáng ghi lại vì cả ba đều **không đỏ**, chỉ cho ra kết quả sai trông như thật:

- **Không có timeout cho lệnh CDP** → một lệnh không bao giờ trả lời làm harness treo ~1,9 giờ
  mà **không in gì thêm**. Tôi đọc dòng log cuối rồi báo "đang chạy" — sai. Nay mỗi lệnh có
  deadline 120 s và `ws.onclose` báo khi trình duyệt biến mất.
- **Chuỗi lệnh nối bằng `;`** → bước chụp hỏng nhưng hai bước dựng Word vẫn chạy, ra file đầy
  đủ **dựng từ ảnh cũ**, kích thước hợp lý, không có dấu hiệu gì. Nay nối bằng `&&`.
- **Backtick trong chú thích nằm bên trong template literal** → đóng chuỗi sớm,
  `SyntaxError`. Chú thích viết trong chuỗi JS thì không được chứa backtick.

## Nội dung tài liệu

9 chương: giới thiệu + 3 nút (kèm **giữ nút**) → kết nối (QR/IP/SoftAP, đọc dòng địa chỉ trên
TFT) → màn Home → **quy trình Amplification 5 bước** → **quy trình đầy đủ có Lysis** → đọc
kết quả (P/N/S/E/B, CT, xem lại sau reboot ~8 s) → Setting (WiFi / Profile / Firmware) → xử
lý sự cố → phụ lục thông số.

Cảnh báo đưa lên đầu vì **nhãn web nói nhẹ hơn hậu quả thật**: nút TRẮNG nhãn `"Return"`,
nhưng ở màn đang gia nhiệt/đang đo nó rơi vào `ebuttonrestart` → *"Reboot in 1 second"* →
**mất trắng mẻ đang chạy**, không hỏi lại.
