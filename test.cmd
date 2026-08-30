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

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!REPO_DIR!\test.ps1"
set "BASE_RC=!ERRORLEVEL!"

if not "!BASE_RC!"=="0" (
    powershell.exe -NoProfile -Command "Write-Host 'WARNING: Baseline regression failed. Stress and Heat stages are skipped, but lifecycle diagnostics will still run.' -ForegroundColor Yellow"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!REPO_DIR!\test_lifecycle_diag.ps1"
    exit /b !BASE_RC!
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!REPO_DIR!\test_stress.ps1"
set "STRESS_RC=!ERRORLEVEL!"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!REPO_DIR!\test_lifecycle_diag.ps1"
set "DIAG_RC=!ERRORLEVEL!"

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $log=Get-ChildItem -LiteralPath 'D:\Temp\FHM\logs' -Filter 'diagnostic-*.log' -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if(-not $log){ Write-Host 'ERROR: Lifecycle diagnostic log was not found for Heat reference output.' -ForegroundColor Red; exit 4 }; $utf8=[Text.UTF8Encoding]::new($false); $lines=@('', '[TEST] Dual-Timescale Activity golden reference', ('Heat test executable: ' + '!HEAT_TEST!')); foreach($line in $lines){ [IO.File]::AppendAllText($log.FullName,$line+[Environment]::NewLine,$utf8); Write-Host $line -ForegroundColor Cyan }; $output=& '!HEAT_TEST!' 2>&1; $rc=$LASTEXITCODE; foreach($line in $output){ $text=[string]$line; [IO.File]::AppendAllText($log.FullName,$text+[Environment]::NewLine,$utf8); Write-Host $text }; if($rc -eq 0){ $status='[PASS] Heat model golden reference regression passed.'; $color='Green' } else { $status='[ERROR] Heat model golden reference regression failed.'; $color='Red' }; [IO.File]::AppendAllText($log.FullName,$status+[Environment]::NewLine,$utf8); Write-Host $status -ForegroundColor $color; Write-Host ('Heat reference output appended to: ' + $log.FullName) -ForegroundColor Gray; exit $rc"
set "HEAT_RC=!ERRORLEVEL!"
if not "!STRESS_RC!"=="0" exit /b !STRESS_RC!
if not "!HEAT_RC!"=="0" exit /b !HEAT_RC!
exit /b !DIAG_RC!
