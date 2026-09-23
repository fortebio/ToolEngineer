# FBT_RAPID App (Flutter)

App đồng hành của thiết bị **Forte Rapid+ / FBT_RAPID**: xem **lịch sử xét nghiệm** + **đồ thị CT**,
quản lý **nhiều máy qua cloud**, **công cụ kỹ thuật** (log nhiệt UART · đọc serial · nạp firmware ESP),
và **cài đặt** thiết bị.

App **yêu cầu đăng nhập**; **3 vai trò** — **Khách hàng** (`user`) · **Nhân viên** (`admin`) · **Root** —
quyết định thấy/làm được gì (xem §1.6).

Build cho **Windows trước**. Kết nối:

- **Đăng nhập** qua **Apps Script accounts riêng** (`POST`) — xác thực + phân quyền (mã máy được cấp).
- **WiFi/HTTP** — lấy kết quả trực tiếp từ máy (`GET /getdata`).
- **Google Drive** qua **Apps Script `doGet`** — đọc lịch sử nhiều máy (link gắn sẵn trong app).
- **UART/COM** — đọc nhiệt độ thời gian thực từ máy.

Backend lịch sử: [sheet/getData.js](sheet/getData.js) · Backend accounts: [sheet/userAuth.js](sheet/userAuth.js)
· Hướng dẫn kiến trúc/gotchas (dev): [CLAUDE.md](CLAUDE.md).

---

## 1. Tính năng

Sau khi **đăng nhập** (§1.6), giao diện dùng **thanh điều hướng dọc ẩn hẳn** — *đưa chuột tới mép trái*
để hiện (overlay **trượt** vào, có "tay nắm" gợi ý). Tab **nội dung theo vai trò**:

- **Khách hàng** (`user`): **Lịch sử**.
- **Nhân sự** (`admin`/`root`): **+ Chăm sóc KH** (Thông tin máy · Xử lý sự cố — §1.8) **+ Quản lý máy**
  **+ Sản xuất** (trạm ATE — §1.10) **+ Hiệu chuẩn** (ống chuẩn quang — server `docs/plan/calib-ong-chuan.md`)
  **+ Kỹ Thuật** (Log nhiệt · Đọc serial · Nạp code).
- **Root**: **+ Quản lý User** (§1.7).

**Thiết lập** + **Đăng xuất** nằm trong **menu của icon tài khoản** (cuối thanh dọc) — KHÔNG còn là tab.
Tab **Lịch sử** là màn **GỘP** hai nguồn **Cục bộ | Cloud** (nút gạt). *(Hiện "Cục bộ" đang tạm ẩn — chỉ
hiển thị Cloud; bật lại bằng cờ `_showLocal` trong `history_combined_screen.dart`.)*

### 1.1 Lịch sử › Cục bộ (lấy trực tiếp từ máy)

- **Lấy kết quả từ máy**: gọi `GET /getdata` tới IP máy (cùng mạng WiFi), lưu lần chạy thành 1 bản ghi
  trên PC (`shared_preferences`).
- Danh sách bản ghi: thời gian, mã máy, **tóm tắt số slot** Dương / Dương nhẹ / Âm / Lỗi.
- Xoá 1 bản ghi hoặc xoá tất cả.
- Bấm vào bản ghi → màn **chi tiết** (xem §1.4).

### 1.2 Lịch sử › Cloud (nhiều máy từ Google Drive)

- **Danh sách mã máy**: mỗi máy hiện **số lần chạy**, **FW** (version firmware mới nhất), **mốc mới nhất**.
  Danh sách được **cache** (hiện tức thì, làm mới ngầm; server cache 5′ giảm tải Apps Script).
- **Tìm kiếm** theo mã máy · **Sắp xếp**: Mới nhất · Cũ nhất · Mã máy A→Z · Mã máy Z→A.
- Bấm 1 máy → **lịch sử các lần chạy của riêng máy đó**:
  - **Phân trang 10 lần chạy/trang** (nút **Trước / Sau**, có ô đếm trang).
  - Mỗi lần chạy hiện thời gian, **FW version**, tóm tắt slot.
  - Bấm 1 lần chạy → tải **chi tiết kèm đường cong** → màn **đồ thị CT** (§1.4).
  - **Đồng bộ về máy** (⬇): kéo các lần chạy của trang hiện tại (kèm đường cong) vào **lịch sử cục bộ**
    để xem offline; khử trùng theo `fileId`.


### 1.3 Kỹ Thuật (chỉ nhân sự) — Log nhiệt · Đọc serial · Nạp code

Tab **Kỹ Thuật** gộp 3 công cụ qua nút gạt (chỉ nhân sự: root/nhân viên). **Các dropdown cổng chỉ hiện
cổng USB-serial** (CP210x/CH340/FTDI/ESP USB-JTAG…), bỏ cổng native/Bluetooth (`util/serial_ports.dart`).
3 công cụ **dùng chung phần cứng COM** → đóng cổng ở công cụ này trước khi dùng đúng cổng đó ở công cụ kia.

#### Log nhiệt (đọc UART/COM, realtime)

Theo dõi nhiệt độ máy RPL qua cổng USB-COM (@115200). App gửi lệnh `TemperatureOutput` để máy xuất nhiệt.

