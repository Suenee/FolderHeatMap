@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "UPGRADE_REV=1.03-counter-only-clean"
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

if "%~1"=="--after-pull" goto after_pull

where git.exe >nul 2>nul
if errorlevel 1 goto git_missing

echo [0/7] Updating repository from origin/devel...
git rev-parse --is-inside-work-tree >nul 2>nul
if errorlevel 1 goto git_tree_error

git fetch origin
if errorlevel 1 goto git_fetch_error

for /f "delims=" %%I in ('git branch --show-current') do set "CURRENT_BRANCH=%%I"
if /I not "!CURRENT_BRANCH!"=="devel" (
    git switch devel
    if errorlevel 1 goto git_switch_error
)

set "LOCAL_STASHED=0"
git diff --quiet --ignore-submodules -- && git diff --cached --quiet --ignore-submodules --
if errorlevel 1 (
    echo [GIT] Local tracked changes detected - stashing them before upgrade...
    git stash push -m "FolderHeatMap automatic pre-upgrade stash"
    if errorlevel 1 goto git_stash_error
    set "LOCAL_STASHED=1"
)

git pull --ff-only origin devel
if errorlevel 1 goto git_pull_error
if "!LOCAL_STASHED!"=="1" echo [GIT] Previous local changes remain safely stored in git stash.

call "%~f0" --after-pull
exit /b %ERRORLEVEL%

:after_pull
if defined TC_PATH goto tc_path_done
for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Ghisler\Total Commander" /v InstallDir 2^>nul ^| find /i "InstallDir"') do set "TC_PATH=%%B"
if not defined TC_PATH for /f "tokens=2,*" %%A in ('reg query "HKLM\Software\Ghisler\Total Commander" /v InstallDir 2^>nul ^| find /i "InstallDir"') do set "TC_PATH=%%B"
if not defined TC_PATH for /f "tokens=2,*" %%A in ('reg query "HKLM\Software\Wow6432Node\Ghisler\Total Commander" /v InstallDir 2^>nul ^| find /i "InstallDir"') do set "TC_PATH=%%B"
:tc_path_done

if not defined TC_INI for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Ghisler\Total Commander" /v IniFileName 2^>nul ^| find /i "IniFileName"') do set "TC_INI=%%B"
if not defined TC_INI for /f "tokens=2,*" %%A in ('reg query "HKLM\Software\Ghisler\Total Commander" /v IniFileName 2^>nul ^| find /i "IniFileName"') do set "TC_INI=%%B"

if defined TC_PATH (
    if exist "!TC_PATH!\TOTALCMD64.EXE" set "TC_EXE=!TC_PATH!\TOTALCMD64.EXE"
    if not defined TC_EXE if exist "!TC_PATH!\TOTALCMD.EXE" set "TC_EXE=!TC_PATH!\TOTALCMD.EXE"
)

if defined TC_INI if exist "!TC_INI!" (
    for /f "usebackq tokens=1,* delims==" %%A in (`findstr /I /C:"FolderHeatMap.wdx64" "!TC_INI!" 2^>nul`) do if not defined TC_PLUGIN set "TC_PLUGIN=%%B"
    if defined TC_PLUGIN set "TC_PLUGIN=!TC_PLUGIN:"=!"
)

echo ============================================================
echo  FolderHeatMap - one-click upgrade/build/deploy
echo  Upgrade revision: %UPGRADE_REV%
echo ============================================================
if defined TC_PATH echo [TC] Total Commander: !TC_PATH!
if defined TC_INI echo [TC] Configuration:   !TC_INI!
if defined TC_PLUGIN echo [TC] Registered plugin: !TC_PLUGIN!
echo.

call :is_tc_running
if "!TC_RUNNING!"=="1" set "TC_WAS_RUNNING=1"

for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE set "CMAKE=%%I"
if not defined CMAKE if exist "%VSWHERE%" for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do if not defined CMAKE set "CMAKE=%%I"
if defined CMAKE goto dependencies

echo C++ build environment is not installed yet.
choice /C YN /N /M "Download and install the required Build Tools now? [Y/N] "
if errorlevel 2 exit /b 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -UseBasicParsing '%VSBT_URL%' -OutFile '%VSBT%'"
if errorlevel 1 goto download_error
start /wait "" "%VSBT%" --passive --wait --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.26100
set "INSTALL_RC=%ERRORLEVEL%"
if "%INSTALL_RC%"=="3010" goto restart_required
if not "%INSTALL_RC%"=="0" goto install_error
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE set "CMAKE=%%I"
if not defined CMAKE if exist "%VSWHERE%" for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do if not defined CMAKE set "CMAKE=%%I"
if not defined CMAKE goto cmake_not_found

:dependencies
if exist "vendor\sqlite\sqlite3.c" if exist "vendor\sqlite\sqlite3.h" goto stop_runtime
echo [SETUP] Downloading SQLite 3.53.4...
if exist "%SQLITE_ZIP%" del /q "%SQLITE_ZIP%" >nul 2>nul
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -UseBasicParsing '%SQLITE_URL%' -OutFile '%SQLITE_ZIP%'"
if errorlevel 1 goto sqlite_error
if exist "vendor\sqlite" rmdir /s /q "vendor\sqlite"
mkdir "vendor\sqlite" >nul 2>nul
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$tmp=Join-Path $env:TEMP 'fhm-sqlite'; Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue; Expand-Archive -LiteralPath '%SQLITE_ZIP%' -DestinationPath $tmp -Force; $src=Get-ChildItem $tmp -Directory | Select-Object -First 1; Copy-Item (Join-Path $src.FullName 'sqlite3.c') 'vendor\sqlite\sqlite3.c'; Copy-Item (Join-Path $src.FullName 'sqlite3.h') 'vendor\sqlite\sqlite3.h'; Remove-Item $tmp -Recurse -Force"
if errorlevel 1 goto sqlite_error

