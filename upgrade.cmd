@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "UPGRADE_REV=reset-tool-v3"
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

if not defined TC_PATH for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Ghisler\Total Commander" /v InstallDir 2^>nul ^| find /i "InstallDir"') do set "TC_PATH=%%B"
if not defined TC_PATH for /f "tokens=2,*" %%A in ('reg query "HKLM\Software\Ghisler\Total Commander" /v InstallDir 2^>nul ^| find /i "InstallDir"') do set "TC_PATH=%%B"
if not defined TC_PATH for /f "tokens=2,*" %%A in ('reg query "HKLM\Software\Wow6432Node\Ghisler\Total Commander" /v InstallDir 2^>nul ^| find /i "InstallDir"') do set "TC_PATH=%%B"
if not defined TC_INI for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Ghisler\Total Commander" /v IniFileName 2^>nul ^| find /i "IniFileName"') do set "TC_INI=%%B"
if not defined TC_INI for /f "tokens=2,*" %%A in ('reg query "HKLM\Software\Ghisler\Total Commander" /v IniFileName 2^>nul ^| find /i "IniFileName"') do set "TC_INI=%%B"

if defined TC_PATH (
    if exist "!TC_PATH!\TOTALCMD64.EXE" set "TC_EXE=!TC_PATH!\TOTALCMD64.EXE"
    if not defined TC_EXE if exist "!TC_PATH!\TOTALCMD.EXE" set "TC_EXE=!TC_PATH!\TOTALCMD.EXE"
)

if not defined TC_INI goto tc_plugin_detected
if not exist "!TC_INI!" goto tc_plugin_detected
for /f "usebackq tokens=1,* delims==" %%A in (`findstr /I /C:"FolderHeatMap.wdx64" "!TC_INI!" 2^>nul`) do if not defined TC_PLUGIN set "TC_PLUGIN=%%B"
if defined TC_PLUGIN set "TC_PLUGIN=!TC_PLUGIN:"=!"
:tc_plugin_detected

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
del /q "%VSBT%" >nul 2>nul
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE set "CMAKE=%%I"
if not defined CMAKE if exist "%VSWHERE%" for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do if not defined CMAKE set "CMAKE=%%I"
if not defined CMAKE goto cmake_not_found

:dependencies
if exist "vendor\sqlite\sqlite3.c" if exist "vendor\sqlite\sqlite3.h" goto build
echo [SETUP] Downloading SQLite 3.53.4...
if exist "%SQLITE_ZIP%" del /q "%SQLITE_ZIP%" >nul 2>nul
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -UseBasicParsing '%SQLITE_URL%' -OutFile '%SQLITE_ZIP%'"
if errorlevel 1 goto sqlite_error
if exist "vendor\sqlite" rmdir /s /q "vendor\sqlite"
mkdir "vendor\sqlite" >nul 2>nul
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$tmp=Join-Path $env:TEMP 'fhm-sqlite'; Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue; Expand-Archive -LiteralPath '%SQLITE_ZIP%' -DestinationPath $tmp -Force; $src=Get-ChildItem $tmp -Directory | Select-Object -First 1; Copy-Item (Join-Path $src.FullName 'sqlite3.c') 'vendor\sqlite\sqlite3.c'; Copy-Item (Join-Path $src.FullName 'sqlite3.h') 'vendor\sqlite\sqlite3.h'; Remove-Item $tmp -Recurse -Force"
if errorlevel 1 goto sqlite_error
del /q "%SQLITE_ZIP%" >nul 2>nul

:build
echo [1/5] Preparing build...
if exist build rmdir /s /q build
if exist "FolderHeatMapConfig.exe" del /f /q "FolderHeatMapConfig.exe" >nul 2>nul
if exist "FolderHeatMapReset.exe" del /f /q "FolderHeatMapReset.exe" >nul 2>nul

echo [2/5] Configuring x64 Release build...
"%CMAKE%" -S . -B build -A x64
if errorlevel 1 goto build_error

echo [3/5] Building plugin, settings GUI and reset utility...
"%CMAKE%" --build build --config Release --target FolderHeatMap FolderHeatMapConfig FolderHeatMapReset
if errorlevel 1 goto build_error

rem Fail before touching Total Commander if any expected artifact is missing.
if not exist "build\Release\FolderHeatMap.wdx64" goto missing_artifact
if not exist "build\Release\FolderHeatMapConfig.exe" goto missing_artifact
if not exist "build\Release\FolderHeatMapReset.exe" goto missing_reset_artifact
echo [BUILD] Reset utility verified: build\Release\FolderHeatMapReset.exe

echo [4/5] Preparing dist folder...
taskkill /IM FolderHeatMapConfig.exe >nul 2>nul
taskkill /IM FolderHeatMapReset.exe >nul 2>nul
for /l %%N in (1,1,20) do (
    tasklist /FI "IMAGENAME eq FolderHeatMapConfig.exe" 2>nul | find /I "FolderHeatMapConfig.exe" >nul || goto config_closed
    timeout /t 1 /nobreak >nul
)
taskkill /F /IM FolderHeatMapConfig.exe >nul 2>nul
:config_closed

call :is_tc_running
if "!TC_RUNNING!"=="1" call :stop_tc
if errorlevel 1 goto tc_stop_error

