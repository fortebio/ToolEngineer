; Inno Setup script — trình cài đặt FBT_RAPID App (Windows)
; Biên dịch: "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" installer.iss
; Kết quả:   C:\Users\nvdat\Downloads\FBT_RAPID-Setup-v<version>.exe

#define MyAppName "FBT_RAPID"
#define MyAppVersion "1.0.7"
#define MyAppPublisher "Fortebiotech"
#define MyAppExeName "fbt_dxd_app.exe"
; Path tương đối theo vị trí file .iss (gốc repo app) — không phụ thuộc máy/thư mục.
#define MySource AddBackslash(SourcePath) + "build\windows\x64\runner\Release"
#define MyIcon AddBackslash(SourcePath) + "windows\runner\resources\app_icon.ico"

[Setup]
AppId={{B7E9F3A2-5C41-4D8E-9A6B-2F1C8D4E7A90}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=C:\Users\nvdat\Downloads
OutputBaseFilename=FBT_RAPID-Setup-v{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile={#MyIcon}
UninstallDisplayIcon={app}\{#MyAppExeName}

[Files]
Source: "{#MySource}\*"; DestDir: "{app}"; Excludes: "HUONG-DAN.txt"; Flags: recursesubdirs ignoreversion
; esptool đi kèm (tab Kỹ Thuật → Nạp code) — ép gói từ windows\vendor\ kể cả khi CMake chưa copy.
; skipifsourcedoesntexist: chưa đặt esptool.exe thì vẫn đóng gói được (không lỗi compile).
Source: "{#AddBackslash(SourcePath)}windows\vendor\esptool.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Gỡ cài đặt {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Tạo biểu tượng ngoài màn hình (Desktop)"; GroupDescription: "Tùy chọn:"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Mở {#MyAppName} ngay"; Flags: nowait postinstall skipifsilent
