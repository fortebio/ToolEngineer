# Đóng gói FBT_RAPID App (Windows): build -> stage (kèm VC++ runtime) -> ZIP + installer.
# Chạy:  powershell -ExecutionPolicy Bypass -File tools\package.ps1
$ErrorActionPreference = 'Stop'

$appDir = 'c:\Users\nvdat\Downloads\FBT-DXD\app'
$rel    = "$appDir\build\windows\x64\runner\Release"
$stage  = 'C:\Users\nvdat\Downloads\FBT_RAPID-App'   # phải KHỚP MySource trong installer.iss
$iss    = 'c:\Users\nvdat\Downloads\FBT-DXD\tools\installer.iss'
$iscc   = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
$out    = 'C:\Users\nvdat\Downloads'
$dlls   = 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'

# Đóng app đang chạy (nếu có) để khỏi khóa file .exe khi build
Stop-Process -Name fbt_dxd_app -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

Write-Host '== flutter build windows ==' -ForegroundColor Cyan
Push-Location $appDir
flutter build windows
Pop-Location

Write-Host '== stage (build + VC++ runtime) ==' -ForegroundColor Cyan
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory $stage | Out-Null
Copy-Item "$rel\*" $stage -Recurse -Force
foreach ($d in $dlls) { Copy-Item "C:\Windows\System32\$d" $stage -Force }

@'
FBT_RAPID App (Windows)
- Giải nén TOÀN BỘ thư mục rồi chạy fbt_dxd_app.exe (giữ nguyên các .dll + thư mục data cạnh .exe).
- Nếu SmartScreen cảnh báo (app chưa ký số): More info -> Run anyway.
- 4 tab: Lịch sử | Cloud | Nhiệt độ (UART) | Cài đặt. Link cloud đã gắn sẵn (không cần nhập).
'@ | Set-Content "$stage\HUONG-DAN.txt" -Encoding utf8

Write-Host '== ZIP ==' -ForegroundColor Cyan
$zip = "$out\FBT_RAPID-App-Windows.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal

Write-Host '== installer (Inno Setup) ==' -ForegroundColor Cyan
& $iscc $iss | Out-Null

Write-Host "`nDONE. Sản phẩm trong $out :" -ForegroundColor Green
Get-ChildItem "$out\FBT_RAPID-*" |
  Select-Object Name, @{ n = 'MB'; e = { [math]::Round($_.Length / 1MB, 1) } }
