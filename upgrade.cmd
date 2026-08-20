@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "UPGRADE_REV=1.14-bootstrap-transport"
set "ORIGINAL_REPO=%~dp0"
if "%ORIGINAL_REPO:~-1%"=="\" set "ORIGINAL_REPO=%ORIGINAL_REPO:~0,-1%"
set "HAD_WARNING=0"
set "FAIL_PHASE=UNKNOWN"

rem ---------------------------------------------------------------------------
rem INTERNAL ENTRY
rem Current bootstrap communication is environment-only. No repository path or
rem stage token is passed on argv between CMD and PowerShell.
rem ---------------------------------------------------------------------------
if /I "%FHM_UPGRADE_INTERNAL%"=="1" if /I "%FHM_UPGRADE_STAGE%"=="fresh" goto captured_fresh

rem ---------------------------------------------------------------------------
rem NORMAL ENTRY / SELF-UPDATE
rem 1) silently fetch origin/devel
rem 2) extract the newest logger and newest upgrade.cmd to TEMP
rem 3) pass bootstrap data only through environment variables
rem 4) logger captures/colors the entire real run into upgrade.log
rem
rem Keeping the real run in a TEMP copy means git pull may safely replace the
rem repository's upgrade.cmd while the upgrade is executing.
rem ---------------------------------------------------------------------------
cd /d "%ORIGINAL_REPO%" || (
    set "BOOTSTRAP_ERROR=Could not enter repository directory."
    goto bootstrap_fail
)
where git.exe >nul 2>nul || (
    set "BOOTSTRAP_ERROR=Git was not found in PATH."
    goto bootstrap_fail
)
git rev-parse --is-inside-work-tree >nul 2>nul || (
    set "BOOTSTRAP_ERROR=This folder is not a Git working tree."
    goto bootstrap_fail
)
git fetch origin >nul 2>nul || (
    set "BOOTSTRAP_ERROR=git fetch origin failed before upgrade logging could start."
    goto bootstrap_fail
)

set "LOGGER_TEMP=%TEMP%\FolderHeatMap-upgrade-logger-%RANDOM%-%RANDOM%.ps1"
set "FRESH_UPGRADER=%TEMP%\FolderHeatMap-upgrade-%RANDOM%-%RANDOM%.cmd"

git show origin/devel:upgrade_logger.ps1 > "!LOGGER_TEMP!" 2>nul || (
    set "BOOTSTRAP_ERROR=Could not extract upgrade_logger.ps1 from origin/devel."
    goto bootstrap_cleanup_fail
)
git show origin/devel:upgrade.cmd > "!FRESH_UPGRADER!" 2>nul || (
    set "BOOTSTRAP_ERROR=Could not extract upgrade.cmd from origin/devel."
    goto bootstrap_cleanup_fail
)
if not exist "!LOGGER_TEMP!" (
    set "BOOTSTRAP_ERROR=Temporary upgrade logger was not created."
    goto bootstrap_cleanup_fail
)
if not exist "!FRESH_UPGRADER!" (
    set "BOOTSTRAP_ERROR=Temporary fresh upgrade.cmd was not created."
    goto bootstrap_cleanup_fail
)
for %%I in ("!LOGGER_TEMP!") do if %%~zI LSS 500 (
    set "BOOTSTRAP_ERROR=Temporary upgrade logger is unexpectedly small."
    goto bootstrap_cleanup_fail
)
for %%I in ("!FRESH_UPGRADER!") do if %%~zI LSS 1000 (
    set "BOOTSTRAP_ERROR=Temporary fresh upgrade.cmd is unexpectedly small."
    goto bootstrap_cleanup_fail
)

set "FHM_UPGRADE_INTERNAL=1"
set "FHM_UPGRADE_STAGE=fresh"
set "FHM_UPGRADE_REPO=%ORIGINAL_REPO%"
set "FHM_UPGRADE_SCRIPT=!FRESH_UPGRADER!"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!LOGGER_TEMP!"
set "BOOTSTRAP_RC=!ERRORLEVEL!"

