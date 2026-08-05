# GUI — web dashboard (`data/`)

> **Nguồn sự thật là code**: `data/index.html` (khung tĩnh) · `data/script.js` (mọi thứ render
> động + SSE) · `data/style.css`. File này mô tả **cấu trúc màn hình và cái gì hiện khi nào**.
> Hợp đồng SSE/route đầy đủ nằm ở [CLAUDE.md](../../CLAUDE.md).
>
> Cập nhật **2026-08-04**. Bản trước (2026-07-15) là bản vẽ **thiết kế dự kiến** và đã lệch hẳn
> khỏi máy thật: nó ghi bottom nav là `Home / Process / Setting` (thật là **Result**), gọi các
> khối là "Room Card"/"Room Button" (tên từ template smart-home), và không có chart, không có
> bảng đặt tên, không có tab Setting dạng master-detail — tức là không mô tả 3 trong 5 thứ người
> dùng thao tác nhiều nhất.

---

## 0. Từ vựng: `phase` do firmware phát ra

Client **không tự suy** trạng thái máy. Mỗi giây một frame SSE `home` mang `status.phase`, và
firmware sinh nó từ `type_infor` (`webDashboard.cpp:177-238`). **Đúng 7 giá trị:**

| `phase` | Máy đang | Home hiện gì |
| --- | --- | --- |
| `idle` | màn chính, chờ | bình thường |
| `heater` | sấy (lysis / preheat 67°C / hotlid) | bình thường |
| `waitlysis` | chờ cắm ống lysis | bình thường *(client không key riêng)* |
| `waitname` | vừa bấm Amplification, **chưa sấy** | **đặt tên** |
| `waitamp` | chờ cắm ống amp | **đặt tên**, hoặc **chart** nếu đã Confirm |
| `amplification` | đang đo | **chart** |
| `finished` | xong, kết quả trên máy | **chart** (ở lại) |

**Cập nhật 2026-08-05** — `fillStatus` nay phủ **34/38 state** thay vì 12, nên tập `phase` rộng hơn
bảng trên. Ngoài 7 giá trị chạy-run ở trên còn: `waitphase2` (chờ rút ống lysis) · `setting` ·
`upload` · `ota` · `qr` · `calib` · `review` · `error` · `restart`. Client coi **mọi phase nó không
biết tên** là màn hình thường, nên thêm phase mới là an toàn — nhưng hai nhãn thì không:

- **`"idle"`**: `script.js` viết lại cú bấm **đỏ** thành cổng đặt tên khi `phase === "idle"`. State
  nào nút đỏ mang nghĩa khác mà báo `"idle"` là web bắn nhầm lệnh. `escreenStart` là ca cơ sở.
- **`"finished"`**: nằm trong `chartMode` → phát nhãn này là kéo chart lên Home.

