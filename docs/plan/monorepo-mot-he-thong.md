# Phương án gom toàn bộ sản phẩm vào MỘT monorepo `ToolEngineer` (server · client · firmware)

> Trạng thái: **P0 đang thực hiện** (2026-09-15). Chốt với chủ dự án 2026-09-14. Tài liệu này là
> bản ghi quyết định; tiến độ từng bước ghi ở `docs/history/2026-09-14-monorepo.md` (gốc monorepo).

## 1. Vì sao

Công ty đang có **một hệ thống nhưng rải ở 4 repo + 2 thư mục không git**, mỗi nơi một quy trình:

| Nguồn | Là gì | Trạng thái lúc khảo sát |
|---|---|---|
| `00. Tool/03. FBT-ToolRapidPlus` (repo **A**, `wuanpham/FBT-ToolRapidPlus`, 18 commit) | App FBT_RAPID (Flutter desktop + web) **và** Engineer Server (`server/`, FastAPI + Postgres, box `hub.fortebio.tech`) + Apps Script `sheet/` + port CF `server-cf/` | Đang dùng, deploy 12/09 |
| `04.RapidPlus/03.Firmware/FBT-DXD` (repo **B**, `wuanpham/FBT-DXD`, ~107 commit) | Firmware RapidPlus ESP32, fleet ~109 máy | Đang phát triển, **nhánh-theo-phiên-bản** (~45 nhánh `v2.x`; 14/09 ở `v2.4.5`, 15/09 đã sang `v2.4.5at`) |
| `04.RapidPlus/03.Firmware/FBT-RapidPlus-Production` (repo **C**, 0 commit) | Scaffold firmware viết lại IEC 62304 | Chưa nạp máy |
| `02.RapidReader/PRV-Reader/reader` (repo **R**, `fortebio/reader`, 3 commit) | Firmware Reader v2.6.6 | Đang bán, chỉ POST Apps Script |
| `00. Tool/06. ToolEngineer` (`fortebio/ToolEngineer`) | **Repo rỗng** | Đích |

Hai sự thật quyết định cách làm:

1. **Nhánh đang phát triển của repo B vẫn track `src/secrets.h` chứa token thật** (ingest token
   Engineer Server + API key ERP; 4 phiên bản trong lịch sử). Commit gỡ file (`3f1d1db`) nằm nhánh
   khác chưa merge; `.gitignore` có dòng `# src/secrets.h` bị comment. → Lịch sử B phải **tẩy** trước
   khi vào repo của org, và token phải **xoay** (đã lộ trên GitHub `wuanpham/FBT-DXD`).
2. Server đã có nền đa sản phẩm (kho OTA `products/<product>/`, thẻ `FBTIMG1`, `/devices` trả
   `product/hw`, 95 test — `docs/plan/ota-nhieu-san-pham.md` giai đoạn 0) nhưng **app và firmware
   chưa có khái niệm `product`**; 10 slot / 6 kênh / tiền tố `RPL` / tên file `fbt_v<ver>.bin` còn
   cứng ở ~12 chỗ (`test_result.dart:216,275`, `temp_types.dart`, `log_triage.dart:153`,
   `fbt_api.dart:337-442`, `manager_machine_screen.dart:1984`, `server/app/config.py ARRAY_FIELDS`…).
   Một chỗ đứng chung + một file registry là cách gỡ dần các chỗ cứng đó mà không phải đoán.

## 2. Quyết định đã chốt (chủ dự án, 2026-09-14)

- **"Một" = một cây thư mục / monorepo**, không phải chỉ một tài liệu kiến trúc.
- **Phạm vi VÀO**: Engineer Server + app FBT_RAPID + firmware thiết bị (RapidPlus, Production, Reader).
  **ĐỨNG NGOÀI** (chỉ khai báo hợp đồng tích hợp trong registry): RAPID ERP `fbterp` (team khác,
  production), tool cơ khí `Drawing`/`DrawingGEN`, `hubRD`.