del /q "!LOGGER_TEMP!" >nul 2>nul
del /q "!FRESH_UPGRADER!" >nul 2>nul
exit /b !BOOTSTRAP_RC!

:bootstrap_cleanup_fail
del /q "!LOGGER_TEMP!" >nul 2>nul
del /q "!FRESH_UPGRADER!" >nul 2>nul
goto bootstrap_fail

:bootstrap_fail
> "%ORIGINAL_REPO%\upgrade.log" echo FolderHeatMap upgrade bootstrap failed.
>> "%ORIGINAL_REPO%\upgrade.log" echo ERROR: !BOOTSTRAP_ERROR!
>> "%ORIGINAL_REPO%\upgrade.log" echo STATUS: FAILED - phase=SELF-UPDATE/BOOTSTRAP
powershell.exe -NoProfile -Command "Write-Host 'ERROR: !BOOTSTRAP_ERROR!' -ForegroundColor Red; Write-Host 'STATUS: FAILED - phase=SELF-UPDATE/BOOTSTRAP' -ForegroundColor Red"
pause
exit /b 1

rem ---------------------------------------------------------------------------
rem CAPTURED FRESH RUN
rem Reached only from upgrade_logger.ps1. Repository and stage came through the
rem environment; no bootstrap argv is parsed here.
rem ---------------------------------------------------------------------------
:captured_fresh
set "REPO_DIR=%FHM_UPGRADE_REPO%"
if not defined REPO_DIR (
    set "FAIL_PHASE=SELF-UPDATE"
    echo ERROR: FHM_UPGRADE_REPO is missing in captured run.
    goto fail
)
if not exist "%REPO_DIR%\.git" (
    set "FAIL_PHASE=SELF-UPDATE"
    echo ERROR: Captured repository path is not a Git working tree: %REPO_DIR%
    goto fail
)
cd /d "%REPO_DIR%" || (
    set "FAIL_PHASE=SELF-UPDATE"
    echo ERROR: Could not enter captured repository path: %REPO_DIR%
    goto fail
)
call :write_header

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSBT=%TEMP%\vs_BuildTools.exe"
set "VSBT_URL=https://aka.ms/vs/17/release/vs_BuildTools.exe"
set "SQLITE_VERSION=3530400"
set "SQLITE_ZIP=%TEMP%\sqlite-amalgamation-%SQLITE_VERSION%.zip"
set "SQLITE_URL=https://www.sqlite.org/2026/sqlite-amalgamation-%SQLITE_VERSION%.zip"
set "CMAKE="
set "TC_PATH=%COMMANDER_PATH%"
set "TC_INI=%COMMANDER_INI%"
set "TC_PLUGIN="
set "TC_EXE="
set "TC_WAS_RUNNING=0"
set "SETTINGS_INI="

where git.exe >nul 2>nul || (
    set "FAIL_PHASE=SELF-UPDATE"
    echo ERROR: Git was not found in PATH.
    goto fail
)

echo [0/7] Updating repository from origin/devel...
git fetch origin || (
    set "FAIL_PHASE=SELF-UPDATE"
    echo ERROR: git fetch origin failed.
    goto fail
)
for /f "delims=" %%I in ('git branch --show-current') do set "CURRENT_BRANCH=%%I"
if /I not "!CURRENT_BRANCH!"=="devel" git switch devel || (
    set "FAIL_PHASE=SELF-UPDATE"
    echo ERROR: Could not switch to devel.
    goto fail
)

set "LOCAL_STASHED=0"
git diff --quiet --ignore-submodules -- && git diff --cached --quiet --ignore-submodules --
if errorlevel 1 (
    echo WARNING: Local tracked changes detected - stashing them safely.
    set "HAD_WARNING=1"
    git stash push -m "FolderHeatMap automatic pre-upgrade stash" || (
        set "FAIL_PHASE=SELF-UPDATE"
        echo ERROR: Local changes could not be stashed safely.
        goto fail
    )
    set "LOCAL_STASHED=1"
)

