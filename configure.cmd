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

rem Validate the ACTUAL settings window, not just any dialog owned by the process.
rem The previous test mistook the startup error MessageBox for a healthy GUI.
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$p=Get-Process FolderHeatMapConfig -ErrorAction SilentlyContinue | Select-Object -First 1; if(-not $p){exit 2}; Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class FhmWin { [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string title); }'; $h=[FhmWin]::FindWindow('FolderHeatMapConfigWindow',$null); if($h -ne [IntPtr]::Zero){exit 0}; exit 3"
set "RC=%ERRORLEVEL%"

if "%RC%"=="0" (
    echo FolderHeatMap configuration window started successfully.
    exit /b 0
)

if "%RC%"=="2" (
    echo ERROR: FolderHeatMapConfig.exe terminated during startup.
) else (
    echo ERROR: FolderHeatMapConfig.exe started, but the real FolderHeatMap settings window was not created.
    echo An error dialog may still be visible; close it after taking a screenshot.
)

echo.
echo IMPORTANT: If src\ConfigApp.cpp changed after your last successful upgrade,
echo run upgrade.cmd before testing configure.cmd, otherwise dist contains the old EXE.
pause
exit /b %RC%
