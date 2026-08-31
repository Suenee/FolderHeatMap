@echo off
cls
setlocal EnableExtensions EnableDelayedExpansion

set "UPGRADE_REV=1.52-bootstrap-network-safe-directory"
set "RUN_TEST=0"

if /I "%~1"=="--bootstrap-internal" goto :bootstrap_internal
if /I "%~1"=="--test" (set "RUN_TEST=1") else if not "%~1"=="" (powershell.exe -NoProfile -Command "Write-Host 'ERROR: Unknown upgrade option. Supported: --test' -ForegroundColor Red" & exit /b 2)

set "REPO_DIR=%~dp0"
if "!REPO_DIR:~-1!"=="\" set "REPO_DIR=!REPO_DIR:~0,-1!"
cd /d "!REPO_DIR!"

where git.exe >nul 2>nul
if errorlevel 1 (powershell.exe -NoProfile -Command "Write-Host 'ERROR: Git was not found in PATH. Install Git for Windows, then run upgrade.cmd again.' -ForegroundColor Red" & exit /b 1)

call :detect_git_repository
if "!GIT_REPO_STATE!"=="1" goto :repository_ready
if "!GIT_REPO_STATE!"=="2" exit /b 1
goto :bootstrap

:repository_ready
if not exist "!REPO_DIR!\logs" mkdir "!REPO_DIR!\logs" >nul 2>nul
git fetch origin >nul 2>nul
if errorlevel 1 (> "!REPO_DIR!\logs\upgrade.log" echo ERROR: git fetch origin failed before PowerShell runner bootstrap. & >> "!REPO_DIR!\logs\upgrade.log" echo STATUS: FAILED - phase=SELF-UPDATE/BOOTSTRAP & powershell.exe -NoProfile -Command "Write-Host 'ERROR: git fetch origin failed before upgrade bootstrap.' -ForegroundColor Red" & exit /b 1)
set "RUNNER_TEMP=%TEMP%\FolderHeatMap-upgrade-%RANDOM%-%RANDOM%.ps1"
git show origin/devel:upgrade.ps1 > "!RUNNER_TEMP!" 2>nul
if errorlevel 1 (> "!REPO_DIR!\logs\upgrade.log" echo ERROR: Could not extract origin/devel:upgrade.ps1. & >> "!REPO_DIR!\logs\upgrade.log" echo STATUS: FAILED - phase=SELF-UPDATE/BOOTSTRAP & powershell.exe -NoProfile -Command "Write-Host 'ERROR: Could not extract upgrade.ps1 from origin/devel.' -ForegroundColor Red" & exit /b 1)
(
    set "FHM_UPGRADE_REPO=!REPO_DIR!"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "!RUNNER_TEMP!"
    set "UPGRADE_RC=!ERRORLEVEL!"
    del /q "!RUNNER_TEMP!" >nul 2>nul
    if "!UPGRADE_RC!"=="0" if "!RUN_TEST!"=="1" (
        if not exist "!REPO_DIR!\test.cmd" (powershell.exe -NoProfile -Command "Write-Host 'ERROR: Upgrade succeeded, but test.cmd is missing.' -ForegroundColor Red" & set "UPGRADE_RC=3") else (powershell.exe -NoProfile -Command "Write-Host 'Upgrade succeeded. Starting test.cmd...' -ForegroundColor Cyan" & call "!REPO_DIR!\test.cmd" & set "UPGRADE_RC=!ERRORLEVEL!")
    )
    exit /b !UPGRADE_RC!
)

