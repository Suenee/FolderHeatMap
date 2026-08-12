@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo ============================================================
echo  FolderHeatMap - local upgrade/build
echo ============================================================
echo.

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake was not found in PATH.
    echo Install Visual Studio 2022 with "Desktop development with C++"
    echo and the CMake tools, then run this script again.
    pause
    exit /b 1
)

if exist build (
    echo [1/4] Removing previous build...
    rmdir /s /q build
) else (
    echo [1/4] No previous build found.
)

echo [2/4] Configuring x64 Release build...
cmake -S . -B build -A x64
if errorlevel 1 goto :error

echo [3/4] Building FolderHeatMap.wdx64...
cmake --build build --config Release
if errorlevel 1 goto :error

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
echo See TESTING.md for Total Commander installation instructions.
pause
exit /b 0

:error
echo.
echo BUILD FAILED. See the errors above.
pause
exit /b 1
