# 2026-08-02 — Soát UI tab Setting: nhãn không gắn với ô, disabled mờ tới mất chữ, WiFi trùng hàng

Soát bằng ảnh chụp thật (390 / 844×390 / 1280) + đọc DOM qua CDP. Ba lỗi đúng nghĩa, hai
chỉnh hiển thị. Guard mới: `node tools/test_setting_a11y.js`.

## 1. `<label>` không gắn với ô nhập — TOÀN BỘ tab Setting

```js
var lbl = el("label", "f-lbl");   // không có for=
row.appendChild(lbl);
row.appendChild(inp);             // input là ANH EM, không phải con
```

Một `<label>` **không `for=` và không phải tổ tiên** thì **không đặt tên cho gì cả**. Hậu quả:
screen reader đọc "edit, blank", và **bấm vào chữ nhãn không focus vào ô** — mất hẳn vùng bấm
mở rộng, thứ đáng giá nhất trên điện thoại, đúng thiết bị mà luồng QR sinh ra để phục vụ.

Dính mọi panel: `renderFields`, Device ID, SSID, Password, file `.bin`, Calib slot.

**Cách nối**: các panel tự dựng đã sẵn `id` (`idInput`, `wifiSsid`, `wifiPass`) → chỉ cần
`lbl.htmlFor = i.id`. `renderFields` thì nối **một chỗ duy nhất**, sau cả chuỗi if/else:

```js
var ctrls = row.querySelectorAll(".f-in, .f-sel");
if (ctrls.length === 1) {
  ctrls[0].id = "f-" + f.p.replace(/[^a-z0-9]+/gi, "-");
  lbl.htmlFor = ctrls[0].id;
}
```

Mọi nhánh (`num`/`text`/`sel`/`bool`) tự thừa hưởng. Hàng `f.arr`/`f.mat` có **nhiều ô** nên
`ctrls.length !== 1` → **tự rơi ra ngoài đúng ý**, và mỗi ô nhận `aria-label` riêng ghép từ
nhãn + chữ trong `.f-idx`.

## 2. Disabled làm mờ bằng `opacity` — chữ tụt dưới ngưỡng đọc được

`opacity` nhạt **cả chữ lẫn nền phía sau**, nên tỷ lệ sập từ hai phía:

| | bình thường | `opacity: .6` | sau khi sửa |
|---|---|---|---|
| `.set-card-desc` | 4.83 | **2.26** | **4.51** |
| `.set-card-title` | 14.54 | **3.92** | **13.63** |
| `.save-btn` | 5.45 | **2.55** | **6.24** |
| `.wifi-forget` / `.wifi-use` | 4.72 / 4.84 | **1.49** | 8.31 / 5.53 |

Comment trong CSS chỉ đo **giá trị cũ** (`.45 → 1.83`, `.5 → 1.49`) rồi nâng lên `.6` mà
**không đo lại**. Đây chính là luật repo đã chốt hôm 2026-07-23 cho `.noact` — chỗ này sót.

### Sửa lần một sai — và đó là điều kiểm định đối kháng bắt được

`filter: grayscale(1)` bảo toàn luminance nên tương phản **không thể tụt**… nhưng trên card
**trắng chữ đen** thì nó gần như **vô hình**: chỉ ô icon 40px mất màu. `cursor: not-allowed`
**không tồn tại trên cảm ứng**. Tức là tôi cứu được tương phản và **giết mất tín hiệu**.

Sửa đúng: card khoá **rơi xuống màu nền trang + bỏ shadow** — hết nổi khỏi mặt phẳng, đọc ngay
là "không phải nút", mà chữ giữ nguyên tương phản.

```css
.set-card[disabled] { filter: grayscale(1); cursor: not-allowed;
                      background: var(--bg); box-shadow: none; }
.set-card[disabled]:hover { border-color: var(--hairline); box-shadow: none; }
```

