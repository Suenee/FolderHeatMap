@echo off
setlocal EnableExtensions

rem FolderHeatMap NAS Identity Test launcher 1.00
set "ROOT=%~dp0"
set "EXE=%ROOT%build\Release\FolderHeatMapNasIdTest.exe"

if not exist "%EXE%" (
    echo ERROR: NAS identity test executable was not found.
    echo Expected: "%EXE%"
    echo Run upgrade.cmd first so the diagnostic target is built.
    exit /b 2
)

if "%~1"=="" goto :usage

if "%~2"=="" (
    "%EXE%" "%~1"
) else (
    "%EXE%" "%~1" "%~2"
)
exit /b %ERRORLEVEL%

:usage
echo FolderHeatMap NAS Identity Test 1.00
echo.
echo Usage:
echo   test-nas-id.cmd "path-A" ["path-B"]
echo.
echo Example:
echo   test-nas-id.cmd "N:\WORK\GitHub" "\\192.168.2.10\root\WORK\GitHub"
exit /b 2