:stop_runtime
echo [1/7] Stopping Total Commander and FolderHeatMap engine...
taskkill /IM FolderHeatMapConfig.exe >nul 2>nul
taskkill /IM FolderHeatMapReset.exe >nul 2>nul
call :is_tc_running
if "!TC_RUNNING!"=="1" call :stop_tc
if errorlevel 1 goto tc_stop_error
call :wait_engine
if errorlevel 1 (
    echo WARNING: FolderHeatMapEngine did not finish shutdown within 10 seconds.
    echo WARNING: Forcing it to stop so the upgrade can continue.
    taskkill /F /IM FolderHeatMapEngine.exe >nul 2>nul
    timeout /t 1 /nobreak >nul
)

:cleanup_tc
echo [2/7] Removing FolderHeatMap color/icon rules from Total Commander...
if exist "cleanup_tc_integration.ps1" (
    if defined TC_INI (
        powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\cleanup_tc_integration.ps1" -WincmdIni "!TC_INI!"
    ) else (
        powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\cleanup_tc_integration.ps1"
    )
    if errorlevel 1 goto cleanup_error
) else (
    echo ERROR: cleanup_tc_integration.ps1 is missing.
    goto cleanup_error
)

:build
echo [3/7] Preparing build...
if exist build rmdir /s /q build

echo [4/7] Configuring x64 Release build...
"%CMAKE%" -S . -B build -A x64
if errorlevel 1 goto build_error

echo [5/7] Building Visits-only WDX/engine, configurator and reset utility...
"%CMAKE%" --build build --config Release --target FolderHeatMap FolderHeatMapEngine FolderHeatMapConfig FolderHeatMapReset
if errorlevel 1 goto build_error

for %%F in (FolderHeatMap.wdx64 FolderHeatMapEngine.exe FolderHeatMapConfig.exe FolderHeatMapReset.exe) do (
    if not exist "build\Release\%%F" goto missing_artifact
)

echo [6/7] Preparing dist package...
if not exist dist mkdir dist
copy /y "build\Release\FolderHeatMap.wdx64" "dist\FolderHeatMap.wdx64" >nul || goto dist_error
copy /y "build\Release\FolderHeatMapEngine.exe" "dist\FolderHeatMapEngine.exe" >nul || goto dist_error
copy /y "build\Release\FolderHeatMapConfig.exe" "dist\FolderHeatMapConfig.exe" >nul || goto dist_error
copy /y "build\Release\FolderHeatMapReset.exe" "dist\FolderHeatMapReset.exe" >nul || goto dist_error
copy /y "cleanup_tc_integration.ps1" "dist\cleanup_tc_integration.ps1" >nul
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
    echo [TC] Updated Visits-only WDX and engine in: !TC_PLUGIN_DIR!
) else (
    echo [TC] Registered plugin already points to dist; engine is beside the WDX.
)

goto success

:success
echo.
echo SUCCESS.
echo WDX:       %CD%\dist\FolderHeatMap.wdx64
echo Engine:    %CD%\dist\FolderHeatMapEngine.exe
if "!TC_WAS_RUNNING!"=="1" if defined TC_EXE start "" "!TC_EXE!"
echo.
echo FolderHeatMap 1.03 diagnostic baseline: only Visits exists. Heat fields, TC heat colors/icons, math and prediction are disabled.
pause
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
echo [TC] Normal close timed out - forcing Total Commander to stop...
taskkill /F /IM TOTALCMD64.EXE >nul 2>nul
taskkill /F /IM TOTALCMD.EXE >nul 2>nul
call :is_tc_running
if "!TC_RUNNING!"=="1" exit /b 1
exit /b 0

:wait_engine
for /l %%N in (1,1,10) do (
    tasklist /FI "IMAGENAME eq FolderHeatMapEngine.exe" 2>nul | find /I "FolderHeatMapEngine.exe" >nul || exit /b 0
    timeout /t 1 /nobreak >nul
)
exit /b 1

:git_missing
echo ERROR: Git was not found in PATH.
goto fail
:git_tree_error
echo ERROR: This folder is not a Git working tree.
goto fail
:git_fetch_error
echo ERROR: git fetch origin failed.
goto fail
:git_switch_error
echo ERROR: Could not switch to devel.
goto fail
:git_stash_error
echo ERROR: Local changes could not be stashed safely.
goto fail
:git_pull_error
echo ERROR: Could not fast-forward devel from origin/devel.
goto fail
:download_error
echo ERROR: Microsoft Build Tools bootstrapper could not be downloaded.
goto fail
:sqlite_error
echo ERROR: SQLite source dependency could not be prepared.
goto fail
:cmake_not_found
echo ERROR: CMake could not be located after Build Tools installation.
goto fail
:install_error
echo ERROR: Microsoft Build Tools installation failed or was cancelled. Code: %INSTALL_RC%
goto fail
:restart_required
echo Build Tools were installed successfully, but Windows requires a restart.
goto fail
:cleanup_error
echo ERROR: Could not remove FolderHeatMap-managed TC color/icon rules safely.
goto fail
:build_error
echo ERROR: Build failed. See the errors above.
goto fail
:missing_artifact
echo ERROR: One or more required 1.03 artifacts are missing.
goto fail
:tc_stop_error
echo ERROR: Total Commander could not be stopped for deployment.
goto fail
:dist_error
echo ERROR: Could not prepare the dist package.
goto fail
:deploy_error
echo ERROR: Could not deploy FolderHeatMap WDX/engine beside the registered plugin.
goto fail

:fail
pause
exit /b 1
