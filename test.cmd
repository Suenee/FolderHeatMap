@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "REPO_DIR=%~dp0"
if "!REPO_DIR:~-1!"=="\" set "REPO_DIR=!REPO_DIR:~0,-1!"
cd /d "!REPO_DIR!"

for %%F in (test.ps1 test_stress.ps1) do (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$tokens=$null; $errors=$null; [void][System.Management.Automation.Language.Parser]::ParseFile('!REPO_DIR!\%%F',[ref]$tokens,[ref]$errors); if($errors.Count -gt 0){ Write-Host 'ERROR: %%F syntax validation failed.' -ForegroundColor Red; $errors | ForEach-Object { Write-Host ('  ' + $_.Message + ' at line ' + $_.Extent.StartLineNumber + ', column ' + $_.Extent.StartColumnNumber) -ForegroundColor Red }; exit 2 } else { Write-Host 'PASS: %%F syntax validation passed.' -ForegroundColor Green }"
    if errorlevel 1 exit /b !ERRORLEVEL!
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!REPO_DIR!\test.ps1"
set "BASE_RC=!ERRORLEVEL!"
if not "!BASE_RC!"=="0" exit /b !BASE_RC!

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!REPO_DIR!\test_stress.ps1"
set "STRESS_RC=!ERRORLEVEL!"
exit /b !STRESS_RC!