git pull --ff-only origin devel || (
    set "FAIL_PHASE=SELF-UPDATE"
    echo ERROR: Could not fast-forward devel from origin/devel.
    goto fail
)
if "!LOCAL_STASHED!"=="1" echo WARNING: Previous local changes remain safely stored in git stash.

set "REMOTE_UPGRADE_HASH="
set "LOCAL_UPGRADE_HASH="
for /f "delims=" %%H in ('git rev-parse origin/devel:upgrade.cmd 2^>nul') do set "REMOTE_UPGRADE_HASH=%%H"
for /f "delims=" %%H in ('git hash-object upgrade.cmd 2^>nul') do set "LOCAL_UPGRADE_HASH=%%H"
if not defined REMOTE_UPGRADE_HASH (
    set "FAIL_PHASE=SELF-UPDATE"
    echo ERROR: Could not resolve origin/devel upgrade.cmd hash.
    goto fail
)
if not defined LOCAL_UPGRADE_HASH (
    set "FAIL_PHASE=SELF-UPDATE"
    echo ERROR: Could not calculate local upgrade.cmd hash.
    goto fail
)
if /I not "!REMOTE_UPGRADE_HASH!"=="!LOCAL_UPGRADE_HASH!" (
    set "FAIL_PHASE=SELF-UPDATE"
    echo ERROR: Local upgrade.cmd is not identical to origin/devel after update.
    goto fail
)

echo [BOOTSTRAP] upgrade.cmd verified current: !LOCAL_UPGRADE_HASH!
for /f "delims=" %%H in ('git rev-parse HEAD 2^>nul') do set "BUILD_COMMIT=%%H"
echo [GIT] Build commit: !BUILD_COMMIT!

if not defined TC_PATH for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Ghisler\Total Commander" /v InstallDir 2^>nul ^| find /i "InstallDir"') do set "TC_PATH=%%B"
if not defined TC_PATH for /f "tokens=2,*" %%A in ('reg query "HKLM\Software\Ghisler\Total Commander" /v InstallDir 2^>nul ^| find /i "InstallDir"') do set "TC_PATH=%%B"
if not defined TC_PATH for /f "tokens=2,*" %%A in ('reg query "HKLM\Software\Wow6432Node\Ghisler\Total Commander" /v InstallDir 2^>nul ^| find /i "InstallDir"') do set "TC_PATH=%%B"
if not defined TC_INI for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Ghisler\Total Commander" /v IniFileName 2^>nul ^| find /i "IniFileName"') do set "TC_INI=%%B"
if not defined TC_INI for /f "tokens=2,*" %%A in ('reg query "HKLM\Software\Ghisler\Total Commander" /v IniFileName 2^>nul ^| find /i "IniFileName"') do set "TC_INI=%%B"

if defined TC_PATH (
    if exist "!TC_PATH!\TOTALCMD64.EXE" set "TC_EXE=!TC_PATH!\TOTALCMD64.EXE"
    if not defined TC_EXE if exist "!TC_PATH!\TOTALCMD.EXE" set "TC_EXE=!TC_PATH!\TOTALCMD.EXE"
)
if defined TC_INI for %%I in ("!TC_INI!") do set "SETTINGS_INI=%%~dpIFolderHeatMap.ini"
if defined TC_INI if exist "!TC_INI!" (
    for /f "usebackq tokens=1,* delims==" %%A in (`findstr /I /C:"FolderHeatMap.wdx64" "!TC_INI!" 2^>nul`) do if not defined TC_PLUGIN set "TC_PLUGIN=%%B"
    if defined TC_PLUGIN set "TC_PLUGIN=!TC_PLUGIN:"=!"
)

echo ============================================================
echo FolderHeatMap - one-click upgrade/build/deploy
echo Upgrade revision: %UPGRADE_REV%
echo ============================================================
if defined TC_PATH echo [TC] Total Commander: !TC_PATH!
if defined TC_INI echo [TC] Configuration:   !TC_INI!
if defined TC_PLUGIN echo [TC] Registered plugin: !TC_PLUGIN!
if defined SETTINGS_INI echo [FHM] Settings:       !SETTINGS_INI!
echo [FHM] Engine log:     %CD%\FolderHeatMap.log
echo [FHM] Upgrade log:    %CD%\upgrade.log
echo.

