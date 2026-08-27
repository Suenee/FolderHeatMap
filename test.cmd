@echo off
setlocal EnableExtensions

set "REPO_DIR=%~dp0"
if "%REPO_DIR:~-1%"=="\" set "REPO_DIR=%REPO_DIR:~0,-1%"
cd /d "%REPO_DIR%"

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$tokens=$null; $errors=$null; [void][System.Management.Automation.Language.Parser]::ParseFile('%REPO_DIR%\test.ps1',[ref]$tokens,[ref]$errors); if($errors.Count -gt 0){ Write-Host 'ERROR: test.ps1 syntax validation failed.' -ForegroundColor Red; $errors | ForEach-Object { Write-Host ('  ' + $_.Message + ' at line ' + $_.Extent.StartLineNumber + ', column ' + $_.Extent.StartColumnNumber) -ForegroundColor Red }; exit 2 } else { Write-Host 'PASS: test.ps1 syntax validation passed.' -ForegroundColor Green }"
if errorlevel 1 exit /b %ERRORLEVEL%

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%REPO_DIR%\test.ps1"
set "TEST_RC=%ERRORLEVEL%"
exit /b %TEST_RC%
