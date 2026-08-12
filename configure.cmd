@echo off
setlocal
cd /d "%~dp0"

if exist "FolderHeatMapConfig.exe" (
    start "" "FolderHeatMapConfig.exe"
    exit /b 0
)
if exist "dist\FolderHeatMapConfig.exe" (
    start "" "dist\FolderHeatMapConfig.exe"
    exit /b 0
)
if exist "build\Release\FolderHeatMapConfig.exe" (
    start "" "build\Release\FolderHeatMapConfig.exe"
    exit /b 0
)

echo FolderHeatMapConfig.exe was not found.
echo Run upgrade.cmd first.
pause
exit /b 1
