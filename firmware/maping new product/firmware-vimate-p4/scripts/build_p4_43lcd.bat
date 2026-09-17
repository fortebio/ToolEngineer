@echo off
rem Build firmware VIMATE cho board ESP32-P4 + LCD 4.3" ST7102 (Windows, cmd.exe).
rem
rem   scripts\build_p4_43lcd.bat              build
rem   scripts\build_p4_43lcd.bat COM48        build + flash (khong monitor)
rem
rem Ban .bat cua scripts/build_p4_43lcd.sh: tren may Windows, `export.sh` khong dat
rem duoc IDF_PATH va `idf_cmd_init.bat` tu choi khi goi tu Git Bash ("This .bat file
rem is for Windows CMD.EXE shell only") -> phai chay qua cmd.exe (README-P4 muc 2).
rem
rem Bien moi truong tuy chon:
rem   IDF_TOOLS_PATH  thu muc cai ESP-IDF Tools     (mac dinh C:\Espressif)
rem   VIMATE_IDF_ID   id ban ESP-IDF trong idf_cmd_init.bat (mac dinh = 5.5.1 dang dung)
rem File nay phai la ASCII thuan (cmd.exe doc theo codepage OEM).
setlocal
if "%IDF_TOOLS_PATH%"=="" set IDF_TOOLS_PATH=C:\Espressif
if "%VIMATE_IDF_ID%"=="" set VIMATE_IDF_ID=esp-idf-29323a3f5a0574597d6dbaa0af20c775
rem export.bat cua ESP-IDF tu choi khi thay bien MSYSTEM (Git Bash de lai khi goi
rem `cmd.exe /c`). Xoa no trong pham vi script nay; cmd.exe that khong co bien do.
set MSYSTEM=
call "%IDF_TOOLS_PATH%\idf_cmd_init.bat" %VIMATE_IDF_ID%
if errorlevel 1 exit /b 1

cd /d "%~dp0.."
set BD=build_p4_43lcd
set DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.p4-43lcd
set PORT=%1

rem ESP-IDF chi dung sdkconfig.defaults* de SINH sdkconfig lan dau; sau do bo qua
rem im lang (README-P4 muc 2 bay 2). Defaults moi hon sdkconfig -> xoa de sinh lai.
if exist sdkconfig (
  python -c "import os,sys; d=[f for f in ('sdkconfig.defaults','sdkconfig.defaults.p4-43lcd') if os.path.getmtime(f)>os.path.getmtime('sdkconfig')]; print('defaults moi hon sdkconfig:',d); sys.exit(1 if d else 0)"
  if errorlevel 1 (
    echo === xoa sdkconfig de ap defaults moi ===
    del /q sdkconfig
  )
)

rem set-target chi can khi build dir chua cau hinh (no keo theo fullclean).
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