:detect_git_repository
set "GIT_REPO_STATE=0"
set "GIT_DETECT_ERR=%TEMP%\FolderHeatMap-git-detect-%RANDOM%-%RANDOM%.log"
git rev-parse --is-inside-work-tree >nul 2> "!GIT_DETECT_ERR!"
if not errorlevel 1 (
    del /q "!GIT_DETECT_ERR!" >nul 2>nul
    set "GIT_REPO_STATE=1"
    exit /b 0
)
findstr /I /C:"detected dubious ownership" "!GIT_DETECT_ERR!" >nul 2>nul
if errorlevel 1 (
    del /q "!GIT_DETECT_ERR!" >nul 2>nul
    set "GIT_REPO_STATE=0"
    exit /b 0
)
powershell.exe -NoProfile -Command "Write-Host 'Git marked this repository as dubious ownership. Registering this exact repository as safe.directory...' -ForegroundColor Yellow"
set "FHM_GIT_DETECT_ERR=!GIT_DETECT_ERR!"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$text=[IO.File]::ReadAllText($env:FHM_GIT_DETECT_ERR); $m=[regex]::Match($text, \"safe\.directory\s+'([^']+)'\"); if(-not $m.Success){ Write-Host 'ERROR: Git reported dubious ownership, but its safe.directory path could not be parsed.' -ForegroundColor Red; exit 3 }; $safe=$m.Groups[1].Value; & git.exe config --global --add safe.directory $safe; if($LASTEXITCODE -ne 0){ Write-Host ('ERROR: Could not register Git safe.directory: ' + $safe) -ForegroundColor Red; exit $LASTEXITCODE }; Write-Host ('Git safe.directory registered: ' + $safe) -ForegroundColor Green"
set "SAFE_RC=!ERRORLEVEL!"
del /q "!GIT_DETECT_ERR!" >nul 2>nul
set "FHM_GIT_DETECT_ERR="
if not "!SAFE_RC!"=="0" (
    set "GIT_REPO_STATE=2"
    exit /b 0
)
git rev-parse --is-inside-work-tree >nul 2>nul
if errorlevel 1 (
    powershell.exe -NoProfile -Command "Write-Host 'ERROR: Repository is still rejected by Git after safe.directory registration.' -ForegroundColor Red"
    set "GIT_REPO_STATE=2"
    exit /b 0
)
set "GIT_REPO_STATE=1"
exit /b 0

:bootstrap
set "BOOTSTRAP_TARGET=!REPO_DIR!"
set "BOOTSTRAP_PARENT=!REPO_DIR!\.."
for %%I in ("!BOOTSTRAP_PARENT!") do set "BOOTSTRAP_PARENT=%%~fI"
set "BOOTSTRAP_LOG=!BOOTSTRAP_PARENT!\FolderHeatMap-bootstrap.log"
set "BOOTSTRAP_TEMP=%TEMP%\FolderHeatMap-bootstrap-%RANDOM%-%RANDOM%.cmd"

powershell.exe -NoProfile -Command "Write-Host 'FolderHeatMap repository not found. Starting fresh-machine bootstrap in the current directory...' -ForegroundColor Cyan"

set "BOOTSTRAP_EXTRA=0"
for /f "delims=" %%F in ('dir /b /a "!BOOTSTRAP_TARGET!" 2^>nul') do (
    if /I not "%%F"=="upgrade.cmd" set "BOOTSTRAP_EXTRA=1"
)
if "!BOOTSTRAP_EXTRA!"=="1" (
    powershell.exe -NoProfile -Command "Write-Host 'ERROR: Bootstrap directory must contain only upgrade.cmd. Remove the failed nested FolderHeatMap directory or other files, then run upgrade.cmd again.' -ForegroundColor Red"
    exit /b 1
)

copy /y "%~f0" "!BOOTSTRAP_TEMP!" >nul
if errorlevel 1 (
    powershell.exe -NoProfile -Command "Write-Host 'ERROR: Could not create temporary bootstrap runner.' -ForegroundColor Red"
    exit /b 1
)

(
    if "!RUN_TEST!"=="1" (
        call "!BOOTSTRAP_TEMP!" --bootstrap-internal "!BOOTSTRAP_TARGET!" --test
    ) else (
        call "!BOOTSTRAP_TEMP!" --bootstrap-internal "!BOOTSTRAP_TARGET!"
    )
    set "BOOTSTRAP_RC=!ERRORLEVEL!"
    del /q "!BOOTSTRAP_TEMP!" >nul 2>nul
    exit /b !BOOTSTRAP_RC!
)

:bootstrap_internal
set "BOOTSTRAP_TARGET=%~2"
set "BOOTSTRAP_RUN_TEST=0"
if /I "%~3"=="--test" set "BOOTSTRAP_RUN_TEST=1"
for %%I in ("!BOOTSTRAP_TARGET!\..") do set "BOOTSTRAP_PARENT=%%~fI"
set "BOOTSTRAP_LOG=!BOOTSTRAP_PARENT!\FolderHeatMap-bootstrap.log"

> "!BOOTSTRAP_LOG!" echo FolderHeatMap bootstrap %UPGRADE_REV%
>> "!BOOTSTRAP_LOG!" echo Target: !BOOTSTRAP_TARGET!

if not exist "!BOOTSTRAP_TARGET!" mkdir "!BOOTSTRAP_TARGET!" >nul 2>nul
if not exist "!BOOTSTRAP_TARGET!" (
    powershell.exe -NoProfile -Command "Write-Host 'ERROR: Could not create bootstrap target: !BOOTSTRAP_TARGET!' -ForegroundColor Red"
    >> "!BOOTSTRAP_LOG!" echo STATUS: FAILED - phase=BOOTSTRAP-CREATE
    exit /b 1
)

set "BOOTSTRAP_EXTRA=0"
for /f "delims=" %%F in ('dir /b /a "!BOOTSTRAP_TARGET!" 2^>nul') do (
    if /I not "%%F"=="upgrade.cmd" set "BOOTSTRAP_EXTRA=1"
)
if "!BOOTSTRAP_EXTRA!"=="1" (
    powershell.exe -NoProfile -Command "Write-Host 'ERROR: Bootstrap target is not empty. It must contain only upgrade.cmd.' -ForegroundColor Red"
    >> "!BOOTSTRAP_LOG!" echo STATUS: FAILED - phase=BOOTSTRAP-SAFETY
    exit /b 1
)

del /q "!BOOTSTRAP_TARGET!\upgrade.cmd" >nul 2>nul
powershell.exe -NoProfile -Command "Write-Host 'Cloning origin/devel directly into: !BOOTSTRAP_TARGET!' -ForegroundColor Cyan"
git clone --branch devel --single-branch "https://github.com/Suenee/FolderHeatMap.git" "!BOOTSTRAP_TARGET!" >> "!BOOTSTRAP_LOG!" 2>&1
if errorlevel 1 (
    powershell.exe -NoProfile -Command "Write-Host 'ERROR: Git clone failed. See FolderHeatMap-bootstrap.log. If the repository requires authentication, sign in with Git Credential Manager and run upgrade.cmd again.' -ForegroundColor Red"
    >> "!BOOTSTRAP_LOG!" echo STATUS: FAILED - phase=BOOTSTRAP-CLONE
    exit /b 1
)
if not exist "!BOOTSTRAP_TARGET!\.git" (
    powershell.exe -NoProfile -Command "Write-Host 'ERROR: Clone completed, but .git is missing from the target directory.' -ForegroundColor Red"
    >> "!BOOTSTRAP_LOG!" echo STATUS: FAILED - phase=BOOTSTRAP-VERIFY
    exit /b 1
)
if not exist "!BOOTSTRAP_TARGET!\upgrade.cmd" (
    powershell.exe -NoProfile -Command "Write-Host 'ERROR: Clone completed, but the authoritative upgrade.cmd is missing.' -ForegroundColor Red"
    >> "!BOOTSTRAP_LOG!" echo STATUS: FAILED - phase=BOOTSTRAP-VERIFY
    exit /b 1
)

>> "!BOOTSTRAP_LOG!" echo STATUS: CLONE OK - handing off to repository upgrade.cmd
powershell.exe -NoProfile -Command "Write-Host 'Repository cloned successfully into the current directory. Handing off to its current upgrade.cmd...' -ForegroundColor Green"
if "!BOOTSTRAP_RUN_TEST!"=="1" (
    call "!BOOTSTRAP_TARGET!\upgrade.cmd" --test
) else (
    call "!BOOTSTRAP_TARGET!\upgrade.cmd"
)
set "BOOTSTRAP_RC=!ERRORLEVEL!"
if "!BOOTSTRAP_RC!"=="0" (
    >> "!BOOTSTRAP_LOG!" echo STATUS: SUCCESS
    powershell.exe -NoProfile -Command "Write-Host 'FolderHeatMap bootstrap and upgrade completed successfully.' -ForegroundColor Green"
) else (
    >> "!BOOTSTRAP_LOG!" echo STATUS: FAILED - repository upgrade exit code !BOOTSTRAP_RC!
    powershell.exe -NoProfile -Command "Write-Host 'ERROR: Repository was cloned, but its upgrade failed. Check logs\upgrade.log in the project directory.' -ForegroundColor Red"
)
exit /b !BOOTSTRAP_RC!
