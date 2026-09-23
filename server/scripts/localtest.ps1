# Bật bộ TEST LOCAL cho app FBT_RAPID: PostgreSQL (:5433) + FBT Home Server (:8080)
# serve luôn bản web đã build tại http://127.0.0.1:8080/app/ (nếu có) — không đụng production.
#
# Hai cách có Postgres, script tự chọn:
#   A. Postgres PORTABLE (box ADM, dựng 2026-09-04): $Base\pgsql + $Base\pgdata (initdb -U postgres -A trust),
#      DB `mydb` đã chạy deploy/schema.sql, bảng users có 3 tài khoản test.
#   B. DOCKER (máy Admin, 2026-09-21): không thấy pg_ctl.exe ở đâu nhưng có `docker` → container
#      `fbt-localtest-pg` (postgres:18-alpine, trust, volume `fbt-localtest-pgdata`, cổng 127.0.0.1:5433).
#      Lần đầu tự chạy deploy/schema.sql + tạo 3 tài khoản test. Base mặc định đổi sang %USERPROFILE%\fbt-localtest
#      (AppData\Local bị sandbox gói Claude desktop ảo hoá — xem CLAUDE.md gốc).
#   - Bản web (chạy TRONG apps/fbt_rapid): flutter build web --release --base-href /app/ --dart-define=FBT_URL=http://127.0.0.1:8080
#     --dart-define=FBT_TOKEN=localtok   (build lại KHÔNG dart-define trước khi deploy thật). Không có thì vẫn
#     dùng được API + Swagger http://127.0.0.1:8080/docs.
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
$PgContainer = "fbt-localtest-pg"
$UseDocker = $false
# Bo localtest duoc dung 2026-09-04 tu phien AI bi Windows "ao hoa" AppData\Local: thu muc that nam o
# %LOCALAPPDATA%\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Local\fbt-localtest (sandbox goi Claude desktop).
# Khong thay o duong mac dinh thi tu roi sang do; van co the -Base tuong minh. Khong co ca hai -> Docker.
if (-not (Test-Path "$Base\pgsql\bin\pg_ctl.exe")) {
    $alt = "$env:LOCALAPPDATA\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Local\fbt-localtest"
    if (Test-Path "$alt\pgsql\bin\pg_ctl.exe") {
        $Base = $alt; Write-Output "Base -> $Base (sandbox goi Claude)"
    } elseif (Get-Command docker -ErrorAction SilentlyContinue) {
        $UseDocker = $true
        if ($Base -eq "$env:LOCALAPPDATA\fbt-localtest") { $Base = "$env:USERPROFILE\fbt-localtest" }
        Write-Output "Khong co Postgres portable -> dung Docker container $PgContainer (:$PgPort); Base -> $Base"
    }
}
$server = Split-Path -Parent $PSScriptRoot          # ...\server
$mono = Split-Path -Parent $server                  # gốc MONOREPO (app ở apps\fbt_rapid, là anh em của server/)
$pg = "$Base\pgsql\bin"
$venv = "$Base\venv"

if ($Stop) {
    $pids = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty OwningProcess -Unique
    foreach ($p in $pids) { Stop-Process -Id $p -Force -Confirm:$false; "da tat server pid $p" }
    if ($UseDocker) { docker stop $PgContainer | Out-Null; "da tat container $PgContainer" }
    else { & "$pg\pg_ctl.exe" -D "$Base\pgdata" stop -m fast }
    return
}

if (-not $UseDocker -and -not (Test-Path "$pg\pg_ctl.exe")) { throw "Khong thay Postgres portable o $pg va khong co docker (xem CLAUDE.md)" }

# venv Python cho server (tao neu chua co — scratchpad cua phien AI la thu muc TAM, khong tin vao no).
# May Admin: `python` tren PATH la Python 3.11 cua ESP-IDF (co ensurepip, venv tao ra van co pip).
if (-not (Test-Path "$venv\Scripts\python.exe")) {
    Write-Output "Tao venv $venv ..."
    python -m venv $venv
    & "$venv\Scripts\python.exe" -m pip install -q fastapi uvicorn httpx "psycopg[binary]" pytest jsonschema python-multipart
}

