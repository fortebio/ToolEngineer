@echo off
rem Build firmware Rapid4P theo board (Windows, cmd.exe).
rem
rem   scripts\build.bat                       build board mac dinh p4_43lcd   -> in BUILD_EXIT=0/1
rem   scripts\build.bat s3_28lcd              build board S3 + LCD 2.8"
rem   scripts\build.bat s3_28lcd COM5         build + flash (khong monitor)
rem   scripts\build.bat COM48                 (tuong thich cu) = p4_43lcd + flash COM48
rem
rem Board:  p4_43lcd = FBT ESP32-P4C5 + LCD 4.3" DSI  (khoa san pham rapid4p,    target esp32p4)
rem         s3_28lcd = ES3N28P ESP32-S3 + LCD 2.8" SPI (khoa san pham rapid4p-s3, target esp32s3)
rem Moi board mot build dir build_<board>\ va sdkconfig RIENG trong do (CMakeLists.txt goc dat
rem SDKCONFIG theo R4P_BOARD) -> doi board khong de sdkconfig cua nhau.
rem
rem Tu Git Bash:  MSYS_NO_PATHCONV=1 cmd.exe /c "scripts\build.bat s3_28lcd"
rem (export.sh cua ESP-IDF khong dat IDF_PATH tren Windows; idf_cmd_init.bat tu choi khi
rem thay bien MSYSTEM -> script xoa bien do.) Ke thua firmware-vimate-p4/scripts.
rem
rem Bien moi truong tuy chon:
rem   IDF_TOOLS_PATH  thu muc cai ESP-IDF Tools     (mac dinh C:\Espressif)
rem   R4P_IDF_ID      id ban ESP-IDF trong idf_cmd_init.bat (mac dinh: ban dang chon
rem                   trong esp_idf.json; 5.5.1 va 5.5.4 deu build duoc)
rem File nay phai la ASCII thuan (cmd.exe doc theo codepage OEM).
setlocal
if "%IDF_TOOLS_PATH%"=="" set IDF_TOOLS_PATH=C:\Espressif
rem R4P_IDF_ID: id ban ESP-IDF trong idf_cmd_init.bat. Id la hash RIENG TUNG MAY -> mac dinh
rem doc "idfSelectedId" tu %IDF_TOOLS_PATH%\esp_idf.json (ban dang chon trong ESP-IDF Tools);
rem dat bien de chon ban khac. Board P4 rev v1.3 can toolchain esp32p4: kiem idf-env.json.
if "%R4P_IDF_ID%"=="" for /f "tokens=2 delims=:, " %%a in ('findstr /c:"idfSelectedId" "%IDF_TOOLS_PATH%\esp_idf.json"') do set R4P_IDF_ID=%%~a
if "%R4P_IDF_ID%"=="" set R4P_IDF_ID=esp-idf-29323a3f5a0574597d6dbaa0af20c775

rem --- tham so: [board] [COM] ; tham so 1 bat dau bang COM -> board mac dinh ---
set BOARD=p4_43lcd
set PORT=
set A1=%~1
if "%A1%"=="" set A1=none
if /i "%A1:~0,3%"=="COM" (
  set PORT=%1
) else (
  if not "%1"=="" set BOARD=%1
  set PORT=%2
)
set TARGET=
if /i "%BOARD%"=="p4_43lcd" set TARGET=esp32p4
if /i "%BOARD%"=="s3_28lcd" set TARGET=esp32s3
if "%TARGET%"=="" (
  echo Board khong hop le: %BOARD% - dung p4_43lcd hoac s3_28lcd
  exit /b 2
)

set MSYSTEM=
set PYTHONIOENCODING=utf-8
call "%IDF_TOOLS_PATH%\idf_cmd_init.bat" %R4P_IDF_ID%
if errorlevel 1 exit /b 1

cd /d "%~dp0.."
set BD=build_%BOARD%
set DEF1=sdkconfig.defaults
set DEF2=sdkconfig.defaults.%BOARD%
if not exist %DEF2% (
  echo Thieu profile %DEF2%
  exit /b 2
)

rem ESP-IDF chi dung sdkconfig.defaults* de SINH sdkconfig lan dau; defaults moi hon
rem sdkconfig -> xoa de sinh lai (README-P4 muc 2 bay 2). sdkconfig nam TRONG build dir.
if exist %BD%\sdkconfig (
  python -c "import os,sys; s=os.path.getmtime(r'%BD%/sdkconfig'); d=[f for f in ('%DEF1%','%DEF2%') if os.path.getmtime(f)>s]; print('defaults moi hon sdkconfig:',d); sys.exit(1 if d else 0)"
  if errorlevel 1 (
    echo === xoa %BD%\sdkconfig de ap defaults moi ===
    del /q %BD%\sdkconfig
  )
)

if exist %BD% (
  if not exist %BD%\CMakeCache.txt (
    echo === build dir cu khong co CMakeCache, xoa ===
    rmdir /s /q %BD%
  )
)
if not exist %BD%\CMakeCache.txt (
  echo === set-target %TARGET% [%BD%] board=%BOARD% ===
  idf.py -B %BD% -DR4P_BOARD=%BOARD% set-target %TARGET%
  if errorlevel 1 exit /b 1
)

echo === build [%BD%] board=%BOARD% ===
idf.py -B %BD% build
if errorlevel 1 (
  echo BUILD_EXIT=1
  exit /b 1
)
for %%f in (%BD%\rapid4p*.bin) do echo BIN %%f %%~zf bytes
echo BUILD_EXIT=0

if not "%PORT%"=="" (
  echo === flash [%PORT%] ===
  idf.py -B %BD% -p %PORT% -b 460800 flash
  exit /b %errorlevel%
)
exit /b 0
