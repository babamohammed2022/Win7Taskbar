@echo off
rem ==========================================================================
rem  Win7Taskbar - compilazione a un clic.
rem
rem  Doppio clic su questo file: installa da solo il .NET SDK se manca,
rem  compila l'applicazione (self-contained: l'utente finale non dovra'
rem  installare nessuna versione di .NET) e crea lo zip pronto da distribuire.
rem
rem  Non serve alcuna conoscenza tecnica: lascia la finestra aperta e alla
rem  fine leggi il riepilogo.
rem ==========================================================================
chcp 65001 >nul 2>nul
setlocal
cd /d "%~dp0"

set "SCRIPT=%~dp0build\Compila-Release.ps1"
if not exist "%SCRIPT%" (
  echo.
  echo ERRORE: non trovo build\Compila-Release.ps1
  echo Assicurati di aver avviato COMPILA.bat dalla cartella principale del repository.
  echo.
  pause
  exit /b 1
)

echo.
echo ============================================================
echo   Win7Taskbar - compilazione automatica
echo ============================================================
echo.

where pwsh.exe >nul 2>nul
if %errorlevel%==0 (
  pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" %*
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" %*
)

echo.
echo (se la finestra mostra errori, copia il testo e aprilo come issue su GitHub)
echo Premi un tasto per chiudere questa finestra...
pause >nul
endlocal
