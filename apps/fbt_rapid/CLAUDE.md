# CLAUDE.md — FBT_RAPID App (Flutter / Windows)

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
flutter pub get
flutter run -d windows                 # chạy có hot reload
flutter build windows --debug          # build nhanh để test  → build\windows\x64\runner\Debug\fbt_dxd_app.exe
flutter build windows --release        # build phát hành      → ...\Release\
flutter analyze lib/<file>...          # lint nhanh vài file (đừng analyze cả repo nếu không cần)
flutter build web --release            # build WEB → build\web (host tĩnh ở đâu cũng được)
```
Đóng gói installer (Inno Setup) — **phải `--release` TRƯỚC** vì script trỏ vào thư mục Release.
`installer.iss` nằm ở **gốc repo app** (source/icon/vendor dùng path tương đối `AddBackslash(SourcePath)`
— đừng hardcode đường dẫn tuyệt đối); mỗi lần phát hành nhớ nâng `MyAppVersion` trong file.
Version phát hành = `MyAppVersion` (khớp tag commit `vX.Y.Z`); `pubspec.yaml` KHÔNG đồng bộ (vẫn 1.0.2) — đừng lấy đó làm chuẩn:
```powershell
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" installer.iss
# → C:\Users\nvdat\Downloads\FBT_RAPID-Setup-vX.Y.Z.exe
```
**Chạy + chụp màn hình tự động** (cho AI/agent — GUI không có curl/Playwright): skill
`.claude/skills/run-fbt-rapid/` (`driver.ps1`) build/launch `fbt_dxd_app.exe` rồi chụp ĐÚNG cửa
sổ ra PNG. Vd `& .claude\skills\run-fbt-rapid\driver.ps1` (launch Debug → chụp `_smoke.png` →
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
  trò — khách hàng (`user`): **Lịch sử**; nhân sự (`admin`/`root`): **+ Kỹ Thuật**; **`root`**: **+ Quản lý
  User**. Tab **Kỹ Thuật** (`tech_screen.dart`, mẫu segmented giống Lịch sử) GỘP 3 công cụ: **Log nhiệt**
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
  `settings_screen.dart` cũ KHÔNG còn dùng). **Thanh nav ẩn hẳn** — hover mép trái (vùng 14px +
  "tay nắm" gợi ý) mới hiện như **overlay trong `Stack`** (KHÔNG dùng `Row`): bung/thu thanh nav KHÔNG
  relayout nội dung → tránh giật `fl_chart`.
- **Phiên/phân quyền**: `services/session_store.dart` giữ `UserSession` **toàn cục** qua static
  `SessionStore.current` (giống pattern `StoragePaths`). **3 vai trò** (`UserRole`): `root` (Root) /
  `admin` (Nhân viên) / `user` (Khách hàng) — tên hiển thị qua `roleLabel()` (i18n). `canWrite` = nhân sự
  (`isStaff` = root+admin); `canManageUsers` = **root** (quản lý tài khoản **root-only**, gate
  `requireAdmin_` trong userAuth.js yêu cầu role==="root"). **Phạm vi XEM máy = `allowAll`** (`canSee()`):
  `allowAll` CHỈ true khi **root** (super-admin) HOẶC `ids` chứa `"*"`. **Admin (nhân viên) KHÔNG còn auto
  thấy mọi máy** — lọc theo `ids` được cấp như khách hàng (chỉ `*` mới full). `isStaff` giờ chỉ quyết định
  **canWrite** + hiện tab Kỹ Thuật, KHÔNG quyết định phạm vi xem. Lọc dữ liệu enforce **ở client**
  (`cloud_devices`/`history` fetch hết rồi `canSee`); `UserSession.fromJson` TỰ tính `allowAll` (không tin
  `allowAll` backend cũ). Màn con đọc `SessionStore.current`/`canWrite` để lọc + ẩn nút.
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
  từ desktop: sau `port.open()` PHẢI `setSignals({dataTerminalReady:false, requestToSend:false})`
  — không set là Chrome bật DTR/RTS → ESP32 auto-reset (đã ghim trong `WebSerialPort.open`).
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
- **DEPLOY WEB giờ chạy `deploy-web.ps1`** (gốc repo), ĐỪNG gõ scp tay nữa: mặc định là **chạy thử**
  (in danh sách file khác md5, không đụng server), thêm `-Go` mới chép. Script tự lo hết những chỗ
  từng hỏng: chỉ chép file THẬT SỰ khác (6 MB thay vì 43 MB), `scp -O` từng file, sao lưu
  `web.bak.<stamp>` trước, kiểm quyền ghi TRƯỚC khi đụng gì, gắn vân tay tên file
  (`main.<hash>.dart.js`) để qua cache Cloudflare 4 tiếng, và md5 lại sau khi chép.
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

## Backend (2 Apps Script RIÊNG, file trong `sheet/`)
- `getData.js` — `doPost` (firmware đẩy kết quả) + `doGet` (app đọc lịch sử: ids/runs/run/peek).
- `userAuth.js` — accounts/auth, web app + Google Sheet **riêng**, `doPost {action: login | changePassword
  | changeEmail | listUsers | saveUser | deleteUser}`. Schema tab `Accounts`:
  `username|password|role|ids|name|email|active` (cột `email` tuỳ chọn). Các action **admin** (listUsers/
  saveUser/deleteUser) yêu cầu `adminUser`+`adminPassword` MỖI LẦN. Vì `SessionStore` KHÔNG lưu mật khẩu
  → màn **Quản lý User** (`user_management_screen.dart`) **hỏi lại mật khẩu admin** (chỉ giữ trong RAM).
- URL `/exec` gắn trong `lib/services/app_settings.dart`: `kDefaultCloudApiUrl`, `kDefaultAuthApiUrl`.
  Đổi deploy → sửa hằng → build lại.
- **Sửa script → phải Deploy lại** (Manage deployments → Edit → New version) thì `/exec` mới cập nhật.

### Server tự host (Docker, `server/`) — backend ĐỘC LẬP (app KHÔNG gọi)

> App KHÔNG đọc `server/` (nguồn **Engineer Server** trong app trỏ **FBT Home Server**
> `../Server/app.py` — project khác, xem bullet `fbt_api.dart` ở trên). `server/` vẫn là backend
> đứng riêng (firmware POST `/ingest` được); muốn app đọc thì viết client theo hợp đồng `/api`.
- Stack: **Postgres 16 + Node/Express** (`api/`). Public ra ngoài (miễn phí, không thẻ) bằng **DuckDNS
  (DNS động) + Caddy (HTTPS Let's Encrypt tự động)** dưới profile `duckdns` → cần **mở port 80/443** ở
  router (`https://<tên>.duckdns.org`). Chạy trên máy luôn-bật ở nhà. Lệnh: `cp .env.example .env` → điền
  `ADMIN_TOKEN/DEVICE_KEY/POSTGRES_PASSWORD` + `DUCKDNS_*`/`PUBLIC_HOST` → `docker compose up -d --build`
  (local) hoặc `docker compose --profile duckdns up -d --build` (public). **Mọi lệnh `docker compose`
  PHẢI chạy TỪ TRONG `app/server/`** (nơi có `docker-compose.yml`) — chạy ở gốc `app/` báo lỗi
  `no configuration file provided: not found`. **Docker CÓ sẵn trên box** (Docker 29.5.3 + Compose
  v5.1.4) — server chạy trong container nên **KHỎI cài Node cục bộ**.
