# CLAUDE.md — FBT_RAPID App (Flutter / Windows)

> **Từ 2026-09-15 app nằm trong monorepo `ToolEngineer` tại `apps/fbt_rapid/`** (server ở `server/`,
> firmware ở `firmware/<product>/`, registry sản phẩm `system/products.yaml`; quy tắc toàn hệ ở
> `CLAUDE.md` gốc). Mọi lệnh `flutter` trong file này chạy TỪ `apps/fbt_rapid/`; đường dẫn
> `lib/… test/… build/…` tính từ đó, đường dẫn `server/… legacy/sheet/… .claude/…` tính từ gốc.

Hướng dẫn cho AI/người phát triển. Danh sách **tính năng đầy đủ** xem [README.md](README.md);
file này tập trung vào **kiến trúc, lệnh, quy ước, và các cạm bẫy (gotchas)**.

## Tổng quan
App desktop **Windows** (Flutter) đồng hành thiết bị xét nghiệm **FBT_RAPID / Forte Rapid+**:
đăng nhập phân quyền → xem **lịch sử + đồ thị CT** (cục bộ qua HTTP `/getdata`, và cloud qua Google
Drive), **theo dõi nhiệt độ realtime qua UART/COM**, lưu kết quả/đồ thị ra file.

- Tên hiển thị: **FBT_RAPID** · tên gói (`pubspec name`): `RapidPlusApp` · **tên exe**: `fbt_dxd_app.exe`
  (đặt trong `windows/CMakeLists.txt` → `BINARY_NAME`). Ba tên này KHÁC nhau — dễ nhầm.
- Build/chạy **Windows desktop** (đầy đủ) và **WEB** (thêm 2026-07, host tại
  `https://fbt.basa-luma.ts.net/app/`: đăng nhập, Lịch sử cloud + đồ thị, tab Thư Mục/JSON data,
  cả tab Kỹ Thuật qua Web Serial + esptool-js trên Chrome/Edge desktop — mục Web dưới).
  Chưa làm android/ios.

## Lệnh hay dùng (PowerShell)
```powershell
cd apps\fbt_rapid                      # MỌI lệnh flutter chạy từ đây (monorepo từ 2026-09-15)
flutter pub get
flutter run -d windows                 # chạy có hot reload
flutter build windows --debug          # build nhanh để test  → build\windows\x64\runner\Debug\fbt_dxd_app.exe
flutter build windows --release        # build phát hành      → ...\Release\
flutter analyze lib/<file>...          # lint nhanh vài file (đừng analyze cả repo nếu không cần)
flutter analyze lib/ 2>&1 | grep -E "error|warning"   # ĐỌC KẾT QUẢ KIỂU NÀY, đừng | tail
flutter build web --release            # build WEB → build\web (host tĩnh ở đâu cũng được)
```
⚠️ **`flutter analyze | tail -N` GIẤU MẤT lỗi biên dịch**: repo có sẵn ~11 dòng `info`
(deprecated_member_use…) nên `error` bị đẩy lên đầu, `tail` chỉ thấy lint vô hại và dòng tổng
"N issues found" — đọc thành "sạch". Đã suýt commit code không biên dịch được vì cái này
(2026-08-28). Luôn lọc `grep -E "error|warning"` rồi mới xem tổng số.
Đóng gói installer (Inno Setup) — **phải `--release` TRƯỚC** vì script trỏ vào thư mục Release.
`installer.iss` nằm ở **gốc thư mục app (`apps/fbt_rapid/`)** (source/icon/vendor dùng path tương đối `AddBackslash(SourcePath)`
— đừng hardcode đường dẫn tuyệt đối); mỗi lần phát hành nhớ nâng `MyAppVersion` trong file.
Version phát hành = `MyAppVersion` (khớp tag commit `vX.Y.Z`); `pubspec.yaml` KHÔNG đồng bộ (vẫn 1.0.2) — đừng lấy đó làm chuẩn:
**Version app HIỆN TRÊN MÀN HÌNH** (2026-09-22) lấy từ `lib/util/app_version.dart` — một chỗ duy nhất cho
cả ba nơi bày: màn đăng nhập, chân thanh điều hướng (rail bung / ngăn kéo điện thoại), **Thiết lập › Phiên bản**.
`kAppVersion` = bản ĐANG PHÁT TRIỂN (hiện `v1.1.0-dev`), `kAppLastRelease` = `MyAppVersion` của `installer.iss`
(`test/app_version_test.dart` đọc file .iss và bắt lệch — đây là cách giữ hai chỗ khỏi trôi như `pubspec.yaml` đã trôi).
Dựng bản phát hành thì thêm `--dart-define=FBT_CHANNEL=release` (+ `FBT_BUILD_DATE`, `FBT_BUILD_REV`) —
không có cờ này thì app tự nhận mình là bản dev và in cảnh báo vàng "chưa phát hành".
```powershell
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" installer.iss
# → <OutputDir trong installer.iss>\FBT_RAPID-Setup-vX.Y.Z.exe
```
**Xem MÀN SAU ĐĂNG NHẬP mà không cần tài khoản/mạng** (máy không build được Windows desktop, vd máy
`Admin`) — dùng `mock-server.js` (2026-09-22): `flutter build web --release --base-href /app/
--dart-define=FBT_URL=http://localhost:8090` rồi `node mock-server.js 8090` (chạy nền) → mở
`http://localhost:8090/app/`, **đăng nhập bằng user/pass BẤT KỲ, vào thẳng vai trò root** nên thấy đủ
mọi tab. Khác `serve-web-local.js` (file kia PROXY về server thật, cần mạng + tài khoản). Trình duyệt
của Claude bấm được bằng toạ độ (Flutter web canvas → `find`/`read_page` KHÔNG thấy chữ, phải chụp màn
hình rồi click theo pixel); sửa Dart xong phải **build lại** (~60 s) mới thấy đổi.

**Chạy + chụp màn hình tự động** (cho AI/agent — GUI không có curl/Playwright): skill
`.claude/skills/run-fbt-rapid/` (ở GỐC monorepo; `driver.ps1`, mặc định trỏ `apps\fbt_rapid`) build/launch `fbt_dxd_app.exe` rồi chụp ĐÚNG cửa
sổ ra PNG. Vd `& ..\..\.claude\skills\run-fbt-rapid\driver.ps1` (launch Debug → chụp `_smoke.png` →
đóng; cờ `-KeepOpen`/`-Attach`/`-Release`). Muốn **bấm nút** trong app (driver chỉ chụp): script
PS `SetForegroundWindow` → `GetWindowRect` → `SetCursorPos(rect + offset-theo-ảnh-PNG)` →
`mouse_event` down/up — offset lấy thẳng từ toạ độ pixel trên PNG vừa chụp (PrintWindow 1:1 với
GetWindowRect). Click có thể TRƯỢT im lặng (không lỗi) → sau mỗi click phải chụp lại xác nhận;
kết quả bấm-mở-màn có khi tới CHẬM (fetch mạng) — chụp thấy chưa đổi thì chờ rồi chụp lại
trước khi kết luận hỏng.
⚠️ **Click "trượt" hoài thường KHÔNG phải sai toạ độ mà là app đang NẰM DƯỚI cửa sổ khác**
(VS Code…): Windows chặn cướp focus nên `SetForegroundWindow` im lặng không nâng app lên, và
click rơi vào **app khác** đang phủ lên. `driver.ps1` chụp theo HWND nên ảnh vẫn ra y hệt dù
click chẳng tới đâu → nhìn ảnh không phát hiện được. Dấu hiệu: bấm 2–3 lần vẫn không đổi gì.
Cách chẩn: chụp **CẢ MÀN HÌNH** (`[System.Windows.Forms.SystemInformation]::VirtualScreen` +
`Graphics.CopyFromScreen`) sẽ thấy ngay ai đang che. Cách nâng app lên thật:
`(New-Object -ComObject WScript.Shell).AppActivate($pid)` rồi mới click.
Hộp thoại **native** (chọn/lưu file) cũng là cửa sổ RIÊNG → chụp theo HWND app không thấy nó;
lái bằng `SendKeys` gõ đường dẫn đầy đủ + `{ENTER}`, và xác nhận bằng ảnh cả màn hình.

## Kiến trúc
- **Entry**: `lib/main.dart` → `_AuthGate` khôi phục phiên đã lưu → `LoginScreen` (chưa đăng nhập)
  hoặc `HomeShell` (đã đăng nhập).
