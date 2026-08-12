@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSBT=%TEMP%\vs_BuildTools.exe"
set "VSBT_URL=https://aka.ms/vs/17/release/vs_BuildTools.exe"
set "SQLITE_VERSION=3530400"
set "SQLITE_ZIP=%TEMP%\sqlite-amalgamation-%SQLITE_VERSION%.zip"
set "SQLITE_URL=https://www.sqlite.org/2026/sqlite-amalgamation-%SQLITE_VERSION%.zip"
set "CMAKE="

echo ============================================================
echo  FolderHeatMap - one-click upgrade/build
echo ============================================================
echo.

rem Find CMake.
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE set "CMAKE=%%I"
if not defined CMAKE if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do if not defined CMAKE set "CMAKE=%%I"
)
if defined CMAKE goto dependencies

echo C++ build environment is not installed yet.
echo FolderHeatMap can install the minimal Microsoft Build Tools
echo required to compile the x64 Total Commander plugin.
echo.
choice /C YN /N /M "Download and install the required Build Tools now? [Y/N] "
if errorlevel 2 exit /b 1

echo.
echo [SETUP] Downloading official Microsoft Visual Studio Build Tools bootstrapper...
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -UseBasicParsing '%VSBT_URL%' -OutFile '%VSBT%'"
if errorlevel 1 goto download_error
if not exist "%VSBT%" goto download_error

echo [SETUP] Starting Microsoft Build Tools installer...
echo [SETUP] Only the minimal x64 C++ toolchain, CMake and Windows SDK are requested.
echo.
start /wait "" "%VSBT%" --passive --wait --norestart --nocache ^
  --add Microsoft.VisualStudio.Workload.VCTools ^
  --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
  --add Microsoft.VisualStudio.Component.VC.CMake.Project ^
  --add Microsoft.VisualStudio.Component.Windows11SDK.26100
set "INSTALL_RC=%ERRORLEVEL%"
if "%INSTALL_RC%"=="3010" goto restart_required
if not "%INSTALL_RC%"=="0" goto install_error

del /q "%VSBT%" >nul 2>nul
set "CMAKE="
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE set "CMAKE=%%I"
if not defined CMAKE if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do if not defined CMAKE set "CMAKE=%%I"
)
if not defined CMAKE goto cmake_not_found

:dependencies
echo [SETUP] Checking embedded SQLite dependency...
if exist "vendor\sqlite\sqlite3.c" if exist "vendor\sqlite\sqlite3.h" goto build

echo [SETUP] Downloading SQLite 3.53.4 amalgamation from sqlite.org...
if exist "%SQLITE_ZIP%" del /q "%SQLITE_ZIP%" >nul 2>nul
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -UseBasicParsing '%SQLITE_URL%' -OutFile '%SQLITE_ZIP%'"
if errorlevel 1 goto sqlite_error
if not exist "%SQLITE_ZIP%" goto sqlite_error

if exist "vendor\sqlite" rmdir /s /q "vendor\sqlite"
mkdir "vendor\sqlite" >nul 2>nul
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$tmp=Join-Path $env:TEMP 'fhm-sqlite'; Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue; Expand-Archive -LiteralPath '%SQLITE_ZIP%' -DestinationPath $tmp -Force; $src=Get-ChildItem $tmp -Directory | Select-Object -First 1; Copy-Item (Join-Path $src.FullName 'sqlite3.c') 'vendor\sqlite\sqlite3.c'; Copy-Item (Join-Path $src.FullName 'sqlite3.h') 'vendor\sqlite\sqlite3.h'; Remove-Item $tmp -Recurse -Force"
if errorlevel 1 goto sqlite_error
if not exist "vendor\sqlite\sqlite3.c" goto sqlite_error
if not exist "vendor\sqlite\sqlite3.h" goto sqlite_error
del /q "%SQLITE_ZIP%" >nul 2>nul

echo [SETUP] SQLite dependency ready.

:build
echo.
echo Using CMake:
echo   %CMAKE%
echo.
if exist build (
    echo [1/4] Removing previous build...
    rmdir /s /q build
) else (
    echo [1/4] No previous build found.
)
echo [2/4] Configuring x64 Release build...
"%CMAKE%" -S . -B build -A x64
if errorlevel 1 goto build_error
echo [3/4] Building FolderHeatMap.wdx64...
"%CMAKE%" --build build --config Release
if errorlevel 1 goto build_error
echo [4/4] Preparing dist folder...
if not exist dist mkdir dist
copy /y "build\Release\FolderHeatMap.wdx64" "dist\FolderHeatMap.wdx64" >nul
copy /y "README.md" "dist\README.md" >nul
copy /y "TESTING.md" "dist\TESTING.md" >nul
echo.
echo SUCCESS.
echo Test plugin is ready here:
echo   %CD%\dist\FolderHeatMap.wdx64
echo.
echo SQLite persistence is enabled in this build.
pause
exit /b 0

:download_error
echo.
echo ERROR: Microsoft Build Tools bootstrapper could not be downloaded.
pause
exit /b 1

:sqlite_error
echo.
echo ERROR: SQLite source dependency could not be prepared.
echo Check the Internet connection and run upgrade.cmd again.
pause
exit /b 1

:restart_required
echo.
echo Build Tools were installed successfully, but Windows requires a restart.
echo Restart Windows and run upgrade.cmd again.
pause
exit /b 0

:cmake_not_found
echo.
echo ERROR: Build Tools installation finished, but CMake could not be located.
echo Restart Windows and run upgrade.cmd again.
pause
exit /b 1

:install_error
echo.
echo ERROR: Microsoft Build Tools installation failed or was cancelled.
echo Installer return code: %INSTALL_RC%
pause
exit /b 1

:build_error
echo.
echo BUILD FAILED. See the errors above.
pause
exit /b 1
