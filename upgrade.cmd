@echo off
cls
setlocal EnableExtensions

set "UPGRADE_REV=1.15-powershell-runner-reset"
set "REPO_DIR=%~dp0"
if "%REPO_DIR:~-1%"=="\" set "REPO_DIR=%REPO_DIR:~0,-1%"
cd /d "%REPO_DIR%"

where git.exe >nul 2>nul
if errorlevel 1 (
    powershell.exe -NoProfile -Command "Write-Host 'ERROR: Git was not found in PATH.' -ForegroundColor Red"
    exit /b 1
)

git rev-parse --is-inside-work-tree >nul 2>nul
if errorlevel 1 (
    powershell.exe -NoProfile -Command "Write-Host 'ERROR: This folder is not a Git working tree.' -ForegroundColor Red"
    exit /b 1
)

git fetch origin >nul 2>nul
if errorlevel 1 (
    > "%REPO_DIR%\upgrade.log" echo ERROR: git fetch origin failed before PowerShell runner bootstrap.
    >> "%REPO_DIR%\upgrade.log" echo STATUS: FAILED - phase=SELF-UPDATE/BOOTSTRAP
    powershell.exe -NoProfile -Command "Write-Host 'ERROR: git fetch origin failed before upgrade bootstrap.' -ForegroundColor Red"
    exit /b 1
)

set "RUNNER_TEMP=%TEMP%\FolderHeatMap-upgrade-%RANDOM%-%RANDOM%.ps1"
git show origin/devel:upgrade.ps1 > "%RUNNER_TEMP%" 2>nul
if errorlevel 1 (
    > "%REPO_DIR%\upgrade.log" echo ERROR: Could not extract origin/devel:upgrade.ps1.
    >> "%REPO_DIR%\upgrade.log" echo STATUS: FAILED - phase=SELF-UPDATE/BOOTSTRAP
    powershell.exe -NoProfile -Command "Write-Host 'ERROR: Could not extract upgrade.ps1 from origin/devel.' -ForegroundColor Red"
    exit /b 1
)

set "FHM_UPGRADE_REPO=%REPO_DIR%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%RUNNER_TEMP%"
set "UPGRADE_RC=%ERRORLEVEL%"
del /q "%RUNNER_TEMP%" >nul 2>nul
exit /b %UPGRADE_RC%
