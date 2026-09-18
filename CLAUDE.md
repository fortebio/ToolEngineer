# CLAUDE.md — ToolEngineer (monorepo hệ thống sản phẩm Forte Biotech)

> File này chỉ giữ **bản đồ + quy tắc TOÀN HỆ THỐNG**. Sửa phần nào thì đọc CLAUDE.md của phần đó
> (Claude Code tự nạp CLAUDE.md theo thư mục con). Bài học riêng của một phần ghi vào CLAUDE.md
> của phần đó, KHÔNG ghi vào đây. Phương án gom & lý do: `docs/plan/monorepo-mot-he-thong.md`.

## Bản đồ

| Thư mục | Là gì | Đọc thêm |
|---|---|---|
| `system/` | **Registry sản phẩm** `products.yaml` (nguồn sự thật DUY NHẤT về sản phẩm/khoá `product`/tiền tố mã máy/hw/kênh/firmware) + `contracts/` JSON Schema + **`brand/tokens.json`** (màu/chữ/khoảng cách thương hiệu lấy từ logo gốc, kèm toạ độ vector logo) | chú thích đầu `products.yaml`, `system/brand/README.md` |
| `server/` | Engineer Server (FastAPI + Postgres), box `hub.fortebio.tech` / `fbt.basa-luma.ts.net` | `server/CLAUDE.md`, `server/README.md` |
| `apps/fbt_rapid/` | App FBT_RAPID (Flutter): desktop Windows + web `/app/` | `apps/fbt_rapid/CLAUDE.md` (dài, nhiều gotcha), `README.md`, `docs/01–08` |
| `firmware/rapidplus/` | Firmware Forte Rapid+ (PlatformIO, ESP32) — fleet ~109 máy | `firmware/rapidplus/CLAUDE.md` |
| `firmware/rapidplus-prod/` | Firmware Rapid+ viết lại theo IEC 62304 (dev, chưa nạp máy) | `firmware/rapidplus-prod/CLAUDE.md` |
| `firmware/FBT-Reader/` | Firmware **Forte Rapid Reader** v2.6.8 (PlatformIO, ESP32, core Arduino 1.0.6) — **REPO GIT RIÊNG** `github.com/wuanpham/FBT-Reader` lồng trong cây này, monorepo KHÔNG track (đã cho vào `.gitignore`). Tổ chức lại theo mẫu rapidplus 2026-09-18; bản chép cũ `firmware/reader/` (v2.6.6) đã xoá. **v2.6.8 (2026-09-18): OTA qua Engineer Server** (`/ota/check?product=reader`, token nhập qua web, thẻ `FBTIMG1`) — chưa nạp máy thật; đội máy ≤ v2.6.7 còn đọc GitHub `FBTRapidReaderOTA` nhánh `v2.6.7` | `firmware/FBT-Reader/CLAUDE.md`, `README.md`, `docs/architecture/`, `docs/history/2026-09-18-reader-ota-server.md` |
| `firmware/rapid4p/` | Firmware **Forte Rapid4P** (RAPID READER **5** SLOT — số khe = `BOARD_SENSOR_SLOTS`, kế hoạch `docs/plan/rapid4p-5-slot.md`) — ESP32-P4C5 + LCD 4.3" DSI + touch, **ESP-IDF native** (`build_system: idf`). Đã nạp máy thật 2026-09-17: LCD/touch/WiFi/web dashboard chạy, chưa có bo cảm biến | `firmware/rapid4p/CLAUDE.md`, `README.md` |
| `firmware/maping new product/` | Khảo sát Rapid4P: phần cứng tham chiếu `firmware-vimate-p4/` (dự án ngoài — chỉ đọc, `AGENTS.md`/`README-P4.md` là nhật ký bring-up của board) + firmware gốc `FBT-ReaderPlus-1.0/` (Arduino) + `tham-khao/` (chép chọn lọc **xiaozhi-esp32** — ESP32-P4/DSI/ESP-Hosted, driver AXP2101, ML307 — và **CrossInk** — quy trình/AGENTS.md/heap/simulator; cả hai MIT, chỉ đọc) | `MAPPING-Rapid4P.md` (mapping + quyết định §5), `THAM-KHAO-xiaozhi-CrossInk.md` (bài học + 12 việc đề xuất), `firmware-vimate-p4/docs/HARDWARE-PINOUT.md` |
| `legacy/sheet/` | Apps Script `getData.js` (**CÒN SỐNG**: fw ≤ v2.4.5 + reader vẫn POST) · `userAuth.js` (đường lùi đăng nhập) | `legacy/sheet/README.md` |
| `legacy/server-cf/` | Port Cloudflare Workers, chưa deploy | `legacy/server-cf/README.md` |
| `tools/` | `registry_check.py` (kiểm registry đối chiếu code thật) · `monorepo/assemble.ps1` (script đã lắp repo này) | — |
| `docs/` | Tài liệu XUYÊN PHẦN: `plan/`, `history/`. Tài liệu từng phần nằm trong thư mục của nó | `docs/README.md` |
| `.claude/ .agents/ .codex/` | Skill/hook dùng chung: `run-fbt-rapid` (trỏ vào `apps/fbt_rapid`) + bộ **ui-ux-pro-max** (7 skill: `ui-ux-pro-max`, `design`, `design-system`, `ui-styling`, `brand`, `banner-design`, `slides` — chép từ [nextlevelbuilder/ui-ux-pro-max-skill](https://github.com/nextlevelbuilder/ui-ux-pro-max-skill) 2026-09-17, cài cục bộ, đường dẫn đã đổi sang `.claude/skills/...`; tra cứu: `python .claude/skills/ui-ux-pro-max/scripts/search.py "<query>" --domain style\|color\|ux\|…`) | `.claude/skills/run-fbt-rapid/SKILL.md`, `.claude/skills/ui-ux-pro-max/SKILL.md` |

Hệ NGOÀI repo (chỉ khai báo hợp đồng trong `system/products.yaml` › `external_systems`): **RAPID ERP**
`fbterp` (`api.fortebio.tech`, team khác), tool cơ khí Drawing/DrawingGEN, hubRD.

## Quy tắc toàn hệ thống

1. **Không bao giờ commit bí mật.** `.gitignore` gốc chặn `**/secrets.h`, `*.env`; firmware đọc token từ
   `src/secrets.h` (copy từ `secrets.example.h`, điền tay); server đọc `/etc/fbt-receiver.env` trên box.
   Lịch sử firmware đã bị **tẩy** một lần (2026-09-15) vì file token lọt vào repo — token đó coi như đã lộ.
2. **`system/products.yaml` là nguồn sự thật về sản phẩm.** Thêm/đổi sản phẩm = sửa file đó trước, chạy
   `python tools/registry_check.py` (CI `registry`) rồi mới đụng code. Khoá `product` theo
   `server/app/config.py::valid_product` và KHÔNG đổi khoá đã phát hành.
3. **Firmware phát hành bằng TAG, không bằng nhánh**: `fw/<product>/<FIRMWARE_VERSION>` trên `main`;
   biến thể = `[env:…]` + build flag trong `platformio.ini`. Nhánh `v2.x` cũ của FBT-DXD nằm ở
   `refs/archive/fbt-dxd/*` (+ tag `archive/fbt-dxd/<tên>` cho nhánh có commit riêng), lấy lại bằng
   `git cherry-pick -x <sha>` (path đã prefix `firmware/rapidplus/`).
   Commit thẳng lên `main` và push `origin main` (lịch sử repo làm vậy). **`origin/HEAD` đang trỏ nhánh
   khởi tạo cũ `claude/claude-md-docs-namqct` (1 commit)** — tool nào suy "nhánh mặc định" từ đó là sai;
   nhánh thật là `main`. Working tree checkout CRLF trên Windows → commit file LF sẽ in hàng loạt
   warning `LF will be replaced by CRLF`, vô hại (`git -c core.safecrlf=false commit` cho im).
4. **Route server mới không được là POST** (`POST /{path}` catch-all nuốt hết) — dùng PUT/DELETE; app phải
   nuốt lỗi phòng thủ. Chi tiết: `server/CLAUDE.md`.
5. **Mỗi phần tự ghi `docs/history/YYYY-MM-DD*.md` của mình** khi đổi; thay đổi chạm ≥2 phần ghi ở
   `docs/history/` gốc. Comment & UI tiếng Việt.
6. **Màu/chữ thương hiệu lấy từ `system/brand/tokens.json`** (2026-09-17, từ logo gốc) — LCD (`ui_theme.h`),
   app Flutter, web, portal không tự bịa mã màu; đổi token = sửa JSON trước, đo lại tương phản ≥ 4,5:1.
7. **Đường dẫn trong doc từng phần tính từ thư mục phần đó** (`lib/…`, `test/…` trong CLAUDE.md app =
   `apps/fbt_rapid/lib/…`); đường dẫn tới phần khác viết từ gốc (`server/…`, `legacy/sheet/…`).

## Môi trường box `ADM` (Windows 10, không Visual Studio)

- **Flutter 3.44.1** không trên PATH: `$env:PUB_CACHE="$env:LOCALAPPDATA\Pub\Cache"; &
  C:\Users\ADM\fvm\versions\3.44.1\bin\flutter.bat …` — chạy TRONG `apps/fbt_rapid`. Không build được
  Windows desktop ở đây (chỉ analyze/test/build web). Lọc kết quả `analyze` bằng `grep -E "error|warning"`,
  đừng `| tail`.
- **PlatformIO** không trên PATH: `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e esp32dev`
  trong `firmware/<x>`; firmware/rapidplus cần `src/secrets.h` local (copy từ example) mới build.
- **Python 3.14** (Python Install Manager) bị Windows **ảo hoá `AppData\Local`**: thư mục do git/PowerShell
  tạo dưới `%LOCALAPPDATA%` Python (và `git` con của nó) không nhìn thấy → việc cần Python + git chung chỗ
  (git-filter-repo…) làm dưới `%TEMP%`. **Bộ test local (Postgres portable :5433 + venv + pgdata + kho ota/) nằm THẬT ở
  `%LOCALAPPDATA%\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Local\fbt-localtest`** — "ảo hoá AppData" chính là
  sandbox của gói Claude desktop: phiên AI 2026-09-04 dựng nó tưởng đang ghi vào `%LOCALAPPDATA%\fbt-localtest`,
  nên đường mặc định đó KHÔNG tồn tại với ai cả (tìm ra 2026-09-18 bằng `find …/AppData/Local/Packages
  -maxdepth 4 -iname fbt-localtest`). `server\scripts\localtest.ps1` giờ tự rơi sang đường sandbox khi đường
  mặc định trống (và dọn `postmaster.pid` cũ) → gọi KHÔNG tham số là lên cả Postgres + server :8080 + web
  `/app/` (web build TRƯỚC bằng `flutter build web --release --base-href /app/ --dart-define=FBT_URL=
  http://127.0.0.1:8080 --dart-define=FBT_TOKEN=localtok` trong PowerShell tool, không phải Git Bash).
  Bash tool gọi thẳng `pg_ctl -w start` sẽ TREO tới timeout (server con giữ pipe) → để script làm hoặc chạy
  nền. `registry_check.py` vẫn chạy bằng python hệ thống sau `python -m pip install --user pyyaml jsonschema`
  (đã cài 2026-09-17).
- **Máy `Admin` (khác box ADM)**: `python` trên PATH là Python 3.11 của ESP-IDF (`C:\Espressif\tools\idf-python`)
  **không có pip** → tool Python của repo (`registry_check.py`, pytest server) chạy bằng venv IDF
  `C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe` (đã cài pyyaml/jsonschema/pytest/httpx 2026-09-17);
  venv `%LOCALAPPDATA%\fbt-localtest` KHÔNG có trên máy này.
  Git trên máy này **không có user.name/email toàn cục** → `git commit` fail ("Author identity unknown")
  nhưng lệnh `git push` nối sau vẫn đẩy HEAD CŨ lên (trông như thành công) — đã đặt identity cục bộ cho
  repo (`git config user.email fbt.engineer@gmail.com`, 2026-09-17); sau commit phải kiểm `git log -1`.
- Node 24, Docker 29 (daemon không tự chạy), Git 2.50 (`git subtree` có; `git filter-repo` cài
  `pip install --user git-filter-repo`, gọi `python -m git_filter_repo`).

## Kiểm nhanh (từ gốc)

```powershell
python tools\registry_check.py                                   # registry đối chiếu code thật
Set-Location server; & "$env:LOCALAPPDATA\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Local\fbt-localtest\venv\Scripts\python.exe" -m pytest tests -q
Set-Location apps\fbt_rapid; & C:\Users\ADM\fvm\versions\3.44.1\bin\flutter.bat analyze lib/ 2>&1 | Select-String "error|warning|No issues"
Set-Location firmware\rapidplus; & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e esp32dev
Set-Location firmware\rapid4p; cmd /c scripts\build.bat                 # ESP-IDF 5.5.1 tại C:\Espressif → BUILD_EXIT=0
```

## Gotchas môi trường máy dev (chuyển từ CLAUDE.md app 2026-09-15 — đã gặp thật)

- **File `.ps1` có tiếng Việt PHẢI lưu UTF-8 CÓ BOM**: PowerShell 5.1 đọc file không BOM theo ANSI →
  chuỗi vỡ → lỗi parse "ma" ở dòng vô can (`token '&&' is not a valid statement separator`, "missing
  terminator"). Tool Write ghi KHÔNG BOM → sau khi viết phải thêm BOM
  (`[IO.File]::WriteAllText(p, t, (New-Object Text.UTF8Encoding($true)))`); kiểm nhanh bằng
  `[Management.Automation.Language.Parser]::ParseFile` (ANSI) so với `ParseInput` (UTF-8).
- **`mv`/`rm -rf` thư mục báo `Device or resource busy` (Windows)**: do **shell Bash đang `cd` BÊN TRONG** thư
  mục đó (CWD persist giữa các call) hoặc IDE/Docker giữ handle. Cách xử lý: `cd` ra ngoài hẳn → `cp -r` sang
  đích (copy đọc được dù bị giữ) → xoá nguồn bằng **PowerShell `Remove-Item -Recurse -Force`** (qua được khoá
  mà `rm -rf` của Git Bash không qua).
- **Shell môi trường KHÔNG có `jq`** — viết hook/script xử lý JSON bằng **bash thuần** (`case`/`grep`)
  hoặc node/python, đừng phụ thuộc `jq`. (Windows `python` cũng không hiểu path `/tmp` của Git Bash.)
- **KHÔNG dán token/API key trần vào lệnh inline** (vd `curl -H "Authorization: Bearer <token>"`):
  classifier permission của box sẽ CHẶN vì lộ credential trong transcript. Cách qua: đọc từ file vào
  biến trong CÙNG lệnh — `TOK=$(grep -oP 'RECEIVER_TOKEN=\K\S+' note.md) && curl -H "Authorization:
  Bearer $TOK" …`. Áp dụng khi test RAPID ERP key / RECEIVER_TOKEN Engineer Server.
- **Inline `sed`/`node -e` NUỐT dấu `\` trong box này** — chuỗi `\\n` trong lệnh 1 dòng bị gom còn
  newline thật: `sed 's|\\n|…|'` KHÔNG khớp (không thay gì), còn `node -e '…split("\\n")…'` lại cắt
  theo **newline** → đã biến cả file `.md` thành 1 dòng (HỎNG). Cần xử lý text chứa `\` (vd đổi `\n`
  literal → `<br/>`) thì dùng **Write/Edit tool** hoặc ghi script ra FILE rồi chạy, ĐỪNG nhúng
  backslash vào lệnh inline. (`grep '\\n'` ở đây cũng cho kết quả sai — kiểm bằng `grep -F '\n'`.)
  **Heredoc `<<'EOF'` trong Bash tool CŨNG nuốt** (2026-09-11: regex `[^/\\]` ghi ra file thành `[^/\]`
  → lỗi compile) → file Python/regex có `\` phải ghi bằng Write tool. **Kể cả ĐƯỜNG DẪN WINDOWS trong
  văn bản**: 2026-09-18 heredoc biến `LocalCache\Local\fbt-localtest` thành `Local<form-feed>bt-localtest`
  (`\f`), `\r`/`\n`/`\t` sau backslash cũng vậy → mọi patch text có `\` ghi script ra FILE bằng Write tool rồi
  `python file.py`; sau patch `grep -c $'\f'` để chắc. Python in tiếng Việt ra console
  dính cp1252 → đặt `PYTHONIOENCODING=utf-8` trước lệnh. **Backtick trong `python -c "…"` (nháy kép) bị
  bash command-substitute** (2026-09-18: `` `system/contracts/x.json` `` trong chuỗi Markdown → bash chạy nó,
  ghi ra chuỗi RỖNG mà script vẫn báo "ok"); heredoc `<<'PYEOF'` dài chứa `~~`/nháy lẫn lộn cũng vỡ parse
  ("unexpected EOF") → cùng cách xử lý: script ra FILE. Sau patch text Markdown, `grep` lại anchor vừa ghi.
- **Nhiều phiên Claude cùng sửa MỘT cây làm việc** (2026-09-18: 6 phiên song song; `products.yaml`, `define.h`
  đổi giữa lúc đọc và lúc ghi — phiên khác vừa làm xong OTA reader v2.6.8 mà phiên này đang lên kế hoạch
  chính phần đó). Trước khi ghi file đã đọc từ lâu: `git status`/`md5sum` lại, script patch dùng
  **anchor phải xuất hiện đúng 1 lần** (`assert t.count(old) == 1`) để bắt lệch thay vì ghi đè mù;
  `ListAgents` cho biết phiên nào đang chạy/idle; quyết định đã chốt có thể **đã bị một phiên khác làm
  theo hướng ngược lại** → đọc `docs/history/` mới nhất của phần đó trước khi hỏi người dùng chốt.
  **Gộp nhánh của các phiên song song vào `main`** (2026-09-18): xung đột lặp ở 2 chỗ — **cuối `CLAUDE.md`
  từng phần** (mỗi phiên nối thêm một gotcha vào đuôi → giữ CẢ HAI, chỉ xoá marker) và
  **`.vscode/settings.json`** (`cmake.sourceDirectory` là đường dẫn tuyệt đối máy cục bộ, mỗi nhánh trỏ
  firmware khác → lấy bản của nhánh đang gộp). Dò trước KHÔNG đụng cây làm việc:
  `git merge-tree --write-tree --name-only main <nhánh>` (Git ≥ 2.38) in tên file xung đột; sau merge
  chạy `python tools/registry_check.py` rồi mới push.
- **Classifier permission của box chặn 3 việc "trông nguy hiểm" dù vô hại** (gặp 2026-09-12): (1) mở
  HTTP server ra mạng (`python -m http.server --bind <IP tailnet>`); (2) ghi FILE hướng dẫn có chứa lệnh
  kiểu `mv/rm … /path/*` hoặc `echo key >> authorized_keys` — mô tả bằng lời trong chat thì được;
  (3) Bash gọi `Remove-Item -Force` qua chuỗi. Đừng loay hoay lách; chuyển sang đường khác (dán base64
  qua terminal, để người dùng tự chạy lệnh đổi cấu hình bảo mật). (4) `npm install -g <gói bên thứ ba>`
  ("Untrusted Code Integration", gặp 2026-09-17 với `ui-ux-pro-max-cli`) — thay bằng `git clone --depth 1`
  vào scratchpad rồi **chép file** sang đích (không thực thi gì); gói skill Claude Code thường cũng chỉ là
  `.claude/skills/<tên>/` trong repo của họ.
- **Chép dự án ngoài vào repo để tham khảo** (`firmware/maping new product/tham-khao/`, 2026-09-17): clone
  `--depth 1` vào scratchpad rồi **chép chọn lọc** (docs + module trúng bài toán, bỏ asset/lib nặng — 110 MB
  → 3 MB), ghi commit SHA + giấy phép vào `README.md` của thư mục. **Đổi tên** `.claude/ → _claude/`,
  `CLAUDE.md → CLAUDE.md.orig`, `.github/ → _github/` — Claude Code nạp CLAUDE.md và quét `.claude/skills/`
  lồng trong thư mục con nên skill/luật của dự án ngoài sẽ chui vào phiên làm việc. `AGENTS.md` giữ nguyên.
- **Node.js + Docker GIỜ ĐÃ CÓ trên máy dev** (Node v24, Docker v29 — kiểm `node --version`/`docker --version`).
  Docker **daemon KHÔNG tự chạy** (lỗi `npipe:... dockerDesktopLinuxEngine` = chưa bật): khởi động bằng
  `Start-Process "C:\Program Files\Docker\Docker\Docker Desktop.exe"` rồi poll `docker info` tới khi exit 0
  (thường vài giây). ⚠️ **Trên box ADM Docker Desktop 4.86 KHÔNG lên được** (2026-09-18: `wsl --status` →
  `WSL_E_WSL_OPTIONAL_COMPONENT_REQUIRED`, cần admin cài WSL + reboot) → đừng dựa vào Docker cho Postgres
  local; dùng Postgres portable của bộ fbt-localtest (trên). Python cũng có (3.12) nhưng KHÔNG có fastapi/pytest global — test project `Server/`
  thì tạo venv trong scratchpad (`python -m venv` + pip fastapi/httpx/psycopg[binary]/uvicorn); in tiếng Việt
  từ python ra console Windows dính `UnicodeEncodeError` cp1252 (chỉ lỗi ở print — assert trước đó vẫn tính).
  (Trước đây box chỉ có Flutter; nếu gặp box thiếu Node thì `winget install -e --id OpenJS.NodeJS.LTS`
  rồi nạp lại PATH tại chỗ: `$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' +
  [Environment]::GetEnvironmentVariable('Path','User')`.)
