# CLAUDE.md — FBT_RAPID App (Flutter / Windows)

Hướng dẫn cho AI/người phát triển. Danh sách **tính năng đầy đủ** xem [README.md](README.md);
file này tập trung vào **kiến trúc, lệnh, quy ước, và các cạm bẫy (gotchas)**.

## Tổng quan
App desktop **Windows** (Flutter) đồng hành thiết bị xét nghiệm **FBT_RAPID / Forte Rapid+**:
đăng nhập phân quyền → xem **lịch sử + đồ thị CT** (cục bộ qua HTTP `/getdata`, và cloud qua Google
Drive), **theo dõi nhiệt độ realtime qua UART/COM**, lưu kết quả/đồ thị ra file.

- Tên hiển thị: **FBT_RAPID** · tên gói (`pubspec name`): `RapidPlusApp` · **tên exe**: `fbt_dxd_app.exe`
  (đặt trong `windows/CMakeLists.txt` → `BINARY_NAME`). Ba tên này KHÁC nhau — dễ nhầm.
- Chỉ build/chạy **Windows desktop** (chưa làm android/ios/web).

## Lệnh hay dùng (PowerShell)
```powershell
flutter pub get
flutter run -d windows                 # chạy có hot reload
flutter build windows --debug          # build nhanh để test  → build\windows\x64\runner\Debug\fbt_dxd_app.exe
flutter build windows --release        # build phát hành      → ...\Release\
flutter analyze lib/<file>...          # lint nhanh vài file (đừng analyze cả repo nếu không cần)
```
Đóng gói installer (Inno Setup) — **phải `--release` TRƯỚC** vì script trỏ vào thư mục Release:
```powershell
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" "C:\Users\nvdat\Downloads\app\installer.iss"
# → C:\Users\nvdat\Downloads\FBT_RAPID-Setup-vX.Y.Z.exe
```
**Chạy + chụp màn hình tự động** (cho AI/agent — GUI không có curl/Playwright): skill
`.claude/skills/run-fbt-rapid/` (`driver.ps1`) build/launch `fbt_dxd_app.exe` rồi chụp ĐÚNG cửa
sổ ra PNG. Vd `& .claude\skills\run-fbt-rapid\driver.ps1` (launch Debug → chụp `_smoke.png` →
đóng; cờ `-KeepOpen`/`-Attach`/`-Release`).

## Kiến trúc
- **Entry**: `lib/main.dart` → `_AuthGate` khôi phục phiên đã lưu → `LoginScreen` (chưa đăng nhập)
  hoặc `HomeShell` (đã đăng nhập).