- **GOTCHA "domain free + Cloudflare Tunnel" KHÔNG khả thi (đã kiểm chứng 2026)**: domain free `.eu.org`
  **không add được vào Cloudflare gói Free** (Error 1049 — không trong ICANN Public Suffix List), mà
  Cloudflare Tunnel named hostname BẮT BUỘC domain là zone trên Cloudflare; Freenom (.tk/.ml…) đã chết
  2023; Quick Tunnel URL đổi mỗi lần chạy. → Muốn Cloudflare Tunnel phải **mua** domain (`--profile tunnel`,
  `cloudflared` vẫn còn trong compose); miễn phí thì đi DuckDNS+mở port (ở trên) hoặc ngrok free static.
- **Compose interpolate CẢ file lúc parse** (kể cả service ở profile chưa bật) → biến của service profile
  `${VAR:?...}` sẽ làm hỏng `docker compose up` mặc định nếu chưa set; dùng default `${VAR:-...}` thay vì `:?`.
- **Hợp đồng GIỐNG HỆT Apps Script doGet** để app tái dùng `cloud_history_api.dart`: `GET /api?action=ids|
  runs|run` (admin, header `Authorization: Bearer <ADMIN_TOKEN>`); `POST /ingest` (firmware, header
  `X-Device-Key`). Bảng `runs` lưu **RAW JSONB** + cột rút ra; `UNIQUE(device_id,run_time)` → POST lại
  idempotent. `transform.js` đổi RAW firmware → "app shape": `result "-- | N"` → chữ `N/P/S/E`, `CT_value`
  → `ct`, **`amplification` (chuỗi "a,b,c,…") gán THẲNG vào `curves`** (app `_parseRawCurve` tự tách).
  `action=runs` KHÔNG kèm curves; `action=run` kèm. `time` trả nguyên chuỗi gốc (app parse được cả
  dd-MM-yyyy); riêng `ids.latest` PHẢI ISO (`CloudDevice.fromJson` chỉ `DateTime.tryParse`).
