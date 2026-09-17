@echo off
rem Build firmware Rapid4P cho board ESP32-P4C5 + LCD 4.3" (Windows, cmd.exe).
rem
rem   scripts\build.bat              build                      -> in BUILD_EXIT=0/1
rem   scripts\build.bat COM48        build + flash (khong monitor)
rem
rem Tu Git Bash:  MSYS_NO_PATHCONV=1 cmd.exe /c "scripts\build.bat"
rem (export.sh cua ESP-IDF khong dat IDF_PATH tren Windows; idf_cmd_init.bat tu choi khi
rem thay bien MSYSTEM -> script xoa bien do.) Ke thua firmware-vimate-p4/scripts.
rem
rem Bien moi truong tuy chon:
rem   IDF_TOOLS_PATH  thu muc cai ESP-IDF Tools     (mac dinh C:\Espressif)
rem   R4P_IDF_ID      id ban ESP-IDF trong idf_cmd_init.bat (mac dinh = 5.5.1)
rem File nay phai la ASCII thuan (cmd.exe doc theo codepage OEM).
setlocal
if "%IDF_TOOLS_PATH%"=="" set IDF_TOOLS_PATH=C:\Espressif
if "%R4P_IDF_ID%"=="" set R4P_IDF_ID=esp-idf-29323a3f5a0574597d6dbaa0af20c775
set MSYSTEM=
set PYTHONIOENCODING=utf-8
call "%IDF_TOOLS_PATH%\idf_cmd_init.bat" %R4P_IDF_ID%
if errorlevel 1 exit /b 1

cd /d "%~dp0.."
set BD=build
set DEFAULTS=sdkconfig.defaults
set PORT=%1

rem ESP-IDF chi dung sdkconfig.defaults de SINH sdkconfig lan dau; defaults moi hon
rem sdkconfig -> xoa de sinh lai (README-P4 muc 2 bay 2).
if exist sdkconfig (
  python -c "import os,sys; d=os.path.getmtime('sdkconfig.defaults')>os.path.getmtime('sdkconfig'); print('defaults moi hon sdkconfig:',d); sys.exit(1 if d else 0)"
  if errorlevel 1 (
    echo === xoa sdkconfig de ap defaults moi ===
    del /q sdkconfig
  )
)

if exist %BD% (
  if not exist %BD%\CMakeCache.txt (
    echo === build dir cu khong co CMakeCache, xoa ===
    rmdir /s /q %BD%
  )
)
if not exist %BD%\CMakeCache.txt (
  echo === set-target esp32p4 [%BD%] ===
  idf.py -B %BD% -DSDKCONFIG_DEFAULTS="%DEFAULTS%" set-target esp32p4
  if errorlevel 1 exit /b 1
)

echo === build [%BD%] ===
idf.py -B %BD% -DSDKCONFIG_DEFAULTS="%DEFAULTS%" build
if errorlevel 1 (
  echo BUILD_EXIT=1
  exit /b 1
)
echo BUILD_EXIT=0

if not "%PORT%"=="" (
  echo === flash [%PORT%] ===
  idf.py -B %BD% -p %PORT% -b 460800 flash
  exit /b %errorlevel%
)
exit /b 0
