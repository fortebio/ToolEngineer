# Deploy lên FBT Home Server (box `fbt-server`, bản copy PHẲNG ở ~/fbt_server/, KHÔNG git):
#   -Server : scp app/*.py + scripts/migrate_ota.py -> ~/fbt_server/app/ | scripts/ (kho OTA theo sản phẩm)
#   -Web    : scp NỘI DUNG apps/fbt_rapid/build/web_prod/* -> ~/fbt_server/web/ (bản web /app/; -WebProd để đổi)
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
    # Thư mục bản web đã build. Mặc định <gốc monorepo>\apps\fbt_rapid\build\web_prod — từ 2026-09-15
    # server/ và apps/fbt_rapid/ là hai thư mục ANH EM trong monorepo, app không còn là cha của server/.
    [string]$WebProd = "",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$serverDir = Split-Path -Parent $PSScriptRoot           # ...\server
$mono = Split-Path -Parent $serverDir                   # gốc MONOREPO (server/ và apps/ là anh em)
$webProd = if ($WebProd) { $WebProd } else { Join-Path $mono "apps\fbt_rapid\build\web_prod" }

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
    # KIỂM IMPORT bằng venv THẬT của box TRƯỚC khi restart (không cần sudo, không hạ service):
    # bắt ImportError/thiếu package/lỗi cú pháp — nếu restart mà uvicorn không lên là mất /ingest
    # của cả fleet. main.py chỉ mkdir lúc import (đường mặc định = ~/fbt_server/* đã có) nên vô hại.
    # Lỗi ở đây → khôi phục ngay: ssh <host> "rm -rf ~/fbt_server/app && mv ~/fbt_server/app.bak.<stamp> ~/fbt_server/app"
    Run "ssh $Target `"cd $Remote && venv/bin/python -c 'import app.main' && echo IMPORT_OK`""
    Write-Output ""
    Write-Output "==> Giờ restart dịch vụ (cần sudo, tự dán):"
    Write-Output "    ssh -t $Target `"sudo systemctl restart fbt-receiver && sleep 2 && systemctl is-active fbt-receiver && journalctl -u fbt-receiver -n 15 --no-pager`""
    Write-Output "==> Kiểm code đã NẠP (từ máy dev, không cần SSH/token):"
    Write-Output "    python scripts\check_deploy.py     # openapi prod == local → 'giống hệt: CÓ'"
    Write-Output "==> Không lên / sai → quay lại bản cũ (30 giây):"
    Write-Output "    ssh -t $Target `"rm -rf $Remote/app && mv $Remote/app.bak.$stamp $Remote/app && sudo systemctl restart fbt-receiver`""
}

if ($Web) {
    if (-not (Test-Path "$webProd\index.html")) {
        throw "Chưa có $webProd — build trước (trong apps/fbt_rapid): flutter build web --release --base-href /app/ --output build/web_prod (KHÔNG --dart-define)"
    }
    $bundle = Get-Content "$webProd\main.dart.js" -Raw
    if ($bundle -match "127\.0\.0\.1:8080" -or $bundle -match "localtok") {
        throw "Bundle web_prod đang nhúng URL/token TEST LOCAL — build lại KHÔNG --dart-define"
    }
    Write-Output "=== Web: $webProd -> $Remote/web ==="
    # Dọn bak cũ TRƯỚC; bản sao lưu của lần này do deploy-web.ps1 tạo (bước 4 của nó).
    # Chỉ giữ 3 bản web.bak.* MỚI NHẤT (~60 MB/bản; box từng tích 14 bản). Xoá bản cũ hơn.
    # Xếp theo TÊN (`sort -r`, tên mang timestamp), KHÔNG `ls -t`: `cp -a` giữ nguyên mtime của
    # web/ gốc nên bản vừa chép trông "cũ nhất" và bị xoá ngay (đã dính 2026-09-12 17:44).
    # Bản bak cũ (19/08) có thư mục `dr-x` do scp lỗi → phải `chmod -R u+rwX` trước khi rm, và
    # dọn thất bại KHÔNG được chặn deploy (`; true`) — dọn là việc phụ.
    # ⚠️ KHÔNG dùng `$(…)`/`$d` trong lệnh remote: `Run` đi qua Invoke-Expression nên PowerShell
    # diễn giải lại `$(` thành subexpression cục bộ (đã dính: `2>/dev/null` thành C:\dev\null).
    # xargs -I{} thay cho vòng for có biến.
    Run "ssh $Target `"cd $Remote && ls -d web.bak.* 2>/dev/null | sort -r | tail -n +4 | xargs -r -I{} sh -c 'chmod -R u+rwX {} && rm -rf {}'; ls -d web.bak.* 2>/dev/null; true`""
    # Chép bằng apps/fbt_rapid/deploy-web.ps1 — GẮN HASH vào tên main/bootstrap/font icon/favicon
    # (2026-09-23). Chép thẳng `main.dart.js` như trước là dính Cloudflare: nó ghi đè `no-cache` của
    # origin thành `max-age=14400` cho *.js/*.otf → trình duyệt chạy JS cũ tới 4 giờ sau deploy
    # (tab Hiệu chuẩn "không thấy" dù box đã đúng). Tên mới theo nội dung = URL mới = buộc tải lại;
    # index.html + FontManifest.json (nơi tham chiếu) thì CF không cache (DYNAMIC).
    # Script đó còn chép CHỈ file khác md5, tự sao lưu web.bak.<stamp> và kiểm md5 sau chép.
    $deployWeb = Join-Path $mono "apps\fbt_rapid\deploy-web.ps1"
    Write-Output ">> $deployWeb -WebDir $webProd -RemoteHost $Target -RemoteDir $Remote/web$(if (-not $DryRun) { ' -Go' })"
    if ($DryRun) { & $deployWeb -WebDir $webProd -RemoteHost $Target -RemoteDir "$Remote/web" }
    else         { & $deployWeb -WebDir $webProd -RemoteHost $Target -RemoteDir "$Remote/web" -Go }
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw "deploy-web.ps1 lỗi (exit $LASTEXITCODE)" }
    Write-Output "Kiểm: curl https://hub.fortebio.tech/app/ → index.html trỏ flutter_bootstrap.<hash>.js → mainJsPath main.<hash>.dart.js"
}