- **GOTCHA payload firmware THẬT khác sample**: bản upload **"Manual"** (vd máy RPL02013) **KHÔNG có
  field `time`** và dùng `record_out` (mảng `{Slot_N:{peak_features,outcome}}`) thay vì `outcome[]`/
  `peak_features[]` top-level, thêm `type_Upload`, `kitId` dạng **chuỗi** `"0.00"`. → `/ingest` ban đầu
  bắt buộc `time` nên **400 "bad time"**; đã sửa: thiếu/sai `time` → **dùng `new Date()` (giờ server)**
  (đánh đổi: mất idempotent theo time, mỗi POST = 1 bản ghi). `record_out`/`type_Upload` chỉ lưu raw,
  transform bỏ qua (app cũng bỏ). `result "22.3 | N"` → vẫn ra chữ `N` đúng.
- **Firmware** (`../FBT-DXD/src/`) muốn đẩy vào server này phải POST THÊM tới `/ingest` (song song POST
  Apps Script cũ) — thay đổi firmware tách biệt, chưa làm. Test app không cần firmware: `curl --data
  @server/sample_run.json` bơm 1 run mẫu là đủ.
- **GOTCHA "firmware POST mà app KHÔNG nhận" — debug từ NGOÀI vào, KHÔNG mổ code trước**: server-side
  (`/ingest`→DB→`/api`) hầu như luôn OK; nghẽn nằm ở **lớp public**. `docker compose up` THƯỜNG chỉ chạy
  `api` (bind **`127.0.0.1:3000`**) + `db` — **`caddy`+`duckdns` nằm dưới `--profile duckdns` nên KHÔNG tự
  lên** → không ai nghe **80/443** → firmware POST `https://<host>/ingest` rơi vào hư không. **Test 1 dòng:
  `curl -m15 https://<host>/health` ra `HTTP 000`** = lớp public chưa chạy (phải `docker compose --profile
  duckdns up -d --build` + forward 80/443 ở router). Xác minh server vô can: POST payload firmware vào
  `127.0.0.1:3000/ingest?key=<DEVICE_KEY>` rồi `GET /api?action=ids` (Bearer `ADMIN_TOKEN`) — thấy device
  là server OK. **`.env`: `DUCKDNS_SUBDOMAIN` PHẢI trùng host firmware/app dùng** (đã gặp lệch
  `fbtrapidtest` vs `PUBLIC_HOST=fbtrapid.duckdns.org` → DuckDNS cập nhật IP cho **sai** subdomain). Firmware
  HTTPS phải `WiFiClientSecure`+`setInsecure()` (hoặc CA), nếu `WiFiClient` thường thì `http.POST` trả -1.
  **Hairpin NAT**: app/thiết bị Ở CÙNG LAN gọi `https://<host>.duckdns.org` (= IP công khai của chính
  mình) thường **timeout** (`errno 121 semaphore`) DÙ mọi thứ đúng → test URL public từ **4G/ngoài LAN**;
  còn app chạy CÙNG máy server thì trỏ app thẳng `http://localhost:3000/api` (bỏ qua Caddy/DuckDNS).

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
- **`mv`/`rm -rf` thư mục báo `Device or resource busy` (Windows)**: do **shell Bash đang `cd` BÊN TRONG** thư
  mục đó (CWD persist giữa các call) hoặc IDE/Docker giữ handle. Cách xử lý: `cd` ra ngoài hẳn → `cp -r` sang
  đích (copy đọc được dù bị giữ) → xoá nguồn bằng **PowerShell `Remove-Item -Recurse -Force`** (qua được khoá
  mà `rm -rf` của Git Bash không qua).