- **Đọc nhiều cổng COM cùng lúc** (ngầm) — **bấm một cổng để xem đồ thị realtime của cổng đó**; chuyển
  qua lại giữa các cổng (cổng không xem vẫn đọc ngầm). Cổng đang xem có viền màu + icon ▶.
- Nút **Tất cả đồ thị** (⊞ trên thanh tiêu đề) → xem **đồng thời đồ thị của mọi cổng đang hoạt động** (dạng lưới).
- **Đặt tên gợi nhớ cho cổng** (giữ chip COM, hoặc nút ✏ cạnh tiêu đề đồ thị) → hiện "Tên (COM3)", lưu lại.
- **6 kênh nhiệt**: Lysis · Amp1 · Amp2 · Hotlid1 · Hotlid2 · Ambient (từ dòng `TimeRB`/`TimeRT`).
- **Đồ thị** các đường nhiệt; trục **thời gian = phút (số nguyên)**, trục
  **nhiệt độ −10…MAX+10** (nấc chính **5/10** + **nấc phụ** ở giữa); **tự co giãn theo cửa sổ** (realtime & xem lại).
  **Rê/chạm vào điểm** hiện **tooltip** (kênh · phút · °C).
- **Ẩn/hiện từng kênh trên đồ thị**: mỗi kênh là 1 **chip** (kèm **giá trị hiện tại** khi realtime) — **bấm
  để bật/tắt đường** của kênh đó; trục nhiệt + chú thích + ảnh lưu đều **theo kênh đang hiện**. Áp dụng cả
  **realtime** lẫn **xem lại log đã lưu**.
- **Giữ toàn bộ mẫu cả quy trình** (không còn giới hạn 5000) — số mẫu hiển thị là số thật từ đầu đến cuối;
  đồ thị **lấy thưa (~3000 điểm)** để vẽ mượt nhưng **file lưu vẫn đầy đủ độ phân giải**.
- **Bảng UART thô** (nút **UART thô**): xem trực tiếp **mọi dòng** UART máy gửi (live, tự cuộn xuống cuối) —
  giữ lại **toàn bộ** dòng từ đầu đến cuối để đối chiếu/gỡ lỗi.
- **Tự kết nối lại** khi tạm mất kết nối (rút cáp / lỗi UART) → đọc tiếp UART và **vẽ đồ thị liên tục với nhiệt
  kế tiếp** (mốc thời gian theo app nên không gãy khi máy reset). Thẻ hiện trạng thái "đang kết nối lại…".
- **Lưu** (1 nút — gộp log + đồ thị): mỗi lần lưu tạo **1 folder** `<tên>_<thời gian>\` gồm `log.json`
  (+ `log.csv` cho Excel) **+ `uart.txt`** (toàn bộ UART thô) **+ `chart.png`** (ảnh đồ thị lúc lưu).
  Tên folder theo **tên gợi nhớ** của cổng (nếu có), vd `Máy Lysis (COM3)_20260610_143022\`.
- **Xem lại** (menu ⌛): **Log đã lưu** → vẽ lại đồ thị + bảng thống kê (min/max/cuối), và nút **⌨ UART**
  để xem lại toàn bộ UART thô đã lưu; **Đồ thị đã lưu** → gallery `chart.png`, bấm xem to (zoom/pan).
- Lưu tại `<thư mục lưu>\FBT_RAPID_templog\` (mặc định Documents; có nút **Mở thư mục**).
- Nếu mở cổng mà **chưa thấy dữ liệu** (lệnh máy là toggle), app **tự gửi lại lệnh** sau ~3s để lấy nhiệt;
  hoặc bấm **Gửi lệnh** thủ công.

#### Đọc serial (console raw, đa cổng)

Console đọc/ghi thô để kiểm tra/debug — **mở nhiều cổng COM cùng lúc**, mỗi cổng 1 tab riêng (log RX +
ô gửi). Chọn cổng + baud → **Mở cổng**; xem dữ liệu đến dạng **text hoặc HEX** (tự cuộn), **gửi lệnh** kèm
ký tự xuống dòng (None/LF/CR/CRLF). Không parse — raw thuần.

#### Nạp code (flash firmware ESP qua esptool)

Nạp firmware cho **ESP32 / ESP32-S3 / ESP32-C3 / ESP8266** bằng **esptool** (gọi qua `Process`):

- Chọn **cổng** + **loại chip** + **baud nạp**; **Kiểm tra chip** (`flash_id`: chip/MAC/dung lượng flash).
- 3 file `.bin` (**Bootloader / Partition / Firmware**) với **offset chỉnh được** (tự đặt mặc định theo
  chip; để trống file nào thì bỏ qua file đó).
- **Flash mode** (keep/dio/qio/…) + **Flash size** — chỉnh khi *nạp xong boot-loop* (sai mode rất hay gặp).
- **Nạp** (`write_flash`) · **Xóa flash** (erase, có xác nhận) · **Dừng** (kill tiến trình); log esptool
  **stream trực tiếp**.
- **Theo dõi serial sau khi nạp** (mặc định bật): tự mở COM đọc **log boot** để xem **lý do reset/boot**.
- **esptool đi kèm app**: đặt `esptool.exe` cạnh `fbt_dxd_app.exe` (qua installer + `windows/vendor/`); app
  **tự dò** → không cần nhập đường dẫn (không thấy file kèm → gọi `esptool` trên PATH).

### 1.4 Chi tiết kết quả & đồ thị CT (dùng chung cho Lịch sử và Cloud)

- **Bố cục**: **đồ thị CT bên trái**, **kết quả bệnh bên phải** (cửa sổ hẹp tự xếp dọc: đồ thị trên,
  kết quả dưới).
- **Kết quả bệnh**: lưới 10 slot, mỗi slot hiện **phân loại** + **giá trị CT**, được **tô đúng màu đường
  của slot trong đồ thị** (viền trái + chấm màu). **Bấm vào slot để ẩn/hiện đường** của slot đó trong đồ
  thị (gộp luôn chức năng bật/tắt cũ — slot đang ẩn bị mờ + icon 👁). Phân loại: Dương tính · Dương tính
  nhẹ · Âm tính · Lỗi · Không rõ.
- **Đồ thị CT**: 10 đường cong khuếch đại (`fl_chart`), **co giãn theo cửa sổ**, trục X = phút (quy đổi từ
  *Khoảng đọc*); **trục tung bao trọn dữ liệu, đáy/đỉnh làm tròn về bội số của 50** (đáy ≈ min−100, đỉnh ≈
  max+150) + đường mốc y=0; nút **Hiện/Ẩn tất cả**. **Rê/chạm vào điểm** hiện **tooltip** (Slot · phút · giá trị).
- **4 dạng đường cong** (chỉ với run **Cloud** vì cần dữ liệu raw) — chọn bằng nút **Raw · Calib · Baseline · SG**:
  1. **Raw (draw)** — giá trị thô từ máy.
  2. **Calibrate** — `raw ÷ slope`.
  3. **Baseline** — trừ nền, chưa làm mượt.
  4. **Baseline + Savitzky–Golay** — trừ nền rồi làm mượt.

  Pipeline `raw → ÷slope → baseline → SG`, **tham số mặc định firmware** (baseline 3–7′, SG window 4 / order 2).
  Nút **"Xem cả 4 đồ thị"** hiển thị cả 4 cùng lúc. Tab **Lịch sử** (`/getdata`) cũng có đủ 4 đồ thị **nếu
  firmware trả thêm `amplification` (raw) + `slopes`**; nếu chỉ trả `#1..#10` (đã xử lý) thì hiện 1 đường.