call :is_tc_running
if "!TC_RUNNING!"=="1" set "TC_WAS_RUNNING=1"

for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE set "CMAKE=%%I"
if not defined CMAKE if exist "%VSWHERE%" for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do if not defined CMAKE set "CMAKE=%%I"
if defined CMAKE goto dependencies

echo C++ build environment is not installed yet.
choice /C YN /N /M "Download and install the required Build Tools now? [Y/N] "
if errorlevel 2 (
    set "FAIL_PHASE=DEPENDENCIES"
    echo ERROR: Build Tools installation was declined.
    goto fail
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -UseBasicParsing '%VSBT_URL%' -OutFile '%VSBT%'" || (
    set "FAIL_PHASE=DEPENDENCIES"
    echo ERROR: Microsoft Build Tools bootstrapper could not be downloaded.
    goto fail
)
start /wait "" "%VSBT%" --passive --wait --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.26100
set "INSTALL_RC=%ERRORLEVEL%"
if "%INSTALL_RC%"=="3010" (
    set "FAIL_PHASE=DEPENDENCIES"
    echo WARNING: Build Tools installed successfully, but Windows requires a restart.
    set "HAD_WARNING=1"
    goto fail
)
if not "%INSTALL_RC%"=="0" (
    set "FAIL_PHASE=DEPENDENCIES"
    echo ERROR: Microsoft Build Tools installation failed or was cancelled. Code: %INSTALL_RC%
    goto fail
)
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE set "CMAKE=%%I"
if not defined CMAKE if exist "%VSWHERE%" for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do if not defined CMAKE set "CMAKE=%%I"
if not defined CMAKE (
    set "FAIL_PHASE=DEPENDENCIES"
    echo ERROR: CMake could not be located after Build Tools installation.
    goto fail
)

:dependencies
if exist "vendor\sqlite\sqlite3.c" if exist "vendor\sqlite\sqlite3.h" goto stop_runtime
echo [SETUP] Downloading SQLite 3.53.4...
if exist "%SQLITE_ZIP%" del /q "%SQLITE_ZIP%" >nul 2>nul
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -UseBasicParsing '%SQLITE_URL%' -OutFile '%SQLITE_ZIP%'" || (
    set "FAIL_PHASE=DEPENDENCIES"
    echo ERROR: SQLite source dependency could not be downloaded.
    goto fail
)
if exist "vendor\sqlite" rmdir /s /q "vendor\sqlite"
mkdir "vendor\sqlite" >nul 2>nul
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$tmp=Join-Path $env:TEMP 'fhm-sqlite'; Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue; Expand-Archive -LiteralPath '%SQLITE_ZIP%' -DestinationPath $tmp -Force; $src=Get-ChildItem $tmp -Directory | Select-Object -First 1; Copy-Item (Join-Path $src.FullName 'sqlite3.c') 'vendor\sqlite\sqlite3.c'; Copy-Item (Join-Path $src.FullName 'sqlite3.h') 'vendor\sqlite\sqlite3.h'; Remove-Item $tmp -Recurse -Force" || (
    set "FAIL_PHASE=DEPENDENCIES"
    echo ERROR: SQLite source dependency could not be prepared.
    goto fail
)

:stop_runtime
echo [1/7] Stopping Total Commander and FolderHeatMap engine...
taskkill /IM FolderHeatMapConfig.exe >nul 2>nul
taskkill /IM FolderHeatMapReset.exe >nul 2>nul
call :is_tc_running
if "!TC_RUNNING!"=="1" call :stop_tc
if errorlevel 1 (
    set "FAIL_PHASE=STOP-RUNTIME"
    echo ERROR: Total Commander could not be stopped for deployment.
    goto fail
)
call :wait_engine
if errorlevel 1 (
    echo WARNING: FolderHeatMapEngine did not finish graceful shutdown within 30 seconds.
    echo WARNING: Forcing it to stop so the upgrade can continue.
    set "HAD_WARNING=1"
    taskkill /F /IM FolderHeatMapEngine.exe >nul 2>nul
    timeout /t 1 /nobreak >nul
)