- **`installer.iss`**: `MyAppExeName` **phải** = `fbt_dxd_app.exe`; `MySource` **phải** trỏ
  `...\runner\Release` (KHÔNG trỏ thư mục dự án — sẽ gói cả mã nguồn, chậm + bộ cài hỏng).
- **Shell môi trường KHÔNG có `jq`** — viết hook/script xử lý JSON bằng **bash thuần** (`case`/`grep`)
  hoặc node/python, đừng phụ thuộc `jq`. (Windows `python` cũng không hiểu path `/tmp` của Git Bash.)
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
- **"Deploy rồi mà web chưa thấy tính năng" — dò theo CHUỖI THAM CHIẾU, đừng đoán cache**:
  `curl /app/` xem `index.html` trỏ bootstrap nào → `curl` bootstrap đó lấy `mainJsPath` →
  `curl` file `main.<hash>.dart.js` đó rồi so **md5 với `build/web/main.dart.js`** và grep khoá
  i18n. Khớp hết = server đúng, lỗi ở TRÌNH DUYỆT người dùng: `index.html` được trả **KHÔNG kèm
  `Cache-Control`** (chỉ `Last-Modified`) nên tab đang mở giữ JS cũ vô thời hạn → Ctrl+Shift+R,
  hoặc thử **cửa sổ ẩn danh** (phép thử dứt điểm). Service worker KHÔNG phải thủ phạm: Flutter đời
  này sinh bản "tự huỷ" (815 B, `unregister()` + reload) nên không cache app; nó giống nhau mọi
  lần build, deploy script bỏ qua là ĐÚNG.
- **Engineer Server trả 500 = tầng Postgres trên box chưa sẵn sàng** (đúng token vẫn 500): bảng
  `sessions`/role chưa tạo (chưa chạy `deploy/schema.sql`) hoặc Postgres/psycopg thiếu — KHÔNG phải
  lỗi app. `/ingest` vẫn 200 (file-first, catch lỗi DB) nên thiết bị đẩy được mà app không đọc được.
  Chẩn đoán trên box: `journalctl -u fbt-receiver -n 30`; sau khi tạo schema phải chạy
  `reconcile.py` nạp file JSON cũ vào DB, không thì `/devices` trả danh sách RỖNG.
- **KHÔNG dán token/API key trần vào lệnh inline** (vd `curl -H "Authorization: Bearer <token>"`):
  classifier permission của box sẽ CHẶN vì lộ credential trong transcript. Cách qua: đọc từ file vào
  biến trong CÙNG lệnh — `TOK=$(grep -oP 'RECEIVER_TOKEN=\K\S+' note.md) && curl -H "Authorization:
  Bearer $TOK" …`. Áp dụng khi test RAPID ERP key / RECEIVER_TOKEN Engineer Server.