Dòng `:hover` là **bắt buộc** — bản cũ trả lại `var(--shadow)`, rê chuột là card sống lại.

## 3. Focus rơi mất khi mở/đóng panel — và cái bẫy khi sửa

`openPanel()` `display:none` cái menu đang chứa nút vừa bấm → focus rơi về `<body>`, người dùng
bàn phím phải Tab lại từ đầu trang. Thêm `setBack.focus()` khi mở, và trả focus về card khi Back.

**Bẫy**: đặt phần trả-focus vào trong `showMenu()` là **sai**, vì hàm đó có **3 caller**:

| Caller | Panel có đang giữ focus không? |
|---|---|
| `#setBack` click | **có** — đây là chỗ duy nhất đúng |
| bottom nav vào lại tab (`script.js:46`) | không — nút nav đang giữ |
| `backToHomeAfterSave()` | không — đang rời đi |

`openCard` **sống sót qua việc rời tab**, nên vào lại tab Setting sẽ giật focus khỏi nút nav vừa
bấm sang một card — rồi `loadConfig().then(renderSetMenu)` chạy `m.innerHTML = ""` **xoá đúng
node đang giữ focus** → về `<body>`. Đo được:

```
nav → Setting (chưa từng mở panel) → nav-item, giữ nguyên       ✓
mở card WiFi                        → setBack   (openCard="wifi")
nav → Home                          → nav-item  (openCard VẪN "wifi")
nav → Setting                       → set-card  ← giật khỏi nút nav
   +1.5 s                           → BODY      ← renderSetMenu xoá node
```

Cùng một thao tác, hai kết quả khác nhau, và nhánh sai kết thúc đúng ở chỗ mà tính năng này sinh
ra để tránh. **Trả focus phải nằm trong listener của `#setBack`**, không phải trong `showMenu()`.

## 4. WiFi: mạng đang nối hiện hai lần

`FBT-Office` nằm ở **cả** "Saved networks" (badge CONNECTED) **và** "Nearby networks". Bấm ở
Nearby thì đổ SSID vào ô, **xoá ô mật khẩu** rồi focus vào đó — tức là mời người dùng gõ lại mật
khẩu của mạng đang chạy tốt.

Lọc SSID đã lưu khỏi Nearby. **Không giấu thông tin**: hàng đó nằm ngay khối trên, kèm nút
Connect dùng mật khẩu đã lưu trong máy.

**Hai điểm bắt buộc**:

- **Lọc lúc RENDER, không lọc `wifiScanNets`** — mảng đó chống lưng cho việc kiểm SSID gõ tay
  trước khi reboot; lọc nó thì gõ đúng tên một mạng đã lưu sẽ bị từ chối là "typo".
- Hai danh sách là **hai fetch độc lập, thứ tự bất kỳ**. `loadSavedWifi()` set `wifiSavedSsids`
  rồi **render lại** Nearby — thiếu bước này thì Forget xong mạng đó không quay lại Nearby cho
  tới lần quét sau.

## 4b. Chọn mạng xong thì thu danh sách quét lại

Sau khi bấm một mạng ở Nearby, danh sách vẫn trải hết — ô SSID và Password bị đẩy xuống dưới 3-8
hàng, trên điện thoại phải cuộn qua chính cái danh sách vừa dùng xong mới nhập được mật khẩu.

Bọc danh sách trong **`<details>`** (`#wifiScan`), mở sẵn; click một hàng → `scan.open = false`.

**Dùng `<details>` chứ không tự dựng toggle**: vùng bấm, phím Enter/Space, và trạng thái
đóng/mở báo cho screen reader đều do trình duyệt lo, không tốn dòng nào. Hàng **không bị xoá**,
chỉ ẩn — bấm lại `<summary>` là mở ra đổi mạng khác.

