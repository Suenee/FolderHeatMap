@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "INSTALL_REV=1.07"
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

set "CUSTOM_COLUMNS_REPAIR=%~dp0repair_custom_columns.ps1"
if not exist "%CUSTOM_COLUMNS_REPAIR%" if exist "%~dp0..\repair_custom_columns.ps1" set "CUSTOM_COLUMNS_REPAIR=%~dp0..\repair_custom_columns.ps1"
if not exist "%CUSTOM_COLUMNS_REPAIR%" (
    echo ERROR: repair_custom_columns.ps1 was not found; duplicate FolderHeatMap views cannot be repaired safely.
    exit /b 1
)

echo [PRECHECK] Validating PowerShell installer scripts...
powershell.exe -NoProfile -Command "$files=@('%~dp0install.ps1','%CUSTOM_COLUMNS_REPAIR%'); $failed=$false; foreach($file in $files){$tokens=$null;$errors=$null;[void][System.Management.Automation.Language.Parser]::ParseFile($file,[ref]$tokens,[ref]$errors); if($errors.Count -gt 0){$failed=$true; foreach($e in $errors){Write-Host ('ERROR: PowerShell parser error in ' + $file + ' line ' + $e.Extent.StartLineNumber + ', column ' + $e.Extent.StartColumnNumber + ': ' + $e.Message)}}}; if($failed){exit 2}else{Write-Host '[PRECHECK] PowerShell syntax OK.';exit 0}"
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
set "INSTALL_RC=%ERRORLEVEL%"
if not "%INSTALL_RC%"=="0" exit /b %INSTALL_RC%

echo [TC] Verifying that exactly one FolderHeatMap custom-column view exists...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CUSTOM_COLUMNS_REPAIR%"
set "REPAIR_RC=%ERRORLEVEL%"
if not "%REPAIR_RC%"=="0" (
    echo ERROR: FolderHeatMap custom-column de-duplication failed with exit code %REPAIR_RC%.
    exit /b %REPAIR_RC%
)

exit /b 0
