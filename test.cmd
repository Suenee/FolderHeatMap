@echo off
setlocal EnableExtensions

set "REPO_DIR=%~dp0"
if "%REPO_DIR:~-1%"=="\" set "REPO_DIR=%REPO_DIR:~0,-1%"
cd /d "%REPO_DIR%"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%REPO_DIR%\test.ps1"
set "TEST_RC=%ERRORLEVEL%"
exit /b %TEST_RC%