- **Sản phẩm phủ** (khoá `product`): `rapidplus` (+ `rapidplus-a` = build `SHAPE_RULE_NEGATIVE`,
  `rapidplus-prod` = firmware viết lại), `reader`, `readermax`; mô hình mở cho Delta/Tray…; DxD hub
  KHÔNG vào registry (legacy 2024).
- **Giữ lịch sử git** bằng `git subtree` (không copy sạch), rewrite lịch sử **chỉ vì bảo mật**.
- `pubspec name: RapidPlusApp` **không đổi** (đổi = đổi mọi `package:` import).

## 3. Kiến trúc đích

```
ToolEngineer/
├── README.md · CLAUDE.md (≤80 dòng: bản đồ + quy tắc chung) · AGENTS.md · .gitignore · .gitattributes
├── .github/workflows/          # 6 workflow lọc theo path (mục 7)
├── .claude/ .agents/ .codex/   # từ A, sửa path + hook
├── system/                     # ← "1 system file"
│   ├── products.yaml           # registry sản phẩm — nguồn sự thật DUY NHẤT
│   ├── contracts/              # products.schema.json; (P2) ingest-rapidplus.schema.json, ota-manifest…
│   └── openapi/                # (P2) snapshot engineer-server.json, CI so lệch với app.openapi()
├── server/                     # Engineer Server nguyên khối ← A/server (CLAUDE.md riêng giữ)
├── apps/fbt_rapid/             # Flutter app ← phần còn lại của A (lib test web assets pubspec docs design tool/ …)
├── firmware/
│   ├── rapidplus/              # ← B, lịch sử ĐÃ TẨY secrets.h
│   ├── rapidplus-prod/         # ← C (copy sạch — nguồn 0 commit)
│   ├── reader/                 # ← R (subtree)
│   └── common/                 # (P3) fbt_image_tag.h, fbt_ota_client, _template/
├── legacy/
│   ├── sheet/                  # ← A/sheet; getData.js CÒN SỐNG (fw ≤ v2.4.5 + reader gọi)
│   └── server-cf/              # ← A/server-cf (chưa deploy)
├── tools/
│   ├── monorepo/assemble.ps1   # script lắp (có -DryRun) — chạy MỘT lần
│   └── registry_check.py       # pha 1 tiêu thụ registry
└── docs/                       # tài liệu XUYÊN PHẦN; docs 01–08 của app Ở LẠI apps/fbt_rapid/docs
    ├── README.md · plan/monorepo-mot-he-thong.md · history/2026-09-14-monorepo.md
```

Nguyên tắc: (1) mỗi tầng giữ nguyên bố cục nội bộ + `CLAUDE.md`/`.gitignore` riêng — Claude Code
nạp CLAUDE.md theo thư mục con, nên chia doc theo thư mục là đúng cơ chế; (2) không dời `docs/01–08`
của app lên gốc (link tương đối trong CLAUDE.md/README app sẽ gãy); (3) `tool/` (số ít, Dart CLI)
ở lại app, `tools/` (số nhiều) là của monorepo; (4) `server/OTA/firmware.bin` (2 MB đang track) →
`git rm --cached` + ignore; (5) không rewrite vì file to (`docs/architecture/02-rtos-tasks.html`
17.5 MB đã trong pack, để P3 `git rm`).

## 4. Cách lắp (P0 — `tools/monorepo/assemble.ps1`)

Chạy cục bộ, **KHÔNG push**, repo nguồn chỉ đọc (trừ một commit vào A chứa chính 6 file P0 để chúng
đi theo lịch sử). `-DryRun` in mọi lệnh trước.

1. **Commit gốc**: README, `.gitignore` (`**/secrets.h`, `.pio/`, `server/OTA/`…), `.gitattributes`
   **chỉ đánh dấu binary** — không `text=auto` để khỏi renormalize hàng loạt blob đã import.
