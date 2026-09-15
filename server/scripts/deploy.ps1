# Deploy lên FBT Home Server (box `fbt-server`, bản copy PHẲNG ở ~/fbt_server/, KHÔNG git):
#   -Server : scp app/*.py + scripts/migrate_ota.py -> ~/fbt_server/app/ | scripts/ (kho OTA theo sản phẩm)
#   -Web    : scp NỘI DUNG build/web_prod/*    -> ~/fbt_server/web/   (bản web /app/)
# Sau -Server PHẢI restart dịch vụ (cần sudo, chạy tay — script chỉ in lệnh):
#   ssh <host> "sudo systemctl restart fbt-receiver"
#
# Dùng (PowerShell, từ thư mục server/ hoặc bất kỳ):
#   .\scripts\deploy.ps1 -Server -Web                 # host mặc định engineer@fbt (MagicDNS)
#   .\scripts\deploy.ps1 -Server -Target engineer@100.109.127.87
#   .\scripts\deploy.ps1 -Web -DryRun                 # chỉ in lệnh, không chạy
#
# Cần: SSH tới box được (Tailscale SSH theo ACL tailnet, hoặc key). Máy bị
# "tailnet policy does not permit you to SSH" thì phải được cấp quyền trước.
#
# GOTCHA scp (CLAUDE.md gốc repo): OpenSSH 9+ trên Windows dùng SFTP → `scp -r thư_mục`
# vào thư mục ĐÃ CÓ dễ "Permission denied" + lồng assets/assets. Vì vậy dùng `scp -O`
# (legacy) và chép NỘI DUNG từng thư mục con (assets/*, canvaskit/*, icons/*), không chép cả cây.
param(
    [switch]$Server,
    [switch]$Web,
    # Tên đầy đủ MagicDNS: tên ngắn `fbt` KHÔNG resolve trên box chưa bật MagicDNS search domain.
    [string]$Target = "engineer@fbt.basa-luma.ts.net",
    [string]$Remote = "~/fbt_server",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$serverDir = Split-Path -Parent $PSScriptRoot           # ...\server
$repo = Split-Path -Parent $serverDir                   # gốc repo app
$webProd = Join-Path $repo "build\web_prod"

if (-not $Server -and -not $Web) { throw "Chọn ít nhất một: -Server và/hoặc -Web" }

function Run($cmd) {
    Write-Output ">> $cmd"
    if (-not $DryRun) { Invoke-Expression $cmd; if ($LASTEXITCODE -ne 0) { throw "Lệnh lỗi (exit $LASTEXITCODE): $cmd" } }
}

# 0. Kiểm SSH trước khi làm gì (không đợi tới lúc scp mới biết bị chặn).
Run "ssh -o BatchMode=yes -o ConnectTimeout=12 $Target `"echo SSH_OK; md5sum $Remote/app/main.py $Remote/app/config.py`""

if ($Server) {
    # TOÀN BỘ app/*.py + scripts/migrate_ota.py: từ 2026-09-11 kho OTA nằm ở module mới
    # app/ota.py và logic.py cũng đổi — chép lẻ config+main như trước là server import lỗi.
    $appFiles = Get-ChildItem "$serverDir\app\*.py" | ForEach-Object { "`"$($_.FullName)`"" }
    Write-Output "=== Server: app/*.py ($($appFiles.Count) file) + scripts/migrate_ota.py ==="
    # SAO LƯU app/ trên box TRƯỚC khi ghi đè. 2026-09-12: box chạy code của nhánh
    # `ota-rollout-docs-tests` (monitor.py + route /monitor) mà clone deploy chưa merge → scp từ
    # `main` xoá mất route, bản cũ không còn đâu để lấy lại (pyc cũng bị ghi đè lúc restart).
    # Có bản .bak thì 30 giây là quay lại được. Dọn bak cũ bằng tay khi cần.
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    Run "ssh $Target `"cp -a $Remote/app $Remote/app.bak.$stamp && ls -d $Remote/app.bak.*`""
    # Cảnh báo sớm: file nào trên box KHÔNG có trong local = code từ nơi khác, sẽ bị bỏ sót/ghi đè.
    Run "ssh $Target `"cd $Remote/app && ls *.py`""
    Run "scp -O $($appFiles -join ' ') ${Target}:$Remote/app/"
    Run "scp -O `"$serverDir\scripts\migrate_ota.py`" ${Target}:$Remote/scripts/"
    Run "ssh $Target `"md5sum $Remote/app/*.py`""
    Write-Output "Local md5:"; Get-FileHash -Algorithm MD5 "$serverDir\app\*.py" | ForEach-Object { "  $($_.Hash.ToLower())  $($_.Path)" }
    Write-Output ""
    Write-Output "==> Xem trước di cư kho OTA phẳng → products/<legacy>/ (server cũng tự làm lúc restart):"
    Write-Output "    ssh $Target `"cd $Remote && FBT_OTA_DIR=~/fbt_server/ota python3 -m scripts.migrate_ota --dry-run`""
    Write-Output "==> Giờ restart dịch vụ (cần sudo, tự dán):"
    Write-Output "    ssh $Target `"sudo systemctl restart fbt-receiver`""
    Write-Output "    rồi kiểm route đã nạp + log di cư:"
    Write-Output "    ssh $Target `"curl -s http://127.0.0.1:8080/openapi.json | grep -o '/ota/products'; journalctl -u fbt-receiver -n 20 | grep 'ota migrate'`""
}