:configure_logging
echo [2/7] Configuring repository-local logging path...
if not defined SETTINGS_INI (
    set "FAIL_PHASE=CONFIGURATION"
    echo ERROR: FolderHeatMap settings path could not be resolved.
    goto fail
)
if not exist "configure_logging_path.ps1" (
    set "FAIL_PHASE=CONFIGURATION"
    echo ERROR: configure_logging_path.ps1 is missing.
    goto fail
)
rem Safe here: both values are file/directory paths without a trailing separator.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\configure_logging_path.ps1" -SettingsIni "!SETTINGS_INI!" -RepositoryRoot "%CD%"
if errorlevel 1 (
    set "FAIL_PHASE=CONFIGURATION"
    echo ERROR: Could not configure FolderHeatMap.log in the repository root.
    goto fail
)

:build
echo [3/7] Preparing build...
if exist build rmdir /s /q build

echo [4/7] Configuring x64 Release build...
"%CMAKE%" -S . -B build -A x64 || (
    set "FAIL_PHASE=CMAKE-CONFIGURE"
    echo ERROR: CMake configuration failed.
    goto fail
)

echo [5/7] Building FolderHeatMap 1.14 FAST/SLOW engine and tools...
"%CMAKE%" --build build --config Release --target FolderHeatMap FolderHeatMapEngine FolderHeatMapConfig FolderHeatMapReset
if errorlevel 1 (
    set "FAIL_PHASE=BUILD"
    echo ERROR: Build failed. See the compiler/CMake output above.
    goto fail
)

for %%F in (FolderHeatMap.wdx64 FolderHeatMapEngine.exe FolderHeatMapConfig.exe FolderHeatMapReset.exe) do if not exist "build\Release\%%F" (
    set "FAIL_PHASE=BUILD"
    echo ERROR: Required build artifact %%F is missing.
    goto fail
)

echo [6/7] Preparing dist package...
if not exist dist mkdir dist
copy /y "build\Release\FolderHeatMap.wdx64" "dist\FolderHeatMap.wdx64" >nul || goto dist_error
copy /y "build\Release\FolderHeatMapEngine.exe" "dist\FolderHeatMapEngine.exe" >nul || goto dist_error
copy /y "build\Release\FolderHeatMapConfig.exe" "dist\FolderHeatMapConfig.exe" >nul || goto dist_error
copy /y "build\Release\FolderHeatMapReset.exe" "dist\FolderHeatMapReset.exe" >nul || goto dist_error
copy /y "configure.cmd" "dist\configure.cmd" >nul
copy /y "README.md" "dist\README.md" >nul
copy /y "TESTING.md" "dist\TESTING.md" >nul

echo [7/7] Deploying to Total Commander...
if not defined TC_PLUGIN goto success
for %%I in ("!TC_PLUGIN!") do set "TC_PLUGIN_DIR=%%~dpI"
for %%I in ("%CD%\dist\FolderHeatMap.wdx64") do set "DIST_PLUGIN=%%~fI"
for %%I in ("!TC_PLUGIN!") do set "TC_PLUGIN_FULL=%%~fI"
if /I not "!DIST_PLUGIN!"=="!TC_PLUGIN_FULL!" (
    copy /y "dist\FolderHeatMap.wdx64" "!TC_PLUGIN!" >nul || goto deploy_error
    copy /y "dist\FolderHeatMapEngine.exe" "!TC_PLUGIN_DIR!FolderHeatMapEngine.exe" >nul || goto deploy_error
    echo [TC] Updated WDX and engine in: !TC_PLUGIN_DIR!
) else (
    echo [TC] Registered plugin already points to dist; engine is beside the WDX.
)

