@echo off
rem Chi nap (khong build) firmware Rapid4P: scripts\flash.bat COMxx
rem COMxx: python scripts\readlog.py --list (so COM cua CH343 doi theo lan cam).
setlocal
if "%IDF_TOOLS_PATH%"=="" set IDF_TOOLS_PATH=C:\Espressif
if "%R4P_IDF_ID%"=="" set R4P_IDF_ID=esp-idf-29323a3f5a0574597d6dbaa0af20c775
if "%1"=="" (
  echo dung: scripts\flash.bat COMxx
  exit /b 2
)
set MSYSTEM=
call "%IDF_TOOLS_PATH%\idf_cmd_init.bat" %R4P_IDF_ID%
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
idf.py -B build -p %1 -b 460800 flash
exit /b %errorlevel%