- **`HomeShell`** (`NavigationRail` dọc + `IndexedStack`): thanh dọc chỉ chứa **tab nội dung** theo vai
  trò — khách hàng (`user`): **Lịch sử**; nhân sự (`admin`/`root`): **+ Kỹ Thuật**; **`root`**: **+ Giám sát
  + Quản lý User**. Tab **Giám sát** (`monitor_screen.dart`, root-only, thêm 2026-08-28) đọc
  `GET /monitor` của Engineer Server: uptime dịch vụ · Postgres sống không · CPU/RAM/đĩa · phiên đo
  24h/7 ngày + biểu đồ 7 cột. Bố cục: **băng phán quyết** (một dòng, mức xấu nhất + LÝ DO) → thanh
  tài nguyên | chấm trạng thái → **biểu đồ CPU/RAM realtime** → luồng dữ liệu. (Danh sách "máy im
  lặng >7 ngày" ĐÃ GỠ 2026-08-28 theo yêu cầu chủ dự án — nó đầy máy đã ngừng dùng nên giữ lại là
  băng phán quyết vàng vĩnh viễn, che mất cảnh báo thật; muốn biết máy nào lâu không gửi thì xem cột
  "Lần cuối" ở tab Quản lý máy.) Lấy mẫu **5 giây/lần, CHỈ khi tab đang mở** (`TickerMode` — xem gotcha `IndexedStack`), vòng
  đệm 60 mẫu trong RAM; vòng đó gọi **`GET /monitor?flow=0`** để KHÔNG kéo theo 3 truy vấn quét bảng
  `sessions` mỗi 5 giây (server cũ chưa biết tham số này thì bỏ qua, vẫn trả đủ). Gác **hai tầng**:
  server `/monitor` dùng `Depends(ota_admin)` = chỉ token NHÂN SỰ
  (token thiết bị nằm trong 4 KB đầu mọi `.bin` nên KHÔNG được đọc `disk.free` rồi bơm `/ingest` cho
  đầy đĩa); còn "chỉ root" là quy ước GIAO DIỆN vì server không phân biệt root với admin (chung
  token) → đừng thêm dữ liệu nhạy cảm vào endpoint rồi tưởng "chỉ root" che được. Tab **Kỹ Thuật** (`tech_screen.dart`, mẫu segmented giống Lịch sử) GỘP 3 công cụ: **Log nhiệt**
  (`temperature_log_screen.dart`) | **Đọc serial** (`serial_console_screen.dart`) | **Nạp code**
  (`flasher_screen.dart`). **Thiết lập + Đăng xuất** KHÔNG còn là tab mà nằm trong **menu của icon tài khoản**
  (shield = nhân sự root/admin · person = khách hàng) ở `trailing`. Tab **Lịch sử**
  (`history_combined_screen.dart`) chỉ còn Cloud (admin có segmented chọn nguồn). Tab **Thư Mục**
  (`folder_screen.dart`, mọi vai trò, có cả trên web) — mục lớn duyệt dữ liệu dạng file, mẫu segmented
  giống Kỹ Thuật, hiện có 1 mục con **JSON data** (`json_files_screen.dart`): liệt kê mọi file JSON
  thiết bị đã đẩy lên **Engineer Server** — dùng `GET /sessions` KHÔNG lọc máy (`FbtApi.listRuns('')`
  phải **BỎ hẳn param `device`**; gửi `device=''` server lọc theo chuỗi rỗng → trả RỖNG); phân trang
  "Tải thêm", lọc `canSee`, bấm từng file → xem **JSON THÔ** pretty-print (`FbtApi.fetchSessionJson`
  = `GET /sessions/{id}`, KHÔNG parse thành đồ thị), có nút copy. Kèm nút mở file JSON lẻ trên máy
  (cũng xem thô). Màn chỉ dùng HTTP, KHÔNG `dart:io` → chạy được cả web. (`TestResult.fromLooseJson`
  đoán 3 shape data.json/payload/run cloud vẫn còn cho nơi khác dùng, test `test/loose_json_test.dart`.)
  Thiết lập đẩy như route (admin
  **1 màn DÙNG CHUNG** `user_settings_screen.dart` cho cả admin lẫn user (đồng bộ giống nhau;
  `settings_screen.dart` cũ KHÔNG còn dùng). **Thanh nav (rail) LUÔN HIỆN, rộng 64px chỉ-icon**, rê
  chuột vào thì bung 248px kèm nhãn; dựng bằng `Stack` + 2 `AnimatedPositioned` (KHÔNG `Row`).
  **Từ 2026-09-23 nội dung CO THEO rail**: mép trái vùng nội dung animate cùng nhịp (`_railAnim`
  220ms / `_railCurve`) nên bung ra không che gì. Bản trước inset nội dung cố định 64px để rail phủ
  lên (tránh `fl_chart` relayout) — nhưng 184px chênh nuốt mất thanh mục con + đầu dòng dữ liệu, đúng
  lúc người dùng đang rê chuột để đọc tên mục. **Cửa sổ hẹp thì giữ kiểu phủ lên**: đẩy chỉ khi
  `maxWidth - 248 >= _minContentWidth` (560) — dưới ngưỡng đó bảng số đo tab Hiệu chuẩn vỡ cột, tệ hơn
  bị che tạm lúc hover. Đổi `_slim`/`_open` thì đổi luôn `_minContentWidth`.
  **Nút THU/MỞ ở CHÂN rail** (`_RailFoot`, 2026-09-23) là nút DUY NHẤT đổi bề rộng — đặt cạnh dòng
  version theo mẫu sidebar hubOTA (`04.FBT-OTA`). Trạng thái là MỘT `bool _collapsed`: mở = rail đứng
  nguyên 248px và nội dung co theo; thu = 64px, **cứng**.
  ⚠️ **"Rê chuột vào rail thì bung" ĐANG TẠM ĐÓNG** (cờ const `kRailHoverExpand = false`, 2026-09-23
  theo yêu cầu chủ dự án — bật lại đổi thành `true`, code hover còn nguyên chỗ): bề rộng rail giờ CHỈ
  đổi bằng nút ở chân rail, đưa chuột ngang qua không làm bố cục nhảy nữa. Vì vậy **tooltip tên mục
  trong `_RailItem` là đường DUY NHẤT đọc tên mục khi rail thu — đừng bỏ**. `MouseRegion` cũng không
  `setState` khi cờ đóng (chuột đi ngang mà dựng lại cả cây widget là phí, nhất là tab có `fl_chart`). Icon chỉ **hướng SẼ ĐI** (`»` khi đang thu = bấm là
  mở), vẽ ngược là ai cũng bấm nhầm một lần. *(Nút ghim `_PinButton` ở ĐẦU rail và enum 3 nấc
  `_RailMode` là hai bản nháp cùng ngày, chủ dự án đã bác — hai nút cho một việc là rối. Đừng dựng lại.)*
  Nhớ giữa các lần mở app qua `shared_preferences` khoá **`rail_collapsed_v1`**, có đọc bù hai khoá nháp
  cũ (`rail_mode_v1` == `'open'`, `rail_pinned_v1` == true) để máy đang để rail mở sẵn không bị thu lại
  sau khi cập nhật; đọc/ghi THẲNG trong `_HomeShellState`, KHÔNG nhét vào `AppPrefs`
  (`AppPrefs.notifyListeners()` dựng lại `MaterialApp`, mọi màn đang mở mất vị trí cuộn chỉ vì một nút
  bố cục). **Mở thì LUÔN đẩy nội dung**, kể cả dưới `_minContentWidth`: ngưỡng đó để bảo vệ người rê
  chuột ngang qua, còn mở là lựa chọn chủ động — che nội dung vĩnh viễn mới là hỏng. Bấm THU phải tự hạ
  `_wide = false`: lúc bấm, con trỏ vẫn nằm trên rail nên `MouseRegion.onExit` KHÔNG bắn, không hạ tay
  thì rail kẹt bung và trông như nút hỏng.
- **Tab Quản lý máy** (`manager_machine_screen.dart`, nhân sự; chỉ HTTP nên chạy cả web): **Cập nhật
  OTA** (kho `.bin` trên Engineer Server: tải lên `PUT /ota/{file}` với tên `fbt_v<version>.bin`, chọn
  bản chung/ghim máy `PUT /ota/target/…`, xoá `DELETE`, **tải về** `FbtApi.downloadOta` = `GET /ota/{file}`
  bytes thô qua `_getBytes` (timeout riêng 3 phút) → `pf.saveBytesFileDialog` (desktop "Lưu thành…",
  web tải xuống; thêm 2026-09-04)) **| Trạng thái máy** (`/devices` + `fw-log`). Mọi thao tác ghi dùng
  PUT/DELETE vì server catch-all POST. **Server đã tách kho OTA THEO SẢN PHẨM** (2026-09-11,
  `server/app/ota.py`, giai đoạn 0 của `docs/plan/ota-nhieu-san-pham.md`): `products/<product>/`,
  máy không khai `?product=` → kho `OTA_LEGACY_PRODUCT` (`rapidplus`); **app hiện vẫn gọi route
  CŨ không `{product}`** (= kho legacy, chạy nguyên) — giai đoạn 2 app chuyển sang `GET /ota/products`
  + `/ota/{product}/…`, upload không gõ version (`PUT /ota/{product}`, server đọc thẻ `FBTIMG1`
  trong .bin); `/ota/check` trả thêm `ver/product/hw/reason`, `/devices` thêm `product/hw/product_effective`.
  Firmware giai đoạn 1 (chưa làm): nhúng thẻ + gửi `&product=&hw=` + đối chiếu manifest.
- **Tab Chăm sóc KH** (`support_screen.dart`, nhân sự, 2026-09-04; doc `docs/07-cham-soc-khach-hang.md`):
  mẫu segmented **Thông tin máy** (`support_info_screen.dart`, nội dung = dữ liệu ở
  `data/machine_info_content.dart`, tiếng Việt, KHÔNG qua `tr()`, **viết cho CSKH không nền kỹ
  thuật** — thứ tự sản phẩm → quy trình → kết quả → … → kịch bản → từ điển; mục `advanced: true`
  ("Dành cho kỹ thuật") thu gọn mặc định; chip "Đi tới" = `jump` + `GlobalKey`/`ensureVisible`
  nên danh sách là `SingleChildScrollView`+`Column`; kèm bộ tách mã lỗi 4 số `decodeErrorCode`)
  **| Xử lý sự cố** (`support_troubleshoot_screen.dart`: khung "Máy đang nói gì" mở đầu bằng
  **kết luận 1 dòng** đỏ/vàng/xanh (`sp.statusError/Warn/Ok`) rồi mới liệt kê dấu hiệu; nút
  "Yêu cầu máy" giấu lệnh thật vào tooltip; Kết nối USB → log theo
  dòng + **bộ quét `util/log_triage.dart`** (regex ESP32 → khoá i18n `triage.<key>`/`<key>Hint`,
  test `test/log_triage_test.dart`) → **Gửi log** `FbtApi.uploadDeviceLog` = `PUT /devices/{id}/logs`
  / `listDeviceLogs` / `fetchDeviceLog`). ⚠️ **Log CSKH là FILE `~/fbt_server/logs/<máy>_<UTC>_<sha8>.json`
  trên box (`FBT_LOGS_DIR`), KHÔNG vào Postgres → KHÔNG nằm trong `pg_dump`, chưa có backup/retention**;
  và **chỉ tra được THEO MÃ MÁY** (`GET /devices/{id}/logs`) — không có `GET /logs` chung, không thông
  báo, nên kỹ thuật không biết CSKH vừa gửi gì nếu không được nhắn (rà 2026-09-15: 1 bản `RPL01015`
  ngày 14/09 nằm đó). Kiểm nhanh: `ssh engineer@100.109.127.87 'ls ~/fbt_server/logs'`. Muốn "quản lý"
  thật thì thêm route liệt kê chung + mục Log CSKH trong tab Thư Mục + trạng thái xử lý (chưa làm). **MỘT màn cho cả desktop lẫn web** nhờ facade
  **`util/serial_link.dart`** (`export _io if (dart.library.html) _web`; kiểu chung ở
  `serial_link_types.dart`): `serialLinkCanListPorts` false trên web → bỏ ô chọn cổng, `openSerialLink`
  bật hộp thoại trình duyệt và trả `null` khi Hủy. Cổng COM dùng chung tab Kỹ Thuật → `HomeShell`
  dựng list tab bằng `add` để biết chỉ số và truyền `active: _index == supportIndex`; màn nhả cổng
  (giữ log) khi `active` → false. Lệnh nhanh CHỈ lệnh an toàn (`ParaRead`/`M`/`TemperatureOutput`/`Res`
  có xác nhận) — KHÔNG `P` (treo firmware).
- **Tab Hiệu chuẩn** (`calib_screen.dart` + `calib_batch_screen.dart` + `services/calib_api.dart`, 2026-09-21;
  kế hoạch + hợp đồng server `server/docs/plan/calib-ong-chuan.md`): ống chuẩn Fluorescein cho `eSensorcalib`
  — Lô pha (checklist pha C1V1=C2V2 tick = ghi giờ/người NGAY, bảng số đo ống × nồng độ, xếp hạng tổ hợp +
  LOD theo WI, gợi ý bộ không trùng ống, Đóng gói) · Bộ ống (stored → issued(SN) → used/discarded) · Ngưỡng
  (`canEditLimits`). Gác `canSeeCalib`/`canWriteCalib` (root/admin/manager/operator; `user` không thấy).
  Chỉ HTTP → chạy cả web, KHÔNG có bản `_web` riêng (analyze sạch + chạy thử web local 2026-09-21).
  ⚠️ **ĐỪNG so hai tổ hợp bằng R²** (2026-09-23, chủ dự án báo "R2 LUÔN LUÔN = 1"): với 4 điểm và dải
  x rộng, R² bão hoà ở 0,9999xx nên cả chục dòng đầu bảng xếp hạng in ra y hệt nhau. Bảng giờ có cột
  **Lệch** = `se` (sai số dư, đơn vị raw) — số KHÔNG bão hoà, dùng nó để so; R² hiển thị 6 chữ số
  (server đã bỏ làm tròn `r2` vì nó là khoá sắp xếp — xem `server/CLAUDE.md`). Cột `Lệch` là `null`
  khi server còn bản cũ → in `—`, đừng để `0`.
  **Nhãn QR** (`services/calib_label.dart` + `screens/calib_label_print.dart` + `widgets/qr_view.dart`,
  2026-09-23, test `test/calib_label_test.dart`): nút "Nhãn QR" trên thẻ bộ + "In nhãn QR" cho cả danh
  sách → tờ A4 10 nhãn. QR mang TEXT rời `FBTCAL1|mã bộ|lô|hạn|slope|intercept|R²|LOD|ống|ngưỡng`
  (không phải URL: kho lạnh không mạng, `/calib/sets/{id}` lại đòi token). **Chỉ bộ ĐẠT còn dùng được**
  mới in được — chi tiết + lý do ở `docs/history/2026-09-23-nhan-qr-ong-chuan.md`.
  **Đọc số thô TỰ ĐỘNG** (`services/calib_reader.dart`, THUẦN Dart qua `SerialLink`, test
  `test/calib_reader_test.dart`): panel "Đọc từ máy" ở thẻ 3 màn lô — nối cổng (COM desktop / Web Serial),
  chọn **Khe đọc 1–10** ngay trong panel (viền đỏ khi lô chưa ghi khe; chọn = ghi ô thẻ 1 + PUT `reader.slot`
  ngay — khe là dữ liệu truy vết), đặt ống → ĐỌC hoặc **Enter/Space** → gửi **ĐÚNG 1 byte** `'0'..'9'` (khe 1..10; firmware
  `ForteSetting::loop` chỉ vào `OptoCommandProcess` khi `recvLen == 1`, kèm `
` là thành lệnh 2 byte bị
  bỏ qua) → chờ dòng `{Green: N}` (fleet) hay `raw,calibrated` (Beta prototype WI) → điền ô đang chọn →
  **PUT ngay ô đó** → nhảy ống kế (`nextCalibCell`). Phím tắt bắt bằng `HardwareKeyboard.instance.addHandler`
  (bỏ qua khi focus đang ở `EditableText` hoặc route không phải current) chứ KHÔNG `CallbackShortcuts`+`Focus`:
  bản đầu dùng cách đó, sau khi bấm nút ĐỌC thì Enter không tới đâu — kỹ sư một tay cầm ống không thể
  "bấm vào panel cho có focus" trước mỗi lần đọc.
  **Firmware v2.4.6 (`firmware/FBT-RapidPlus/`, chế độ `eCalibTube`)**: Kết nối → `CalibStart,<khe>` +
  `CalibLabel,<ô đang chọn>` (ASCII ≤ 31, `calibLabelFor`), máy hiện LCD; **nút ĐỎ trên máy** in `{Green: N}`
  không ai hỏi → `CalibReader.readings` (chỉ phát khi KHÔNG có `readSlot` đang chờ — tránh điền hai lần) →
  `_applyReading` y như Enter; XANH trên máy → `{CalibSlot: n}` → `_setSlot(n, fromDevice: true)` (không gửi
  ngược, tránh vòng lặp); Ngắt/rời màn → `CalibEnd`. `readSlot` GIỮ 1 byte để fleet firmware cũ vẫn đọc
  được (nó trả "Command is not supported!" cho `Calib*`, vô hại).
  **Nghi "truyền số lên app lỗi" thì kiểm theo thứ tự** (2026-09-22): (1) `GET /calib/batches/<id>` trên server
  (`readings.<conc>.<ống>`) — mỗi ô PUT ngay lúc đọc nên số trên server = số app đã nhận; (2) muốn bắt log thô
  máy thì app phải **Ngắt** trước — cổng COM bị Web Serial giữ, pyserial báo `PermissionError(13, 'Access is
  denied')`; (3) **ô ghi đúng số = byte khe (khe 4 → 3) là số GIẢ**: firmware gom byte tới trong 1 s vào một
  buffer rồi echo nguyên buffer — Enter ngay sau `CalibLabel` (app gửi sau mỗi lần đọc) → máy in dòng `3` →
  parser số trần nhận làm số đo (11/40 ô một lô, 2026-09-22). Nay `readSlot` khi `modeOn` gửi `CalibShot` và
  CHỈ nhận `{Green: N}`; số trần/`raw,calibrated` chỉ còn cho Beta prototype. Firmware ≥ aaee4c8 cũng không echo
  buffer `Calib*` nữa. **Đừng nới parser nhận số trần trong chế độ** dù log máy trông "sạch".
  **Màn viết cho NHÂN VIÊN KHÔNG CHUYÊN, không phải cho kỹ sư** (2026-09-22): đầu màn có thanh 4 việc +
  ô "Việc tiếp theo" nói bằng lời thường (suy từ dữ liệu lô trong `_nextAction`, bấm là cuộn tới thẻ đó);
  mỗi thẻ chỉ MỘT câu hướng dẫn, mọi công thức/ngưỡng/giao thức nằm trong `tech:` của `_card()` = khối
  "Chi tiết kỹ thuật" gập sẵn; nút ĐỌC cao 56 px kèm câu "Đặt ống <nồng độ · số> vào khe <n>"; bộ gợi ý
  đọc là "300 nM số 3, 200 nM số 5" (`_tubesPlain`) chứ không phải `300/3 · 200/5`. **Thêm gì vào màn này
  thì đặt số liệu kỹ thuật vào `tech:`, đừng nối thêm vào `hint:`** — hint dày biệt ngữ chính là thứ vừa
  gỡ. Chi tiết: [docs/history/2026-09-22-tab-hieu-chuan-cho-nguoi-khong-chuyen.md](docs/history/2026-09-22-tab-hieu-chuan-cho-nguoi-khong-chuyen.md).
  **Nút TRẮNG trên máy giữa lô** (2026-09-22): máy in `{CalibMode: off}` về màn chính; app cũ vẫn gửi `CalibLabel`
  sau mỗi ô → máy trả `notInMode` → hiện "Máy từ chối đọc/Máy báo: notInMode" cho mọi ống kế. Nay `CalibReader`
  nhớ `_modeSeen`: máy đã thoát thì `setLabel` chỉ ghi nhớ, `readSlot` kế gửi **một gói** `CalibStart,<khe>` +
  `CalibLabel,<nhãn cuối>` + `CalibShot` (firmware tách dòng) và chỉ chờ `{Green}`; màn nghe `modeChanges`
  để báo "bấm ĐỌC là máy vào lại" thay vì lỗi. Test `nút TRẮNG trên máy ({CalibMode: off})…`.
- **Tab Sản xuất (ATE)** (`ate_screen.dart`, nhân sự, 2026-09-07; doc `docs/08-tram-san-xuat-ate.md`,
  kế hoạch đầy đủ `docs/plan/ate-san-xuat.md`): trạm nghiệm thu máy ở xưởng, **pha P0** = nạp → khai
  sinh → hồ sơ. Mẫu segmented **Chạy trạm | Hồ sơ máy | Thống kê**. Ba lớp tách BẠCH, đừng trộn:
  **`services/ate_runner.dart`** = kịch bản, **THUẦN Dart** (không Flutter/dart:io — mọi thứ chạm phần
  cứng qua interface `AteStation`; có test `test/ate_runner_test.dart` với máy giả) ·
  **`services/ate_station_io.dart`** = bản thật (esptool `Process.start` + `util/serial_link.dart`) ·
  **`services/ate_api.dart`** + **`ate_queue_io.dart`** = hồ sơ (ghi file `FBT_RAPID_ate\cho_gui\`
  TRƯỚC, `PUT /ate/records` SAU, mạng sống lại thì flush). 5 bước P0: `FW-02` (flash_id → chip/flash/MAC)
  → `FW-01` (write_flash + `verify_flash` + **sha256 của .bin** bằng `util/sha256.dart` tự viết, KHÔNG
  thêm package `crypto`) → `BOOT-01` (nghe UART, quét bằng `util/log_triage.dart` dùng chung tab CSKH)
  → `ID-01` (ghi số máy rồi **`ParaRead` đọc lại đối chiếu**) → `ID-02` (tham số lô). **P1 thêm 6 bước
  tự kiểm** (2026-09-07): `OPT-01` (10 cảm biến quang — `GET /errors` nếu máy có IP, chưa có mạng thì
  lùi về UART `R`) · `OPT-03` (tín hiệu sáng từng slot: gửi `0`–`9` **MỘT slot một lần**) · `TMP-01`
  (6 kênh nhiệt qua `TemperatureOutput`, parser `parseTempSamples` sao y `temperature_serial`) ·
  `FAN-01`/`BUZ-01`/`HMI-01` (gửi lệnh rồi hỏi người vận hành qua hook `confirm`). **Tiêu chuẩn đặt theo TỪNG LÔ
  SẢN XUẤT** (2026-09-07): `GET|PUT /ate/limits?batch=` + `GET /ate/limits/list`; server lùi dần **bộ
  của lô → bộ chung → mặc định** và trả `source` để màn trạm cảnh báo khi lô đang chạy chưa có bộ riêng.
  Hồ sơ mang `batch` (lọc được ở `/ate/records`, `/ate/stats`) và `limits_ver`. Màn đặt ngưỡng:
  `ate_limits_screen.dart` (mục Tiêu chuẩn), gác `canEditLimits` = nhân sự kỹ thuật; có **nhập/xuất
  JSON** (một bộ · một bộ kèm `batch` · nhiều lô qua `batches`/`items`) — bộ đọc file là hàm THUẦN
  `parseAteLimitsImport` trong `models/ate_record.dart` (test `test/ate_limits_import_test.dart`),
  khoá lạ được GIỮ và gửi lại khi lưu. Bản web
  **Trạm chạy được CẢ TRÊN WEB** (2026-09-08): `services/ate_station.dart` là facade
  `export _io if (dart.library.html) _web` — desktop = esptool + libserialport, web = **esptool-js +
  Web Serial**; hàng đợi cũng có facade `ate_queue.dart` (web = localStorage, lược log, trần 60 hồ sơ).
  **MỘT màn Chạy trạm** dùng chung, không có bản `_web` riêng. Ba khác biệt web phải nhớ: firmware lấy
  từ **kho OTA của server** (`fetchBin` → `GET /ota/{file}`, `AteBinPart.path` là TÊN file chứ không
  phải đường dẫn), **chưa verify được** sau nạp (FW-01 hạ xuống `info`, xem `AteFlashResult.verified`),
  và `http://<ip>` tới máy bị chặn mixed content nên OPT-01 lùi về UART.
- **Phiên/phân quyền**: `services/session_store.dart` giữ `UserSession` **toàn cục** qua static
  `SessionStore.current` (giống pattern `StoragePaths`). **3 vai trò GỐC** (`UserRole` — nay là 5, xem bullet ngay dưới): `root` (Root) /
  `admin` (Nhân viên) / `user` (Khách hàng) — tên hiển thị qua `roleLabel()` (i18n). `canWrite` = nhân sự
  (`isStaff` = root+admin); `canManageUsers` = **root** (quản lý tài khoản **root-only**, gate
  `requireAdmin_` trong userAuth.js yêu cầu role==="root"). **Phạm vi XEM máy = `allowAll`** (`canSee()`):
  `allowAll` CHỈ true khi **root** (super-admin) HOẶC `ids` chứa `"*"`. **Admin (nhân viên) KHÔNG còn auto
  thấy mọi máy** — lọc theo `ids` được cấp như khách hàng (chỉ `*` mới full). `isStaff` giờ chỉ quyết định
  **canWrite** + hiện tab Kỹ Thuật, KHÔNG quyết định phạm vi xem. Lọc dữ liệu enforce **ở client**
  (`cloud_devices`/`history` fetch hết rồi `canSee`); `UserSession.fromJson` TỰ tính `allowAll` (không tin
  `allowAll` backend cũ). Màn con đọc `SessionStore.current`/`canWrite` để lọc + ẩn nút.
- **5 vai trò từ 2026-09-07** (thêm 2 vai trò XƯỞNG — `docs/plan/tai-khoan-nha-may.md`): `root` ·
  `admin` (nhân viên kỹ thuật/CSKH) · **`manager`** (quản lý sản xuất) · **`operator`** (thao tác viên) ·
  `user` (khách hàng). Quyền đọc theo **TÊN VIỆC**, không theo chức danh — `UserSession`/`SessionStore`
  có `canSeeClinical` · `canWriteClinical` · `canSupport` · `canUseTech` · `canWriteOta` · `canSeeOta` ·
  `canSeeProduction` · `canSeeProductionStats` · `canRunStation` · `canManageUsers` +
  `canManageRole(target)`. `canWrite` GIỮ làm **alias** của `canWriteClinical` (21 chỗ gọi cũ) — code mới
  đừng dùng nó. Ma trận đầy đủ có test: `test/user_session_test.dart`.
  ⚠️ Ba luật dễ quên: (1) **người của xưởng KHÔNG xem dữ liệu lâm sàng** (chủ dự án chốt) → tab Lịch sử
  biến mất với họ, tab đầu tiên là Sản xuất; (2) **hồ sơ ATE KHÔNG lọc theo `ids`** — máy vừa ra khỏi
  chuyền chưa cấp cho ai, dùng `canSeeProduction` chứ đừng dùng `canSee`; (3) `roleFromCode` quy vai trò
  lạ về `user` (fail-closed) → **cập nhật app trên máy trạm TRƯỚC khi tạo tài khoản manager/operator**,
  không thì họ đăng nhập vào chỉ thấy Lịch sử.
- **Token API phát theo vai trò** (`server/app/auth.py::api_token_for`): root/admin → `OTA_ADMIN_TOKEN`
  (ghi được OTA); **manager/operator/user → `TOKEN` thiết bị** (đọc + ghi hồ sơ ATE, KHÔNG arm được
  firmware cho fleet). Đổi vai trò một tài khoản thì **phải đăng xuất/đăng nhập lại** mới đổi token —
  quyền cũ còn nguyên trong máy cho tới lúc đó.
- **Nguồn dữ liệu** (mỗi nguồn 1 service):
  - `device_api.dart` — HTTP `GET /getdata` tới IP máy trong LAN (tab Lịch sử).
  - **3 nguồn cloud, 1 giao diện chung** `CloudHistoryClient` (trong `cloud_history_api.dart`):
    màn `cloud_devices`/`cloud_runs` chỉ phụ thuộc interface; chọn lớp triển khai qua factory
    **`buildCloudClient(settings, source)`** (trong `rapid_erp_api.dart`) theo `enum CloudSource
    {google, rapidErp, engineer}` (`app_settings.dart`, helper `cloudUrlFor/cloudHeadersFor`).
    Thêm nguồn (checklist THỰC TẾ, đã làm với `engineer` 2026-07): enum + nhánh
    `cloudUrlFor`/`cloudHeadersFor` (`app_settings.dart`) + nhánh factory + `ButtonSegment`
    (`history_combined_screen.dart`) + case hint chưa-cấu-hình (switch trong `_buildBody`
    `cloud_devices_screen.dart` — switch enum Dart bắt exhaustive, thiếu case là lỗi
    compile) + field/persist + ô nhập Cài đặt + key i18n. Logic màn cloud KHÔNG phải sửa.
  - `fbt_api.dart` — nguồn **`engineer` = Engineer Server** (thêm 2026-07): **FBT Home Server** của
    kỹ sư (FastAPI, source ở `../Server/app.py` — project RIÊNG cạnh `app/`, expose qua Tailscale
    Funnel, mặc định `kDefaultEngineerUrl = https://fbt.basa-luma.ts.net`). REST: `GET /devices`
    (id_device/sessions/last_seen) · `/sessions?device&page&limit` (phân trang **page 1-based**,
    app quy đổi từ offset) · `/sessions/{id}` (payload firmware gốc, KHÔNG amplification) ·
    `/sessions/{id}/amplification` (`slots[].points` = mảng số server đã parse → `curvesAreRaw`).
    Auth `Authorization: Bearer <RECEIVER_TOKEN>`: admin nhập ở Cài đặt, HOẶC nạp lúc build
    `--dart-define=FBT_TOKEN=…` (`kDefaultEngineerToken`); token thật chỉ nằm trong
    `/etc/fbt-receiver.env` trên server box (token trong `Server/note.md` đã LỘ và bị xoay).
    GOTCHA: item `/sessions` cần cả `payload->'result'` (chữ P/N/S/E) — đã thêm vào `app.py`
    2026-07-11, server phải **redeploy** mới có; app parse phòng thủ (thiếu `result` → phân loại
    "?" ở danh sách, mở chi tiết vẫn đủ).
  - `cloud_history_api.dart` — `GET` (ids/runs/run) đọc lịch sử cloud Google Apps Script `doGet`
    (Drive). Khác nguồn RAPID ERP chỉ ở **baseUrl + field `headers`**.
    Tab Cloud có **segmented chọn nguồn** (`history_combined_screen.dart`) **chỉ hiện cho admin**
    (`SessionStore.canWrite`) — **KHÔNG bắt buộc nhập key** (admin luôn thấy; chưa có key → API RAPID
    ERP có thể trả 401). Admin nhập URL+key (tuỳ chọn) ở `user_settings_screen.dart`.
  - `rapid_erp_api.dart` — nguồn cloud **THỨ 3 = RAPID ERP** (server NGOÀI repo, REST, header
    **`X-API-Key`**; mặc định `kDefaultRapidErpUrl`). Hợp đồng KHÁC Apps Script: `GET /external/device/
    {id}/results?limit&offset` (`items[]`, mỗi item `result_codes` = map `{"0".."9":"22.3 | N"}`) +
    `GET /external/results/{id}/detail` (10 kênh + `amplification_data`). Doc: `docs/api-guide-external-vi.md`.
    **GOTCHA — KHÔNG có endpoint liệt kê máy** (`/external/*` chỉ theo từng mã). Nguồn danh sách máy
    của `listDevices` (ưu tiên giảm dần): (1) **`rapidErpDeviceIds`** — admin **dán danh sách mã máy**
    ở Cài đặt (`AppSettings.rapidErpDeviceIdList`; hydrate runCount theo **lô 8**, >60 máy thì bỏ
    hydrate); (2) fallback `SessionStore.current.ids`; (3) **ô gõ mã máy thủ công**
    (`_manualEntryBar`/`_openManual` ở `cloud_devices`). Tất cả vẫn gate `canSee` (root/`*` mọi mã;
    admin/khách chỉ mã được cấp). Muốn AUTO lấy TOÀN BỘ máy thì phải đăng nhập ERP — OpenAPI public ở
    `https://api.fortebio.tech/openapi.json`; list ở `/api/device-registry/devices` | `/api/portal/devices`
    nhưng dùng **OAuth2 `/api/auth/login`** (KHÔNG phải `X-API-Key`) → đã CHỐT KHÔNG làm (không lưu mật khẩu ERP). **Shape `/detail` ĐÃ XÁC MINH bằng data thật
    (2026-06-29)**: `{… , channels:[{channel_index, ct_value (số), result_code "08.0 | N",
    calibration_slope, amplification_data:[số,…], peak_*…}]}` → nhánh đầu `_channelMaps` (khoá
    `channels`) khớp đúng, `amplification_data` non-empty ⇒ `curvesAreRaw=true` (vẽ 4 đồ thị như Google).
    Các nhánh fallback (dạng cột / `raw_payload`) chỉ là lưới an toàn. **Server HIỆN KHÔNG bắt buộc
    `X-API-Key`** (gọi không key vẫn 200) — vẫn nên nhập key khi server bật xác thực.
    Test nhanh ngoài app: `curl ".../external/device/<id>/results?limit=5"`.
  - `temperature_serial.dart` — UART/COM qua `flutter_libserialport`, đa cổng (tab Nhiệt độ).
  - `auth_api.dart` — tài khoản/đăng nhập. **ĐÃ CHUYỂN về Engineer Server** (2026-07-14):
    `AuthApi.engineer()` → `POST {engineerUrl}/auth` kèm Bearer (bảng `users` Postgres,
    `../Server/app/auth.py`) — server nói ĐÚNG hợp đồng JSON userAuth.js nên client giữ nguyên;
    3 call site (login/thiết lập/quản lý user) đều qua factory này. Apps Script `userAuth.js` +
    `kDefaultAuthApiUrl` chỉ còn là đường lùi khẩn cấp (đổi lại trong `AuthApi.engineer`).
    LƯU Ý: login CẦN token Engineer Server → build phát hành phải nạp
    `--dart-define=FBT_TOKEN=…` (hoặc user nhập ở Cài đặt); server phải deploy `/auth` và
    import tài khoản (xem `Server/docs/history/2026-07-14.md`) TRƯỚC khi phát hành app.
- **Lưu file**: gốc = `StoragePaths.parent` (static, set từ Cài đặt, mặc định `Documents`).
  Kết quả CT → `FBT_RAPID_ketqua\`; log nhiệt → `FBT_RAPID_templog\`; log đọc serial → `FBT_RAPID_seriallog\`
  (`<COM>_<thời gian>.txt`). Mở thư mục/chọn file = `Process.run('explorer.exe', ['/select,', path])`.
  **Tải hàng loạt theo máy** (nút ⤓ trên thẻ máy, `download_device_dialog.dart` + `BulkExport`
  trong `result_export.dart`) — cả mẻ đi vào **MỘT nơi** tên `<MãMáy>_toanbo_<ngày>`:
  desktop là **thư mục** `FBT_RAPID_ketqua\<MãMáy>\<MãMáy>_toanbo_<ngày>\`, web là **MỘT file
  `.zip`** cùng tên (`package:archive`, thêm 2026-08-26 — trước đó mỗi file một lượt tải, chọn 50
  lần đo là trình duyệt hỏi 50 lần). Cây bên trong GIỐNG NHAU ở hai nền tảng:
  **JSON** → mỗi lần đo một file `<Ngày>_<Giờ>_<Firmware>.json` (chủ dự án đã bác kiểu dồn 1 file
  gộp); **Ảnh đồ thị** → mỗi lần đo một **thư mục con** `<Ngày>_<Giờ>_<Firmware>\` chứa 4 PNG +
  `data.json`. Đường dẫn trong `ResultExport.chartEntries` ngăn bằng **`/`** (bản desktop tự đổi
  sang `\`) — dùng `\` là hỏng zip. Tên giờ pad 2 chữ số để **sắp theo tên = sắp theo thời gian**;
  trùng tên thì thêm `_2/_3` (trùng giây + trùng firmware là ĐÈ MẤT bản ghi mà không báo gì).
  ⚠️ **File JSON tải về là LOG NGUYÊN BẢN máy đẩy lên server** (`GET /sessions/{id}` =
  `FbtApi.fetchSessionJson`), KHÔNG phải `runToJson` app tự dựng — chỉ in thụt lề. Nguồn Google /
  RAPID ERP không có endpoint trả payload thô nên đành rơi về bản app dựng; đừng "thống nhất" hai
  đường này. Nút "Lưu" ở màn chi tiết (`saveRun`) vẫn giữ chỗ cũ `<MãMáy>\<Ngày_Giờ_Fw>\`.
- **Đồ thị** (`fl_chart`): `widgets/ct_chart.dart` (CT), `widgets/temp_chart.dart` (nhiệt). Lưu ảnh =
  bọc `RepaintBoundary` rồi `util/chart_capture.dart::captureBoundaryPng` (chụp off-screen qua Overlay).

## Web (build trình duyệt — thêm 2026-07)

- **Phạm vi**: đăng nhập + Lịch sử cloud + đồ thị CT + tab Thư Mục (JSON data) + **tab Kỹ Thuật
  đầy đủ 3 công cụ qua Web Serial** (mục dưới). Ẩn trên web: mục "Nơi lưu file" trong Cài đặt.
- **Tab Kỹ Thuật TRÊN WEB (2026-07-14)** — `tech_screen_web.dart` không còn là stub: đủ **Log nhiệt
  | Đọc serial | Nạp code** bằng **Web Serial API** (binding tự viết `util/web_serial.dart`,
  dart:js_interop) + **esptool-js** (bundle chính thức Espressif, binding `util/esptool_js.dart`).
  CHỈ chạy Chrome/Edge DESKTOP (Firefox/Safari/mobile → màn cảnh báo `webSerialSupported`); mỗi lần
  kết nối user tự chọn cổng trong hộp thoại browser (không liệt kê cổng tự động được). 3 màn web
  (`web_temp_log_screen` / `web_serial_console_screen` / `web_flasher_screen`) VIẾT TÁCH RIÊNG bám
  spec desktop (KHÔNG đụng code serial desktop): parse TimeRT/TimeRB + TemperatureOutput y hệt,
  console đa cổng + HEX + cap buffer y hệt, flasher offsets/flash mode/size + monitor-sau-nạp y hệt
  (khác: "Dừng" = ngắt cổng vì không có process để kill; "Lưu" = tải Downloads). GOTCHA giữ nguyên
  từ desktop: sau `port.open()` PHẢI tắt DTR/RTS — không set là Chrome bật DTR/RTS → ESP32 auto-reset
  (đã ghim trong `WebSerialPort.open`). **Nhưng tắt gộp một lệnh `setSignals({dtr:false, rts:false})`
  VẪN reset** (2026-09-22, máy vẫn reboot mỗi lần Kết nối dù code "đã tắt"): Chromium thực thi thành
  CLRDTR rồi CLRRTS, mà EN bị kéo xuống đúng lúc *RTS còn bật, DTR đã tắt*. Đo Rapid+ CH340: tắt DTR
  trước → reset 3/3, tắt RTS trước → 0/3, mở cổng với cả hai bật → 0/15. Nay `open()` gọi **hai lệnh:
  `setSignals(rts:false)` rồi `setSignals(dtr:false)`** — đừng gộp lại. Script đo:
  `firmware/FBT-RapidPlus/tools/dtr_probe.py`.
  **esptool-js vendor**: bundle 1 file IIFE bằng esbuild (`npm i esptool-js esbuild` →
  `npx esbuild entry.mjs --bundle --format=iife --global-name=esptoolJS --minify`) → `web/esptool.js`
  kèm thẻ `<script src="esptool.js" defer>` trong `web/index.html`; nâng version esptool-js thì làm
  lại bundle. `TempSample`/`kTempChannels` đã TÁCH ra `services/temp_types.dart` thuần để
  `temp_chart.dart` (tái dùng nguyên trên web) không kéo dart:ffi.
- **Cơ chế cắt native** (build web KHÔNG được kéo dart:io/dart:ffi vào import graph):
  (1) facade **`util/platform_files.dart`** `export ..._io.dart if (dart.library.html) ..._web.dart`
  — mọi thao tác file/thư mục đi qua đây (`storage_paths`/`result_export`/`user_settings` đã dùng,
  KHÔNG import dart:io trực tiếp trong file dùng chung nữa); bản web "lưu" = **tải xuống Downloads**
  (`ResultExport.saveRun` trả `''` → caller ẩn nút "Mở" snackbar). (2) conditional import màn hình:
  `tech_screen.dart if (dart.library.html) tech_screen_web.dart` (home_shell) — bản web cùng tên
  class + constructor. Màn nào chỉ HTTP thì KHÔNG cần bản `_web`. File web-only (`web_*.dart`,
  `util/web_serial.dart`, `util/esptool_js.dart`) import trực tiếp `platform_files_web.dart` được.
- **DEPLOY WEB giờ chạy `deploy-web.ps1`** (gốc repo), ĐỪNG gõ scp tay nữa: gọi TRỰC TIẾP thì mặc định
  là **chạy thử** (in danh sách file khác md5, không đụng server), thêm `-Go` mới chép.
  ⚠️ **NHƯNG gọi qua wrapper `server\scripts\deploy.ps1 -Web` thì CHÉP THẬT NGAY** — wrapper tự truyền
  `-Go`, muốn chạy thử phải thêm **`-DryRun`** (2026-09-25: tôi nói với chủ dự án "đây là chạy thử" rồi
  nó deploy luôn). Hai script, hai mặc định NGƯỢC nhau — đọc kỹ đang gọi cái nào. Script tự lo hết những chỗ
  từng hỏng: chỉ chép file THẬT SỰ khác (6 MB thay vì 43 MB), `scp -O` từng file, sao lưu
  `web.bak.<stamp>` trước, kiểm quyền ghi TRƯỚC khi đụng gì, gắn vân tay tên file
  (`main.<hash>.dart.js`) để qua cache Cloudflare 4 tiếng, và md5 lại sau khi chép.
  ⚠️ **Vân tay MỚI CHE ĐƯỢC 3 FILE** (đo 2026-09-25): `main.<hash>.dart.js`,
  `flutter_bootstrap.<hash>.js`, `favicon.<hash>.png`. Bản build còn **20 file tĩnh TÊN CỐ ĐỊNH** mà
  app nạp lúc chạy — `canvaskit/*.{js,wasm}` (14), `assets/assets/fonts/*.ttf` (3 font thương hiệu),
  `assets/fonts/MaterialIcons-Regular.otf`, `esptool.js`, `flutter.js` — và CF ghi đè `no-cache` của
  origin thành `max-age=14400` cho chúng. Nên **nâng Flutter SDK / đổi font thương hiệu / nâng bundle
  esptool là lại dính 4 giờ**, im lặng. Đừng tưởng vân tay đã xử lý xong; cách dứt điểm là Cache Rule
  ở Cloudflare — phương án đo sẵn: `server/docs/plan/cloudflare-cache-app.md`.
  **Từ 2026-09-23 đọc `build\web_prod`** (tham số `-WebDir`; `build\web` là bản test local) và
  `server\scripts\deploy.ps1 -Web` GỌI script này — hai đường deploy web từng tách nhau, đường
  của server chép thẳng `main.dart.js` không hash nên tab mới "không thấy" tới 4 giờ.
  **`flutter_bootstrap.<hash>.js` ĐỔI HASH mỗi lần build dù `main.dart.js` giống hệt từng byte**
  (2026-09-25) — bootstrap nhúng đường dẫn + dấu build. Nên **đừng lấy hash bootstrap làm bằng chứng
  "app đã đổi"**; muốn biết prod có đúng mã nguồn hiện tại không thì `flutter build web --output
  build/web_prod` rồi so **md5 `main.dart.js`** với bản tải từ `…/app/main.<hash>.dart.js` (trùng =
  prod đúng HEAD từng byte). Grep chuỗi tiếng Việt trong bundle để kiểm tính năng là VÔ NGHĨA —
  dart2js escape non-ASCII (gotcha "dò bằng ASCII" bên dưới).
  Chạy xong nhớ 2 việc script in ra: nhờ **purge Cloudflare** `https://hub.fortebio.tech/app/*`
  (chưa purge thì người dùng vẫn thấy bản CŨ, không báo lỗi gì) và dọn file vân tay cũ trên box.
  ⚠️ **Build web PHẢI chạy bằng PowerShell hoặc `MSYS_NO_PATHCONV=1`**: qua Bash (Git Bash) thì
  `--base-href /app/` bị MSYS dịch thành `C:/Program Files/Git/app/` → lỗi
  "*--base-href should start and end with /*".
- **HOST bản web trên chính Engineer Server (2026-07-14)**: build
  `flutter build web --release --base-href /app/` (KHÔNG `--dart-define=FBT_TOKEN` — token nhúng
  vào JS public là LỘ) rồi scp nguyên `build\web\*` vào `~/fbt_server/web/` trên box → server mount
  tĩnh tại **`https://fbt.basa-luma.ts.net/app/`**. GOTCHA redeploy (OpenSSH 9+ trên Windows dùng SFTP):
  `scp -r build\web\*` vào thư mục ĐÃ CÓ dễ **"stat remote/Permission denied"** + lồng `assets/assets`;
  cách chắc: `scp -O` (cờ legacy protocol) + scp **NỘI DUNG** (`build\web\assets\*` vào `web/assets/`)
  thay vì cả thư mục. Nếu lần scp lỗi để thư mục `web/assets`/`canvaskit` mode `dr-x` (mất quyền ghi) →
  `ssh … "chmod -R u+rwX ~/fbt_server/web"` (đừng `rm -rf` production — classifier chặn, đúng). Font
  Flutter web nằm ở `web/assets/assets/fonts/` (double — pubspec path `assets/fonts/` bị prefix `assets/`).
  **Cùng origin với API → hết CORS** cho
  /auth + /sessions + /devices (nguồn Engineer + mục JSON data chạy được trên web). Token cho web:
  `/auth` KHÔNG cần Bearer; **login thành công server trả `apiToken`** → `AuthApi.login` tự lưu vào
  `engineerToken` (Cài đặt) nếu đang trống.
- **CORS (ĐÃ XÁC MINH 2026-07-13)**: Apps Script trả `Access-Control-Allow-Origin: *` cho cả POST
  login lẫn GET ids → nguồn **Google chạy nguyên bản trên web**. `auth_api` sẵn POST `text/plain`
  (không preflight); vòng redirect thủ công KHÔNG chạy trên web (browser tự follow 302) — đừng "sửa".
  **RAPID ERP vẫn bị browser chặn** tới khi server đó bật CORS (Engineer Server thì đã hết vấn đề
  nhờ cùng origin — bullet trên).
- **Wasm KHÔNG build được** (flutter_libserialport kéo dart:ffi vào dependency) — chỉ build JS mặc
  định; `platform_files_web` dùng `dart:html` (deprecated — đổi `package:web` khi nào cần wasm).
- `.gitignore` dùng **`web/*` + trừ `!web/index.html` `!web/esptool.js`** (pattern `web/` cả thư mục
  thì KHÔNG trừ con được — quirk của git): 2 file sửa tay/vendor này giờ ĐƯỢC version, các file
  `flutter create` sinh khác vẫn ignore. Sinh lại `web/` thì title/manifest + thẻ script esptool
  trong index.html sẽ bị ghi đè — lấy lại từ git.

## Backend Apps Script (LEGACY — file ở `legacy/sheet/` gốc monorepo; getData.js còn được fleet cũ gọi)
- `getData.js` — `doPost` (firmware đẩy kết quả) + `doGet` (app đọc lịch sử: ids/runs/run/peek).
- `userAuth.js` — accounts/auth, web app + Google Sheet **riêng**, `doPost {action: login | changePassword
  | changeEmail | listUsers | saveUser | deleteUser}`. Schema tab `Accounts`:
  `username|password|role|ids|name|email|active` (cột `email` tuỳ chọn). Các action **admin** (listUsers/
  saveUser/deleteUser) yêu cầu `adminUser`+`adminPassword` MỖI LẦN. Vì `SessionStore` KHÔNG lưu mật khẩu
  → màn **Quản lý User** (`user_management_screen.dart`) **hỏi lại mật khẩu admin** (chỉ giữ trong RAM).
- URL `/exec` gắn trong `lib/services/app_settings.dart`: `kDefaultCloudApiUrl`, `kDefaultAuthApiUrl`.
  Đổi deploy → sửa hằng → build lại.
- **Sửa script → phải Deploy lại** (Manage deployments → Edit → New version) thì `/exec` mới cập nhật.

### Engineer Server (`server/` ở GỐC monorepo) — backend CHÍNH của app

> Mục cũ ở đây tả `server/` là "Docker + Node/Express, app KHÔNG gọi" — **sai từ 2026-07**. Thực tế:
> `server/` là **FBT Home Server = Engineer Server** (Python **FastAPI + PostgreSQL 17**, systemd
> `fbt-receiver` cổng 8080 trên MiniPC Debian, public qua Tailscale Funnel `fbt.basa-luma.ts.net` và
> Cloudflare Tunnel `hub.fortebio.tech`), và app gọi nó rất nhiều: `POST /auth` (đăng nhập, cấp
> `apiToken` theo vai trò) · `/devices` `/sessions` (lịch sử, nguồn `engineer`) · `/ota/*` (tab Quản lý
> máy) · `/ate/*` (tab Sản xuất) · `PUT /devices/{id}/logs` (CSKH) · `/monitor` (tab Giám sát).
> Kiến trúc, route, deploy, gotcha server: **`server/CLAUDE.md`** và `server/README.md`. Bản port
> Cloudflare Workers (`legacy/server-cf/`, chưa deploy) và bộ Docker Node/DuckDNS cũ KHÔNG còn dùng.
- Luật dùng chung app↔server hay quên: (1) **mọi thao tác ghi dùng PUT/DELETE** — `POST /{path}`
  catch-all nuốt hết POST thành payload thiết bị; (2) route chưa deploy trả **405** chứ không 404;
  (3) token nhân sự (`OTA_ADMIN_TOKEN`) mở mọi route, token thiết bị (`RECEIVER_TOKEN`) chỉ đọc + ghi
  hồ sơ ATE; (4) app lọc quyền xem máy ở CLIENT (`canSee`), server không phân biệt vai trò ở route Bearer.
- Test local cả bộ (Postgres portable + server + web): `server\scripts\localtest.ps1` (mục Gotchas
  "Công thức TEST LOCAL"). Deploy: `server\scripts\deploy.ps1 -Server -Web` (bản web lấy từ
  `apps/fbt_rapid/build/web_prod`).

## Quy ước
- **Comment & UI bằng tiếng Việt.** Giữ nguyên phong cách này khi sửa.
- Cấu hình/persist: `shared_preferences` (xem các khoá `_k...` trong `app_settings.dart`,
  `session_store.dart`). Singleton static cho trạng thái toàn cục (`StoragePaths`, `SessionStore`).
- Khi thêm hành động **ghi** (Lưu/Đồng bộ/Xóa/Lấy-từ-máy): bọc bằng `if (SessionStore.canWrite)` để
  user read-only không thấy.
- Lọc theo quyền: `UserSession.canSee(deviceId)` (admin/allowAll = thấy hết). Lưu ảnh đồ thị dùng
  `SessionStore.canSaveCharts` (mọi user), KHÁC `canWrite` (admin: đồng bộ/xóa/lấy-từ-máy).
- **Theme + ngôn ngữ**: `AppPrefs.instance` (ChangeNotifier toàn cục, lưu `shared_preferences`); chuỗi UI
  mới dùng `tr('key')` (`util/i18n.dart`, fallback tiếng Việt). `MaterialApp` bọc trong
  `AnimatedBuilder(animation: AppPrefs.instance)`.
- **Design system (2026-07, port từ `UI_UX_FLUTTER.md` web → Flutter Material 3)**: TẤT CẢ ở
  **`lib/theme/app_theme.dart`** — top-level const `kNavy #0A1F47` (primary) / `kAccent #E0A63A`
  (amber, = tertiary) / `kBg #F1F2F5` / `kCard #FFFFFF` / `kBorder #E7E8EB` / `kMutedFg #7C8088` /
  `kError #D6483B` / `kSuccess #2E9E6B` / `kInfo #3E7BC7`; `AppRadius` base 12 / card 16;
  `kCardShadow` (bóng xếp lớp); **`AppCard`** (recipe nền surface + bo 16 + viền + `kCardShadow`, có
  hover nhấc 2px); **`AppSemantic`** = `ThemeExtension` cho `success/warning/info/surfaceSunken`
  (đăng ký trong `appTheme()` qua `extensions:`, đọc bằng `AppSemantic.of(context).success`); và
  **`appTheme(Brightness)`** — theme builder DUY NHẤT (ColorScheme.fromSeed(navy)+copyWith, font
  `DM Sans`). `main.dart` chỉ gọi `appTheme(...)` (KHÔNG còn `_theme` cục bộ). Font nhúng
  `assets/fonts` (variable TTF, pubspec): **`JetBrains Mono`** cho số/log/JSON (`fontFamily:
  'JetBrains Mono'`, KHÔNG 'Consolas'), **`Source Serif 4`** tiêu đề display. Đổi "look" → sửa
  `app_theme.dart`. Màn con **luôn `Theme.of(context).colorScheme.*`** / `AppSemantic.of(context)`,
  KHÔNG hardcode `Colors.red/green/orange/grey` (GIỮ ngoại lệ: `Colors.white` trên nút màu, palette
  đồ thị `kSlotColors`/`kTempColors`, nền ảnh chart đen). Thẻ "hero" dùng `AppCard`; số/ngày dùng
  `FontFeature.tabularFigures()`.
- **Tự cập nhật doc**: `.claude/settings.json` có **Stop hook** nhắc bổ sung bài học mới vào CLAUDE.md sau
  mỗi lượt (chống lặp bằng cờ `stop_hook_active`). Vì vậy hãy giữ file này luôn cập nhật.

## Gotchas (đã gặp thật — đừng dẫm lại)
> Gotchas về **server / deploy / box** đã chuyển sang `server/CLAUDE.md` (mục "Gotchas server /
> deploy / box"); gotchas **môi trường máy dev** (BOM `.ps1`, backslash, classifier, jq…) sang
> `CLAUDE.md` gốc monorepo. Ở đây chỉ còn gotcha của chính app Flutter.
- **ATE — máy giả phải trả UART theo TỪNG MẨU, không phải cả khối** (bài học đắt 2026-09-09): bộ nghe
  BOOT-01 ngắt ngay khi thấy banner ROM (`ets `/`rst:0x`), trên bo thật log về từng ~30 byte nên nó
  cắt đúng ở `ets Jul 29 2019 12:21:46 / rst:` rồi chấm "không thấy version" — **14 hồ sơ đầu tiên
  của trạm web FAIL sạch** dù nạp thành công. 48 test vẫn xanh vì `FakeStation` trả cả khối một lần
  nên `until` đúng ngay chunk đầu. Giờ máy giả trả mẩu 8 ký tự và tôn trọng `until`; luật cho mọi
  `until` mới: chỉ ngắt khi đã có THỨ CẦN CHẤM (version/lỗi/`{Green: n}`/đủ 2 mẫu nhiệt), không
  ngắt ở "dấu hiệu bắt đầu". Nhìn hồ sơ thật (`%LOCALAPPDATA%\fbt-localtest\ate\*.json`, cột
  `raw` của bước hỏng) trước khi tin test.
- **Tiến độ OTA NGƯỢC với version có hậu tố** (rà 2026-09-18, CHƯA sửa): `versionInFileName`
  (`manager_machine_screen.dart`) rút version bằng regex `v?(\d+(?:\.\d+)+)` → **cắt mất hậu tố**
  (`fbt_v2.4.5AT1.bin` → `2.4.5`), còn `normalizeDeviceVersion` **giữ** hậu tố → máy đang chạy
  `v2.4.5AT1` bị tính "chưa lên", máy còn `v2.4.5` cũ lại tính "đã lên". Fleet đang build
  `v2.4.5AT1`/`v2.4.5a1` nên bảng tiến độ, ô Trạng thái update, dấu "đang chạy" trong menu ghim và
  CSV rollout đều sai; `ota_version_match_test.dart` không có ca target mang hậu tố. **Server đã sửa
  tận gốc cùng ngày** (chưa deploy): `GET /devices` trả khối `ota{target, ver, pinned, reason, state,
  offered_at}` với `state ∈ on|offered|waiting|skipped|unknown|none` tính ở server (so version giữ
  hậu tố), và `GET /ota/{product}/progress` trả `counts` — app giai đoạn 2 **bỏ `versionInFileName`/
  `isDeviceOnTarget`, đọc `ota.state`** (`server/docs/plan/ota-nhieu-san-pham.md` mục "Bổ sung
  2026-09-18"). Chừng nào app chưa đổi, bảng tiến độ vẫn sai với bản có hậu tố.
- **`ExpansionTile` căn GIỮA phần thân**: `expandedCrossAxisAlignment` mặc định là `center`, nên một
  `Container` không đặt bề rộng sẽ co lại bằng dòng dài nhất rồi nằm lọt thỏm giữa thẻ (khung Nhật ký
  trạm dính đúng lỗi này). Thân chiếm hết bề ngang thì phải `CrossAxisAlignment.stretch`.
- **`DropdownButtonFormField` bề rộng CỐ ĐỊNH thì phải `isExpanded: true`**: chữ dài hơn ô là
  RenderFlex tràn phải — bản debug kẻ sọc vàng, **bản release cắt cụt im lặng**. Bắt được nhờ widget
  test, không phải nhờ nhìn màn hình.
- **Ô chọn hiện TRỐNG chưa chắc là lỗi widget — soi DỮ LIỆU trước**: ô "Chọn bootloader" của trạm web
  trống trong khi cấu hình đã lưu tên bản, tôi đoán ngay là `FormField.initialValue` chỉ gieo một lần.
  Sai: kho OTA đã đổi sang bộ tên khác (`fbt_v2.4.4-bootloader.bin`) nên tên cũ không còn tồn tại —
  `DropdownButtonFormField` bỏ giá trị lạ là ĐÚNG. Bài học kép: (1) so danh sách thật với giá trị đã
  lưu trước khi mổ widget; (2) trạng thái "tên đã lưu không còn trong kho" phải **nói ra**
  (`ate.binGone`), vì ô trống trông y hệt "chưa ai cấu hình".
- **Browser tool `key` KHÔNG tới được canvas Flutter web** (2026-09-21): `computer{action:key,text:Return}`
  không làm app thấy phím dù `HardwareKeyboard` handler đúng; muốn thử phím tắt thì bắn thẳng
  `document.querySelector('flutter-view').dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',code:'Enter',
  keyCode:13,bubbles:true}))` (+ `keyup`) qua `javascript_tool` — app phản ứng ngay. `scroll` của tool cũng
  hay rơi im (trang đứng yên) → cuộn bằng `dispatchEvent(new WheelEvent('wheel',{deltaY:300,clientX,clientY,
  bubbles:true}))` lặp vài lần, ảnh chụp timeout thì chụp lại lần hai là ra. **Pane trình duyệt bị ẨN thì
  `left_click` cũng không tới canvas** (viewport co còn 640×360, click rơi lệch một mục rail) → `resize_window`
  1280×800 rồi bắn `PointerEvent` vào `flutter-view`: `pointermove` → chờ ~150 ms → `pointerdown` (`buttons:1`,
  `pointerId:1`, `pointerType:'mouse'`, `isPrimary:true`) → `pointerup` (`buttons:0`), toạ độ CSS = toạ độ ảnh ×
  (1280/800); Flutter nhận như click thật (đã dùng để mở tab Hiệu chuẩn, mở lô, bấm Kết nối 2026-09-21). Cổng serial giả cho
  Web Serial (công thức TEST LOCAL bên dưới) + `window.__fakeWrites` là cách rẻ nhất để xác nhận app gửi
  ĐÚNG byte (`[48]` = `'0'`) mà không cần máy.
  ⚠️ **Khung xem trước TỰ ĐỔI KÍCH THƯỚC giữa chừng mà ảnh chụp KHÔNG nói** (2026-09-23): `resize_window`
  1280×800 xong vài lệnh sau pane đã co còn ~656×410, nhưng ảnh trả về vẫn ghi "800x500" y hệt — chỉ khác
  ở chỗ mọi thứ trong ảnh to gấp đôi. Toạ độ quy từ khung 1280 lúc đó lệch cả trăm px → click rơi vào MỤC
  KHÁC của rail mà trông như "click trượt". Trước mỗi loạt click theo toạ độ phải hỏi lại
  `javascript_tool` → `({w: innerWidth, h: innerHeight})` và tính tỉ lệ `w / bề-ngang-ảnh`, đừng tin con
  số đã `resize_window`. Thử **hover** (rail bung) thì chuỗi `pointermove` đi dần vào mép trái rồi DỪNG —
  trạng thái hover giữ nguyên qua lệnh chụp màn hình, chụp được cả lúc rail đang bung.
- **Lái bản WEB bằng browser tool: BẬT SEMANTICS trước, đừng đoán toạ độ**. Flutter web vẽ lên canvas
  nên `find`/`read_page` trả rỗng và click theo pixel hay trượt im lặng. Bấm nút ẩn của Flutter một
  lần là có cây accessibility để click theo `ref`:
  `document.querySelector('flt-semantics-placeholder').click()` (qua `javascript_tool`), chờ ~1s.
  Ba lưu ý: cây **tự tắt lại** sau vài thao tác (bấm lại), `ref` **cũ đi ngay khi layout đổi** (đọc
  lại `read_page` trước mỗi click), và menu `DropdownButton` chỉ đưa vào cây **những mục đang thấy** —
  cuộn trong menu rồi đọc lại mới đủ. Ảnh chụp thỉnh thoảng timeout/ra khung phóng to (pane bị che):
  đó là lỗi CHỤP, không phải app — kiểm bằng `read_page` hoặc đọc thẳng `localStorage` để biết state.
  ⚠️ **`document.querySelector('flt-semantics…')` trả RỖNG vì cây nằm trong SHADOW DOM** (Flutter 3.44,
  2026-09-23): cả `flt-semantics-placeholder` lẫn `flt-semantics-host` đều không thấy từ `document` →
  tưởng semantics không bật được rồi quay về đoán toạ độ (mất nhiều lượt). Phải duyệt đệ quy
  `shadowRoot`: gom `querySelectorAll('*')`, cái nào có `.shadowRoot` thì đẩy vào stack, tìm tiếp.
  Có host rồi thì **đừng quy toạ độ từ ảnh chụp nữa** — `getBoundingClientRect()` của node semantics
  CHÍNH LÀ toạ độ `clientX/clientY` cần cho `PointerEvent`; hoặc gọi thẳng `node.click()` (ăn ngay, đã
  dùng để bỏ ghim rail). Nhãn có khi nằm ở `textContent` chứ không phải `aria-label` → lọc bằng cả hai.
  ⚠️ **ĐÍNH CHÍNH 2026-09-23 (chiều) — cách bấm RẺ NHẤT là `computer{left_click}` với toạ độ ĐỌC THẲNG
  TỪ ẢNH `screenshot{scale:1}`, KHÔNG quy đổi gì.** Hệ toạ độ của tool chính là pixel của ảnh nó trả về
  (ở đây 800×500), KHÔNG phải CSS viewport (1280×800) — nhân 1,6 cho "đúng CSS" là sai và trượt hoài.
  Bấm nút thu/mở ở chân rail: đọc ảnh thấy nút ở (139, 486) → `left_click [139, 486]` trúng ngay phát
  đầu, sau khi đã phí hàng chục lượt dò `PointerEvent` ở (222, 778). Ghi chú cũ "left_click không tới
  canvas Flutter" là do lần đó pane đang ẩn/khác cỡ, KHÔNG phải luật.
  `PointerEvent` tự dựng (`clientX/clientY` = CSS px — hệ KHÁC) chỉ dùng khi cần thứ `left_click` không
  làm được: giữ **hover** một chỗ, hoặc move+down cùng tick cho nút đổi chỗ theo hover (dưới).
  Cây semantics cũng đừng phụ thuộc: `flt-semantics-placeholder` **biến mất sau lần bật đầu tiên** và
  tree có lúc trả về RỖNG dù `flt-semantics-host` vẫn còn — dùng được thì tốt, không thì quay về ảnh.
  ⚠️ **Nút ĐỔI CHỖ theo hover thì `pointermove` rồi chờ là hụt**: nút ghim rail nằm giữa khi rail thu,
  trượt sang phải (~222 CSS) khi rail bung. Rê vào → chờ → `pointerdown` thì lúc down nút đã đi mất.
  Bắn `pointermove` + `pointerdown` **trong CÙNG một tick** (không `await` ở giữa) để hit-test rơi vào
  bố cục lúc chưa hover.
- **ATE — `AteStation` nói Ý ĐỊNH, không nói cú pháp esptool**: `chipInfo()` / `flash(AteFlashRequest)`
  chứ không phải `esptool(List<String> args)`. Đổi từ bản cũ (truyền tham số dòng lệnh) vì bản web
  không có tiến trình nào để chạy — nó gọi esptool-js. Đặt ranh giới ở cú pháp CLI thì bản web phải đi
  phân tích chuỗi tham số; đặt ở ý định thì một kịch bản chạy cả hai nền tảng.
- **ATE web — nền tảng không verify được thì bước nạp là `info`, KHÔNG phải `pass`**
  (`AteFlashResult.verified`). Đừng "cho qua" để hồ sơ đẹp: hồ sơ nghiệm thu mà ghi "verify khớp" cho
  thứ không ai đối chiếu là hỏng đúng cái giá trị của nó.
- **ATE — một `version` bộ ngưỡng = một NỘI DUNG**: `PUT /ate/limits` trả **400** nếu lưu nội dung khác
  dưới version đã có (`ate_limits_conflict`). Hồ sơ chỉ ghi chuỗi `limits_ver`, nên tái dùng version là
  mất khả năng trả lời "máy này bị chấm theo ngưỡng nào" — đúng vết xe `fbt_v2.4.5.bin` (hai image, một
  tên). Sửa ngưỡng thì đổi version.
- **ATE — ô ngưỡng để TRỐNG nghĩa là "chưa chốt", KHÔNG phải 0**: app không gửi khoá đó → `AteLimits`
  đọc ra `null` → bước đo trả `info`. Đừng "tạm điền 0 cho nó chạy": 0 là một ngưỡng thật và mọi máy
  đều vượt qua nó.
- **ATE — ngưỡng chưa chốt thì trả `info`, KHÔNG phải `pass`**: `AteLimits` để `null` cho mọi ngưỡng
  quang + nhiệt phòng; bước đo ghi số vào hồ sơ với verdict `info` (không làm hỏng hồ sơ, cũng không
  giả vờ đã kiểm). Chạy 10–20 golden unit → đọc số trong hồ sơ → `PUT /ate/limits` là tự chuyển sang
  chấm. Đừng "tạm điền một con số cho nó chạy" — cả cột dữ liệu sau đó thành vô nghĩa.
- **ATE — `testShot` (`0`–`9`) KHÔNG echo số slot**: phải gửi ĐÚNG MỘT slot rồi chờ đúng một dòng
  `{Green: …}` (`parseGreenMean`). Bắn cả 10 slot rồi parse hàng loạt là gán nhầm số sang slot khác mà
  vẫn "ĐẠT" — kiểu sai âm thầm nguy hiểm nhất của trạm.
- **ATE — bước bán tự động không có hook `confirm` thì `skip`**, không bao giờ tự `pass`. Máy trạm chạy
  không người mà đóng dấu "quạt đạt" thì cột dữ liệu đó vứt đi.
- **ATE — `-127` là cảm biến nhiệt Dallas MẤT KẾT NỐI**, không phải nhiệt độ âm: `TMP-01` chấm FAIL và
  gọi đúng tên kênh (`kTempChannels`). Cùng luật với bộ quét log của tab CSKH.
- **ATE — firmware trả `true` cả khi KHÔNG ghi gì**: `JsonDataConfig()` bỏ qua toàn bộ cấu hình nếu
  thiếu khoá `"para version"` (hoặc sai tên khoá) mà **vẫn báo thành công**. Vì vậy `ate_runner`
  (1) LUÔN chèn `para version` vào mọi JSON `{...}@` gửi qua Serial — đường `POST /config` được
  firmware tự chèn, đường Serial thì KHÔNG; (2) **không chấm theo ACK**, luôn `ParaRead` rồi đối chiếu
  chuỗi. Tên khoá (`device ID` / `para version` / `PCB version`) là hằng ở đầu `ate_runner.dart`, lấy
  theo `GET /config` của firmware v2.4.4 — **đối chiếu lại với firmware trước khi chạy lô đầu**.
- **ATE — hồ sơ đẩy bằng PUT, không POST** (`PUT /ate/records`): `POST /{path}` catch-all của server
  nuốt mọi POST thành payload thiết bị → 400 "missing id_device". Cùng luật với OTA và log CSKH.
- **`--base-href /app/` KHÔNG chạy qua Git Bash**: MSYS đổi `/app/` thành `C:/Program Files/Git/app/`
  → `flutter build web` báo "should start and end with /" rồi **vẫn thoát mã 0** (tưởng build xong,
  thật ra không). Build web có base-href thì chạy từ **PowerShell**, hoặc đặt `MSYS_NO_PATHCONV=1`.
- **Thêm màn desktop-only phải build lại web để kiểm**: `flutter analyze` KHÔNG phát hiện `dart:io` lọt
  vào cây import của bản web (conditional import chỉ giải quyết lúc build). Sau khi thêm tab mới chạy
  `flutter build web --release --output build/web_check` (thư mục riêng để không đè bản test ở
  `build\web`) rồi xoá đi.
- **Flutter trên box `ADM` (2026-09-04)**: dự án ghim **3.44.1** qua FVM (`.fvm/fvm_config.json`) nhưng
  máy KHÔNG có `fvm` lẫn `flutter` trong PATH. SDK đã tải thủ công (zip stable chính thức, 1.14 GB) vào
  **`C:\Users\ADM\fvm\versions\3.44.1`** (đúng layout FVM — cài `fvm` sau là nhận). Gọi bằng đường dẫn
  đầy đủ từ PowerShell, kèm `PUB_CACHE`:
  `$env:PUB_CACHE="$env:LOCALAPPDATA\Pub\Cache"; & C:\Users\ADM\fvm\versions\3.44.1\bin\flutter.bat analyze …`
  (chạy `flutter.bat` từ Git Bash dễ lỗi script sh). **Máy KHÔNG có Visual Studio** → không build được
  Windows desktop ở đây (chỉ analyze/test/build web); `windows/` cũng gitignored (sinh lại bằng
  `flutter create` trên máy có VS — nhớ tên pubspec `RapidPlusApp` phải đổi tạm, xem gotcha dưới).
- **Công thức TEST LOCAL bản web + server (đã chạy 2026-09-04, không cần Postgres/thiết bị)**:
  (1) venv Python trong scratchpad (`pip install fastapi httpx uvicorn pytest`), test:
  `PYTHONIOENCODING=utf-8 python -m pytest server/tests/test_api.py -q` (chạy TỪ `server/`).
  (2) `flutter build web --release --base-href /app/ --dart-define=FBT_URL=http://127.0.0.1:8080
  --dart-define=FBT_TOKEN=localtok` rồi chạy server local **serve luôn bản web** (cùng origin, hết CORS):
  `FBT_WEB_DIR=<repo>\build\web FBT_DATA_DIR/OTA/LOGS_DIR=<scratch> RECEIVER_TOKEN=localtok
  FBT_DB="dbname=__nope__ connect_timeout=1" python -m uvicorn app.main:app --port 8080` (từ `server/`)
  → mở `http://127.0.0.1:8080/app/`. (3) **Login thật cần Postgres** → trên box ADM đã dựng
  **PostgreSQL 17.7 portable** (zip binaries EDB, không cài đặt/không service) ở
  `%LOCALAPPDATA%\fbt-localtest\pgsql`, data `…\pgdata` (`initdb -U postgres -A trust`, port **5433**),
  DB `mydb` đã chạy `deploy/schema.sql`, bảng `users` có 3 tài khoản test: **`cskh`/`cskh123`** (admin,
  `*`) · **`root`/`root123`** (root) · **`khach`/`khach123`** (user, chỉ RPL02013); 1 phiên mẫu
  `docs/data_sample/data_RPL.json` đã ingest. Bật/tắt cả bộ bằng **`server\scripts\localtest.ps1`**
  (`-Stop` để tắt) — script tự tạo venv ở `%LOCALAPPDATA%\fbt-localtest\venv` (venv trong scratchpad
  phiên AI là thư mục TẠM, đừng trông vào nó). Không có Postgres thì đường lùi: nhét phiên vào
  localStorage bằng JS rồi reload: khoá `flutter.user_session_v1` = `JSON.stringify(JSON.stringify({username,
  name, role:'admin', ids:['*'], allowAll:true}))` (shared_preferences web bọc JSON 2 lớp), thêm
  `flutter.engineer_url`/`flutter.engineer_token` cùng kiểu. (4) **Cổng serial GIẢ** để test Web Serial
  không cần máy: ghi đè `navigator.serial.requestPort = async () => fakePort` với `fakePort` có
  `getInfo()`, `open()` (tạo `readable` = `ReadableStream` enqueue từng dòng log ESP32 theo timer,
  `writable` = `WritableStream` ghi vào `window.__fakeWrites`), `setSignals()`, `close()` — dart2js gọi
  đúng property đó. Đã dùng để xác nhận toàn chuỗi Chăm sóc KH: kết nối → quét dấu hiệu → lệnh nhanh
  ghi `ParaRead\n` → PUT log lên server local → "Log đã gửi" đọc lại → đổi tab tự đóng cổng.
  ⚠️ Bản `build\web` sau bước (2) nhúng URL localhost + token test — build lại KHÔNG dart-define trước
  khi deploy. **Bật lại bộ local thì KIỂM `build\web` có CŨ hơn `lib/` không** (`find lib -newer
  build/web/main.dart.js -name '*.dart'` ra dòng nào là cũ) — 2026-09-12 bản web local còn là build
  11-09 trong khi `main` đã merge thêm tab Giám sát/tải hàng loạt; server serve file trên đĩa nên
  người dùng test bản thiếu tính năng mà không có dấu hiệu gì. Cũ thì build lại rồi mới đưa URL. Click trong Flutter web (canvas) thỉnh thoảng TRƯỢT không báo lỗi → chụp lại xác nhận
  trước khi kết luận (gặp thật: nút Đóng dialog + đổi tab).
- **Phân trang + lọc quyền `canSee` ở CLIENT → user hạn chế kẹt ở trang rỗng**: màn tải theo trang
  rồi lọc `canSee` (vd `json_files_screen`) — 1 trang có thể TOÀN máy không-được-xem → lọc ra RỖNG.
  Nếu coi `_items.isEmpty` là "hết" (nút Tải thêm nằm trong ListView, không hiện) thì user chỉ được
  cấp vài mã máy sẽ thấy "rỗng" và KẸT dù máy họ nằm ở trang sau (root/`*` không dính vì canSee luôn
  true). Fix: `_load` tự tải tiếp khi trang lọc ra rỗng mà còn trang (cap vòng lặp), tới khi có item
  xem được hoặc hết.
- **Apps Script POST trả 302**: `http.post` của Dart **không** tự đi theo redirect tới URL "echo"
  (`script.googleusercontent.com`) → nhận 302. Phải: gửi `Request` với `followRedirects=false`, đọc
  header `Location`, **`await streamed.stream.drain()`** (KHÔNG drain → hop GET sau bị **401** do kết nối
  tái dùng bẩn), rồi `GET` Location. Giải mã body bằng **`utf8.decode(resp.bodyBytes)`** để giữ dấu
  tiếng Việt. Mẫu chuẩn: `services/auth_api.dart`.
- **Deploy Apps Script phải "Who has access" = Anyone**, nếu không `/exec` trả **trang đăng nhập Google
  (HTML)** thay vì JSON. Test nhanh: `curl -sL "<url>"` phải ra `{"ok":...}`.
- **Plugin symlink hỏng** (`windows\flutter\ephemeral\.plugin_symlinks\...` là thư mục thật thay vì
  symlink → `PathExistsException`/errno 183) và **CMake cache trỏ path cũ** (sau khi *di chuyển/đổi tên*
  thư mục dự án): **`flutter clean`** rồi `flutter pub get` xử lý cả hai. Tạo symlink trên Windows cần
  Developer Mode.
  - **Khi `flutter clean` CŨNG fail** (`Failed to remove ... cannot access the file or directory` ở
    `build`/`.dart_tool`/`ephemeral`): dự án nằm **TRONG OneDrive** (`OneDrive\Desktop\...`) → OneDrive
    giữ handle. `.plugin_symlinks` thường là **symlink HỢP LỆ** (không phải thư mục thật) nhưng cờ
    **ReadOnly**; `Remove-Item -Recurse` fail vì **đi THEO symlink vào pub-cache** rồi vướng khoá ở đó.
    Fix: (1) xoá stale cache path cũ bằng `Remove-Item -Recurse -Force build`; (2) xoá TỪNG symlink
    plugin bằng `[System.IO.Directory]::Delete($link, $false)` (**`$false` = KHÔNG recurse vào target**,
    chỉ gỡ link) — KHÔNG dùng `Remove-Item -Recurse`; (3) `flutter pub get` tự tạo lại symlink sạch rồi
    `flutter run` chạy. (Tắt/pause OneDrive khi build là gốc rễ, nhưng 3 bước trên đủ để qua.)
- **Build debug bị "cũ" (stale)**: đôi khi `flutter build windows --debug` không thay được `.exe` (nghi
  do lock/incremental) → app chạy code cũ. Nếu kết quả lạ: **xóa `.exe` trước khi build** (hoặc
  `flutter clean`) để chắc chắn binary mới.
- **Build fail ở bước INSTALL `file INSTALL cannot copy file ... Permission denied`** (DLL plugin như
  `file_selector_windows_plugin.dll`): do **1 instance app đang CHẠY khoá DLL**. Tắt trước rồi build lại:
  `Get-Process fbt_dxd_app -ErrorAction SilentlyContinue | Stop-Process -Force`. (Khác lỗi ephemeral
  `cpp_client_wrapper/*.cc` thiếu → cái đó dùng `flutter clean` + `pub get`.)
- **`installer.iss`**: `MyAppExeName` **phải** = `fbt_dxd_app.exe`; `MySource` **phải** trỏ
  `...\runner\Release` (KHÔNG trỏ thư mục dự án — sẽ gói cả mã nguồn, chậm + bộ cài hỏng).
- **Engineer Server trả 405 = URL trong Cài đặt THỪA path** (vd `.../api`): `app.py` có route
  `POST` catch-all `/{_path}` nên GET vào path lạ ra **405 thay vì 404** (`GET /devices` đúng → 401
  khi thiếu token). Lưu ý gốc rễ: **URL đã lưu trong `shared_preferences` KHÔNG tự đổi khi đổi hằng
  default** (`_orDefaultUrl` chỉ áp khi ô TRỐNG) — đổi hợp đồng/URL mặc định thì user phải xóa trống
  ô URL cũ (áp cho cả `cloudApiUrl`/`rapidErpUrl`).
  **Nguyên nhân 405 THỨ HAI (2026-08-26): route CÓ trong file nhưng SERVICE CHƯA RESTART.**
  `md5sum` main.py trên box KHỚP repo vẫn không có nghĩa route đã sống — uvicorn giữ code nạp lúc
  khởi động. Phân biệt: `GET /sessions/1` → 401 (route sống) nhưng `GET /sessions/1/errors` → 405
  (route chưa nạp). Kiểm đúng chỗ: so `stat -c %y app/main.py` với
  `systemctl show fbt-receiver -p ActiveEnterTimestamp`, hoặc đếm route trong TIẾN TRÌNH:
  `curl -s localhost:8080/openapi.json | grep -c '<route>'`.
  ⚠️ **Chạy lệnh kiểm NGAY SAU `systemctl restart` cho kết quả 0 GIẢ** (2026-08-26): uvicorn mất
  ~1 s mới `Application startup complete`, curl trong khoảng đó không nối được → `grep -c` ra 0,
  trông y như restart không ăn. Trước khi kết luận: `systemctl is-active` phải `active` VÀ
  `curl -w '%{http_code}'` phải 200 — ra `000`/0 byte là service chưa lên chứ không phải thiếu route.
- **Client CỐ Ý nuốt lỗi → route server thiếu trông y hệt "app không có tính năng"**:
  `FbtApi.sessionErrors`/`fwLog` bọc `try/catch` trả **rỗng** (lý do đúng: rỗng là kết quả bình
  thường, và không nên vỡ cả màn chi tiết vì một bảng phụ). Hệ quả: 405/401/CORS đều biểu hiện
  thành bảng KHÔNG hiện, không một thông báo nào. Gặp "tính năng X không có trên bản web/desktop"
  thì **đừng đi tìm code bị gate theo nền tảng trước** — kiểm 3 bước rẻ hơn nhiều: (1) `grep kIsWeb|
  dart:io|Platform\.` trong file tính năng, (2) `grep` chuỗi nhãn trong `main.dart.js` ĐANG HOST
  (bundle cũ hay không), (3) gọi thẳng endpoint xem 401 hay 405. Lần 2026-08-26 cả (1) và (2) đều
  sạch, thủ phạm là (3) — và nó hỏng ở CẢ hai nền tảng chứ không riêng web.
  ⚠️ **Bước (2) phải dò bằng chuỗi ASCII, ĐỪNG dò bằng tiếng Việt**: dart2js escape ký tự
  non-ASCII nên `grep -F "Đang đếm số lần chạy" main.dart.js` LUÔN ra 0 kể cả khi tính năng có
  trong bundle — âm tính giả, suýt kết luận sai 2026-08-26. Dò bằng **khoá i18n** (`dl.tooltip`)
  hoặc bản tiếng Anh. Đối chứng nhanh: grep một chuỗi tiếng Việt CŨ chắc chắn có; ra 0 nghĩa là
  phép grep sai chứ không phải bundle thiếu.
- **Kiểm i18n tự động trước khi review/phát hành**: script Python ngắn — regex `^\s{2}'(key)':` trong
  `lib/util/i18n.dart` lấy khoá đã khai, `\btr\(\s*'([^']+)'` quét `lib/**` lấy khoá dùng → in khoá THIẾU
  (`tr()` trả nguyên key) và khoá không có `'en'`. Khoá động `tr('ate.step.$code')` bỏ qua. ⚠️ Thân entry
  phải cắt theo **ranh giới khoá kế tiếp**, KHÔNG dùng `\{([^}]*)\}`: chuỗi có placeholder `{n}` làm regex
  đó cắt ở `}` đầu tiên → 76 khoá bị báo "thiếu en" SAI (2026-09-12, mất một lượt review vì thế; thật ra
  509/509 đủ). Màn lẫn hai thứ tiếng khi chọn English là do `data/machine_info_content.dart` cố ý chỉ
  tiếng Việt, không phải thiếu khoá.
- **"Tab này đang được xem" = `TickerMode`**: `HomeShell` bọc mỗi tab trong `TickerMode(enabled: i ==
  _index)` (IndexedStack dựng hết con, con ẩn vẫn tick) → màn cần dừng poll/animation khi ẩn đọc
  `TickerMode.valuesOf(context).enabled` trong `didChangeDependencies` (mẫu `monitor_screen.dart`), KHÔNG
  thêm prop `active` riêng. `AppTabScaffold` (mục con) cũng bọc TickerMode (2026-09-12) — lồng nhau thì
  `valuesOf` trả giá trị HIỆU DỤNG (cha tắt = con tắt) nên một màn con đọc một chỗ là đủ cả hai tầng;
  tab Chăm sóc KH đã bỏ cờ `active`, 3 màn Kỹ Thuật vẫn còn cờ (chưa chuyển).
- **Đổi NGÔN NGỮ** (hệ `tr()` tự viết, không dùng Localizations): phải **key `MaterialApp` theo locale**
  (`key: ValueKey('locale_..')`) thì các màn mới dịch lại — rebuild `MaterialApp` thường KHÔNG rebuild
  route `home`. Theme thì áp **live qua prop** (`theme`/`themeMode`), không cần key.
- **`userAuth.js` có `ACCOUNTS_SHEET_ID = "<FILL_ME>"`** — khi dán lại/deploy phải ĐIỀN lại ID sheet thật,
  nếu không login trả "Chưa cấu hình ACCOUNTS_SHEET_ID". Đổi email cần thêm cột `email` vào tab Accounts.
- **Mật khẩu BĂM trong sheet** (`userAuth.js`): lưu `sha256$<salt>$<hash>` (`makePasswordHash_`), so khớp
  qua `verifyPassword_` (tương thích ngược tài khoản plaintext cũ); `migratePasswordsToHash()` chạy tay để
  băm hết plaintext. ⚠️ `Utilities.computeDigest` trả **byte CÓ DẤU** (−128..127) → đổi hex phải
  `((b+256)%256).toString(16)` + zero-pad, nếu không hash sai. App gửi plaintext qua HTTPS (server băm).
- **`PopupMenuButton` trong overlay tự-ẩn-theo-hover** (`MouseRegion.onExit`): mở menu rồi rê chuột tới
  menu = rời rail → `onExit` ẩn rail → gỡ button khỏi tree → **huỷ menu, KHÔNG bấm được**. Fix: cờ
  `_menuOpen` (bắt `onOpened`/`onSelected`/`onCanceled`) → menu mở thì KHÔNG ẩn rail (`home_shell.dart`).
- **`flutter test` exit 1 dù cây sạch**: `test/widget_test.dart` đã **cũ** — kỳ vọng tab nav cũ
  (`'Cloud'`/`'Cài đặt'`) nhưng app giờ qua `_AuthGate`→`LoginScreen` nên không thấy → 5 pass, 1 fail.
  Logic test `test/curve_processing_test.dart` vẫn xanh. Đừng tưởng thay đổi của mình làm hỏng test.
- **Chụp màn hình app GUI (Flutter) bằng Win32 `PrintWindow` PHẢI dùng flag `2`**
  (`PW_RENDERFULLCONTENT`); flag `0` ra ảnh **đen** vì Flutter render qua DWM composition. Chụp theo
  **HWND** nên KHÔNG cần đưa cửa sổ lên foreground (xem `driver.ps1` trong skill `run-fbt-rapid`).
- **Xuất CSV cho người dùng VIỆT mở bằng Excel — thiếu 2 thứ là hỏng IM LẶNG** (2026-08-26,
  `services/rollout_csv.dart`): (1) **BOM UTF-8** ở đầu file, không có thì Excel đọc UTF-8 thành
  ký tự rác, tiếng Việt hỏng sạch; (2) dòng **`sep=,`** đầu file, vì Windows tiếng Việt lấy dấu
  phẩy làm dấu THẬP PHÂN nên list separator là `;` → mở file phẩy ra là dồn hết vào **MỘT cột**.
  Đánh đổi: công cụ đọc CSV nghiêm ngặt phải bỏ dòng đầu (`skiprows=1`) — ghi rõ trong doc hàm.
  Và **luôn escape ô** theo RFC 4180 (bọc nháy khi có `,`/`"`/xuống dòng, nháy trong nhân đôi):
  mã máy thật có dấu cách (`proto 1`) và cột trạng thái là tiếng Việt có dấu phẩy — không escape
  là lệch cột mà không ai báo lại.
- **Xuất PNG đồ thị KHÔNG đồng nhất giữa các màn** (quan trọng khi đổi nền/theme đồ thị): `result_detail`
  chụp 1 layer **off-screen RIÊNG nền trắng** (Overlay `left:-10000`) → PNG **luôn trắng** dù app dark.
  NHƯNG `curve_compare`/`temperature_log` có `RepaintBoundary` **bọc thẳng widget ĐANG hiển thị**
  (display == export) → đổi màu khung đồ thị theo theme thì ảnh **lưu** cũng đổi (dark mode → PNG nền
  tối). Muốn PNG luôn trắng ở 2 màn này phải bọc vùng chụp trong **light-theme cố định**. Bảng màu
  đường cong `kSlotColors`/`kTempColors` **đồng bộ web UI** (`sheet`/`data/script.js`) — đừng tô lại
  theo theme (mất phân biệt slot/kênh); chỉ token-hoá lưới/viền/mốc-0/slot-off.
- **Nạp code (`flasher_screen.dart`)**: gọi **esptool** qua `Process.start(exe, args, runInShell: true)`,
  stream `stdout`+`stderr` (decode `Utf8Decoder(allowMalformed:true)`) vào log, giữ `Process` để `kill()`
  (nút Dừng). Đường dẫn esptool **KHÔNG có UI** — `_resolveEsptool()` tự dò `esptool.exe` cạnh
  `Platform.resolvedExecutable` (= thư mục cài sau installer); không thấy → gọi `esptool` trên PATH.
  Chỉ hiện 1 dòng trạng thái "đã tích hợp / dùng PATH". (esptool **KHÔNG kèm app** — xem mục đóng gói dưới.)
  `--chip auto` thì **bỏ** cờ `--chip` (để esptool tự nhận). Offset mặc định theo chip: ESP32
  `0x1000/0x8000/0x10000`; S3/C3 bootloader `0x0`; ESP8266 chỉ app `0x0`. Có dropdown **flash mode**
  (`--flash_mode`) + **flash size** (`--flash_size`, chỉ thêm khi ≠ `keep`); và **theo dõi serial sau nạp**
  (mở `SerialPortReader` đọc log boot @baud chọn) để đọc lý do reset — `_run` gọi `_stopMonitor()` trước
  để nhả cổng cho esptool.
- **Debug "nạp xong chip tự reset"**: reset **1 lần** = bình thường (`--after hard_reset` vào app mới);
  reset **lặp** = boot-loop → đọc log boot trên serial: `invalid header: 0xffffffff` = thiếu/sai
  bootloader/partition hoặc sai offset; `Guru Meditation` = firmware crash; `Brownout` = nguồn yếu;
  `flash read err`/`checksum failed` = sai **flash mode** (DIO/QIO). Fix hay dùng: nạp ĐỦ 3 file đúng
  offset (đừng chỉ app) + mode `dio` + size `detect` + `erase_flash` trước.
- **Đóng gói esptool kèm app** (không cần đường dẫn ngoài): bỏ `esptool.exe` vào `windows/vendor/` →
  `windows/CMakeLists.txt` có rule `install(FILES vendor/esptool.exe DESTINATION CMAKE_INSTALL_PREFIX)`
  (guard `EXISTS`) copy nó cạnh `fbt_dxd_app.exe` mỗi lần build → installer (gói Release) tự kèm; app tự dò.
  Guard EXISTS đánh giá lúc **configure** → sau khi BỎ file vào phải **`flutter clean`** 1 lần. File `.exe`
  vài chục MB thường KHÔNG commit git. `installer.iss` cũng có 1 dòng `[Files]` ép gói `vendor\esptool.exe`
  (cờ **`skipifsourcedoesntexist`** để vẫn compile được khi chưa đặt file) làm lưới an toàn nếu CMake chưa copy.
- **Cổng COM dùng CHUNG trong tab Kỹ Thuật**: Log nhiệt / Đọc serial / Nạp code đều mở cùng phần cứng COM.
  `tech_screen` truyền cờ **`active: _seg==i`** cho từng màn (IndexedStack giữ state); màn nào `active`→`false`
  tự **nhả COM** trong `didUpdateWidget` (Đọc serial đóng cổng nhưng GIỮ log; Nạp code dừng theo-dõi, KHÔNG
  ngắt tiến trình nạp). **Log nhiệt CỐ TÌNH giữ đọc nền** (stop = `dispose` reader = MẤT mẫu) → muốn nạp
  đúng cổng đang log nhiệt thì **phải Dừng thủ công** trước.
- **Lọc cổng COM**: 3 màn Kỹ Thuật dùng `util/serial_ports.dart::usableSerialPorts()` (KHÔNG dùng thẳng
  `SerialPort.availablePorts`) — chỉ giữ cổng **USB-serial** (`transport==usb` hoặc có `vendorId`), bỏ native/
  Bluetooth. Nếu lọc ra RỖNG → trả TOÀN BỘ để không kẹt. Lưu ý: mỗi lần lọc có mở/`dispose` `SerialPort`.
- **Thiết bị ESP32/Forte TỰ RESET khi mở cổng / gửi UART**: DTR/RTS là **mạch auto-reset** (DTR→EN,
  RTS→GPIO0). `SerialPortConfig` KHÔNG set `dtr`/`rts` → để `invalid` → driver Windows tự BẬT 2 chân khi
  mở/ghi → máy reset. Fix: cấu hình **`..dtr = SerialPortDtr.off ..rts = SerialPortRts.off`** (sau
  `setFlowControl(none)`) để ghim trạng thái KHÔNG-reset (đã áp CẢ `serial_console_screen.dart` LẪN
  `temperature_serial.dart` — 2026-07; web `web_serial.dart` dùng `setSignals(dtr:false,rts:false)`).
  Config chỉ áp lúc **mở cổng** → phải đóng/mở lại mới ăn.
  Nếu reset **CHỈ khi gửi** (không reset lúc mở) thì là **firmware tự reboot theo lệnh nhận được** (crash/
  watchdog/lệnh reset), sửa ở firmware FBT-DXD — không phải app; xem RX có banner boot để phân biệt.
- **Vị trí các phần trong monorepo (từ 2026-09-15)**: app này ở `apps/fbt_rapid/`; Engineer Server ở
  `server/` (gốc); Apps Script cũ ở `legacy/sheet/` (getData.js CÒN được fleet cũ + reader gọi);
  firmware Rapid+ ở `firmware/rapidplus/` (không còn là repo `FBT-DXD` riêng), Reader ở `firmware/reader/`;
  registry sản phẩm `system/products.yaml`. Đường dẫn `lib/… test/… build/…` trong file này tính từ
  `apps/fbt_rapid/`; đường dẫn tới phần khác viết từ gốc monorepo.
- **`flutter create --platforms web .` từ chối tên pubspec `RapidPlusApp`** (không phải tên package
  Dart hợp lệ): đổi TẠM `name: rapidplusapp` → chạy create → đổi lại. Create cũng đụng
  `.plugin_symlinks` (lỗi OneDrive như trên — kệ, build windows sau đó vẫn chạy) và sinh
  `web/index.html`/`manifest.json` mang tên `rapidplusapp` → nhớ sửa lại title/manifest `FBT_RAPID`.
- **Chụp màn hình app WEB trong Chromium bằng PrintWindow ra ảnh XÁM** (dù flag `2`): nội dung
  Chrome/Edge composite bằng GPU nên PrintWindow không thấy. Fix: launch trình duyệt với
  **`--disable-gpu`** (+ `--app=<url>` để có cửa sổ riêng title = `<title>` trang, chờ title
  `FBT_RAPID*`) rồi chụp như driver.ps1. Đã đóng gói sẵn:
  `& ..\..\.claude\skills\run-fbt-rapid\webshot.ps1 -Out web.png` (tự serve `build\web` bằng
  `web-server.js` node tĩnh cùng thư mục — không cần `flutter run -d web-server`; build web trước).
  Chụp bản web đang HOST THẬT: thêm `-Url https://fbt.basa-luma.ts.net/app/` (bỏ bước serve cục bộ).
- **`IndexedStack` DỰNG MỌI TAB ngay khi đăng nhập — `initState` của màn chưa ai mở VẪN chạy**:
  `HomeShell` (và mẫu segmented của tab con) đặt tất cả trang vào `IndexedStack`, nó build hết,
  chỉ vẽ một cái. Nên mọi tác dụng phụ trong `initState` — `Timer.periodic`, fetch, mở cổng —
  chạy suốt phiên cho màn KHÔNG ai xem. Bắt được 2026-08-28: tab Giám sát tự làm mới 30 giây
  → 3 truy vấn gom nhóm mỗi 30 giây vào uvicorn 1 worker, cho trang chưa từng được bấm vào.
  **Cách xử lý CHUẨN (từ 2026-08-28)**: `HomeShell` bọc mỗi tab trong
  **`TickerMode(enabled: i == _index)`**, màn nào cần biết mình có đang hiển thị thì đọc
  `TickerMode.valuesOf(context).enabled` trong `didChangeDependencies` rồi bật/tắt timer.
  ⚠️ **`IndexedStack` KHÔNG tự tắt ticker** — đã đo bằng probe test: con bị ẩn vẫn
  `tickerMode=true`, nên phải bọc tay. (`TickerMode.of` đã deprecated, dùng `valuesOf`.)
  Cách khác tuỳ ca: cờ `lazy: true` của `AppTabScaffold` (chỉ dựng mục ĐANG chọn — Lịch sử
  dùng vì 3 nguồn cloud sẽ bắn 3 request cùng lúc) · cờ `active:` như `tech_screen` truyền
  xuống khi màn nắm PHẦN CỨNG và phải nhả (cổng COM) · hoặc bỏ hẳn việc định kỳ.
  Kèm theo: đừng gọi mạng trong `initState` của màn-là-tab (chạy ngay lúc đăng nhập), và
  `setState` gọi thẳng trong `didChangeDependencies` là "setState() during build" → hoãn
  bằng `Future.microtask`.
- **Lịch sử firmware — số lần đo phải cắt theo MỐC NẠP, không gả nguyên cụm** (`services/firmware_history.dart`):
  `buildFirmwareHistory` gộp các lần đo LIỀN NHAU cùng version thành MỘT quãng, nên **nạp lại đúng bản
  đang chạy** (v2.4.5 → v2.4.5) chỉ có 1 quãng cho **2** mốc nhật ký `fw-log`. `mergeFirmwareLog` bản đầu
  gả cả cụm cho mốc khớp ĐẦU TIÊN → dòng CŨ ôm hết số lần đo còn **dòng bản MÁY ĐANG CHẠY hiện "0 lần đo"**
  (báo về 2026-08-20). Fix: truyền `lanDo: runs` (lần đo thô) vào `mergeFirmwareLog`, đếm theo khoảng
  `[updatedAt dòng này, updatedAt dòng kế)` **và** vẫn khớp version — giờ trong phiên là giờ MÁY tự khai,
  mốc `fw-log` là giờ SERVER, lệch đồng hồ thì thà không đếm còn hơn đếm nhầm bản. Test:
  `flutter test test/firmware_history_test.dart`.
- **In giấy từ app (nhãn QR ống chuẩn, 2026-09-23)** — `services/calib_label.dart` + `util/printable.dart`:
  app dựng **HTML tự chứa** (QR vẽ bằng SVG inline, gói `qr` thuần Dart; không tải gì từ mạng để máy kho
  không internet vẫn in được) rồi nhờ TRÌNH DUYỆT in, không dùng plugin `printing`/`pdf` — một đường cho
  cả desktop lẫn web. Desktop ghi ra `%TEMP%\fbt_rapid_in\*.html` **kèm BOM UTF-8** (mở file cục bộ không
  có header Content-Type → thiếu BOM thì bản in tiếng Việt ra "Ã´ng chuáº©n") rồi `cmd /c start "" <file>`
  (tham số rỗng đầu tiên là TIÊU ĐỀ của `start`, thiếu nó thì không mở gì).
  ⚠️ **Web: ĐỪNG in bằng `window.open`** — pop-up bị chặn thì `dart:html` KHÔNG trả `null` như kiểu
  `WindowBase` hứa mà **ném** `Attempting to use a null window opened in Window.open` (gặp thật). Dùng
  **iframe ẩn** + trang tự gọi `window.print()` trong `onload` của nó: iframe không phải pop-up nên không
  ai chặn, và Chrome in đúng nội dung iframe. Hỏng nữa thì lùi về tải file .html xuống Downloads.
  ⚠️ Hộp thoại in bật lên sẽ **khoá khung xem trước của Claude** (screenshot/phím time-out) → đóng tab rồi
  `preview_start` lại; đó cũng chính là dấu hiệu lệnh in đã chạy.
- **Kiểm mã QR thì phải QUÉT THỬ, đừng tin mắt** (2026-09-23): SVG QR lật hàng/cột vẫn "đúng" với chính
  nó và vẫn qua mọi test Dart. Cách kiểm đã dùng: sinh `qr.svg` ra scratchpad → trang HTML nhỏ vẽ nó lên
  canvas rồi giải bằng **jsQR** (`curl` về từ `cdn.jsdelivr.net/npm/jsqr@1.4.0/dist/jsQR.js` — cdnjs trả
  404, và trang trong khung xem trước KHÔNG tải được script CDN nên phải tải về rồi tự phục vụ) → so
  chuỗi giải ra với payload. Phải phục vụ qua HTTP (`file://` làm canvas bị taint, `getImageData` ném).
- **`mock-server.js` giờ có route `/calib/*` giả** (2026-09-23): xem được tab Hiệu chuẩn trên bản web mà
  không cần server thật — dữ liệu cố tình có đủ bộ PASS / FAIL / đã huỷ để thấy nút nào hiện với bộ nào.