if ($UseDocker) {
    # Docker Desktop daemon KHONG tu chay -> bat roi doi `docker info` OK (thuong vai giay, toi da ~90 s)
    docker info *> $null
    if ($LASTEXITCODE -ne 0) {
        Write-Output "Docker daemon chua chay -> mo Docker Desktop, doi..."
        Start-Process "C:\Program Files\Docker\Docker\Docker Desktop.exe"
        $t = 0
        do { Start-Sleep -Seconds 3; $t += 3; docker info *> $null } while ($LASTEXITCODE -ne 0 -and $t -lt 90)
        if ($LASTEXITCODE -ne 0) { throw "Docker daemon khong len sau $t s" }
    }
    $fresh = $false
    $exists = docker ps -a --filter "name=^$PgContainer$" --format "{{.Names}}"
    if (-not $exists) {
        Write-Output "Tao container $PgContainer (postgres:18-alpine, trust, volume fbt-localtest-pgdata)..."
        docker run -d --name $PgContainer -e POSTGRES_HOST_AUTH_METHOD=trust -e POSTGRES_DB=mydb `
            -p "127.0.0.1:${PgPort}:5432" -v fbt-localtest-pgdata:/var/lib/postgresql postgres:18-alpine | Out-Null
        $fresh = $true
    } else {
        docker start $PgContainer | Out-Null
    }
    $t = 0
    do { Start-Sleep -Seconds 1; $t += 1; docker exec $PgContainer pg_isready -U postgres -d mydb *> $null } while ($LASTEXITCODE -ne 0 -and $t -lt 60)
    if ($LASTEXITCODE -ne 0) { throw "Postgres trong container khong san sang sau $t s (docker logs $PgContainer)" }
    if ($fresh) {
        # Volume moi tao -> entrypoint co the chay lai init 1 nhip; doi them roi nap schema
        Start-Sleep -Seconds 2
        docker cp "$server\deploy\schema.sql" "${PgContainer}:/tmp/schema.sql"
        docker exec $PgContainer psql -q -U postgres -d mydb -v ON_ERROR_STOP=1 -f /tmp/schema.sql | Out-Null
        Write-Output "da nap deploy/schema.sql"
    }
} else {
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
}

# Server: tat ban cu tren cung port roi bat lai voi env local
$old = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty OwningProcess -Unique
foreach ($p in $old) { Stop-Process -Id $p -Force -Confirm:$false }
New-Item -ItemType Directory -Force "$Base\data", "$Base\ota", "$Base\logs", "$Base\ate", "$Base\calib" | Out-Null
$env:FBT_DATA_DIR = "$Base\data"
$env:FBT_OTA_DIR = "$Base\ota"
$env:FBT_LOGS_DIR = "$Base\logs"
$env:FBT_ATE_DIR = "$Base\ate"          # ho so tram ATE (tab San xuat)
$env:FBT_CALIB_DIR = "$Base\calib"      # ong chuan hieu chuan (tab Hieu chuan, app/calib.py)
$env:FBT_WEB_DIR = "$mono\apps\fbt_rapid\build\web"
$env:RECEIVER_TOKEN = "localtok"
$env:OTA_ADMIN_TOKEN = "localtok"          # cung token: app dang nhap root/cskh deu ghi OTA duoc o local
$env:OTA_LEGACY_PRODUCT_BY_PREFIX = "RDR=reader,RPL=rapidplus"   # giong deploy/fbt-receiver.env.example
$env:OTA_REQUIRE_TAG = "rapid4p"           # kho rapid4p bat buoc the (esp_app_desc) nhu production du kien
$env:FBT_DB = "dbname=mydb host=127.0.0.1 port=$PgPort user=postgres connect_timeout=5"
$env:PYTHONIOENCODING = "utf-8"

# Tai khoan test (chi khi bang users con trong — DB Docker moi tao)
$py = "$venv\Scripts\python.exe"
$n = & $py -c "import sys; sys.path.insert(0, r'$server'); from app import db; print(len(db.list_users()))"
if ("$n" -eq "0") {
    & $py "$server\scripts\manage_users.py" add root root123 root | Out-Null
    & $py "$server\scripts\manage_users.py" add cskh cskh123 admin "*" "CSKH" | Out-Null
    & $py "$server\scripts\manage_users.py" add khach khach123 user "RPL02013" "Khach" | Out-Null
    Write-Output "da tao 3 tai khoan test"
}

$proc = Start-Process -FilePath $py `
    -ArgumentList "-m", "uvicorn", "app.main:app", "--host", "127.0.0.1", "--port", "$Port" `
    -WorkingDirectory $server -RedirectStandardOutput "$Base\uvicorn.out" `
    -RedirectStandardError "$Base\uvicorn.err" -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 3
if ($proc.HasExited) { throw "uvicorn thoat ngay - xem $Base\uvicorn.err" }

Write-Output "Server local pid $($proc.Id): http://127.0.0.1:$Port/  (Swagger: /docs; web: /app/ neu da build)"
Write-Output "Tai khoan test:  cskh / cskh123 (Nhan vien)   root / root123 (Root)   khach / khach123 (Khach hang, may RPL02013)"
Write-Output "Token API local: localtok   (Cai dat > Engineer URL = http://127.0.0.1:$Port)"