2. **Repo A** → `git subtree add --prefix=_import/app` rồi `git mv` ra đúng chỗ trong MỘT commit
   (`server/`, `legacy/{sheet,server-cf}`, `.claude/ .agents/ .codex/`, `system/`, `tools/`,
   `docs/plan/monorepo-…`, phần còn lại → `apps/fbt_rapid/`). Chọn cách này thay vì `subtree split`
   server trước: một lần import, lịch sử server không bị nhân đôi, `git log --follow` vẫn lần qua
   rename; cái mất là không `subtree push` ngược — không cần vì repo cũ sẽ đóng băng.
3. **Repo B**: `git clone --mirror` vào scratch → `git filter-repo --invert-paths --path src/secrets.h`
   → kiểm `git log --all -- src/secrets.h` rỗng + `git grep -F -f tokens.txt $(git rev-list --all)`
   rỗng (giá trị token trích từ `secrets.h` cây làm việc, **không in ra**; còn sót → `--replace-text`)
   → `git subtree add --prefix=firmware/rapidplus <nhánh hiện tại của B>` → tag `fw/rapidplus/<ver>`
   và `fw/rapidplus-a/<ver>` đọc từ `src/define.h` (2 nhánh `#ifdef SHAPE_RULE_NEGATIVE`/`#else`)
   → **mọi nhánh cũ** giữ dưới `refs/archive/fbt-dxd/<tên>` (ẩn khỏi `git branch`, không gc, push
   riêng khi cần); nhánh nào có commit riêng không nằm trong nhánh import → thêm tag
   `archive/fbt-dxd/<tên>` để nhìn thấy được. Hash B **đổi** → clone cũ FBT-DXD phải clone lại.
4. **Repo R** → `git subtree add --prefix=firmware/reader`, tag `fw/reader/v2.6.6`.
5. **Repo C** → `robocopy` sạch (bỏ `.git .pio .vscode`), commit import.

Sau đó 3 commit tay: `fix(paths)` (mục 5) → `docs: tách CLAUDE.md` (mục 6) → `ci:` (mục 7), rồi bộ
kiểm chứng mục 9.

## 5. Chỗ gãy vì server/ và app tách thành 2 thư mục anh em (`fix(paths)`)

