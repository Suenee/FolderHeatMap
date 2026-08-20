$ErrorActionPreference = 'Stop'

# 1.14 hardening: do not use a PowerShell param() block at all. Bootstrap
# values are read from environment variables first. For compatibility with
# short-lived 1.13 callers, plain positional $args are accepted as a fallback.
$ScriptPath = $env:FHM_UPGRADE_SCRIPT
$RepositoryRoot = $env:FHM_UPGRADE_REPO
$CaptureStage = $env:FHM_UPGRADE_STAGE

if ([string]::IsNullOrWhiteSpace($ScriptPath) -and $args.Count -ge 1) {
    $ScriptPath = [string]$args[0]
}
if ([string]::IsNullOrWhiteSpace($RepositoryRoot) -and $args.Count -ge 2) {
    $RepositoryRoot = [string]$args[1]
}
if ([string]::IsNullOrWhiteSpace($CaptureStage) -and $args.Count -ge 3) {
    $CaptureStage = [string]$args[2]
}

if ([string]::IsNullOrWhiteSpace($ScriptPath) -or
    [string]::IsNullOrWhiteSpace($RepositoryRoot) -or
    [string]::IsNullOrWhiteSpace($CaptureStage)) {
    Write-Host ('STATUS: FAILED - phase=LOGGER/ARGUMENTS - bootstrap data missing; args=' + $args.Count) -ForegroundColor Red
    exit 64
}

try {
    $ScriptPath = [System.IO.Path]::GetFullPath($ScriptPath)
    $RepositoryRoot = [System.IO.Path]::GetFullPath($RepositoryRoot)
}
catch {
    Write-Host ('STATUS: FAILED - phase=LOGGER/ARGUMENTS - invalid path: ' + $_.Exception.Message) -ForegroundColor Red
    exit 64
}

if (-not (Test-Path -LiteralPath $ScriptPath -PathType Leaf)) {
    Write-Host ('STATUS: FAILED - phase=LOGGER/ARGUMENTS - script not found: ' + $ScriptPath) -ForegroundColor Red
    exit 64
}
if (-not (Test-Path -LiteralPath $RepositoryRoot -PathType Container)) {
    Write-Host ('STATUS: FAILED - phase=LOGGER/ARGUMENTS - repository not found: ' + $RepositoryRoot) -ForegroundColor Red
    exit 64
}

switch ($CaptureStage.ToLowerInvariant()) {
    'bootstrap'            { $BatchSwitch = '--captured-bootstrap' }
    'fresh'                { $BatchSwitch = '--captured-fresh' }
    '--captured-bootstrap' { $BatchSwitch = '--captured-bootstrap' }
    '--captured-fresh'     { $BatchSwitch = '--captured-fresh' }
    default {
        Write-Host ('STATUS: FAILED - phase=LOGGER/ARGUMENTS - unsupported stage: ' + $CaptureStage) -ForegroundColor Red
        exit 64
    }
}

$logPath = Join-Path $RepositoryRoot 'upgrade.log'
[System.IO.File]::WriteAllText($logPath, '', [System.Text.UTF8Encoding]::new($false))

function Write-ClassifiedLine {
    param([AllowEmptyString()][string]$Line)

    [System.IO.File]::AppendAllText(
        $logPath,
        $Line + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false)
    )

    if ($Line -match '^STATUS:\s+SUCCESS') {
        Write-Host $Line -ForegroundColor Green
    }
    elseif ($Line -match '^STATUS:\s+WARNING' -or $Line -match '(?i)\bWARNING\b') {
        Write-Host $Line -ForegroundColor Yellow
    }
    elseif ($Line -match '^STATUS:\s+FAILED' -or
            $Line -match '(?i)\b(ERROR|FAILED|FATAL ERROR)\b' -or
            $Line -match '(?i)MSB\d+.*\berror\b' -or
            $Line -match '(?i)\berror\s+(C|LNK)\d+') {
        Write-Host $Line -ForegroundColor Red
    }
    else {
        Write-Host $Line -ForegroundColor Gray
    }
}

# PowerShell invokes the .cmd file directly with separate argv values. No
# reconstructed command string is used, avoiding quoting/metacharacter issues.
try {
    & $ScriptPath $BatchSwitch $RepositoryRoot 2>&1 | ForEach-Object {
        Write-ClassifiedLine ([string]$_)
    }
    $rc = $LASTEXITCODE
}
catch {
    Write-ClassifiedLine ('ERROR: Logger could not execute upgrade.cmd: ' + $_.Exception.Message)
    $rc = 65
}
if ($null -eq $rc) { $rc = 65 }

$lastLine = ''
if (Test-Path -LiteralPath $logPath) {
    $lastLine = Get-Content -LiteralPath $logPath -Tail 1 -ErrorAction SilentlyContinue
}
if ($lastLine -notmatch '^STATUS:\s+(SUCCESS|WARNING|FAILED)') {
    $fallback = "STATUS: FAILED - phase=LOGGER/UNEXPECTED-EXIT - exit_code=$rc"
    Write-ClassifiedLine $fallback
    if ($rc -eq 0) { $rc = 1 }
}

exit $rc