goto success

:success
echo.
echo SUCCESS - FolderHeatMap 1.14 installed.
echo WDX:         %CD%\dist\FolderHeatMap.wdx64
echo Engine:      %CD%\dist\FolderHeatMapEngine.exe
echo Config:      %CD%\dist\FolderHeatMapConfig.exe
echo Engine log:  %CD%\FolderHeatMap.log
echo Upgrade log: %CD%\upgrade.log
echo.
echo 1.14 uses environment-only bootstrap transport and keeps the 1.11 FAST/SLOW lifecycle behavior.
echo Upload upgrade.log to ChatGPT for a complete build/deploy diagnosis.

rem Do not leak internal bootstrap state into Total Commander.
set "FHM_UPGRADE_INTERNAL="
set "FHM_UPGRADE_STAGE="
set "FHM_UPGRADE_REPO="
set "FHM_UPGRADE_SCRIPT="

if "!TC_WAS_RUNNING!"=="1" if defined TC_EXE start "" "!TC_EXE!"
pause
if "!HAD_WARNING!"=="1" (
    echo STATUS: WARNING - phase=COMPLETE
) else (
    echo STATUS: SUCCESS - phase=COMPLETE
)
exit /b 0

:is_tc_running
set "TC_RUNNING=0"
tasklist /FI "IMAGENAME eq TOTALCMD64.EXE" 2>nul | find /I "TOTALCMD64.EXE" >nul && set "TC_RUNNING=1"
tasklist /FI "IMAGENAME eq TOTALCMD.EXE" 2>nul | find /I "TOTALCMD.EXE" >nul && set "TC_RUNNING=1"
exit /b 0

:stop_tc
echo [TC] Closing Total Commander...
taskkill /IM TOTALCMD64.EXE >nul 2>nul
taskkill /IM TOTALCMD.EXE >nul 2>nul
for /l %%N in (1,1,20) do (
    call :is_tc_running
    if "!TC_RUNNING!"=="0" exit /b 0
    timeout /t 1 /nobreak >nul
)
echo WARNING: Normal Total Commander close timed out; forcing process termination.
set "HAD_WARNING=1"
taskkill /F /IM TOTALCMD64.EXE >nul 2>nul
taskkill /F /IM TOTALCMD.EXE >nul 2>nul
call :is_tc_running
if "!TC_RUNNING!"=="1" exit /b 1
exit /b 0

:wait_engine
for /l %%N in (1,1,30) do (
    tasklist /FI "IMAGENAME eq FolderHeatMapEngine.exe" 2>nul | find /I "FolderHeatMapEngine.exe" >nul || exit /b 0
    timeout /t 1 /nobreak >nul
)
exit /b 1

:write_header
for /f "delims=" %%T in ('powershell.exe -NoProfile -Command "Get-Date -Format ''dd.MM.yyyy HH:mm:ss.fff''"') do set "STARTED_AT=%%T"
set "START_BRANCH=unknown"
set "START_COMMIT=unknown"
for /f "delims=" %%B in ('git branch --show-current 2^>nul') do set "START_BRANCH=%%B"
for /f "delims=" %%H in ('git rev-parse HEAD 2^>nul') do set "START_COMMIT=%%H"
echo ============================================================
echo FolderHeatMap upgrade diagnostic log
echo Version:    %UPGRADE_REV%
echo Started:    !STARTED_AT!
echo Repository: %CD%
echo Branch:     !START_BRANCH!
echo Commit:     !START_COMMIT!
echo Transport:  environment-only
necho ============================================================
exit /b 0

:dist_error
set "FAIL_PHASE=DIST"
echo ERROR: Could not prepare the dist package.
goto fail

:deploy_error
set "FAIL_PHASE=DEPLOY"
echo ERROR: Could not deploy FolderHeatMap WDX/engine beside the registered plugin.
goto fail

:fail
echo.
echo UPGRADE FAILED.
pause
echo STATUS: FAILED - phase=!FAIL_PHASE!
exit /b 1
