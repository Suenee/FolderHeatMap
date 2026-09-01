@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "INSTALL_REV=1.04"
cd /d "%~dp0"
echo FolderHeatMap install %INSTALL_REV%

if not exist "%~dp0install.ps1" (
    echo ERROR: install.ps1 was not found beside install.cmd.
    exit /b 1
)

rem Resolve Total Commander executable before the internal installer runs.
rem Older TC installations commonly live in C:\totalcmd and may not expose
rem InstallDir through the registry used by newer installations.
if defined COMMANDER_PATH (
    if exist "%COMMANDER_PATH%\TOTALCMD64.EXE" goto tc_path_ready
    if exist "%COMMANDER_PATH%\TOTALCMD.EXE" goto tc_path_ready
)

for %%D in (
    "C:\totalcmd"
    "C:\TotalCommander"
    "%ProgramFiles%\totalcmd"
    "%ProgramFiles%\Total Commander"
    "%ProgramFiles(x86)%\totalcmd"
    "%ProgramFiles(x86)%\Total Commander"
    "%LOCALAPPDATA%\Programs\Total Commander"
) do (
    if exist "%%~D\TOTALCMD64.EXE" (
        set "COMMANDER_PATH=%%~D"
        goto tc_path_ready
    )
    if exist "%%~D\TOTALCMD.EXE" (
        set "COMMANDER_PATH=%%~D"
        goto tc_path_ready
    )
)

for %%E in (TOTALCMD64.EXE TOTALCMD.EXE) do (
    for /f "delims=" %%P in ('where %%E 2^>nul') do (
        set "TC_EXE=%%~fP"
        for %%Q in ("!TC_EXE!") do set "COMMANDER_PATH=%%~dpQ"
        goto tc_path_ready
    )
)

rem Last-resort discovery from a currently running Total Commander process.
for /f "usebackq delims=" %%P in (`powershell.exe -NoProfile -Command "$p=Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue ^| Where-Object {$_.Path} ^| Select-Object -First 1 -ExpandProperty Path; if($p){[Console]::Write($p)}" 2^>nul`) do (
    set "TC_EXE=%%P"
    for %%Q in ("!TC_EXE!") do set "COMMANDER_PATH=%%~dpQ"
    goto tc_path_ready
)

:tc_path_ready
if defined COMMANDER_PATH echo [TC] Executable directory: %COMMANDER_PATH%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
exit /b %ERRORLEVEL%