if not exist dist mkdir dist
copy /y "build\Release\FolderHeatMap.wdx64" "dist\FolderHeatMap.wdx64" >nul
if errorlevel 1 goto dist_error
copy /y "build\Release\FolderHeatMapConfig.exe" "dist\FolderHeatMapConfig.exe" >nul
if errorlevel 1 goto dist_error
copy /y "build\Release\FolderHeatMapReset.exe" "dist\FolderHeatMapReset.exe" >nul
if errorlevel 1 goto dist_error
if not exist "dist\FolderHeatMapReset.exe" goto missing_reset_dist
copy /y "configure.cmd" "dist\configure.cmd" >nul
copy /y "README.md" "dist\README.md" >nul
copy /y "TESTING.md" "dist\TESTING.md" >nul

echo [5/5] Deploying to Total Commander...
if not defined TC_PLUGIN goto success
for %%I in ("%CD%\dist\FolderHeatMap.wdx64") do set "DIST_PLUGIN=%%~fI"
for %%I in ("!TC_PLUGIN!") do set "TC_PLUGIN_FULL=%%~fI"

call :is_tc_running
if "!TC_RUNNING!"=="1" call :stop_tc
if errorlevel 1 goto tc_stop_error

if /I "!DIST_PLUGIN!"=="!TC_PLUGIN_FULL!" (
    echo [TC] Registered plugin already points to dist.
) else (
    copy /y "dist\FolderHeatMap.wdx64" "!TC_PLUGIN!" >nul
    if errorlevel 1 goto deploy_error
    echo [TC] Updated registered plugin: !TC_PLUGIN!
)

goto success

:success
echo.
echo SUCCESS.
echo Plugin:    %CD%\dist\FolderHeatMap.wdx64
echo Settings:  %CD%\dist\FolderHeatMapConfig.exe
echo Reset:     %CD%\dist\FolderHeatMapReset.exe
if "!TC_WAS_RUNNING!"=="1" if defined TC_EXE (
    call :is_tc_running
    if "!TC_RUNNING!"=="0" (
        echo [TC] Restarting exactly one Total Commander instance...
        start "" "!TC_EXE!"
    ) else (
        echo [TC] Total Commander is already running - not starting another instance.
    )
)
echo.
echo Run configure.cmd to change heat behavior and colors.
pause
exit /b 0

:is_tc_running
set "TC_RUNNING=0"
tasklist /FI "IMAGENAME eq TOTALCMD64.EXE" 2>nul | find /I "TOTALCMD64.EXE" >nul && set "TC_RUNNING=1"
tasklist /FI "IMAGENAME eq TOTALCMD.EXE" 2>nul | find /I "TOTALCMD.EXE" >nul && set "TC_RUNNING=1"
exit /b 0

:stop_tc
echo [TC] Closing all Total Commander instances...
taskkill /IM TOTALCMD64.EXE >nul 2>nul
taskkill /IM TOTALCMD.EXE >nul 2>nul
for /l %%N in (1,1,20) do (
    call :is_tc_running
    if "!TC_RUNNING!"=="0" exit /b 0
    timeout /t 1 /nobreak >nul
)
echo [TC] Normal close timed out - forcing Total Commander processes to stop...
taskkill /F /IM TOTALCMD64.EXE >nul 2>nul
taskkill /F /IM TOTALCMD.EXE >nul 2>nul
timeout /t 1 /nobreak >nul
call :is_tc_running
if "!TC_RUNNING!"=="1" exit /b 1
exit /b 0

:missing_reset_artifact
echo ERROR: FolderHeatMapReset.exe was NOT produced by the build.
echo Expected: %CD%\build\Release\FolderHeatMapReset.exe
echo The upgrade stops here and Total Commander has not been touched.
goto restart_after_error
:missing_artifact
echo ERROR: One or more required build artifacts are missing.
goto restart_after_error
:missing_reset_dist
echo ERROR: Reset utility was built but was not copied to dist.
goto restart_after_error
:dist_error
echo ERROR: Could not update the dist folder. A FolderHeatMap file is still locked by another process.
goto restart_after_error
:tc_stop_error
echo ERROR: Total Commander is still running after forced shutdown. Deployment was aborted.
goto restart_after_error
:deploy_error
echo ERROR: FolderHeatMap.wdx64 could not be deployed even after stopping Total Commander.
goto restart_after_error
:download_error
echo ERROR: Microsoft Build Tools bootstrapper could not be downloaded.
goto restart_after_error
:sqlite_error
echo ERROR: SQLite source dependency could not be prepared.
goto restart_after_error
:cmake_not_found
echo ERROR: CMake could not be located after Build Tools installation.
goto restart_after_error
:install_error
echo ERROR: Microsoft Build Tools installation failed or was cancelled. Code: %INSTALL_RC%
goto restart_after_error
:build_error
echo ERROR: Build failed. See the errors above.
goto restart_after_error
:restart_required
echo Build Tools were installed successfully, but Windows requires a restart.
goto restart_after_error

:restart_after_error
if "!TC_WAS_RUNNING!"=="1" if defined TC_EXE (
    call :is_tc_running
    if "!TC_RUNNING!"=="0" start "" "!TC_EXE!"
)
pause
exit /b 1
