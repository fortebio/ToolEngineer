<#
  Deploy ban WEB len Engineer Server (https://hub.fortebio.tech/app/).

  MAC DINH LA CHAY THU. Xem xong danh sach file thi chay lai voi -Go.

      .\deploy-web.ps1          # chay thu, khong dung gi
      .\deploy-web.ps1 -Go      # thuc su chep

  Vi sao la script chu khong phai vai dong scp dan tay:
   * Chi chep FILE THUC SU KHAC NHAU (so md5 hai dau). Lan nay 6/22 file khac,
     canvaskit ~40MB giong het -> bo qua. Chep tat ca moi lan la 43MB qua tunnel.
   * scp -O (giao thuc cu). OpenSSH 9+ tren Windows mac dinh dung SFTP, va
     scp -r vao thu muc DA CO rat de ra "stat remote/Permission denied" hoac
     long them mot cap assets/assets. Xem CLAUDE.md muc Web.
   * Chep TUNG FILE, khong chep ca thu muc - cung ly do tren.
   * Sao luu truoc, de lui lai bang dung MOT lenh.

  KHONG restart service: /app la StaticFiles doc thang tu dia, khong cache o
  phia server.
#>
param(
  [switch]$Go,
  [string]$RemoteHost = "fbt-server",
  [string]$RemoteDir  = "~/fbt_server/web"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$web  = Join-Path $root "build\web"

# ---- 0. ban build co ton tai va dung base-href khong ----
if (-not (Test-Path (Join-Path $web "index.html"))) {
  throw "Chua co build\web. Chay truoc: flutter build web --release --base-href /app/"
}
$idx = Get-Content (Join-Path $web "index.html") -Raw
if ($idx -notmatch '<base href="/app/"') {
  throw "index.html KHONG co <base href=`"/app/`">. Build lai voi --base-href /app/ (thieu no la trang trang)."
}
Write-Host "[1/6] build\web hop le (base href = /app/)" -ForegroundColor Green
# ---- 0b. CHAN DEPLOY NHAM BAN LOCAL ----
# Ban build de host local mang `--dart-define=FBT_URL=http://localhost:...`.
# Day ban do len production thi app goi API ve localhost cua TUNG MAY KHACH -> hong
# het, va hong IM LANG (trang van len, chi khong lay duoc du lieu).
$mainProbe = Join-Path $web "main.dart.js"
if (Test-Path $mainProbe) {
  $head = [IO.File]::ReadAllText($mainProbe)
  if ($head -match 'http://(localhost|127\.0\.0\.1):\d+') {
    Write-Host ""
    Write-Host "DUNG: ban build nay tro ve $($Matches[0]) - la ban HOST LOCAL." -ForegroundColor Red
    Write-Host "Day len production se lam app goi API ve may khach. Build lai ban that:" -ForegroundColor Yellow
    Write-Host "   flutter build web --release --base-href /app/"
    exit 1
  }
}

# ---- 1b. GAN VAN TAY VAO TEN FILE (cache-busting) ----
#
# Cloudflare cache theo URL. Ba file duoi day bi cache 4 tieng o bien, nen deploy
# xong nguoi dung VAN THAY BAN CU cho toi khi het han hoac co ai do purge - ma
# tai khoan Cloudflare lai nam o ben khac.
#
# Loi thoat: doi TEN chung theo noi dung. URL moi thi bien chua tung thay -> buoc
# phai di lay tu server. Chay duoc vi ca ba NOI THAM CHIEU deu KHONG bi cache
# (do bang curl: cf-cache-status DYNAMIC):
#
#     index.html  ->  flutter_bootstrap.<hash>.js  ->  main.<hash>.dart.js
#     assets/FontManifest.json  ->  fonts/MaterialIcons-<hash>.otf
#
# Font icon PHAI nam trong danh sach: ban cache cu thieu dung 2 ky tu (U+F4A0,
# U+F4A1) so voi ban moi - de nguyen la hai icon hien o vuong trong.
#
# Hash lay tu NOI DUNG nen chay lai nhieu lan khong de rac: cung file thi cung ten.
# Don ban van tay cua lan truoc trong build\web truoc khi tao ban moi.
# `flutter build web` KHONG xoa file la, nen khong don thi moi lan deploy lai
# tich them mot ban - da thay 6 file rac sau vai lan.
Get-ChildItem $web -File | Where-Object {
  $_.Name -match '^(main\.[0-9a-f]{8}\.dart\.js|flutter_bootstrap\.[0-9a-f]{8}\.js|favicon\.[0-9a-f]{8}\.png)$'
} | Remove-Item -Force
$fdir = Join-Path $web "assets\fonts"
if (Test-Path $fdir) {
  Get-ChildItem $fdir -File | Where-Object { $_.Name -match '^MaterialIcons-[0-9a-f]{8}\.otf$' } | Remove-Item -Force
}

# LUU Y: moi `Get-Content` doc file van ban o day deu phai co `-Encoding UTF8`.
# PowerShell 5.1 mac dinh doc theo ANSI; doc ANSI roi ghi lai UTF8 la MA HOA HAI
# LAN - tieng Viet trong index.html thanh rac va file phinh them sau MOI lan chay
# (do duoc: 1904 -> 2024 -> 2822 byte). Da lam hong that mot lan, 2026-08-19.
function New-Fingerprint {
  param([string]$Path)
  return (Get-FileHash $Path -Algorithm MD5).Hash.Substring(0,8).ToLower()
}

$idxPath  = Join-Path $web "index.html"
$bootPath = Join-Path $web "flutter_bootstrap.js"
$mainPath = Join-Path $web "main.dart.js"
$fmPath   = Join-Path $web "assets\FontManifest.json"
$otfPath  = Join-Path $web "assets\fonts\MaterialIcons-Regular.otf"

# (1) main.dart.js -> ten co van tay, va vai vao ban bootstrap moi
$mainFp   = New-Fingerprint $mainPath
$mainNew  = "main.$mainFp.dart.js"
Copy-Item $mainPath (Join-Path $web $mainNew) -Force

$bootText = (Get-Content $bootPath -Raw -Encoding UTF8) -replace '"mainJsPath":"main\.dart\.js"', ('"mainJsPath":"' + $mainNew + '"')
$tmpBoot  = Join-Path $web "._boot.tmp"
[IO.File]::WriteAllText($tmpBoot, $bootText, (New-Object Text.UTF8Encoding $false))
$bootFp   = New-Fingerprint $tmpBoot
$bootNew  = "flutter_bootstrap.$bootFp.js"
Move-Item $tmpBoot (Join-Path $web $bootNew) -Force

# (2) index.html tro sang ban bootstrap co van tay
# Khop CA `flutter_bootstrap.js` LAN `flutter_bootstrap.<hash>.js`: neu chi khop
# ten goc thi lan deploy THU HAI index.html van tro vao ban bootstrap cu -> trang trang.
$idxText = (Get-Content $idxPath -Raw -Encoding UTF8) -replace 'src="flutter_bootstrap[^"]*\.js"', ('src="' + $bootNew + '"')
[IO.File]::WriteAllText($idxPath, $idxText, (New-Object Text.UTF8Encoding $false))

# (3) font icon + FontManifest
$otfFp  = New-Fingerprint $otfPath
$otfNew = "MaterialIcons-$otfFp.otf"
Copy-Item $otfPath (Join-Path $web "assets\fonts\$otfNew") -Force
$fmText = (Get-Content $fmPath -Raw -Encoding UTF8) -replace 'fonts/MaterialIcons-[^"]*\.otf', ("fonts/" + $otfNew)
[IO.File]::WriteAllText($fmPath, $fmText, (New-Object Text.UTF8Encoding $false))

Write-Host "[1b] gan van tay: $bootNew / $mainNew / $otfNew"

# (4) favicon — quen o ban dau, va no la thu de thay nhat (tab trinh duyet).
# Trinh duyet con cache favicon RIET hon ca file thuong, nen doi ten la cach chac
# chan duy nhat: URL moi thi khong con ban cu nao de bam vao.
$favPath = Join-Path $web "favicon.png"
if (Test-Path $favPath) {
  $favFp  = New-Fingerprint $favPath
  $favNew = "favicon.$favFp.png"
  Copy-Item $favPath (Join-Path $web $favNew) -Force
  $idxText = (Get-Content $idxPath -Raw -Encoding UTF8) -replace 'href="favicon[^"]*\.png"', ('href="' + $favNew + '"')
  [IO.File]::WriteAllText($idxPath, $idxText, (New-Object Text.UTF8Encoding $false))
  Write-Host "[1c] favicon      : $favNew"
}

# ---- 1. md5 phia LOCAL ----
$local = @{}
# Bo file an: `.last_build_id` la dau moc build cua Flutter, khong phai thu
# duoc phuc vu - day len chi ton mot vong scp va lam nhieu danh sach.
Get-ChildItem $web -Recurse -File -Force |
  Where-Object { $_.Name -notlike '.*' } | ForEach-Object {
  $rel = $_.FullName.Substring($web.Length + 1).Replace('\','/')
  $local[$rel] = (Get-FileHash $_.FullName -Algorithm MD5).Hash.ToLower()
}
Write-Host ("[2/6] local: {0} file" -f $local.Count)

# ---- 2. md5 phia REMOTE (mot lan ssh) ----
# Tach thanh HAM vi buoc kiem cuoi dung LAI DUNG lenh nay - mot lenh da chung
# minh chay duoc hon hai lenh moi cai mot kieu trich dan.
function Get-RemoteHashes {
  $out = ssh $RemoteHost "cd $RemoteDir && find . -type f -printf '%P\n' | sort | while read f; do printf '%s %s\n' \`"`$(md5sum \`"`$f\`" | cut -d' ' -f1)\`" \`"`$f\`"; done"
  $h = @{}
  foreach ($line in $out) {
    if ($line -match '^([0-9a-f]{32}) (.+)$') { $h[$Matches[2]] = $Matches[1] }
  }
  return $h
}
$remote = Get-RemoteHashes
Write-Host ("[3/6] remote: {0} file" -f $remote.Count)

# ---- 3. tinh phan khac ----
$toCopy = @()
foreach ($rel in ($local.Keys | Sort-Object)) {
  if (-not $remote.ContainsKey($rel)) { $toCopy += ,@($rel, "MOI") }
  elseif ($remote[$rel] -ne $local[$rel]) { $toCopy += ,@($rel, "khac") }
}
# file chi co tren server (favicon/icons/manifest tu ban deploy cu) -> GIU NGUYEN
$onlyRemote = $remote.Keys | Where-Object { -not $local.ContainsKey($_) }

Write-Host ""
if ($toCopy.Count -eq 0) {
  Write-Host "Khong co gi de chep - hai ben da giong nhau." -ForegroundColor Green
  return
}
$bytes = 0
Write-Host ("Se chep {0} file:" -f $toCopy.Count) -ForegroundColor Yellow
foreach ($p in $toCopy) {
  $sz = (Get-Item (Join-Path $web ($p[0].Replace('/','\')))).Length
  $bytes += $sz
  Write-Host ("   {0,-6} {1,10:N0} B  {2}" -f $p[1], $sz, $p[0])
}
Write-Host ("   tong {0:N2} MB" -f ($bytes/1MB))
if ($onlyRemote) {
  Write-Host ""
  Write-Host "Chi co tren server (KHONG dung toi, khong xoa):" -ForegroundColor DarkGray
  $onlyRemote | Sort-Object | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
}

# ---- 3b. KIEM QUYEN GHI TRUOC KHI DUNG BAT CU THU GI ----
# Vi sao co buoc nay: mot lan `scp -r` hong tu truoc de lai `web/assets/assets`
# va `.../fonts` o mode 555 (mat bit ghi cua chu so huu). Khong kiem thi kich ban
# la: tao sao luu xong -> mkdir that bai -> throw mot doi chu do -> nguoi chay
# khong biet trang thai server ra sao. Kiem o day thi biet TRUOC, va chua dung gi.
$needDirs = @($RemoteDir)
foreach ($p in $toCopy) {
  $d = Split-Path $p[0] -Parent
  if ($d) { $needDirs += ($RemoteDir + "/" + $d.Replace('\','/')) }
}
$needDirs = $needDirs | Sort-Object -Unique
# `~` chi no khi KHONG bi nhay bao. Ban dau probe boc duong dan trong nhay don
# nen `~/fbt_server/...` la chuoi tho, `dirname` leo len toi `.` (= home, ghi
# duoc) va probe bao OK trong khi thuc te khong ghi duoc. De trong trong `for`.
$paths = ($needDirs -join " ")
$probe = "for d in $paths; do p=`$d; while [ ! -d `"`$p`" ]; do p=`$(dirname `"`$p`"); done; [ -w `"`$p`" ] || echo `"`$p`"; done"
$notWritable = ssh $RemoteHost $probe
if ($notWritable) {
  Write-Host ""
  Write-Host "KHONG GHI DUOC vao cac thu muc sau tren server:" -ForegroundColor Red
  $notWritable | Sort-Object -Unique | ForEach-Object { Write-Host "   $_" -ForegroundColor Red }
  Write-Host ""
  Write-Host "Nguyen nhan quen thuoc: mot lan scp -r hong de lai thu muc mode 555." -ForegroundColor Yellow
  Write-Host "Sua (an toan, chi them quyen cho CHU SO HUU, khong xoa gi):"
  Write-Host "   ssh $RemoteHost `"chmod -R u+rwX $RemoteDir`""
  Write-Host ""
  Write-Host "Roi chay lai script nay. Chua dung gi tren server." -ForegroundColor Cyan
  exit 1
}
Write-Host "[3b] quyen ghi: OK"

if (-not $Go) {
  Write-Host ""
  Write-Host "== CHAY THU. Chua dung gi tren server. Chay lai voi -Go de chep. ==" -ForegroundColor Cyan
  return
}

# ---- 4. sao luu ----
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
Write-Host ""
Write-Host "[4/6] sao luu -> $RemoteDir.bak.$stamp"
ssh $RemoteHost "cp -a $RemoteDir $RemoteDir.bak.$stamp"
if ($LASTEXITCODE -ne 0) { throw "Sao luu that bai - DUNG, khong chep gi." }

# ---- 5. chep ----
Write-Host "[5/6] dang chep..."
$dirs = $toCopy | ForEach-Object { Split-Path $_[0] -Parent } | Where-Object { $_ } |
        ForEach-Object { $_.Replace('\','/') } | Sort-Object -Unique
foreach ($d in $dirs) { ssh $RemoteHost "mkdir -p $RemoteDir/$d" }
foreach ($p in $toCopy) {
  $rel = $p[0]
  $src = Join-Path $web ($rel.Replace('/','\'))
  $dst = "{0}:{1}/{2}" -f $RemoteHost, $RemoteDir, $rel
  scp -O $src $dst
  if ($LASTEXITCODE -ne 0) { throw "scp that bai o $rel. Lui lai (ghi de tu ban sao luu, KHONG xoa gi): ssh $RemoteHost `"cp -a $RemoteDir.bak.$stamp/. $RemoteDir/`"" }
  Write-Host "   ok  $rel"
}

# ---- 6. kiem lai bang md5 ----
Write-Host "[6/6] kiem lai..."
$after = Get-RemoteHashes
$bad = 0
foreach ($p in $toCopy) {
  $rel = $p[0]
  $ok = ($after.ContainsKey($rel) -and $after[$rel] -eq $local[$rel])
  if (-not $ok) { $bad++ }
  if ($ok) { Write-Host "   KHOP   $rel" } else { Write-Host "   LECH!  $rel" -ForegroundColor Red }
}
Write-Host ""
if ($bad -gt 0) {
  Write-Host "$bad file LECH md5. Lui lai:" -ForegroundColor Red
  Write-Host "   ssh $RemoteHost `"cp -a $RemoteDir.bak.$stamp/. $RemoteDir/`""
  exit 1
}
Write-Host "Chep xong, md5 khop het." -ForegroundColor Green
Write-Host ""
Write-Host "CON MOT VIEC NUA - KHONG BO QUA:" -ForegroundColor Yellow
Write-Host "  Cloudflare dang cache /app/*.js 4 tieng (do duoc: cf-cache-status HIT,"
Write-Host "  Cache-Control max-age=14400). Chua purge thi nguoi dung VAN THAY BAN CU,"
Write-Host "  khong bao loi gi. Nho nguoi giu tai khoan Cloudflare:"
Write-Host "     Caching -> Configuration -> Purge Custom Purge -> https://hub.fortebio.tech/app/*"
Write-Host "  (Ben lau dai: them Cache Rule bypass cho /app/* nhu da lam cho /ota/*.)"
Write-Host ""
Write-Host "  Kiem ORIGIN da moi chua, khong can cho purge (query la moi -> cache MISS):"
Write-Host "     curl.exe -s -o NUL -w `"%{size_download}\`n`" `"https://hub.fortebio.tech/app/main.dart.js?v=$stamp`""
Write-Host ("     phai ra {0} (bang co main.dart.js vua chep)" -f (Get-Item (Join-Path $web 'main.dart.js')).Length) -ForegroundColor DarkGray
Write-Host ""
$keep = @($bootNew, $mainNew, $favNew, "MaterialIcons-$otfFp.otf") -join "|"
Write-Host "  Don ban van tay CU tren server (giu lai ban dang dung):"
Write-Host "     ssh $RemoteHost `"cd $RemoteDir && ls main.*.dart.js flutter_bootstrap.*.js favicon.*.png assets/fonts/MaterialIcons-*.otf 2>/dev/null | grep -vE '($keep)' | xargs -r rm -f`""
Write-Host ""
Write-Host "  Xoa ban sao luu khi da yen tam:"
Write-Host "     ssh $RemoteHost `"rm -rf $RemoteDir.bak.$stamp`""