| File | Sửa |
|---|---|
| `server/scripts/deploy.ps1` | `$repo` → gốc monorepo; web build ở `apps\fbt_rapid\build\web_prod`; thêm `-WebProd` ghi đè |
| `server/scripts/localtest.ps1` | tương tự → `apps\fbt_rapid\build\web` |
| `.claude/skills/run-fbt-rapid/driver.ps1`, `webshot.ps1`, `SKILL.md` | exe/web build dưới `apps\fbt_rapid\`; lệnh flutter kèm `cd apps/fbt_rapid` |
| `.claude/settings.json` | Stop hook: "CLAUDE.md của PHẦN đang sửa; gốc chỉ khi là quy tắc toàn hệ"; bỏ permission path máy cũ `nvdat` |
| `firmware/rapidplus/.gitignore` | bật lại dòng `src/secrets.h` |
| KHÔNG đổi | `apps/fbt_rapid/deploy-web.ps1`, `installer.iss` (tự lấy thư mục chứa mình); `.gitignore` con từng phần |

## 6. Tách CLAUDE.md (815 dòng của A)

- **Gốc** (mới, ngắn): bản đồ thư mục; bảng "sửa phần nào → đọc CLAUDE.md nào"; môi trường box ADM;
  quy tắc chung (không commit secrets; registry là nguồn sự thật; tag `fw/<product>/<ver>` =
  `FIRMWARE_VERSION`; docs/history theo phần; CI xanh; khoá product theo regex server); lệnh kiểm nhanh.
- **`apps/fbt_rapid/CLAUDE.md`**: giữ nguyên nội dung app; **thay** mục "Server tự host (Docker) —
  app KHÔNG gọi" (sai thực tế) bằng mô tả đúng (FastAPI, app gọi `/auth /devices /sessions /ota /ate
  /logs /monitor`) + link `../../server/CLAUDE.md`; sửa đường dẫn.
- **`server/CLAUDE.md`**: nhận các gotcha scp/ssh/box/OTA/deploy đang nằm bên app.
- `legacy/sheet/README.md`, `docs/README.md`, `docs/history/2026-09-14-monorepo.md`.

## 7. CI (`.github/workflows/`, lọc theo `paths:`)

| Workflow | paths | Việc |
|---|---|---|
| `server.yml` | `server/**`, `system/**` | Python 3.12, `pytest -q` trong `server/` (không cần Postgres) |
| `app.yml` | `apps/fbt_rapid/**` | Flutter 3.44.1 (`.fvm/fvm_config.json`), `analyze lib/` → `test` → `build web` smoke |
| `firmware-rapidplus.yml` | `firmware/rapidplus/**`, `firmware/common/**` | PlatformIO, `cp secrets.example.h secrets.h`, `pio run -e esp32dev -e esp32dev_shape_neg`, artifact `.bin` |
| `firmware-others.yml` | `firmware/rapidplus-prod/**`, `firmware/reader/**` | matrix; reader `continue-on-error` (platform cũ) |
| `registry.yml` | `system/**`, `tools/registry_check.py`, `firmware/*/platformio.ini`, file version, `server/app/config.py`, `server/deploy/*.example` | `registry_check.py --json` |
| `secrets.yml` | mọi push/PR + lịch tuần | gitleaks `fetch-depth: 0`; allowlist `**/secrets.example.h` |

## 8. Registry `system/products.yaml` và lộ trình tiêu thụ

Nội dung/luật xem chú thích đầu file. Tiêu thụ theo pha:

| Pha | Ai đọc | Làm gì |
|---|---|---|
| **1 (P0)** | `tools/registry_check.py` (CI `registry`) | validate schema; khoá qua **chính** `server/app/config.py::valid_product`; `OTA_LEGACY_PRODUCT_BY_PREFIX` kỳ vọng == `server/deploy/fbt-receiver.env.example`; thư mục firmware/`[env:]`/build flag tồn tại; version regex bắt được; `--check-tag`; `array_fields` == `ARRAY_FIELDS` |
| 2 (P2) | server `app/registry.py` | đọc yaml (env `FBT_REGISTRY`, `deploy.ps1` scp thêm); suy `LEGACY_PRODUCT_BY_PREFIX`; `validate()` kiểm độ dài mảng theo `optical_slots` thay hằng 10 |
| 3 (P2/P3) | app | `tools/gen_products_dart.py` → `lib/generated/products.g.dart` (`ProductSpec`), thay 10/6/RPL/tên file OTA cứng; file sinh được commit, CI `git diff --exit-code` |
| 4 (P3) | firmware | `gen_fbt_product_h.py` → `include/fbt_product.h` (`FBT_PRODUCT`, `FBT_IMAGE_PATTERN`, `FBT_IMAGE_TAG[]` `__attribute__((used))`) qua `extra_scripts` |

**Quy trình firmware đổi**: chỉ `main`; phát hành = tag `fw/<product>/<FIRMWARE_VERSION>`; biến thể
= `[env:…]` + build flag (`esp32dev`→`rapidplus`, `esp32dev_shape_neg`→`rapidplus-a`; biến thể AT
đang là `#ifdef` trong `define.h` → P3 thành `[env:esp32dev_at]`); nhánh cũ có commit riêng
(`v2.4.3AT`, `v244-alg`…) **không ép merge**, lấy lại bằng `git cherry-pick -x -Xsubtree=firmware/rapidplus <sha>`.

## 9. Kiểm chứng P0 (box ADM — không có Visual Studio, chỉ analyze/test/web)

1. Lịch sử: `git log --follow -- server/app/main.py`, `-- apps/fbt_rapid/lib/main.dart` > 1 commit;
   `git log -- firmware/rapidplus/src/define.h` nhiều commit; `git tag -l "fw/*"`; `refs/archive/` ≈ 45.
2. Token: `git log --all -- firmware/rapidplus/src/secrets.h` rỗng; `-- src/secrets.h` rỗng;
   `git grep -I -l -F -f tokens.txt $(git rev-list --all)` rỗng → xoá `tokens.txt`.
