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

echo Starting FolderHeatMap configuration:
echo %CONFIG_EXE%
echo.

rem Run directly so an immediate startup failure is not hidden by START.
"%CONFIG_EXE%"
set "RC=%ERRORLEVEL%"

if "%RC%"=="0" exit /b 0

echo.
echo ERROR: FolderHeatMapConfig.exe exited with code %RC%.
echo Please send a screenshot of this window.
pause
exit /b %RC%
