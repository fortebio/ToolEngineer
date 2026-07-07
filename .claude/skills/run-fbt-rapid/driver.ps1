# driver.ps1 — build/launch/screenshot harness cho app FBT_RAPID (Flutter Windows desktop).
#
# App là GUI Windows nên KHÔNG có Playwright/curl để "chạm" vào. Cách lái thật:
# launch file .exe đã build -> chờ cửa sổ "FBT_RAPID" xuất hiện -> chụp đúng CỬA SỔ đó
# bằng Win32 PrintWindow (PW_RENDERFULLCONTENT) ra PNG. Chụp theo handle nên KHÔNG
# cần đưa cửa sổ lên foreground, chạy được cả khi có app khác che.
#
# Vi du:
#   pwsh .claude/skills/run-fbt-rapid/driver.ps1                       # launch Debug, chụp, đóng
#   pwsh .claude/skills/run-fbt-rapid/driver.ps1 -KeepOpen             # để app chạy tiếp (in ra PID)
#   pwsh .claude/skills/run-fbt-rapid/driver.ps1 -Release -Out a.png   # chụp build Release
#   pwsh .claude/skills/run-fbt-rapid/driver.ps1 -Attach -Out now.png  # chụp app ĐANG chạy (không launch)
[CmdletBinding()]
param(
  [string]$Exe,                 # đường dẫn .exe; mặc định = build Debug
  [switch]$Release,             # dùng build Release thay vì Debug
  [string]$Out = "_smoke.png",  # nơi lưu ảnh PNG
  [int]$Timeout = 30,           # giây chờ cửa sổ xuất hiện
  [int]$Settle = 6,             # giây chờ sau khi có cửa sổ (để UI render / tải dữ liệu)
  [switch]$KeepOpen,            # KHÔNG đóng app sau khi chụp
  [switch]$Attach               # chụp tiến trình ĐANG chạy (không launch mới)
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSCommandPath | Split-Path -Parent | Split-Path -Parent | Split-Path -Parent
if (-not $Exe) {
  $cfg = if ($Release) { "Release" } else { "Debug" }
  $Exe = Join-Path $root "build\windows\x64\runner\$cfg\fbt_dxd_app.exe"
}

# --- Win32 capture: chụp 1 cửa sổ theo HWND ra PNG (kể cả khi không foreground) ---
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
    if (w < 1 || h < 1) throw new Exception("Cua so kich thuoc 0 (chua hien?)");
    using (Bitmap bmp = new Bitmap(w, h))
    using (Graphics g = Graphics.FromImage(bmp)) {
      IntPtr hdc = g.GetHdc();
      PrintWindow(hwnd, hdc, 2); // 2 = PW_RENDERFULLCONTENT (cần cho Flutter/DWM)
      g.ReleaseHdc(hdc);
      bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
    }
  }
}
"@
}

function Wait-Window($proc, $sec) {
  $deadline = (Get-Date).AddSeconds($sec)
  while ((Get-Date) -lt $deadline) {
    $proc.Refresh()
    if ($proc.HasExited) { throw "App tự thoát (exit $($proc.ExitCode)) trước khi mở cửa sổ." }
    if ($proc.MainWindowHandle -ne 0) { return $proc.MainWindowHandle }
    Start-Sleep -Milliseconds 300
  }
  throw "Hết $sec s mà cửa sổ chưa xuất hiện."
}

if ($Attach) {
  $proc = Get-Process fbt_dxd_app -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if (-not $proc) { throw "Không thấy tiến trình fbt_dxd_app nào có cửa sổ. Bỏ -Attach để launch mới." }
  $hwnd = $proc.MainWindowHandle
} else {
  if (-not (Test-Path $Exe)) { throw "Chưa có exe: $Exe`nChạy: flutter build windows --debug (hoặc --release)" }
  $proc = Start-Process -FilePath $Exe -PassThru
  $hwnd = Wait-Window $proc $Timeout
  Start-Sleep -Seconds $Settle
}

$Out = if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path (Get-Location) $Out }
[WinCap]::Capture($hwnd, $Out)
Write-Output "OK title='$($proc.MainWindowTitle)' pid=$($proc.Id) -> $Out"

if (-not $Attach -and -not $KeepOpen) {
  Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
  Write-Output "Đã đóng app (pid $($proc.Id)). Dùng -KeepOpen để giữ lại."
} elseif ($KeepOpen) {
  Write-Output "App vẫn chạy: pid=$($proc.Id). Đóng: Stop-Process -Id $($proc.Id) -Force"
}
