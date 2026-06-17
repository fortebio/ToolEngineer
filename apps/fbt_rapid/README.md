# FBT_RAPID App (Flutter)

App đồng hành của thiết bị **Forte Rapid+ / FBT_RAPID**: xem **lịch sử xét nghiệm** + **đồ thị CT**,
quản lý **nhiều máy qua cloud**, **theo dõi nhiệt độ realtime qua UART**, và **cài đặt** thiết bị.

Build cho **Windows trước**. Kết nối:

- **WiFi/HTTP** — lấy kết quả trực tiếp từ máy (`GET /getdata`).
- **Google Drive** qua **Apps Script `doGet`** — đọc lịch sử nhiều máy (link gắn sẵn trong app).
- **UART/COM** — đọc nhiệt độ thời gian thực từ máy.

**Hướng dẫn người dùng**: [../docs/HUONG_DAN_SU_DUNG.md](../docs/HUONG_DAN_SU_DUNG.md).
Thiết kế chi tiết: [../docs/APP_SPEC.md](../docs/APP_SPEC.md) · Backend cloud: [../sheet/getData.js](../sheet/getData.js).

---

## 1. Tính năng

App có **4 tab**: **Lịch sử** (cục bộ) · **Cloud** (nhiều máy) · **Nhiệt độ** (UART) · **Cài đặt**.

### 1.1 Lịch sử (cục bộ — lấy trực tiếp từ máy)

- **Lấy kết quả từ máy**: gọi `GET /getdata` tới IP máy (cùng mạng WiFi), lưu lần chạy thành 1 bản ghi
  trên PC (`shared_preferences`).
- Danh sách bản ghi: thời gian, mã máy, **tóm tắt số slot** Dương / Dương nhẹ / Âm / Lỗi.
- Xoá 1 bản ghi hoặc xoá tất cả.
- Bấm vào bản ghi → màn **chi tiết** (xem §1.4).

### 1.2 Cloud (lịch sử nhiều máy từ Google Drive)

- **Danh sách mã máy**: mỗi máy hiện **số lần chạy**, **FW** (version firmware mới nhất), **mốc mới nhất**.
  Danh sách được **cache** (hiện tức thì, làm mới ngầm; server cache 5′ giảm tải Apps Script).
- **Tìm kiếm** theo mã máy · **Sắp xếp**: Mới nhất · Cũ nhất · Mã máy A→Z · Mã máy Z→A.
- Bấm 1 máy → **lịch sử các lần chạy của riêng máy đó**:
  - **Phân trang 10 lần chạy/trang** (nút **Trước / Sau**, có ô đếm trang).
  - Mỗi lần chạy hiện thời gian, **FW version**, tóm tắt slot.
  - Bấm 1 lần chạy → tải **chi tiết kèm đường cong** → màn **đồ thị CT** (§1.4).
  - **Đồng bộ về máy** (⬇): kéo các lần chạy của trang hiện tại (kèm đường cong) vào **lịch sử cục bộ**
    để xem offline; khử trùng theo `fileId`.