if ($Web) {
    if (-not (Test-Path "$webProd\index.html")) {
        throw "Chưa có $webProd — build trước: flutter build web --release --base-href /app/ --output build/web_prod (KHÔNG --dart-define)"
    }
    $bundle = Get-Content "$webProd\main.dart.js" -Raw
    if ($bundle -match "127\.0\.0\.1:8080" -or $bundle -match "localtok") {
        throw "Bundle web_prod đang nhúng URL/token TEST LOCAL — build lại KHÔNG --dart-define"
    }
    Write-Output "=== Web: build/web_prod -> $Remote/web ==="
    # Sao lưu bản web đang host trước khi ghi đè (cùng lý do với app/ ở trên; box đã có nhiều web.bak.*).
    $stampWeb = Get-Date -Format "yyyyMMdd-HHmmss"
    Run "ssh $Target `"[ -d $Remote/web ] && cp -a $Remote/web $Remote/web.bak.$stampWeb; true`""
    # Chỉ giữ 3 bản web.bak.* MỚI NHẤT (~60 MB/bản; box từng tích 14 bản). Xoá bản cũ hơn.
    # Xếp theo TÊN (`sort -r`, tên mang timestamp), KHÔNG `ls -t`: `cp -a` giữ nguyên mtime của
    # web/ gốc nên bản vừa chép trông "cũ nhất" và bị xoá ngay (đã dính 2026-09-12 17:44).
    # Bản bak cũ (19/08) có thư mục `dr-x` do scp lỗi → phải `chmod -R u+rwX` trước khi rm, và
    # dọn thất bại KHÔNG được chặn deploy (`; true`) — dọn là việc phụ.
    # ⚠️ KHÔNG dùng `$(…)`/`$d` trong lệnh remote: `Run` đi qua Invoke-Expression nên PowerShell
    # diễn giải lại `$(` thành subexpression cục bộ (đã dính: `2>/dev/null` thành C:\dev\null).
    # xargs -I{} thay cho vòng for có biến.
    Run "ssh $Target `"cd $Remote && ls -d web.bak.* 2>/dev/null | sort -r | tail -n +4 | xargs -r -I{} sh -c 'chmod -R u+rwX {} && rm -rf {}'; ls -d web.bak.* 2>/dev/null; true`""
    Run "ssh $Target `"mkdir -p $Remote/web/assets $Remote/web/canvaskit $Remote/web/icons && chmod -R u+rwX $Remote/web`""
    # file rời ở gốc
    $top = Get-ChildItem $webProd -File | ForEach-Object { "`"$($_.FullName)`"" }
    Run "scp -O $($top -join ' ') ${Target}:$Remote/web/"
    # NỘI DUNG từng thư mục con (assets có thư mục con lồng: assets/assets/fonts…, packages/… → -r trên nội dung)
    foreach ($d in @("assets", "canvaskit", "icons")) {
        if (Test-Path "$webProd\$d") {
            $items = Get-ChildItem "$webProd\$d" | ForEach-Object { "`"$($_.FullName)`"" }
            if ($items) { Run "scp -O -r $($items -join ' ') ${Target}:$Remote/web/$d/" }
        }
    }
    Run "ssh $Target `"ls -la $Remote/web | head -20; ls $Remote/web/assets | head`""
    Write-Output "Kiểm: mở https://hub.fortebio.tech/app/ (Ctrl+F5) → đăng nhập → thấy tab Chăm sóc KH."
}
