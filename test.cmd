@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "REPO_DIR=%~dp0"
if "!REPO_DIR:~-1!"=="\" set "REPO_DIR=!REPO_DIR:~0,-1!"
cd /d "!REPO_DIR!"

set "DIAG_SCRIPT=!REPO_DIR!\test_lifecycle_diag.ps1"
set "DIAG_RUNTIME=!REPO_DIR!\.test_lifecycle_diag.runtime.ps1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$cm=Get-Content -LiteralPath '!REPO_DIR!\CMakeLists.txt' -Raw; $m=[regex]::Match($cm,'project\(FolderHeatMap VERSION ([0-9]+\.[0-9]+)'); if(-not $m.Success){Write-Host 'ERROR: Cannot resolve FolderHeatMap project version for lifecycle diagnostics.' -ForegroundColor Red; exit 2}; $v=$m.Groups[1].Value; $c=Get-Content -LiteralPath '!DIAG_SCRIPT!' -Raw; $c=[regex]::Replace($c,'\$TestVersion\s*=\s*''[^'']+''',('$TestVersion = '''+$v+''''),1); [IO.File]::WriteAllText('!DIAG_RUNTIME!',$c,[Text.UTF8Encoding]::new($false)); Write-Host ('Lifecycle diagnostic runtime version: '+$v) -ForegroundColor Cyan"
if errorlevel 1 exit /b !ERRORLEVEL!

for %%F in (test.ps1 test_stress.ps1 .test_lifecycle_diag.runtime.ps1) do (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$tokens=$null; $errors=$null; [void][System.Management.Automation.Language.Parser]::ParseFile('!REPO_DIR!\%%F',[ref]$tokens,[ref]$errors); if($errors.Count -gt 0){ Write-Host 'ERROR: %%F syntax validation failed.' -ForegroundColor Red; $errors | ForEach-Object { Write-Host ('  ' + $_.Message + ' at line ' + $_.Extent.StartLineNumber + ', column ' + $_.Extent.StartColumnNumber) -ForegroundColor Red }; exit 2 } else { Write-Host 'PASS: %%F syntax validation passed.' -ForegroundColor Green }"
    if errorlevel 1 (
        del /q "!DIAG_RUNTIME!" >nul 2>nul
        exit /b !ERRORLEVEL!
    )
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!REPO_DIR!\test.ps1"
set "BASE_RC=!ERRORLEVEL!"

if not "!BASE_RC!"=="0" (
    powershell.exe -NoProfile -Command "Write-Host 'WARNING: Baseline regression failed. Stress stage is skipped, but lifecycle diagnostics will still run.' -ForegroundColor Yellow"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!DIAG_RUNTIME!"
    set "DIAG_RC=!ERRORLEVEL!"
    del /q "!DIAG_RUNTIME!" >nul 2>nul
    exit /b !BASE_RC!
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!REPO_DIR!\test_stress.ps1"
set "STRESS_RC=!ERRORLEVEL!"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!DIAG_RUNTIME!"
set "DIAG_RC=!ERRORLEVEL!"
del /q "!DIAG_RUNTIME!" >nul 2>nul

if not "!STRESS_RC!"=="0" exit /b !STRESS_RC!
exit /b !DIAG_RC!