3. `python tools/registry_check.py` ĐẠT.
4. `pytest server/tests -q` (venv `%LOCALAPPDATA%\fbt-localtest\venv`) 95 pass.
5. `apps/fbt_rapid`: `flutter analyze lib/` (lọc `error|warning`), `flutter test`, `flutter build web --release`.
6. `firmware/rapidplus`: `Copy-Item src\secrets.example.h src\secrets.h` → `pio run -e esp32dev` và
   `-e esp32dev_shape_neg`; `git status --short` PHẢI rỗng (bằng chứng `.gitignore` đúng).
7. `server\scripts\deploy.ps1 -Web -DryRun` in đúng `apps\fbt_rapid\build\web_prod`; `localtest.ps1` mở `/app/` 200.

## 10. P1 — Cutover (việc của chủ dự án)

1. **Xoay token đã lộ**: `RECEIVER_TOKEN` (`/etc/fbt-receiver.env`) + X-API-Key phía ERP. Fleet
   ≤ v2.4.5 nhúng token cũ → dùng đúng quy trình 4 bước trong `server/app/config.py` (`RECEIVER_TOKEN`
   mới + `RECEIVER_TOKENS_OLD` cũ → OTA v2.4.6 → mọi máy báo về → bỏ cũ).
2. Push `main` + `--tags` + `refs/archive/*:refs/archive/*` lên `fortebio/ToolEngineer`; branch
   protection (`secrets` + `registry` bắt buộc xanh).
3. Đóng băng repo cũ ngay khi push (tag `pre-monorepo` ở A, B). Vá lẻ lỡ phát sinh:
   `git format-patch` bên cũ → `git am --directory=apps/fbt_rapid` (hoặc `--directory=server`).
4. Archive repo cũ khi đủ 4 điều kiện: CI xanh · đã xoay token · 1 lần `deploy.ps1 -Server -Web`
   từ monorepo thành công · 1 bản `pio run` từ monorepo nạp thử máy bàn. B còn token trong lịch sử
   trên GitHub → sau xoay cân nhắc private/xoá.
5. Dọn: `.git` rỗng của repo C, thư mục `FBT-Reader-2.6.6` trùng, `03. FBT-ToolRapidPlus.rar`.

**Rollback**: monorepo hoàn toàn cộng thêm; repo nguồn không bị chạm; box nhận bản copy phẳng nên
deploy cũ/mới như nhau; fleet không biết gì. Xoá nội dung `06. ToolEngineer` + chạy lại `assemble.ps1`.

## 11. P2 / P3 (định hướng)

- **P2**: registry pha 2 + 3; firmware v2.4.6 nhúng `FBTIMG1` + gửi `product/hw` + token mới (GĐ1
  plan OTA); app chọn sản phẩm ở tab Quản lý máy (GĐ2); chốt nguồn sự thật danh tính máy (bảng
  DEVICE ở Postgres hay đồng bộ từ ERP `device_registry`); dời 4 doc xuyên phần lên `docs/`;
  un-ignore `windows/runner/` + `CMakeLists.txt` (rủi ro có sẵn: `flutter create .` sinh exe tên khác).
- **P3**: `firmware/common/`, `gen_fbt_product_h.py`, env `esp32dev_at`, hợp nhất
  `firmware/rapidplus/tools/ota-release` → `tools/` và 2 bản `sheet/`,
  `docs/checklist-them-san-pham.md` — áp cho Delta/Tray khi đặt tên.

## 12. Điểm chờ chủ dự án xác nhận

1. Xoay `RECEIVER_TOKEN` + ERP key ở P1 (kèm cửa sổ 2 token)?
2. `id_prefix` của Reader là `RDR`? (registry để TODO; đối chiếu ERP `device_registry`).
3. Giữ tên repo `ToolEngineer` hay đổi (`fbt-platform`)? Đổi tên trên GitHub không ảnh hưởng kịch bản lắp.
