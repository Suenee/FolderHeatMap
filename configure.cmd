@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "CONFIG_EXE="
if exist "FolderHeatMapConfig.exe" set "CONFIG_EXE=%CD%\FolderHeatMapConfig.exe"
if not defined CONFIG_EXE if exist "dist\FolderHeatMapConfig.exe" set "CONFIG_EXE=%CD%\dist\FolderHeatMapConfig.exe"
if not defined CONFIG_EXE if exist "build\Release\FolderHeatMapConfig.exe" set "CONFIG_EXE=%CD%\build\Release\FolderHeatMapConfig.exe"

if not defined CONFIG_EXE (
    echo ERROR: FolderHeatMapConfig.exe was not found.
    echo Run upgrade.cmd first.
    echo.
    pause
    exit /b 1
)

rem Kill a stale configuration process left behind by a previous failed test.
taskkill /F /IM FolderHeatMapConfig.exe >nul 2>nul

echo Starting FolderHeatMap configuration:
echo %CONFIG_EXE%
echo.
start "FolderHeatMapConfig" "%CONFIG_EXE%"

timeout /t 2 /nobreak >nul

rem A healthy GUI process must own a visible top-level window. Do not leave the terminal blocked by a headless process.
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$p=Get-Process FolderHeatMapConfig -ErrorAction SilentlyContinue | Select-Object -First 1; if(-not $p){exit 2}; $p.Refresh(); if($p.MainWindowHandle -eq 0){exit 3}; exit 0"
set "RC=%ERRORLEVEL%"

if "%RC%"=="0" (
    echo FolderHeatMap configuration window started successfully.
    exit /b 0
)

if "%RC%"=="2" (
    echo ERROR: FolderHeatMapConfig.exe terminated during startup.
) else (
    echo ERROR: FolderHeatMapConfig.exe is running but did not create a visible window.
    echo The stale process will be terminated automatically.
    taskkill /F /IM FolderHeatMapConfig.exe >nul 2>nul
)

echo Please send a screenshot of this window.
pause
exit /b %RC%
