@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Always prefer the deployed configurator. Do not launch stale copies from
rem the repository root; upgrade.cmd removes those legacy copies as well.
set "CONFIG_EXE="
if exist "dist\FolderHeatMapConfig.exe" set "CONFIG_EXE=%CD%\dist\FolderHeatMapConfig.exe"
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

rem Validate without inline C# / Add-Type. Quoting C# inside CMD was fragile.
rem The real settings window has a longer title than the simple startup error dialog "FolderHeatMap".
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$p=Get-Process FolderHeatMapConfig -ErrorAction SilentlyContinue | Select-Object -First 1; if(-not $p){exit 2}; $p.Refresh(); if($p.MainWindowHandle -eq 0){exit 3}; if($p.MainWindowTitle.Length -le 13){exit 4}; exit 0"
set "RC=%ERRORLEVEL%"

if "%RC%"=="0" (
    echo FolderHeatMap configuration window started successfully.
    exit /b 0
)

if "%RC%"=="2" (
    echo ERROR: FolderHeatMapConfig.exe terminated during startup.
) else if "%RC%"=="3" (
    echo ERROR: FolderHeatMapConfig.exe is running but has no visible main window.
) else (
    echo ERROR: FolderHeatMapConfig.exe showed only a startup/error dialog, not the settings window.
)

echo.
echo IMPORTANT: Run upgrade.cmd after source changes before testing configure.cmd.
pause
exit /b %RC%
