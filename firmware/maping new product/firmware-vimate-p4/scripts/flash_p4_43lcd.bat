@echo off
rem Chi nap firmware da build (build_p4_43lcd\) vao board P4 qua cong CH343.
rem
rem   scripts\flash_p4_43lcd.bat COM48
rem
rem Truoc khi nap: tat moi tien trinh dang giu cong COM (python p4_readlog.py chay
rem nen, idf.py monitor, terminal) - esptool khong mo duoc cong dang ban.
rem File nay phai la ASCII thuan (cmd.exe doc theo codepage OEM).
setlocal
if "%1"=="" (
  echo Dung: scripts\flash_p4_43lcd.bat COMxx
  exit /b 2
)
if "%IDF_TOOLS_PATH%"=="" set IDF_TOOLS_PATH=C:\Espressif
if "%VIMATE_IDF_ID%"=="" set VIMATE_IDF_ID=esp-idf-29323a3f5a0574597d6dbaa0af20c775
rem export.bat cua ESP-IDF tu choi khi thay bien MSYSTEM (Git Bash de lai khi goi
rem `cmd.exe /c`). Xoa no trong pham vi script nay; cmd.exe that khong co bien do.
set MSYSTEM=
call "%IDF_TOOLS_PATH%\idf_cmd_init.bat" %VIMATE_IDF_ID%
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
echo === flash build_p4_43lcd -^> %1 ===
idf.py -B build_p4_43lcd -p %1 -b 460800 flash
exit /b %errorlevel%
