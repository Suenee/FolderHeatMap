@echo off
cls
setlocal EnableExtensions
cd /d "%~dp0"

echo ============================================
echo FolderHeatMap - INSTALL / REPAIR
echo ============================================
echo.

if not exist "%~dp0install.ps1" (
    echo ERROR: Internal installer helper install.ps1 was not found beside install.cmd.
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" (
    echo.
    echo INSTALL FAILED
    echo See: %~dp0logs\install.log
)
exit /b %RC%