### 1.3 Nhiệt độ (đọc UART/COM, realtime)

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
- **Lưu kết quả**: nút **Lưu** ở **trên cùng** (AppBar) màn kết quả → **checklist chọn loại đồ thị**
  (Raw / Calib / Baseline / SG) → lưu **gom theo mã máy**: `<thư mục lưu>\FBT_RAPID_ketqua\<MãMáy>\<Ngày_Giờ_Firmware>\`,
  gồm các **ảnh PNG** đã chọn (`raw`, `calib`, `baseline`, `baseline_sg`) + **`data.json`** (data raw,
  slopes, version firmware, kết quả/CT từng slot). Mỗi ảnh PNG kèm **bảng "Kết quả bệnh"** bên phải (như
  giao diện), **đồ thị cao bằng bảng** (không chừa khoảng trắng dưới), xuất ở **độ phân giải gấp đôi**.
  Đồ thị lưu theo **slot đang chọn**. (Màn **"Xem cả 4 đồ thị"** vẫn có nút **Lưu** cả 4 cùng lúc.)
- Hiện **Firmware** của lần chạy nếu có.

### 1.5 Cài đặt

- **Kết nối máy**: nhập IP máy + **Kiểm tra kết nối** (ping `/getdata`).
- **Khoảng đọc (giây)**: quy đổi trục thời gian của đồ thị CT (mặc định 20).
- **Cài WiFi cho máy**: form SSID/mật khẩu (xem §5 — cần firmware bổ sung `/setwifi`).
- **Thông tin người dùng**: tên, đơn vị/phòng khám.
- **Vị trí lưu file**: chọn **thư mục gốc** cho mọi file lưu ra — **kết quả CT**
  (`<gốc>\FBT_RAPID_ketqua\`) và **log nhiệt** (`<gốc>\FBT_RAPID_templog\`). Mặc định là **Documents**;
  có nút **Chọn thư mục…**, **Về mặc định**, **Mở thư mục**.
- **Sao lưu & Khôi phục**: xuất **lịch sử xét nghiệm + cài đặt** ra 1 file JSON (chọn nơi lưu — USB/Drive…);
  khôi phục từ file đó (**Gộp** giữ dữ liệu hiện có, hoặc **Thay thế**).

> Link cloud (Apps Script `/exec`) **gắn sẵn trong code** (`kDefaultCloudApiUrl`) → tab Cloud chạy ngay,
> không cần nhập. Đổi link: sửa hằng đó rồi build lại.

---

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

1. Tab **Cloud**: chọn (hoặc tìm/sắp xếp) **mã máy** → xem các lần chạy (phân trang) → bấm 1 lần để xem
   **đồ thị CT** → tuỳ chọn **Đồng bộ về máy**.
2. Tab **Lịch sử**: (cần nhập IP ở Cài đặt) → **Lấy kết quả từ máy** → bấm bản ghi để xem kết quả + đồ thị.
3. Tab **Nhiệt độ**: cắm máy qua USB → bấm cổng **COM** → xem 6 nhiệt + đồ thị realtime → **Lưu log/đồ thị**;
   menu ⌛ để **xem lại** log/đồ thị đã lưu.

## 5. Giới hạn hiện tại

- **Lịch sử cục bộ** lưu trên chính PC (`shared_preferences`); `/getdata` chỉ trả **lần chạy gần nhất**.
- **Cài WiFi cho máy**: nút *Gửi tới máy* cần firmware bổ sung endpoint `/setwifi`; trước mắt dùng
  **captive portal** của WiFiManager.
- **Cloud `ids`** quét cả folder Drive nên lần đầu (cache rỗng) có thể mất ~30–60s; sau đó tức thì nhờ cache.
- **Nhiệt độ**: lệnh `TemperatureOutput` của máy là **toggle** (không có ack). Nếu không thấy dữ liệu, bấm
  **Gửi lệnh** để bật/tắt lại.

Roadmap (`/setwifi`, `/config`…) xem [../docs/APP_SPEC.md](../docs/APP_SPEC.md).

## 6. Đóng gói & phân phối (Windows)

- **Portable (ZIP)**: nén thư mục `build\windows\x64\runner\Release\` (kèm 3 DLL VC++ runtime:
  `msvcp140.dll`, `vcruntime140.dll`, `vcruntime140_1.dll`) → người dùng giải nén và chạy `.exe`.
- **Installer** (khuyến nghị): script [../tools/installer.iss](../tools/installer.iss) (Inno Setup) tạo
  `FBT_RAPID-Setup-vX.Y.Z.exe` — cài không cần admin, có shortcut Start Menu + gỡ cài đặt.

> App chưa ký số → Windows SmartScreen có thể cảnh báo 1 lần (*More info → Run anyway*).

## 7. Cấu trúc code

```
lib/
  main.dart                          # entry + theme
  models/test_result.dart            # mô hình kết quả; parse /getdata + cloud; version; slope; 4-đồ-thị
  services/device_api.dart           # HTTP tới máy (GET /getdata, /setwifi)
  services/cloud_history_api.dart    # HTTP tới Apps Script doGet (ids / runs / run)
  services/cloud_cache.dart          # cache danh sách máy cục bộ (load tức thì)
  services/history_store.dart        # lưu + đồng bộ lịch sử cục bộ (add / addAll, dedupe)
  services/app_settings.dart         # cấu hình; cloudApiUrl gắn sẵn (kDefaultCloudApiUrl)
  services/temperature_serial.dart   # đọc UART/COM (flutter_libserialport), parse TimeRB/TimeRT, đa COM
  services/temperature_store.dart    # lưu/đọc log (JSON+CSV) + ảnh đồ thị (PNG)
  screens/home_shell.dart            # điều hướng 4 tab
  screens/history_screen.dart        # lịch sử cục bộ + lấy kết quả từ máy
  screens/cloud_devices_screen.dart  # danh sách máy cloud (cache + tìm kiếm + sắp xếp + FW)
  screens/cloud_runs_screen.dart     # lịch sử 1 máy (phân trang + đồng bộ)
  screens/result_detail_screen.dart  # kết quả bệnh + đồ thị CT (chọn 4 dạng đường cong)
  screens/curve_compare_screen.dart  # xem cả 4 đồ thị cùng lúc
  screens/temperature_log_screen.dart    # tab Nhiệt độ: đa COM realtime + lưu log/đồ thị
  screens/saved_logs_screen.dart         # danh sách log đã lưu
  screens/saved_log_detail_screen.dart   # xem lại 1 log: vẽ lại đồ thị + thống kê
  screens/saved_charts_screen.dart       # gallery ảnh đồ thị đã lưu
  screens/settings_screen.dart       # kết nối máy, WiFi, người dùng
  widgets/ct_chart.dart              # đồ thị CT (fl_chart, đệm trục + y=0)
  widgets/temp_chart.dart            # đồ thị nhiệt + chú thích + chip ẩn/hiện kênh (tái dùng realtime/xem lại)
  widgets/result_badge.dart          # chip phân loại kết quả
  util/format.dart                   # format ngày giờ, CT
  util/curve_processing.dart         # baseline + Savitzky–Golay (port từ firmware)
  util/chart_capture.dart            # chụp RepaintBoundary → PNG
```

Phụ thuộc: `http`, `fl_chart`, `shared_preferences`, `flutter_libserialport` (UART).