`status.calib` vẫn là chuỗi bước riêng và **vẫn** đi kèm `status.busy` — khoá Setting theo `busy`,
không theo `phase` (CLAUDE.md Setting #3).

Guard: `python tools/test_status_coverage.py`.

---

## 1. Vỏ ứng dụng

```text
<body>
│
├── header.header ..................... dính trên, cao 80px (--header-h; 56px ở màn thấp)
│     ├── .brand-mark > img ........... logo.png ĐẦY ĐỦ (mark + chữ), 56px
│     ├── #deviceName ................. ID máy, từ SSE home.device
│     ├── #companyName ................ ẨN HẲN bằng CSS (display:none) — logo đã có chữ
│     │                                 "FORTE BIOTECH", để cả hai là in tên 2 lần.
│     │                                 Vẫn giữ trong DOM vì script.js còn đổ dữ liệu vào.
│     └── #status ..................... badge Online/Offline = trạng thái **SSE**,
│                                       KHÔNG phải WiFi của máy. Header không có
│                                       chỉ báo sóng/IP nào (net{} chỉ chảy về tab Setting).
│
├── main.screens
│     ├── section#screen-home    .screen.active
│     ├── section#screen-result  .screen
│     └── section#screen-setting .screen
│           chuyển tab = listener click trên .nav-item (script.js:28) gắn/gỡ
│           .active cho .screen + nút nav. `show(id, on)` là hàm KHÁC — nó
│           bật/tắt .hide cho MỘT phần tử, dùng trong setHomeMode.
│
└── nav.bottom-nav ................... BOTTOM BAR Ở MỌI KÍCH THƯỚC (không có sidebar desktop)
      ├── Home
      ├── Result
      └── Setting
```

**Ẩn nav khi chạy run**: `applyRunNav(busy && !calib)` → `body.nonav` → ẩn nav **và** thu hồi
`padding-bottom` dành cho nó (`--nav-h: 0px`, chart cao thêm 62px). **Không ẩn khi calib** — wizard
calib nằm trong tab Setting, ẩn nav là nhốt người dùng ở đó.

Run có thể bắt đầu lúc người dùng đang ở Result/Setting → `applyRunNav` **tự click nav Home trước
khi ẩn** (`script.js:266-272`). Thiếu bước này là bỏ họ lại trên một tab không rời được.

---

## 2. Home (`#screen-home`)

### 2.1 Ba chế độ, hai cờ

`renderHome()` (`script.js:108`) tính đúng hai boolean rồi giao cho `setHomeMode()` — **nơi duy
nhất** ẩn/hiện card của Home:

```js
naming    = (phase == "waitname" || phase == "waitamp") && !confirmed
chartMode = (phase == "waitamp" && confirmed) || phase == "amplification" || phase == "finished"
```

`confirmed` sống xuyên heating (chỉ reset khi về `idle`), nên đặt tên ở `waitname` thì `waitamp`
không hỏi lại.

| Card | Bình thường | Đặt tên | Chart |
| --- | --- | --- | --- |
| `#namingCard` | ẩn | **hiện** (form) | **hiện** (làm legend) |
| `#homeChartCard` | ẩn | ẩn | **hiện** |
| `#tempFullLysis` + `#tempFullAmp` | **hiện** | **hiện** | ẩn |
| `#tempCompact` | ẩn | ẩn | **hiện** |
| `#bRed` | thường | **`.locked`** | thường |

### 2.2 Cây (thứ tự DOM = thứ tự nhìn thấy trên điện thoại)

Hai wrapper `.home-main` / `.home-side` là `display: contents` dưới 820px, nên chúng **không tồn
tại** về mặt bố cục cho tới breakpoint desktop.

```text
#screen-home
│
├── .home-main ....................... (display:contents trên điện thoại)
│   ├── #notify .banner.notify ....... chuông + tiêu đề + mô tả
│   │        chỉ hiện khi SSE notify.show — firmware bật DUY NHẤT ở phase "finished"
│   │        ("Amplification finished" / "Results ready - check the device.")
│   │        dùng class .hidden, KHÔNG phải .hide
│   ├── .banner.state ................ LUÔN hiện, mọi phase. Không có điều kiện ẩn.
│   │     ├── #stateIcon ............. nhiệt kế + viền hổ phách khi phase=="heater",
│   │     │                            còn lại là đồng hồ + viền brand
│   │     ├── #stateTitle ............ status.title  (firmware soạn)
│   │     └── #stateSub .............. status.subtitle (còn bao lâu, làm gì tiếp)
│   ├── #buttonsCard "Controls" ...... LUÔN hiện
│   │     └── .btn-row: #bGreen · #bRed · #bWhite   (thứ tự XANH → ĐỎ → TRẮNG)
│   │           chữ trên chip = SSE actions.{green,red,white} = CHỨC NĂNG Ở TRẠNG THÁI
│   │           HIỆN TẠI, không phải tên màu ("Lysis" / "Amplification" / "QR / Web" /
│   │           "Start" / "Return"...). Rỗng → "-" + .noact (mờ bằng grayscale).
│   │           .on = ring + glow ~1.5s sau khi bấm (KHÔNG đổi nền — làm sáng nền
│   │           từng kéo trắng-trên-xanh xuống 2.28:1).
│   │           .locked trên #bRed khi đang đặt tên (chặn phía web; nút VẬT LÝ vẫn chạy).
│   └── #tempCompact ................. strip 5 số 1 hàng: Lysis · Amp L · Amp R · Top L · Top R
│
├── #namingCard ...................... bảng slot, phục vụ HAI giai đoạn
│   ├── #namingTitle / .naming-hint
│   ├── table.slot-table ............. MỘT cột "Sample", 10 hàng
│   │     └── mỗi hàng: [chấm màu] [#N] [select bệnh] [ô tên mẫu]
│   └── #confirmNamesBtn ............. nhãn đổi theo phase:
│           waitname → "Confirm & start heating"   waitamp → "Confirm & start"
│
└── .home-side ....................... (display:contents trên điện thoại)
    ├── #homeChartCard
    │     ├── .card-head: tiêu đề · "Last update: #lastUpdateHome" · label.chart-vis
    │     │     label bọc CHỮ CỦA CHÍNH NÓ ("All slots") → không có vùng bấm nhập nhằng
    │     └── #homeChart .chart-container   (Highcharts, line, không marker)
    ├── #tempFullLysis ............... 1 ô nhiệt
    └── #tempFullAmp ................. 4 ô: Amp Left · Amp Right · Top Left · Top Right
```

### 2.3 Wireframe

```text
ĐẶT TÊN (waitname / waitamp)              ĐANG CHẠY (amplification / finished)
┌────────────────────────────────┐        ┌────────────────────────────────┐
│ [logo] RAPID          ●Online  │        │ [logo] RAPID          ●Online  │
├────────────────────────────────┤        ├────────────────────────────────┤
│ ⏱ Insert amplification tube    │        │ ⏱ Amplification                │
│   Name slots, then start       │        │   ~12.4 min remaining          │
├────────────────────────────────┤        ├────────────────────────────────┤
│ Controls                       │        │ Controls                       │
│  (Amplification)(Start)(Return)│        │  ( - )( - )(Return)            │
│              ↑ .locked         │        ├────────────────────────────────┤
├────────────────────────────────┤        │ Temperatures                   │
│ Name the samples               │        │ Lysis 24  AmpL 65  AmpR 65 ... │
│  ● #1  [PCV ▾] [tên mẫu    ]   │        ├────────────────────────────────┤
│  ● #2  [EHP ▾] [tên mẫu    ]   │        │ Realtime LAMP    ☑ All slots   │
│  ...                     ×10   │        │      ╱‾‾‾                      │
│           [ ✓ Confirm & start ]│        │    ╱                           │
├────────────────────────────────┤        │ ──╯───────────────             │
│ Lysis            24 °C         │        │                                │
├────────────────────────────────┤        │ (bảng slot ẩn trên điện thoại  │
│ Amplification  65 65 100 100   │        │  khi chart lên — legend đã có   │
├────────────────────────────────┤        │  sẵn dưới chart)               │
│  🏠 Home   📊 Result   ⚙ Setting│        │        (nav ẩn: body.nonav)    │
└────────────────────────────────┘        └────────────────────────────────┘
```

### 2.4 Desktop (≥820px **và** ≥600px cao)

Hai cột, và **thứ tự đọc do CSS `order` quyết định, không phải DOM**:

```text
┌──────────────────────────────┬──────────────────────────────┐
│ .home-main (cột trái)        │ .home-side (cột phải)        │
│  1. .banner.state            │  1. #homeChartCard           │
│  2. #notify                  │  2. #tempFullLysis           │
│  3. #buttonsCard             │  3. #tempFullAmp             │
│  4. #tempCompact             │                              │
├──────────────────────────────┴──────────────────────────────┤
│ #namingCard — span CẢ HAI cột (10 hàng select cần bề rộng)  │
└─────────────────────────────────────────────────────────────┘

Khi chart lên (:has(#homeChartCard:not(.hide))):
  #namingCard rơi xuống CỘT TRÁI (nó là legend, không còn là form)
  .home-side span 2 hàng → chart nối tiếp chiều cao cột trái thay vì đẩy bảng xuống dưới
```

---

## 3. Result (`#screen-result`)

Xem lại **run gần nhất** (máy chỉ giữ 1 run).

**Hai đường nạp bảng:**

- **Vào tab** mà `/slots` báo `ready=false` **và `curPhase !== "amplification"`** → client
  `POST /reviewlast?go=1` nạp lại từ EEPROM. Điều kiện phase là bắt buộc: đang chạy run mới thì
  `/slots` **cũng** trả `ready=false` — cố ý giấu cache của run cũ — nên thiếu nó là dựng lại run
  trước đè lên run đang chạy. `?go=1` cũng bắt buộc (GET trần lọt vào cùng handler do khớp bitwise,
  mà đây là một lượt đọc EEPROM ~8 s).
- **Cạnh `busy` → `!busy`** khi `#screen-result` đang `.active` → gọi lại `loadResultSlots()`
  (`script.js:172-177`). Nhờ vậy run vừa xong là bảng tự đầy, không phải bấm gì. Edge-triggered nên
  không spam `/slots` mỗi giây.

Review **mất ~8 giây**, không phải ~10ms (đọc record EEPROM + chạy lại thuật toán trên cả 10 slot).

```text
#screen-result
├── #resultChartCard (.hide cho tới khi bấm "View chart")
│     ├── .card-head: "Stored run curve" · label.chart-vis "All slots"
│     └── #resultChart .chart-container
└── .card "Results & slots"
      ├── table.slot-table — 4 CỘT, thứ tự là thứ tự DOM
      │     <th class="col-vis">  (trống — cột chấm màu)
      │     <th class="col-res">  Result
      │     <th class="col-ct">   CT
      │     <th class="col-name"> Sample
      ├── .res-legend ............ ĐANG BỊ COMMENT trong index.html
      └── #viewChartBtn .......... "View chart"
```

**Kết luận đứng trước** vì đó là thứ người ta mở tab này để đọc; định danh mẫu đi sau vì đó là thứ
đã biết sẵn. **Không đảo thứ tự ô bằng CSS** — screen reader sẽ đọc kết luận trước khi nói kết luận
đó thuộc mẫu nào.

Khác bảng naming ở Home:

| | Home (naming) | Result |
| --- | --- | --- |
| số slot `#N` | **có** (đang khớp ống thật với tên) | **không** (vị trí hàng đã nói rồi; 27px đó là ranh giới giữa hàng 1 dòng và 2 dòng ở máy 360px) |
| chấm màu | nằm trong ô Sample, cạnh `#N` | **cột riêng dẫn đầu** (`td.vis`) |
| cột CT/Result | không có | có |

Chấm màu **là checkbox thật** (`input.vis-dot`, giữ bàn phím + screen reader), style thành **màu
series của slot đó trên chart**. `aria-label` = "Show #N on the chart".

Badge kết quả — chữ cái là **ký tự đầu của chuỗi outcome trong firmware**
(`Alg/AlgoData.h`, `sensor6035.cpp`):

| | | |
| --- | --- | --- |
| **P** Positive | **N** Negative | **S** Slight Positive *(không phải Suspect)* |
| **E** Error | **B** Break *(không phải Blank)* — đường cong không hợp lệ | |

Hàng có `P`/`S` được `tr.hit` (nền `#fff7f5`, CT đậm) — **sắc nền chỉ dẫn mắt**, nghĩa vẫn nằm ở
chữ cái.

---

## 4. Setting (`#screen-setting`)

Master-detail: lưới card → bấm mở panel form.

```text
#screen-setting
├── #setBusy .banner.notify ......... "Settings locked" — hiện khi status.busy
│        (không nói "during a run": chạy run thì nav bị ẩn hẳn, nên tab này
│         chỉ với tới được lúc bận nếu máy đang CALIB)
├── #setMenu .set-grid .............. dựng từ bảng CARDS trong script.js
├── #setDetail (.hide) .............. panel của card đang mở
│     ├── .panel-head: #setBack (◀) + #setPanelTitle
│     ├── #setForm
│     └── #setMsg (aria-live)
└── #setAbout "Device" ............. ID · Company · Network · IP · [Reload]
```

**3 card đang bật / 8 định nghĩa:**

| Card | `custom` | Nội dung | Route |
| --- | --- | --- | --- |
| **WiFi** | `wifi` | xem 4.1 | `/wifiscan` · `/wifi` · `/wifilist` |
| **Profile Configuration** | — (`fields`) | 5 ô: Lysis temperature/time · Amplification temperature/time · Opto preheat. **Mọi thời gian nhập theo PHÚT** — xem dưới | `GET/POST /config` |
| **Firmware** | `ota` | kiểm tra bản mới trên GitHub · cài · **nạp file `.bin`** (XHR, có progress) | `/ota` · `/otaupload` |

**Đang comment trong `CARDS`** (ẩn **CỐ Ý**, sẽ bật lại): **Device ID** (2026-08-05) · **LED** ·
**Calibration** · **PID / heater** · **Other parameters**. Renderer + route của chúng vẫn sống —
bật lại một card = bỏ comment entry của nó. **Đừng "dọn"** `renderCalib()`, `renderDeviceId()`,
nhánh `openPanel` `custom === "calib"`/`"id"`, hook `applySettingLock` `openCard === "calib"`, hay
route `/calib` + `POST /deviceid`.

Ẩn Device ID **không** làm mất ID khỏi giao diện: nó vẫn hiện ở header và ở thẻ `#setAbout`
("Device → ID"). Chỉ mất đường **sửa** — mà `POST /deviceid` là đường ghi **có busy gate**, còn
Serial/BT (`JsonDataConfig`) thì không, nên giờ đường duy nhất còn lại là đường **không được gác**.

**Lưu là BẤT ĐỒNG BỘ** — đây là hành vi người dùng thấy rõ nhất của tab này. Mọi POST (`/config`,
`/deviceid`, `/wifi`) chỉ **xếp hàng** và trả về một `seq`; `#setMsg` hiện "Saving..." rồi
`settleSave`/`awaitCfg` (`script.js:1434-1446`) chờ frame SSE `home` sau đó báo kết quả **thật**:
"Saved to the device." · "Not saved - the device started a run." · "Could not confirm the save"
(timeout 6 s). Lưu xong thì `backToHomeAfterSave()` (`script.js:1481`) đóng panel và **tự chuyển về
tab Home** sau ~900 ms (2600 ms ở panel WiFi).

**Khoá khi bận**: `applySettingLock(busy, calibStep)` làm mờ toàn bộ card (`grayscale`, **không
dùng `opacity`**) và hiện `#setBusy`. **Cố ý KHÔNG đóng panel đang mở** — đóng là giấu mất thông
báo "Not saved - the device started a run", đúng thứ người dùng cần đọc. Khoá phía client là gợi ý;
server vẫn chặn **409** và SettingTask kiểm lại trước khi áp.

### 4.1 Panel WiFi — hai view

```text
#setForm
├── .wifi-seg [role=radiogroup] ..... <input type=radio> thật, KHÔNG phải 2 nút tự chế
│     ( ● Connect )( ○ Saved )        → có sẵn phím mũi tên, MỘT tab stop, trạng thái
│                                       checked cho screen reader. Mặc định Connect.
│                                       Segment đang chọn = chip trắng NỔI (box-shadow),
│                                       không chỉ bằng màu.
├── .wifi-pane ...................... view Connect. Hai pane KHÔNG có id — chỉ phân biệt
│     │                               bằng class `.wifi-pane` + `.hide`, giữ trong biến
│     │                               cục bộ `panes` của renderWifi.
│     ├── p.f-hint ................... "máy sẽ reboot để nối..."
│     ├── details#wifiScan ........... "Nearby networks", mở sẵn, TỰ GẬP khi chọn một mạng
│     │     ├── summary.f-lbl ........ phải display:list-item (mất tam giác = mất tín hiệu gập)
│     │     ├── button.wifi-rescan ... CHỈ ICON, góc phải trên (position:absolute), 30px
│     │     └── #wifiList ............ mỗi hàng: [SSID] .............. [sóng 3 cung + ổ khoá]
│     │             SSID `flex: 1` bên trái, glyph `flex: none` dồn phải
│     │             dBm nằm ở title/aria-label, không in ra chữ
│     │             lọc SSID đã lưu LÚC RENDER (không lọc mảng wifiScanNets)
│     ├── #wifiSsid + #wifiPass ...... mật khẩu ≤ 54 ký tự (khe EEPROM, không phải 63 của WPA2)
│     └── [Save & reboot]
└── .wifi-pane.hide ................. view Saved (cũng không có id)
      └── #wifiSaved ................. mạng đã lưu (NVS `wifinets`, tối đa 5)
              mỗi hàng: SSID · badge "connected" (đúng hàng máy đang nối) · [Connect] [Forget]
              hàng đang nối KHÔNG có nút Connect
```

`loadSavedWifi()` **vẫn chạy dù view Saved đang ẩn** — nó cũng nuôi `wifiSavedSsids` mà danh sách
Nearby bên Connect lọc theo.

**Trial-then-commit**: Save/Connect ghi mạng vào ô *thử* rồi reboot; `setup()` thử trước, **nối được
mới cam kết**. Sai mật khẩu **không ghi đè mạng cũ** → `/wifilist` trả `trial:failed` → web báo
nhập lại.

---

### 4.2 Profile Configuration — nhập PHÚT, máy vẫn lưu giây/vòng

Form đổi đơn vị, **thiết bị thì không**. `parastructure` là 400 B trên trần 402 B nên không có
chỗ cho một bản sao ở đơn vị khác, và validate của firmware (`handleConfigPost`) viết theo **đơn vị
lưu**. Vì vậy chuyển đổi nằm ở **rìa form**, qua hai móc `f.toUi` / `f.toDev`:

| Ô trên web | Key gửi đi | Máy lưu |
| --- | --- | --- |
| Lysis time (min) | `lysis duration` | **giây** (uint16, ≤ 65535) |
| Amplification time (min) | `amplification time` | **số vòng** (≤ **130**) |
| Opto preheat (min) | `opto preheat time` | **giây** (≤ 3600) |

**Gộp hai ô thành một**: "Amplification rounds" + "Time per round" → **Amplification time (phút)**.
`time per loop` **không còn trong form** nên `collectFields` không gửi nó → máy giữ nguyên giá trị
của mình, và chính giá trị đó được dùng để quy đổi (`perLoopMs()` đọc từ `/config`, mặc định
20 000 ms). Máy đặt vòng khác 20 s vẫn đọc ra đúng số phút.

**Clamp 130 không phải sở thích**: `COUNTER` index vào `sensor67Value[10][130]`, quá là tràn buffer
**giữa run**. `toDev` kẹp lại, firmware cũng từ chối — hai cổng, không phải một. Trần thực tế:
130 × 20 s = **43 phút**.

Guard: `node tools/test_profile_minutes.js`.

---

## 5. Responsive — 5 breakpoint

| Điều kiện | Làm gì |
| --- | --- |
| *(mặc định)* | điện thoại: `--maxw: 480px`, nav dưới, 1 cột |
| `max-width: 819px` **hoặc** `max-height: 599px` | mobile thật sự (dấu **phẩy** chứ không `and` → điện thoại xoay ngang cũng tính là mobile): ẩn `#namingCard` **khi chart đang lên**, **và** nén bảng Result — padding ô, `disease-sel` basis `5rem`, `sample-name` basis/`min-width` `3rem`. Chính phần nén này ép mỗi hàng về **một dòng** để đủ 10 slot trên một màn |
| `min-width: 481px` and `max-width: 819px` | `--maxw: 100%` — thiếu rule này thì tablet dọc render dải 480px với **hai viền trắng** 170px mỗi bên |
| `min-width: 820px` and `min-height: 600px` | desktop: 2 cột Home, `body { max-width: none }` + `.bottom-nav { max-width: none }` — **ghi đè property, KHÔNG đổi token**: `--maxw` ở đây vẫn là 480px, chỉ là không còn ai đọc nó. Nav vẫn ở dưới |
| `min-width: 700px` and `max-height: 599px` | rộng-mà-thấp (điện thoại xoay ngang): `--maxw: 700px`, `--header-h: 56px`. **Phải đặt SAU** rule 481–819px |
| `hover: none` and `pointer: coarse` | mọi input **16px** — iOS Safari zoom cả viewport khi focus input < 16px |
| `prefers-reduced-motion: reduce` | tắt transition |

**Breakpoint desktop phải xét CẢ HAI chiều**: điện thoại xoay ngang rộng 844–915px nhưng chỉ cao
~390px. `min-width` **không** đồng nghĩa "màn hình lớn".

**Chiều cao chart nằm trong MỘT token**, khai báo trên `body` (**không** trên `:root` — `var()`
trong custom property được thay tại phần tử khai báo, từ `:root` nó nướng cứng `--nav-h: 62px` của
root và `body.nonav` không bao giờ với tới):

```css
body { --chart-h: calc(100vh - var(--header-h) - var(--nav-h) - 6rem); }
@supports (height: 100dvh) { body { --chart-h: calc(100dvh - ...); } }
```

---

## 6. Bản đồ nhanh: cái gì nuôi cái gì

| Nguồn | Nuôi |
| --- | --- |
| SSE `home` (1s/lần) | header, `.banner.state`, `#notify`, chip nút, nhiệt độ, khoá Setting, ẩn/hiện nav |
| SSE `new_readings` (mỗi vòng đo) | điểm mới trên chart Home |
| `GET /curve` | vẽ lại **toàn bộ** run — gọi khi mở chart và **mỗi lần SSE reconnect** (thiết bị là nguồn sự thật, client không tự giữ lịch sử) |
| `GET /slots` | bảng Result (`#slotBody`, qua `loadResultSlots`) **và** bảng đặt tên trên Home (`#namingBody`, qua `loadNamingSlots`, dựng **1 lần** mỗi lượt vào naming/chart) — cùng một route nuôi cả hai bảng, kèm tên bệnh + tên mẫu đã lưu |
| `GET /config` | form Profile Configuration |
| `GET /wifiscan`, `/wifilist` | hai danh sách của panel WiFi |
| `GET /ota` | thẻ Firmware |