- **`HomeShell`** (`NavigationRail` dọc + `IndexedStack`): thanh dọc chỉ chứa **tab nội dung** theo vai
  trò — khách hàng (`user`): **Lịch sử**; nhân sự (`admin`/`root`): **+ Kỹ Thuật**; **`root`**: **+ Quản lý
  User**. Tab **Kỹ Thuật** (`tech_screen.dart`, mẫu segmented giống Lịch sử) GỘP 3 công cụ: **Log nhiệt**
  (`temperature_log_screen.dart`) | **Đọc serial** (`serial_console_screen.dart`) | **Nạp code**
  (`flasher_screen.dart`). **Thiết lập + Đăng xuất** KHÔNG còn là tab mà nằm trong **menu của icon tài khoản**
  (shield = nhân sự root/admin · person = khách hàng) ở `trailing`. Tab **Lịch sử** là màn
  GỘP `history_combined_screen.dart` (nút gạt **Cục bộ | Cloud**). Thiết lập đẩy như route (admin
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
  - **2 nguồn cloud, 1 giao diện chung** `CloudHistoryClient` (trong `cloud_history_api.dart`):
    màn `cloud_devices`/`cloud_runs` chỉ phụ thuộc interface; chọn lớp triển khai qua factory
    **`buildCloudClient(settings, source)`** (trong `rapid_erp_api.dart`) theo `enum CloudSource
    {google, rapidErp}` (`app_settings.dart`, helper `cloudUrlFor/cloudHeadersFor`).
    Thêm nguồn = thêm 1 lớp `implements CloudHistoryClient` + 1 nhánh factory, KHÔNG sửa UI.
    (Nguồn **server tự host (self-hosted)** ĐÃ BỎ khỏi app — `server/` còn nhưng app không gọi.)
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
  - `auth_api.dart` — `POST` Apps Script accounts (đăng nhập — **vẫn trên Apps Script**).
- **Lưu file**: gốc = `StoragePaths.parent` (static, set từ Cài đặt, mặc định `Documents`).
  Kết quả CT → `FBT_RAPID_ketqua\`; log nhiệt → `FBT_RAPID_templog\`; log đọc serial → `FBT_RAPID_seriallog\`
  (`<COM>_<thời gian>.txt`). Mở thư mục/chọn file = `Process.run('explorer.exe', ['/select,', path])`.
- **Đồ thị** (`fl_chart`): `widgets/ct_chart.dart` (CT), `widgets/temp_chart.dart` (nhiệt). Lưu ảnh =
  bọc `RepaintBoundary` rồi `util/chart_capture.dart::captureBoundaryPng` (chụp off-screen qua Overlay).

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

### Server tự host (Docker, `server/`) — backend ĐỘC LẬP (app KHÔNG còn gọi)

> **App đã BỎ nguồn self-hosted** (chỉ còn Google + RAPID ERP). `server/` vẫn giữ làm backend
> đứng riêng (firmware vẫn POST `/ingest` được); muốn app đọc lại thì khôi phục `CloudSource.selfHosted`.
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
- **Giao diện CHUNG nằm ở `lib/main.dart::_theme(Brightness)`** — theme **token-driven** (M3, seed
  `0xFF1565C0`; nền `#F5F7FA`, thẻ phẳng viền mảnh, input/nút bo góc filled, density `standard` cho
  desktop). Đổi "look" toàn app → sửa ở đây (mọi màn thừa hưởng, cả dark mode). Trong màn con **luôn
  dùng `Theme.of(context).colorScheme.*`** (vd `onSurfaceVariant`, `primaryContainer`), KHÔNG hardcode
  `Colors.grey/black` (sai tương phản dark mode + lệch theme). Số liệu/ngày trong list dùng
  `FontFeature.tabularFigures()` để canh cột.
- **Tự cập nhật doc**: `.claude/settings.json` có **Stop hook** nhắc bổ sung bài học mới vào CLAUDE.md sau
  mỗi lượt (chống lặp bằng cờ `stop_hook_active`). Vì vậy hãy giữ file này luôn cập nhật.

## Gotchas (đã gặp thật — đừng dẫm lại)
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
- **Inline `sed`/`node -e` NUỐT dấu `\` trong box này** — chuỗi `\\n` trong lệnh 1 dòng bị gom còn
  newline thật: `sed 's|\\n|…|'` KHÔNG khớp (không thay gì), còn `node -e '…split("\\n")…'` lại cắt
  theo **newline** → đã biến cả file `.md` thành 1 dòng (HỎNG). Cần xử lý text chứa `\` (vd đổi `\n`
  literal → `<br/>`) thì dùng **Write/Edit tool** hoặc ghi script ra FILE rồi chạy, ĐỪNG nhúng
  backslash vào lệnh inline. (`grep '\\n'` ở đây cũng cho kết quả sai — kiểm bằng `grep -F '\n'`.)
- **Node.js + Docker GIỜ ĐÃ CÓ trên máy dev** (Node v24, Docker v29 — kiểm `node --version`/`docker --version`).
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
  `setFlowControl(none)`) để ghim trạng thái KHÔNG-reset (đã áp `serial_console_screen.dart`; `temperature_serial.dart`
  còn latent — thêm khi log nhiệt cũng reset máy). Config chỉ áp lúc **mở cổng** → phải đóng/mở lại mới ăn.
  Nếu reset **CHỈ khi gửi** (không reset lúc mở) thì là **firmware tự reboot theo lệnh nhận được** (crash/
  watchdog/lệnh reset), sửa ở firmware FBT-DXD — không phải app; xem RX có banner boot để phân biệt.
- **Backend nằm TRONG repo app**: Apps Script ở `app/sheet/` (getData/userAuth/accounts) **và** server
  tự host (Docker) ở `app/server/` — đã GOM từ `FBT-DXD/` về `app/` (doc tham chiếu `sheet/`, `server/`,
  KHÔNG còn `../FBT-DXD/`). **`FBT-DXD/` chỉ còn là repo FIRMWARE RIÊNG** (PlatformIO/ESP —
  `src/ lib/ platformio.ini`), KHÔNG trộn vào app; firmware POST kết quả lên cả Apps Script lẫn `server/`.
