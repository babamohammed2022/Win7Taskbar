@echo off
rem ===========================================================================
rem  Win7Taskbar - one-click build.
rem
rem  Double-click this file: it installs the .NET SDK if needed, builds the
rem  application as self-contained software, and creates the distributable ZIP.
rem
rem  No technical knowledge is required: keep the window open and read the
rem  summary when the build finishes.
rem ===========================================================================
chcp 65001 >nul 2>nul
setlocal
cd /d "%~dp0.."

set "SCRIPT=%~dp0..\build\Compila-Release.ps1"
if not exist "%SCRIPT%" (
  echo.
  echo ERROR: build\Compila-Release.ps1 was not found.
  echo Make sure compile.bat is inside the docs folder.
  echo.
  pause
  exit /b 1
)

echo.
echo ============================================================
echo   Win7Taskbar - automatic build
echo ============================================================
echo.

where pwsh.exe >nul 2>nul
if %errorlevel%==0 (
  pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" %*
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" %*
)

echo.
echo If the window shows errors, copy the output and open a GitHub issue.
echo Press any key to close this window...
pause >nul
endlocal
