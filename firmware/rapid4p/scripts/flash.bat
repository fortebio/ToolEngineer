@echo off
rem Nap firmware da build (khong build lai), theo board.
rem
rem   scripts\flash.bat COM48               nap build_p4_43lcd (board mac dinh)
rem   scripts\flash.bat s3_28lcd COM5       nap build_s3_28lcd
rem
rem File nay phai la ASCII thuan (cmd.exe doc theo codepage OEM).
setlocal
if "%IDF_TOOLS_PATH%"=="" set IDF_TOOLS_PATH=C:\Espressif
if "%R4P_IDF_ID%"=="" for /f "tokens=2 delims=:, " %%a in ('findstr /c:"idfSelectedId" "%IDF_TOOLS_PATH%\esp_idf.json"') do set R4P_IDF_ID=%%~a
if "%R4P_IDF_ID%"=="" set R4P_IDF_ID=esp-idf-29323a3f5a0574597d6dbaa0af20c775

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
if "%PORT%"=="" (
  echo dung: scripts\flash.bat [p4_43lcd^|s3_28lcd] COMxx
  exit /b 2
)
cd /d "%~dp0.."
set BD=build_%BOARD%
if not exist %BD%\CMakeCache.txt (
  echo Chua build %BD% - chay scripts\build.bat %BOARD% truoc
  exit /b 2
)
set MSYSTEM=
call "%IDF_TOOLS_PATH%\idf_cmd_init.bat" %R4P_IDF_ID%
if errorlevel 1 exit /b 1
idf.py -B %BD% -p %PORT% -b 460800 flash
exit /b %errorlevel%