- **Lưu kết quả** (cho **mọi tài khoản đã đăng nhập**, kể cả Khách hàng — `SessionStore.canSaveCharts`):
  nút **Lưu** ở **trên cùng** (AppBar) màn kết quả → **checklist chọn loại đồ thị**
  (Raw / Calib / Baseline / SG) → lưu **gom theo mã máy**: `<thư mục lưu>\FBT_RAPID_ketqua\<MãMáy>\<Ngày_Giờ_Firmware>\`,
  gồm các **ảnh PNG** đã chọn (`raw`, `calib`, `baseline`, `baseline_sg`) + **`data.json`** (data raw,
  slopes, version firmware, kết quả/CT từng slot). Mỗi ảnh PNG kèm **bảng "Kết quả bệnh"** bên phải (như
  giao diện), **đồ thị cao bằng bảng** (không chừa khoảng trắng dưới), xuất ở **độ phân giải gấp đôi**.
  Đồ thị lưu theo **slot đang chọn**. (Màn **"Xem cả 4 đồ thị"** vẫn có nút **Lưu** cả 4 cùng lúc.)
- Hiện **Firmware** của lần chạy nếu có.

### 1.5 Thiết lập (trong icon tài khoản — màn DÙNG CHUNG mọi vai trò)

Mở qua **icon tài khoản** (cuối thanh nav) → **Thiết lập**. Một màn dùng chung
(`user_settings_screen.dart`) cho cả nhân sự lẫn khách hàng (đồng bộ giống nhau):

- **Tài khoản**: email · vai trò (Khách hàng / Nhân viên / Root) · **mã máy được cấp** + nút
  **Đổi mật khẩu** / **Đổi email** (gọi `userAuth.js`; đổi email cần cột `email` trong sheet).
- **Nhà cung cấp**: thông tin Fortebiotech.
- **Giao diện & Ngôn ngữ**: **sáng/tối** + **ngôn ngữ** (Việt/Anh đầy đủ; Trung/Thái đã có khung). Lưu cục bộ
  qua `AppPrefs` (đổi ngôn ngữ → rebuild toàn app; theme áp **live**).
- **Khoảng đọc (giây)**: quy đổi trục thời gian đồ thị CT (mặc định 20).
- **Vị trí lưu file**: thư mục gốc cho mọi file lưu (`<gốc>\FBT_RAPID_ketqua\`, `<gốc>\FBT_RAPID_templog\`);
  nút **Chọn thư mục… / Về mặc định / Mở thư mục**. Mặc định **Documents**.
- **Sao lưu & Khôi phục**: xuất **lịch sử + cài đặt** ra 1 file JSON; khôi phục (**Gộp** hoặc **Thay thế**).

> Link cloud + accounts (`/exec`) **gắn sẵn trong code** (`kDefaultCloudApiUrl`, `kDefaultAuthApiUrl`) →
> chạy ngay, không cần nhập. Đổi deploy: sửa hằng đó rồi build lại.

### 1.6 Đăng nhập & phân quyền (5 vai trò)

Mở app là vào **màn đăng nhập** (tài khoản + mật khẩu). Xác thực qua **Engineer Server**
(`POST /auth`, bảng `users` Postgres — chuyển từ Apps Script `userAuth.js` về từ 2026-07-14; script cũ
chỉ còn là đường lùi khẩn cấp). Đăng nhập thành công server trả kèm **token API theo vai trò**. Phiên
**lưu cục bộ** → lần sau vào thẳng; **Đăng xuất** trong menu icon tài khoản.

| Vai trò (`role`) | Hiển thị | Quyền |
| --- | --- | --- |
| `root` | **Root** | Full app + **Quản lý User** (§1.7) — thêm/sửa/xóa **mọi** tài khoản; **thấy mọi máy** |
| `admin` | **Nhân viên** | Full app (Lịch sử + CSKH + Quản lý máy + Kỹ Thuật + Sản xuất); **chỉ thấy mã máy được cấp** (trừ khi `ids = "*"`); được Lưu/Đồng bộ/Lấy-từ-máy/Xóa — **KHÔNG** quản lý tài khoản |
| `manager` | **Quản lý SX** | **Sản xuất** đầy đủ (Chạy trạm · Hồ sơ máy · Thống kê) + **xem** Quản lý máy; **tạo/khoá tài khoản Thao tác viên**; **KHÔNG** ghi OTA, **KHÔNG** xem dữ liệu lâm sàng |
| `operator` | **Thao tác viên** | Chỉ **Sản xuất › Chạy trạm + Hồ sơ máy**; không Thống kê, không OTA, không Kỹ Thuật, không lịch sử lâm sàng |
| `user` | **Khách hàng** | **Read-only**: chỉ xem đồ thị **mã máy được cấp**; ẩn Kỹ Thuật + mọi nút ghi (vẫn **lưu được ảnh** đồ thị) |

Hai vai trò **xưởng** (`manager`/`operator`, thêm 2026-09-07 — [kế hoạch](docs/plan/tai-khoan-nha-may.md)):
sinh ra để người đứng máy ở nhà máy KHÔNG phải mang vai trò `admin` — mà `admin` thì server phát kèm
**token ghi OTA**, tức là quyền đẩy firmware cho cả fleet ngoài thị trường. Nay `manager`/`operator` nhận
token thiết bị: ghi được hồ sơ ATE, tải được `.bin` để nạp, nhưng không arm được bản nào cho fleet.

Phân quyền qua `SessionStore.current`, đọc theo **tên việc** (`canWriteClinical` · `canWriteOta` ·
`canRunStation` · `canSeeProduction` · `canUseTech` · `canManageUsers`…) chứ không theo chức danh —
ma trận đầy đủ có test ở `test/user_session_test.dart`. **Phạm vi XEM máy** = `allowAll` (`canSee()`): chỉ **root**
(super-admin) **hoặc** `ids` chứa `"*"` mới thấy tất cả — **admin không có `"*"` chỉ thấy mã được cấp**, như
khách hàng. Lọc enforce **ở client** (cloud/history fetch hết rồi `canSee`). Tài khoản (username · mật khẩu ·
vai trò · **mã máy được cấp** · email · active) quản lý trên **Google Sheet** (tab `Accounts`).

> ⚠️ Deploy script accounts **phải** đặt **"Who has access" = Anyone** (không thì app nhận trang đăng nhập
> Google thay vì JSON). Mật khẩu **băm SHA-256 + salt** trong sheet (`sha256$salt$hash`, tương thích ngược
> tài khoản plaintext cũ; hàm `migratePasswordsToHash()` chạy tay để băm hết). App gửi mật khẩu qua HTTPS,
> server băm/so khớp. Schema + setup ở **đầu file `userAuth.js`**.

### 1.7 Quản lý User (chỉ Root)

Tab dọc riêng cho **root**. Mở → **hỏi lại mật khẩu** (chỉ giữ trong RAM, không lưu) → danh sách tài khoản.
Thao tác (qua `POST {action: listUsers|saveUser|deleteUser}`, **root-gated** bởi `requireAdmin_`):

- **Tạo user** (nút +): username · mật khẩu · vai trò (Root/Nhân viên/Khách hàng) · mã máy · tên · email.
- **Cấp/đổi mã máy**: bấm 1 user → sửa ô mã máy.
- **Tắt / Bật** tài khoản: công tắc trên từng dòng.
- **Xóa** user (backend chặn xóa/hạ **root cuối cùng**).

> Bootstrap: vì quản lý là **root-only**, đặt 1 tài khoản `role = root` trong sheet (sửa tay 1 lần) rồi
> đăng nhập bằng tài khoản đó.

---

### 1.8 Chăm sóc KH (nhân sự) — Thông tin máy · Xử lý sự cố

Tab riêng cho **nhân viên chăm sóc khách hàng** vận hành không cần kiến thức kỹ thuật
([lib/screens/support_screen.dart](lib/screens/support_screen.dart), chạy cả desktop lẫn web;
thiết kế chi tiết: [docs/07-cham-soc-khach-hang.md](docs/07-cham-soc-khach-hang.md)).

- **Thông tin máy**: tài liệu viết bằng ngôn ngữ đời thường cho nhân viên CSKH, theo thứ tự nên đọc:
  **sản phẩm là gì → một lần xét nghiệm diễn ra thế nào (5 bước) → đọc kết quả cho khách (P/N/S/E, CT)
  → nút bấm & màn hình (kèm bộ tách mã lỗi 4 số) → WiFi, app & hệ thống → cập nhật phần mềm → câu hỏi
  khách thường gọi (hỏi gì · khách tự thử gì · khi nào chuyển kỹ thuật) → kịch bản tiếp nhận & ticket →
  từ điển thuật ngữ**; mục **"Dành cho kỹ thuật"** (cổng USB, lệnh máy) thu gọn mặc định. Có dải giới
  thiệu, chip **"Đi tới"** cuộn thẳng tới mục, ô tìm. Nội dung ở `lib/data/machine_info_content.dart` —
  sửa một chỗ.
- **Xử lý sự cố** — 3 bước: **(1) Kết nối** cắm USB máy → chọn cổng (web: hộp thoại trình duyệt) →
  Kết nối (115200, DTR/RTS off nên máy không reset) · **(2) Nhật ký máy (log)** hiện theo dòng, dòng
  đáng chú ý tô đỏ + bộ lọc; khung **"Máy đang nói gì"** mở đầu bằng **kết luận một dòng** (đỏ: chuyển
  kỹ thuật · vàng: khách tự thử được · xanh: không thấy lỗi quen thuộc) rồi dịch log ESP32 thành tiếng
  người (nguồn yếu/brownout, crash, thiếu firmware, watchdog, mất cảm biến nhiệt -127, WiFi/HTTP/OTA
  lỗi, lý do reset) kèm gợi ý; nút "Yêu cầu máy" chỉ hiện nhãn tiếng Việt (lệnh thật `ParaRead` · `M` ·
  `TemperatureOutput` · `Res` nằm trong tooltip, `Res` có xác nhận) ·
  **(3) Gửi log về kỹ thuật**: mã máy (tự điền nếu log in `RPL…`) + mô tả → `PUT /devices/{id}/logs`
  Engineer Server; **Lưu file** (`FBT_RAPID_supportlog\`, web: Downloads) khi không có mạng; nút
  **Log đã gửi** xem lại các bản đã gửi của máy đó.
- Cổng COM dùng chung với tab Kỹ Thuật: rời tab/mục là tự ngắt cổng (giữ log).
- Server phải deploy route mới (`server/app/main.py`, xem `server/docs/history/2026-09-04.md`);
  chưa deploy thì nút Gửi báo "server chưa được cập nhật cho tính năng này" (405).

### 1.9 Quản lý máy (nhân sự) — Cập nhật OTA · Trạng thái máy

Tab [lib/screens/manager_machine_screen.dart](lib/screens/manager_machine_screen.dart), chỉ HTTP tới
Engineer Server nên chạy cả web.

- **Cập nhật OTA**: kho firmware `.bin` trên server — **Tải firmware lên** (hỏi tên phiên bản, lưu thành
  `fbt_v<version>.bin`), **Chọn bản này** cho cả fleet (hộp xác nhận + số máy ảnh hưởng), **Tiến độ**
  triển khai, **Huỷ chọn**, **Xoá** (chặn xoá bản đang chọn/đang ghim), và **Tải về máy tính** (icon
  mũi tên xuống trên từng dòng: desktop mở hộp thoại "Lưu thành…", web tải xuống Downloads — cùng route
  `GET /ota/{file}` thiết bị dùng khi tự cập nhật, để kỹ thuật nạp bằng cáp hoặc lưu trữ).
- **Trạng thái máy**: bảng máy với firmware đang chạy, người thiết lập, số phiên, lần gửi cuối (chấm
  xanh/xám theo 24 giờ), ghim riêng bản firmware cho từng máy (chỉ máy ≥ v2.4.4), lịch sử firmware.

### 1.10 Sản xuất — trạm ATE (nhân sự) — Chạy trạm · Hồ sơ máy · Thống kê

Trạm nghiệm thu máy tại xưởng ([lib/screens/ate_screen.dart](lib/screens/ate_screen.dart); thiết kế:
[docs/08-tram-san-xuat-ate.md](docs/08-tram-san-xuat-ate.md), kế hoạch đầy đủ P0→P5:
[docs/plan/ate-san-xuat.md](docs/plan/ate-san-xuat.md)). Hiện có **pha P0: nạp → khai sinh → hồ sơ**.

- **Chạy trạm** (chỉ desktop): khai một lần cho cả ca (mã trạm, người chạy, bộ 3 file `.bin`, chip/flash
  mode/size/baud, `para version`, PCB version, FW kỳ vọng) → **quét mã vạch số máy** (máy quét USB hoạt
  động như bàn phím, Enter là chạy luôn) → kịch bản 5 bước chạy tuần tự, **dừng đúng bước hỏng**:
  **FW-02** nhận chip · flash · MAC → **FW-01** nạp 3 file + `verify_flash` + **sha256 của .bin** →
  **BOOT-01** nghe UART và quét log (dùng chung bộ quét của tab CSKH) → **ID-01** ghi số máy rồi
  **đọc lại `ParaRead` đối chiếu** → **ID-02** tham số mặc định của lô → rồi **6 bước tự kiểm phần
  cứng**: **OPT-01** 10 cảm biến quang (`GET /errors`, chưa có mạng thì lùi về lệnh `R`) → **OPT-03**
  tín hiệu sáng từng slot (`0`–`9`, một slot một lần) → **TMP-01** 6 kênh nhiệt qua `TemperatureOutput`
  (bắt cảm biến mất kết nối `-127`, lệch giữa kênh, lệch nhiệt phòng) → **FAN-01 · BUZ-01 · HMI-01**
  quạt/còi/màn hình + 3 nút (app gửi lệnh rồi hỏi người vận hành ĐẠT/KHÔNG ĐẠT). Kết luận **ĐẠT / HỎNG /
  DỪNG GIỮA CHỪNG** hiện to kèm bước hỏng; có nút **chạy lại một bước** (chốt thành **hồ sơ mới**, không
  sửa hồ sơ cũ) và nhật ký trạm đầy đủ (copy được).
  *Ngưỡng quang chưa chốt thì bước đo **ghi số** (kết luận `info`) chứ không tự nhận là ĐẠT — chạy 10–20
  máy tốt, đọc số trong hồ sơ rồi đặt ngưỡng bằng `PUT /ate/limits`, không phải sửa code.*
- **Hồ sơ máy**: mở ra là thấy ngay **hồ sơ gần đây của cả xưởng** (máy nào vừa chạy, ĐẠT/HỎNG ở bước
  nào, thuộc lô nào) — không phải nhớ sẵn số máy mới xem được gì. Gõ/quét số máy → **hồ sơ khai sinh**
  (ngày, trạm, người chạy, firmware + sha256, MAC, bộ ngưỡng đã chấm) + **mọi lần qua trạm** kể cả các
  lần hỏng; mở từng lần xem chi tiết từng bước và log thô.
- **Thống kê**: **FPY** (máy đạt ngay lần thử đầu / máy đã thử — *không* phải tỉ lệ hồ sơ PASS),
  sản lượng theo ngày, **Pareto mã bước hỏng**; lọc được theo lô.
- **Tiêu chuẩn** (nhân sự kỹ thuật sửa, quản lý SX xem): **bộ ngưỡng đặt cho TỪNG LÔ sản xuất** — chọn
  lô (hoặc thêm lô mới) rồi điền ngưỡng quang/nhiệt/nạp. **Ô trống = chưa chốt** (trạm vẫn đo và ghi số,
  chỉ không chấm đạt/hỏng). Trạm lấy bộ của lô đang chạy; lô chưa có bộ riêng thì lùi về bộ chung/mặc
  định và màn trạm **hiện cảnh báo** cho biết đang chấm theo bộ nào. Sửa ngưỡng phải **đổi version** —
  server từ chối dùng lại version cũ cho nội dung khác, vì hồ sơ chỉ ghi chuỗi version đó.
  **Nhập/Xuất JSON**: khai cả đợt sản xuất bằng file thay vì gõ tay từng lô — nhận file *một bộ*
  (nạp vào biểu mẫu để kiểm rồi Lưu), *một bộ kèm mã lô*, hoặc *nhiều lô một file* (xem trước rồi ghi
  từng lô; lô nào lỗi thì báo riêng, không chặn lô còn lại).
- **Không mất dữ liệu khi rớt mạng**: hồ sơ ghi file cục bộ trước (`FBT_RAPID_ate\cho_gui\`), đẩy
  server sau; mạng sống lại thì tự gửi cả hàng đợi (nút "Gửi lại N hồ sơ đang chờ").
- **Bản web chạy trạm được** (từ 2026-09-08): nạp bằng **esptool-js qua Web Serial** trên Chrome/Edge
  máy tính, **firmware lấy từ kho OTA của server** (không phải file trên máy). Ba khác biệt so với bản
  desktop, màn tự nói ra: bước nạp **chưa đối chiếu lại được** nội dung flash (ghi `info` chứ không phải
  ĐẠT), phải bấm **Chọn cổng** một lần mỗi ca, và hàng đợi offline nằm trong bộ nhớ trình duyệt (lược
  log thô, mất nếu xoá dữ liệu duyệt). Trạm chính vẫn nên dùng bản desktop.
- Server phải deploy bản có `/ate/*` (`server/app/main.py`); chưa deploy thì app báo "Server chưa có
  endpoint /ate (405)" và hồ sơ nằm lại hàng đợi cục bộ, không mất.

## 2. Yêu cầu

- **Flutter SDK** (kênh stable). Kiểm tra: `flutter --version`.
- **Visual Studio 2022/2026** kèm workload **"Desktop development with C++"** (bắt buộc để build Windows).
- **Lịch sử cục bộ**: máy FBT_RAPID nối WiFi cùng mạng LAN với PC và biết **IP của máy**.
- **Cloud**: dùng được ngay (link gắn sẵn) — chỉ cần máy tính có Internet.
- **Nhiệt độ**: máy nối PC qua **cáp USB-COM**.

## 3. Build & chạy

```powershell
cd app
flutter pub get
flutter run -d windows        # chạy có hot reload
```

Build bản phát hành:

```powershell
flutter build windows
# Kết quả: build\windows\x64\runner\Release\
```

> Nếu thư mục `windows/` chưa có (clone mới), sinh runner nền tảng (không ghi đè `lib/`):
> `flutter create --platforms=windows .`  · Thêm nền tảng khác: `flutter create --platforms=android,ios,macos .`

## 4. Cách dùng nhanh

0. **Đăng nhập** (tài khoản từ sheet Accounts). **Hover mép trái** để hiện thanh tab dọc.
1. Tab **Lịch sử** (Cloud): chọn/tìm **mã máy** → xem các lần chạy → bấm để xem **đồ thị CT** → **Lưu**.
2. Tab **Kỹ Thuật** (nhân sự) — nút gạt 3 công cụ:
   - **Log nhiệt**: cắm máy USB → bấm cổng **COM** → xem 6 nhiệt + đồ thị realtime → **Lưu** (⌛ xem lại).
   - **Đọc serial**: mở 1+ cổng COM → đọc/gửi raw (text/HEX) để debug.
   - **Nạp code**: chọn cổng + chip → chọn .bin → **Nạp** (esptool); theo dõi log boot ngay sau nạp.
3. **Icon tài khoản → Thiết lập**: đổi theme/ngôn ngữ, mật khẩu/email, nơi lưu file.
4. **Root** → tab **Quản lý User**: tạo/sửa/xóa tài khoản, cấp mã máy (`*` = full), tắt/bật.

## 5. Giới hạn hiện tại

- **Lịch sử cục bộ** lưu trên chính PC (`shared_preferences`); `/getdata` chỉ trả **lần chạy gần nhất**.
- **Cài WiFi cho máy**: nút *Gửi tới máy* cần firmware bổ sung endpoint `/setwifi`; trước mắt dùng
  **captive portal** của WiFiManager.
- **Cloud `ids`** quét cả folder Drive nên lần đầu (cache rỗng) có thể mất ~30–60s; sau đó tức thì nhờ cache.
- **Nhiệt độ**: lệnh `TemperatureOutput` của máy là **toggle** (không có ack). Nếu không thấy dữ liệu, bấm
  **Gửi lệnh** để bật/tắt lại.

Roadmap: `/setwifi`, `/config`…

## 6. Đóng gói & phân phối (Windows)

- **Portable (ZIP)**: nén thư mục `build\windows\x64\runner\Release\` (kèm 3 DLL VC++ runtime:
  `msvcp140.dll`, `vcruntime140.dll`, `vcruntime140_1.dll`) → người dùng giải nén và chạy `.exe`.
- **Installer** (khuyến nghị): script [installer.iss](installer.iss) (Inno Setup) tạo
  `FBT_RAPID-Setup-vX.Y.Z.exe` — cài không cần admin, có shortcut Start Menu + gỡ cài đặt. **Phải
  `flutter build windows --release` TRƯỚC** (script trỏ thư mục Release).
- **esptool kèm theo** (tab Nạp code): đặt `esptool.exe` vào `windows/vendor/` → CMake copy nó cạnh
  `fbt_dxd_app.exe` mỗi lần build + installer tự gói (xem [windows/vendor/README.md](windows/vendor/README.md));
  app tự dò, không cần đường dẫn ngoài.

> App chưa ký số → Windows SmartScreen có thể cảnh báo 1 lần (*More info → Run anyway*).

## 7. Cấu trúc code

```
lib/
  main.dart                          # entry + theme/locale (AppPrefs) + cổng đăng nhập (_AuthGate)
  models/test_result.dart            # mô hình kết quả; parse /getdata + cloud; version; slope; 4-đồ-thị
  models/user_session.dart           # 3 vai trò root/admin/user; canSee = allowAll(root|ids '*'); isStaff=canWrite
  services/device_api.dart           # HTTP tới máy (GET /getdata, /setwifi)
  services/cloud_history_api.dart    # HTTP tới Apps Script doGet (ids / runs / run)
  services/auth_api.dart             # POST accounts: login/đổi mk·email/quản lý user (theo redirect 302 + drain)
  services/session_store.dart        # phiên toàn cục (isStaff/canWrite/canManageUsers) + lưu/khôi phục
  services/app_prefs.dart            # theme (sáng/tối) + ngôn ngữ — ChangeNotifier toàn cục, lưu prefs
  services/cloud_cache.dart          # cache danh sách máy cục bộ (load tức thì)
  services/history_store.dart        # lưu + đồng bộ lịch sử cục bộ (add / addAll, dedupe)
  services/app_settings.dart         # cấu hình; cloudApiUrl + authApiUrl gắn sẵn (kDefault*ApiUrl)
  services/temperature_serial.dart   # đọc UART/COM (flutter_libserialport), parse TimeRB/TimeRT, đa COM
                                     #   (refreshPorts lọc qua util/serial_ports.dart → chỉ USB-serial)
  services/temperature_store.dart    # lưu/đọc log (JSON+CSV) + ảnh đồ thị (PNG)
  screens/login_screen.dart          # màn đăng nhập (gọi AuthApi)
  screens/home_shell.dart            # nav dọc ẩn (hover hiện) theo vai trò; Thiết lập trong icon tài khoản
  screens/history_combined_screen.dart   # tab Lịch sử GỘP: nút gạt Cục bộ | Cloud
  screens/history_screen.dart        # Lịch sử › Cục bộ + lấy kết quả từ máy
  screens/cloud_devices_screen.dart  # Lịch sử › Cloud: danh sách máy (cache + tìm + sắp xếp + lọc quyền)
  screens/cloud_runs_screen.dart     # lịch sử 1 máy (phân trang + đồng bộ)
  screens/result_detail_screen.dart  # kết quả bệnh + đồ thị CT (chọn 4 dạng đường cong)
  screens/curve_compare_screen.dart  # xem cả 4 đồ thị cùng lúc
  screens/tech_screen.dart               # tab Kỹ Thuật: nút gạt Log nhiệt | Đọc serial | Nạp code
  screens/temperature_log_screen.dart    # Log nhiệt: đa COM realtime + lưu log/đồ thị
  screens/serial_console_screen.dart     # Đọc serial: console raw đa cổng (đọc/ghi, text/HEX) để debug
  screens/flasher_screen.dart            # Nạp code: flash ESP qua esptool (Process) + theo dõi log boot
  screens/ate_screen.dart                # tab Sản xuất (ATE): Chạy trạm | Hồ sơ máy | Thống kê
  screens/ate_screen_web.dart            #   bản web (chỉ Hồ sơ máy | Thống kê — không có Chạy trạm)
  screens/ate_run_screen.dart            #   Chạy trạm: quét số máy → 5 bước → PASS/FAIL → hồ sơ
  screens/ate_profile_screen.dart        #   Hồ sơ máy: hồ sơ khai sinh + mọi lần qua trạm
  screens/ate_stats_screen.dart          #   Thống kê: FPY, sản lượng/ngày, Pareto mã bước hỏng
  screens/calib_screen.dart              # tab Hiệu chuẩn (2026-09-21): Lô pha | Bộ ống | Ngưỡng — ống chuẩn Fluorescein
  screens/calib_batch_screen.dart        #   một lô: nguyên liệu → checklist pha → bảng số đo → xếp hạng/đóng bộ → bộ ống
  services/calib_api.dart                #   client /calib/* (Engineer Server); model CalibBatchMeta/CalibSet/CalibCombo/CalibRank
  services/calib_reader.dart             #   đọc raw từ máy tham chiếu qua SerialLink: gửi 1 byte khe, parse {Green: N} / raw,calibrated
  services/ate_runner.dart               # kịch bản ATE (THUẦN Dart, test được — interface AteStation)
  services/ate_station_io.dart           #   bản thật của AteStation: esptool (Process) + cổng COM
  services/ate_api.dart                  #   client /ate/* của Engineer Server (PUT/GET)
  services/ate_queue_io.dart             #   hàng đợi hồ sơ: ghi file trước, đẩy server sau
  services/ate_prefs.dart                #   cấu hình của TRẠM (mã trạm, người chạy, bộ .bin, tham số nạp)
  models/ate_record.dart                 # hồ sơ + kết quả bước + bộ ngưỡng (AteLimits)
  util/sha256.dart                       # SHA-256 thuần Dart (vân tay file .bin trong hồ sơ ATE)
  screens/saved_logs_screen.dart         # danh sách log đã lưu
  screens/saved_log_detail_screen.dart   # xem lại 1 log: vẽ lại đồ thị + thống kê
  screens/saved_charts_screen.dart       # gallery ảnh đồ thị đã lưu
  screens/user_settings_screen.dart  # Thiết lập DÙNG CHUNG (tài khoản/theme/ngôn ngữ/lưu/sao lưu)
  screens/user_management_screen.dart    # Quản lý User (root): tạo/sửa/xóa/tắt-bật tài khoản
  screens/settings_screen.dart       # (CŨ — không dùng; thay bằng user_settings_screen.dart)
  widgets/ct_chart.dart              # đồ thị CT (fl_chart; trục tung làm tròn bội số 50 + y=0)
  widgets/temp_chart.dart            # đồ thị nhiệt + chú thích + chip ẩn/hiện kênh (realtime/xem lại)
  widgets/result_badge.dart          # chip phân loại kết quả
  util/format.dart                   # format ngày giờ, CT
  util/i18n.dart                     # tr('key') + roleLabel() (Việt/Anh; fallback Việt)
  util/serial_ports.dart             # usableSerialPorts(): lọc chỉ cổng USB-serial (đọc/ghi/nạp)
  util/curve_processing.dart         # baseline + Savitzky–Golay (port từ firmware)
  util/chart_capture.dart            # chụp RepaintBoundary → PNG
windows/vendor/esptool.exe           # (đặt tay) esptool kèm app — CMake copy cạnh exe khi build
```

Phụ thuộc: `http`, `fl_chart`, `shared_preferences`, `flutter_libserialport` (UART), `file_selector` (chọn file).
