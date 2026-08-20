param(
    [Parameter(Mandatory = $true)][string]$ScriptPath,
    [Parameter(Mandatory = $true)][string]$RepositoryRoot,
    [Parameter(Mandatory = $true)][Alias('RunMode')][string]$CaptureMode
)

$ErrorActionPreference = 'Stop'
$logPath = Join-Path $RepositoryRoot 'upgrade.log'
[System.IO.File]::WriteAllText($logPath, '', [System.Text.UTF8Encoding]::new($false))

function Write-ClassifiedLine {
    param([string]$Line)

    Add-Content -LiteralPath $logPath -Value $Line -Encoding UTF8

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

$quotedScript = '"' + $ScriptPath + '"'
$quotedRepo = '"' + $RepositoryRoot + '"'
$command = "$quotedScript $CaptureMode $quotedRepo"

& $env:ComSpec /d /s /c $command 2>&1 | ForEach-Object {
    Write-ClassifiedLine ([string]$_)
}
$rc = $LASTEXITCODE

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