**Bẫy CSS**: `.f-lbl` đặt `display: block`, mà `<summary>` display block thì **mất tam giác
disclosure** ở Blink/WebKit — tức là mất đúng cái tín hiệu nói rằng khối này gập được. Phải ghi đè
`summary.f-lbl { display: list-item }`.

Đo được: ô SSID nhảy từ **521px → 377px** tính từ đỉnh viewport 390px.

## 5. Hiển thị

- Desktop: ô nhập số kéo hết bề ngang panel (**845px** cho giá trị 2 ký tự) →
  `#screen-setting .f-in[type="number"] { max-width: 14rem }` **trong media query desktop**
  (mobile giữ full width; text/file/SSID cũng giữ, chúng dài).
- Banner "Settings locked" ghi *"settings cannot change during a run"* — **sai**: đang chạy run
  thì nav bị ẩn (`applyRunNav`) nên **không vào được tab này**. Bỏ mệnh đề sai đi thay vì khẳng
  định nguyên nhân mới.

## Guard: `node tools/test_setting_a11y.js`

Cần mock chạy sẵn (`python tools/sse_test_server.py`) — **không tự bật**.

Chạy trên **DOM thật**, hỏi trình duyệt `element.labels`. Regex trên `script.js` sẽ pass ngon
lành trên markup mà trình duyệt từ chối liên kết — đúng loại lỗi guard này sinh ra để bắt.
Danh sách panel lấy từ **chính bảng `CARDS` của trang**, nên card nào khôi phục sau này (Calib,
PID) được phủ ngay.

6 nhóm assertion: mọi control có tên · mở panel focus vào `#setBack` · Back trả focus về card ·
vào lại tab **không** cướp focus khỏi nav · Nearby lọc mà `wifiScanNets` còn đủ · disabled dùng
`filter` chứ không `opacity`.

Negative test 5/5 đỏ đúng check:

| Gieo lỗi | Guard báo |
|---|---|
| bỏ `lbl.htmlFor` ở Device ID | `id: all 1 control(s) have an accessible name (idInput)` |
| bỏ `setBack.focus()` | `opening the panel moves focus into it (BODY)` |
| đưa trả-focus về lại `showMenu()` | `re-entering Setting ... (immediate=set-card settled=<body>)` |
| bỏ lọc Nearby | `nearby list hides networks the saved list already shows (FBT-Office)` |
| trả `.save-btn[disabled]` về `opacity: .6` | `disabled controls drain colour ... (.save-btn opacity=0.6)` |

## Ba báo động giả — đều do chính công cụ kiểm

Ghi lại vì cả ba đều suýt dẫn tới kết luận sai:

1. **Ảnh chụp cho thấy panel không mở** → tưởng regression. Thật ra script bấm card sau **250ms
   cố định**, mà card chỉ tồn tại sau khi `loadConfig()` resolve. Đổi sang **poll**.
2. **`B1 WiFi: 0 control(s)` PASS** — "không có control nào thiếu tên" đúng một cách **vô nghĩa**
   trên panel rỗng. Phải assert `n > 0`.
3. **Bản gieo lỗi đầu tiên không áp được** (anchor sai) mà vẫn in "bug reinstated" rồi PASS.
   Nếu không grep lại file thì đã tin nhầm là guard hoạt động.

Bài học chung: **mọi assertion phải chứng minh được là nó có thể đỏ** — bằng cách gieo lỗi và
xem nó đỏ, chứ không phải bằng việc đọc code của chính nó.

## Cố ý KHÔNG làm

- **Không giấu mạng đã lưu bằng cách xoá khỏi `wifiScanNets`** (xem mục 4).
- **Không đụng `.btn-chip.noact` / `.locked`** — đã đo 4.68:1 hôm 2026-07-23, đang đúng.
- **Không thêm `<h1>`** — cả trang không có thẻ heading nào, nhưng đó là chuyện toàn app,
  không phải riêng tab Setting.
- **Không cap chiều rộng ô `text`/`file`/SSID** — chúng dài thật.
