# Bật bộ TEST LOCAL cho app FBT_RAPID: PostgreSQL portable (:5433) + FBT Home Server (:8080)
# serve luôn bản web đã build tại http://127.0.0.1:8080/app/ — không đụng production.
#
# Chuẩn bị 1 lần (đã làm trên box ADM 2026-09-04, xem apps/fbt_rapid/CLAUDE.md mục "Công thức TEST LOCAL"):
#   - Postgres portable giải nén ở $Base\pgsql, data ở $Base\pgdata (initdb -U postgres -A trust),
#     DB `mydb` đã chạy deploy/schema.sql, bảng users có 3 tài khoản test (cskh/root/khach).
#   - Bản web (chạy TRONG apps/fbt_rapid): flutter build web --release --base-href /app/ --dart-define=FBT_URL=http://127.0.0.1:8080
#     --dart-define=FBT_TOKEN=localtok   (build lại KHÔNG dart-define trước khi deploy thật)
#
# Dùng:   .\scripts\localtest.ps1            # bật cả hai, in URL + tài khoản
#         .\scripts\localtest.ps1 -Stop      # tắt server + Postgres
param(
    [string]$Base = "$env:LOCALAPPDATA\fbt-localtest",
    [int]$PgPort = 5433,
    [int]$Port = 8080,
    [switch]$Stop
)

$ErrorActionPreference = "Stop"
# Bo localtest duoc dung 2026-09-04 tu phien AI bi Windows "ao hoa" AppData\Local: thu muc that nam o
# %LOCALAPPDATA%\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Local\fbt-localtest (sandbox goi Claude desktop).
# Khong thay o duong mac dinh thi tu roi sang do; van co the -Base tuong minh.
if (-not (Test-Path "$Base\pgsql\bin\pg_ctl.exe")) {
    $alt = "$env:LOCALAPPDATA\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Local\fbt-localtest"
    if (Test-Path "$alt\pgsql\bin\pg_ctl.exe") { $Base = $alt; Write-Output "Base -> $Base (sandbox goi Claude)" }
}
$server = Split-Path -Parent $PSScriptRoot          # ...\server
$mono = Split-Path -Parent $server                  # gốc MONOREPO (app ở apps\fbt_rapid, là anh em của server/)
$pg = "$Base\pgsql\bin"
$venv = "$Base\venv"

if ($Stop) {
    $pids = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty OwningProcess -Unique
    foreach ($p in $pids) { Stop-Process -Id $p -Force -Confirm:$false; "da tat server pid $p" }
    & "$pg\pg_ctl.exe" -D "$Base\pgdata" stop -m fast
    return
}

if (-not (Test-Path "$pg\pg_ctl.exe")) { throw "Khong thay Postgres portable o $pg (xem CLAUDE.md)" }

# venv Python cho server (tao neu chua co — scratchpad cua phien AI la thu muc TAM, khong tin vao no)
if (-not (Test-Path "$venv\Scripts\python.exe")) {
    python -m venv $venv
    & "$venv\Scripts\python.exe" -m pip install -q fastapi uvicorn httpx "psycopg[binary]" pytest
}

# Postgres: bat neu chua chay. postmaster.pid con sot (phien truoc tat may khong stop) lam pg_ctl in canh bao
# "another server might be running" ra stderr -> voi $ErrorActionPreference=Stop la script CHET truoc khi bat
# server (dinh 2026-09-18). Status bao "no server running" thi don pid cu roi moi start, va start voi
# ErrorAction Continue.
& "$pg\pg_ctl.exe" -D "$Base\pgdata" status *> $null
if ($LASTEXITCODE -ne 0) {
    if (Test-Path "$Base\pgdata\postmaster.pid") {
        Move-Item -Force "$Base\pgdata\postmaster.pid" "$Base\pgdata\postmaster.pid.stale"
        Write-Output "da don postmaster.pid cu"
    }
    $ErrorActionPreference = "Continue"
    & "$pg\pg_ctl.exe" -D "$Base\pgdata" -o "-p $PgPort" -l "$Base\pg.log" -w start 2>&1 | Out-String | Write-Output
    $ErrorActionPreference = "Stop"
    & "$pg\pg_ctl.exe" -D "$Base\pgdata" status *> $null
    if ($LASTEXITCODE -ne 0) { throw "Postgres khong len - xem $Base\pg.log" }
}

# Server: tat ban cu tren cung port roi bat lai voi env local
$old = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty OwningProcess -Unique
foreach ($p in $old) { Stop-Process -Id $p -Force -Confirm:$false }
New-Item -ItemType Directory -Force "$Base\data", "$Base\ota", "$Base\logs", "$Base\ate" | Out-Null
$env:FBT_DATA_DIR = "$Base\data"
$env:FBT_OTA_DIR = "$Base\ota"
$env:FBT_LOGS_DIR = "$Base\logs"
$env:FBT_ATE_DIR = "$Base\ate"          # ho so tram ATE (tab San xuat)
$env:FBT_WEB_DIR = "$mono\apps\fbt_rapid\build\web"
$env:RECEIVER_TOKEN = "localtok"
$env:OTA_ADMIN_TOKEN = "localtok"          # cung token: app dang nhap root/cskh deu ghi OTA duoc o local
$env:OTA_LEGACY_PRODUCT_BY_PREFIX = "RDR=reader,RPL=rapidplus"   # giong deploy/fbt-receiver.env.example
$env:OTA_REQUIRE_TAG = "rapid4p"           # kho rapid4p bat buoc the (esp_app_desc) nhu production du kien
$env:FBT_DB = "dbname=mydb host=127.0.0.1 port=$PgPort user=postgres connect_timeout=5"
$env:PYTHONIOENCODING = "utf-8"
$proc = Start-Process -FilePath "$venv\Scripts\python.exe" `
    -ArgumentList "-m", "uvicorn", "app.main:app", "--host", "127.0.0.1", "--port", "$Port" `
    -WorkingDirectory $server -RedirectStandardOutput "$Base\uvicorn.out" `
    -RedirectStandardError "$Base\uvicorn.err" -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 3

Write-Output "Server local pid $($proc.Id): http://127.0.0.1:$Port/  (web: http://127.0.0.1:$Port/app/)"
Write-Output "Tai khoan test:  cskh / cskh123 (Nhan vien)   root / root123 (Root)   khach / khach123 (Khach hang, may RPL02013)"
Write-Output "Token API local: localtok   (Cai dat > Engineer URL = http://127.0.0.1:$Port)"