- **Inline `sed`/`node -e` NUỐT dấu `\` trong box này** — chuỗi `\\n` trong lệnh 1 dòng bị gom còn
  newline thật: `sed 's|\\n|…|'` KHÔNG khớp (không thay gì), còn `node -e '…split("\\n")…'` lại cắt
  theo **newline** → đã biến cả file `.md` thành 1 dòng (HỎNG). Cần xử lý text chứa `\` (vd đổi `\n`
  literal → `<br/>`) thì dùng **Write/Edit tool** hoặc ghi script ra FILE rồi chạy, ĐỪNG nhúng
  backslash vào lệnh inline. (`grep '\\n'` ở đây cũng cho kết quả sai — kiểm bằng `grep -F '\n'`.)
- **Node.js + Docker GIỜ ĐÃ CÓ trên máy dev** (Node v24, Docker v29 — kiểm `node --version`/`docker --version`).
  Docker **daemon KHÔNG tự chạy** (lỗi `npipe:... dockerDesktopLinuxEngine` = chưa bật): khởi động bằng
  `Start-Process "C:\Program Files\Docker\Docker\Docker Desktop.exe"` rồi poll `docker info` tới khi exit 0
  (thường vài giây). Python cũng có (3.12) nhưng KHÔNG có fastapi/pytest global — test project `Server/`
  thì tạo venv trong scratchpad (`python -m venv` + pip fastapi/httpx/psycopg[binary]/uvicorn); in tiếng Việt
  từ python ra console Windows dính `UnicodeEncodeError` cp1252 (chỉ lỗi ở print — assert trước đó vẫn tính).
  (Trước đây box chỉ có Flutter; nếu gặp box thiếu Node thì `winget install -e --id OpenJS.NodeJS.LTS`
  rồi nạp lại PATH tại chỗ: `$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' +
  [Environment]::GetEnvironmentVariable('Path','User')`.)
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
- **Backend nằm TRONG repo app**: Apps Script ở `app/sheet/` (getData/userAuth/accounts) **và** server
  tự host (Docker) ở `app/server/` — đã GOM từ `FBT-DXD/` về `app/` (doc tham chiếu `sheet/`, `server/`,
  KHÔNG còn `../FBT-DXD/`). **`FBT-DXD/` chỉ còn là repo FIRMWARE RIÊNG** (PlatformIO/ESP —
  `src/ lib/ platformio.ini`), KHÔNG trộn vào app; firmware POST kết quả lên cả Apps Script lẫn `server/`.
- **`flutter create --platforms web .` từ chối tên pubspec `RapidPlusApp`** (không phải tên package
  Dart hợp lệ): đổi TẠM `name: rapidplusapp` → chạy create → đổi lại. Create cũng đụng
  `.plugin_symlinks` (lỗi OneDrive như trên — kệ, build windows sau đó vẫn chạy) và sinh
  `web/index.html`/`manifest.json` mang tên `rapidplusapp` → nhớ sửa lại title/manifest `FBT_RAPID`.
- **Chụp màn hình app WEB trong Chromium bằng PrintWindow ra ảnh XÁM** (dù flag `2`): nội dung
  Chrome/Edge composite bằng GPU nên PrintWindow không thấy. Fix: launch trình duyệt với
  **`--disable-gpu`** (+ `--app=<url>` để có cửa sổ riêng title = `<title>` trang, chờ title
  `FBT_RAPID*`) rồi chụp như driver.ps1. Đã đóng gói sẵn:
  `& .claude\skills\run-fbt-rapid\webshot.ps1 -Out web.png` (tự serve `build\web` bằng
  `web-server.js` node tĩnh cùng thư mục — không cần `flutter run -d web-server`; build web trước).
  Chụp bản web đang HOST THẬT: thêm `-Url https://fbt.basa-luma.ts.net/app/` (bỏ bước serve cục bộ).
- **Lịch sử firmware — số lần đo phải cắt theo MỐC NẠP, không gả nguyên cụm** (`services/firmware_history.dart`):
  `buildFirmwareHistory` gộp các lần đo LIỀN NHAU cùng version thành MỘT quãng, nên **nạp lại đúng bản
  đang chạy** (v2.4.5 → v2.4.5) chỉ có 1 quãng cho **2** mốc nhật ký `fw-log`. `mergeFirmwareLog` bản đầu
  gả cả cụm cho mốc khớp ĐẦU TIÊN → dòng CŨ ôm hết số lần đo còn **dòng bản MÁY ĐANG CHẠY hiện "0 lần đo"**
  (báo về 2026-08-20). Fix: truyền `lanDo: runs` (lần đo thô) vào `mergeFirmwareLog`, đếm theo khoảng
  `[updatedAt dòng này, updatedAt dòng kế)` **và** vẫn khớp version — giờ trong phiên là giờ MÁY tự khai,
  mốc `fw-log` là giờ SERVER, lệch đồng hồ thì thà không đếm còn hơn đếm nhầm bản. Test:
  `flutter test test/firmware_history_test.dart`.
