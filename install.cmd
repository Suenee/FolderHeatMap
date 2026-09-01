@echo off
setlocal EnableExtensions
cd /d "%~dp0"
if not exist "%~dp0install.ps1" (
    echo ERROR: install.ps1 was not found beside install.cmd.
    exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
exit /b %ERRORLEVEL%
