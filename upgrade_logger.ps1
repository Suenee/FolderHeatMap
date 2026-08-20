param(
    [Parameter(Position = 0)][string]$ScriptPath,
    [Parameter(Position = 1)][string]$RepositoryRoot,
    [Parameter(Position = 2)][Alias('Mode','RunMode')][string]$CaptureMode
)

$ErrorActionPreference = 'Stop'

# Never use Mandatory parameters here. This logger is part of the bootstrap
# recovery path, so a missing/mis-bound argument must fail deterministically
# instead of PowerShell opening an interactive "Supply values" prompt.
if ([string]::IsNullOrWhiteSpace($ScriptPath) -or
    [string]::IsNullOrWhiteSpace($RepositoryRoot) -or
    [string]::IsNullOrWhiteSpace($CaptureMode)) {
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
if ($CaptureMode -notin @('--captured-bootstrap','--captured-fresh')) {
    Write-Host ('STATUS: FAILED - phase=LOGGER/ARGUMENTS - unsupported mode: ' + $CaptureMode) -ForegroundColor Red
    exit 64
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

# Invoke the batch file directly with an argument array. Do not rebuild a
# command-line string: that reintroduces quoting/escaping collisions for spaces,
# parentheses, ampersands and bootstrap switches.
try {
    & $ScriptPath $CaptureMode $RepositoryRoot 2>&1 | ForEach-Object {
        Write-ClassifiedLine ([string]$_)
    }
    $rc = $LASTEXITCODE
}
catch {
    Write-ClassifiedLine ('ERROR: Logger could not execute upgrade.cmd: ' + $_.Exception.Message)
    $rc = 65
}
if ($null -eq $rc) { $rc = 65 }

# The child script is responsible for making STATUS the final log line.
# If it terminated unexpectedly before doing so, append an unmistakable fallback.
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
