# webshot.ps1 — launch/screenshot harness cho BUILD WEB của FBT_RAPID.
#
# Serve build\web bằng node (web-server.js cùng thư mục), mở Chrome/Edge chế độ
# --app (cửa sổ riêng, title = <title> trang = FBT_RAPID), chụp cửa sổ bằng
# PrintWindow rồi dọn dẹp. GOTCHA: Chromium composite bằng GPU → PrintWindow ra
# ảnh XÁM nếu thiếu --disable-gpu (đã bật sẵn ở đây; canvaskit vẫn chạy qua
# SwiftShader). Build trước: flutter build web --release.
#
# Vi du:
#   pwsh .claude/skills/run-fbt-rapid/webshot.ps1 -Out web.png
#   pwsh .claude/skills/run-fbt-rapid/webshot.ps1 -Url https://fbt.basa-luma.ts.net/app/ -Out live.png
[CmdletBinding()]
param(
  [string]$WebRoot,             # mặc định = <app root>\build\web
  [string]$Url,                 # URL đang host THẬT — có thì BỎ bước serve cục bộ
  [string]$Out = "_web.png",
  [int]$Port = 8177,
  [int]$Timeout = 40,           # giây chờ cửa sổ FBT_RAPID xuất hiện
  [int]$Settle = 12             # giây chờ Flutter boot sau khi có cửa sổ
)
$ErrorActionPreference = "Stop"
$skillDir = Split-Path -Parent $PSCommandPath
if (-not $Url) {
  if (-not $WebRoot) {
    $root = $skillDir | Split-Path -Parent | Split-Path -Parent | Split-Path -Parent
    $WebRoot = Join-Path $root "build\web"
  }
  if (-not (Test-Path (Join-Path $WebRoot "index.html"))) {
    throw "Chua co build web: $WebRoot`nChay: flutter build web --release"
  }
}

if (-not ([System.Management.Automation.PSTypeName]'WinCap').Type) {
  Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public class WinCap {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public static void Capture(IntPtr hwnd, string path) {
    RECT r; GetWindowRect(hwnd, out r);
    int w = r.Right - r.Left, h = r.Bottom - r.Top;
    if (w < 1 || h < 1) throw new Exception("Cua so kich thuoc 0");
    using (Bitmap bmp = new Bitmap(w, h))
    using (Graphics g = Graphics.FromImage(bmp)) {
      IntPtr hdc = g.GetHdc();
      PrintWindow(hwnd, hdc, 2); // 2 = PW_RENDERFULLCONTENT
      g.ReleaseHdc(hdc);
      bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
    }
  }
}
"@
}

# 1) server tĩnh (node có sẵn trên máy dev) — bỏ qua khi chụp -Url đang host thật
$node = $null
if (-not $Url) {
  $node = Start-Process node -ArgumentList "`"$skillDir\web-server.js`"", "$Port", "`"$WebRoot`"" -PassThru -WindowStyle Hidden
  Start-Sleep -Seconds 1
  $Url = "http://127.0.0.1:$Port/"
}

# 2) trình duyệt chế độ app — profile tạm trong TEMP (đừng xả rác vào repo)
$browser = $null
foreach ($cand in @("$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
                    "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
                    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
                    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe")) {
  if ($cand -and (Test-Path $cand)) { $browser = $cand; break }
}
if (-not $browser) { if ($node) { Stop-Process -Id $node.Id -Force }; throw "Khong tim thay Chrome/Edge" }
$profileDir = Join-Path $env:TEMP "fbtrapid_webshot_profile"
Start-Process $browser -ArgumentList "--app=$Url", "--user-data-dir=`"$profileDir`"", "--window-size=1280,800", "--no-first-run", "--disable-gpu" | Out-Null

# 3) chờ cửa sổ FBT_RAPID (title của trang)
$deadline = (Get-Date).AddSeconds($Timeout)
$win = $null
while ((Get-Date) -lt $deadline) {
  $win = Get-Process chrome, msedge -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowHandle -ne 0 -and $_.MainWindowTitle -like "FBT_RAPID*" } |
    Select-Object -First 1
  if ($win) { break }
  Start-Sleep -Milliseconds 400
}
if (-not $win) {
  if ($node) { Stop-Process -Id $node.Id -Force -ErrorAction SilentlyContinue }
  throw "Het $Timeout s khong thay cua so FBT_RAPID (app web chua boot?)"
}
Start-Sleep -Seconds $Settle

$Out = if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path (Get-Location) $Out }
[WinCap]::Capture($win.MainWindowHandle, $Out)
Write-Output "OK title='$($win.MainWindowTitle)' pid=$($win.Id) -> $Out"

Stop-Process -Id $win.Id -Force -ErrorAction SilentlyContinue
if ($node) { Stop-Process -Id $node.Id -Force -ErrorAction SilentlyContinue }
