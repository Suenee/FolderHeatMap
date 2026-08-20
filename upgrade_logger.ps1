param(
    [Parameter(Position = 0)][string]$ScriptPath,
    [Parameter(Position = 1)][string]$RepositoryRoot,
    [Parameter(Position = 2)][Alias('Mode','RunMode','CaptureMode')][string]$CaptureStage
)

$ErrorActionPreference = 'Stop'

# Bootstrap code must never prompt interactively for missing parameters. All
# parameters are optional at the PowerShell binding layer and validated here.
# This also keeps compatibility with the short-lived 1.12 callers using -Mode.
if ([string]::IsNullOrWhiteSpace($ScriptPath) -or
    [string]::IsNullOrWhiteSpace($RepositoryRoot) -or
    [string]::IsNullOrWhiteSpace($CaptureStage)) {
    Write-Host 'STATUS: FAILED - phase=LOGGER/ARGUMENTS - required bootstrap argument missing' -ForegroundColor Red
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

# Normalize both the stable 1.13 tokens and legacy 1.12 values. New callers use
# plain tokens so PowerShell never has to parse a value beginning with '-'.
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

# Invoke the batch file directly with separate arguments. Do not reconstruct a
# command-line string: separate argv values are safe for spaces, parentheses,
# ampersands and other cmd.exe metacharacters in repository paths.
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
