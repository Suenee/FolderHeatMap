@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "REPO_DIR=%~dp0"
if "!REPO_DIR:~-1!"=="\" set "REPO_DIR=!REPO_DIR:~0,-1!"
cd /d "!REPO_DIR!"

for %%F in (test.ps1 test_stress.ps1 test_lifecycle_diag.ps1) do (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$tokens=$null; $errors=$null; [void][System.Management.Automation.Language.Parser]::ParseFile('!REPO_DIR!\%%F',[ref]$tokens,[ref]$errors); if($errors.Count -gt 0){ Write-Host 'ERROR: %%F syntax validation failed.' -ForegroundColor Red; $errors | ForEach-Object { Write-Host ('  ' + $_.Message + ' at line ' + $_.Extent.StartLineNumber + ', column ' + $_.Extent.StartColumnNumber) -ForegroundColor Red }; exit 2 } else { Write-Host 'PASS: %%F syntax validation passed.' -ForegroundColor Green }"
    if errorlevel 1 exit /b !ERRORLEVEL!
)

set "HEAT_TEST=!REPO_DIR!\build\Release\FolderHeatMapHeatReferenceTest.exe"
if not exist "!HEAT_TEST!" (
    if not exist "!REPO_DIR!\build\CMakeCache.txt" (
        powershell.exe -NoProfile -Command "Write-Host 'ERROR: Heat reference test is not built and the CMake build tree is missing. Run upgrade.cmd first.' -ForegroundColor Red"
        exit /b 3
    )
    powershell.exe -NoProfile -Command "Write-Host 'Building FolderHeatMapHeatReferenceTest...' -ForegroundColor Cyan"
    cmake --build "!REPO_DIR!\build" --config Release --target FolderHeatMapHeatReferenceTest
    if errorlevel 1 exit /b !ERRORLEVEL!
)

if not exist "!HEAT_TEST!" (
    powershell.exe -NoProfile -Command "Write-Host 'ERROR: FolderHeatMapHeatReferenceTest.exe is missing after build.' -ForegroundColor Red"
    exit /b 3
)

powershell.exe -NoProfile -Command "Write-Host 'Running Dual-Timescale Activity golden reference tests...' -ForegroundColor Cyan"
"!HEAT_TEST!"
set "HEAT_RC=!ERRORLEVEL!"
if not "!HEAT_RC!"=="0" (
    powershell.exe -NoProfile -Command "Write-Host 'ERROR: Heat model golden reference regression failed. Runtime/stress tests are skipped.' -ForegroundColor Red"
    exit /b !HEAT_RC!
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!REPO_DIR!\test.ps1"
set "BASE_RC=!ERRORLEVEL!"

if not "!BASE_RC!"=="0" (
    powershell.exe -NoProfile -Command "Write-Host 'WARNING: Baseline regression failed. Stress stage is skipped, but lifecycle diagnostics will still run.' -ForegroundColor Yellow"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!REPO_DIR!\test_lifecycle_diag.ps1"
    exit /b !BASE_RC!
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!REPO_DIR!\test_stress.ps1"
set "STRESS_RC=!ERRORLEVEL!"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!REPO_DIR!\test_lifecycle_diag.ps1"
set "DIAG_RC=!ERRORLEVEL!"

if not "!STRESS_RC!"=="0" exit /b !STRESS_RC!
exit /b !DIAG_RC!
