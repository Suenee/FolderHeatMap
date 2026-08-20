$ErrorActionPreference = 'Stop'
$ScriptArgs = @($args)

# FolderHeatMap 1.14 bootstrap transport
# --------------------------------------
# The stable path is environment-only. No PowerShell param() binding and no
# command-line bootstrap payload are used by current upgrade.cmd versions.
# This avoids the Windows trailing-backslash/quote trap and PowerShell automatic
# variable/parameter-name collisions that affected 1.12/1.13.
#
# A legacy recovery block remains intentionally small so an already-installed
# broken 1.13 upgrade.cmd can bootstrap into 1.14 without another manual edit.

$ScriptPath = $env:FHM_UPGRADE_SCRIPT
$RepositoryRoot = $env:FHM_UPGRADE_REPO
$CaptureStage = $env:FHM_UPGRADE_STAGE
$InternalMarker = $env:FHM_UPGRADE_INTERNAL
$LegacyTempScript = $null

function Resolve-LegacyRepositoryRoot {
    try {
        $cwd = (Get-Location).ProviderPath
        if (-not [string]::IsNullOrWhiteSpace($cwd)) {
            & git -C $cwd rev-parse --is-inside-work-tree 2>$null | Out-Null
            if ($LASTEXITCODE -eq 0) { return [System.IO.Path]::GetFullPath($cwd).TrimEnd('\') }
        }
    }
    catch { }

    if ($ScriptArgs.Count -ge 1) {
        try {
            $candidate = [System.IO.Path]::GetFullPath([string]$ScriptArgs[0])
            $parent = Split-Path -Parent $candidate
            & git -C $parent rev-parse --is-inside-work-tree 2>$null | Out-Null
            if ($LASTEXITCODE -eq 0) { return [System.IO.Path]::GetFullPath($parent).TrimEnd('\') }
        }
        catch { }
    }
    return $null
}

# Compatibility bridge for 1.12/1.13 callers. In the known failure mode a
# repository path ending in '\' swallows the following quoted stage token, so
# PowerShell receives only two positional args. We deliberately do NOT trust the
# malformed second arg. The repository is recovered from the working directory,
# then the latest upgrade.cmd is extracted from origin/devel and run using the
# new environment-only transport.
if ([string]::IsNullOrWhiteSpace($ScriptPath) -or
    [string]::IsNullOrWhiteSpace($RepositoryRoot) -or
    [string]::IsNullOrWhiteSpace($CaptureStage) -or
    $InternalMarker -ne '1') {

    $legacyRepo = Resolve-LegacyRepositoryRoot
    if ([string]::IsNullOrWhiteSpace($legacyRepo)) {
        Write-Host ('STATUS: FAILED - phase=LOGGER/LEGACY-RECOVERY - repository could not be resolved; args=' + $ScriptArgs.Count) -ForegroundColor Red
        exit 64
    }

    try {
        & git -C $legacyRepo fetch origin 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'git fetch origin failed' }

        $lines = @(& git -C $legacyRepo show 'origin/devel:upgrade.cmd' 2>&1)
        if ($LASTEXITCODE -ne 0 -or $lines.Count -eq 0) { throw 'could not read origin/devel:upgrade.cmd' }

        $LegacyTempScript = Join-Path $env:TEMP ('FolderHeatMap-upgrade-recovered-' + [guid]::NewGuid().ToString('N') + '.cmd')
        [System.IO.File]::WriteAllLines($LegacyTempScript, [string[]]$lines, [System.Text.UTF8Encoding]::new($false))
        if (-not (Test-Path -LiteralPath $LegacyTempScript -PathType Leaf)) { throw 'recovered upgrade.cmd was not created' }
        if ((Get-Item -LiteralPath $LegacyTempScript).Length -lt 1000) { throw 'recovered upgrade.cmd is unexpectedly small' }

        $ScriptPath = $LegacyTempScript
        $RepositoryRoot = $legacyRepo
        $CaptureStage = 'fresh'
        $env:FHM_UPGRADE_SCRIPT = $ScriptPath
        $env:FHM_UPGRADE_REPO = $RepositoryRoot
        $env:FHM_UPGRADE_STAGE = $CaptureStage
        $env:FHM_UPGRADE_INTERNAL = '1'
    }
    catch {
        Write-Host ('STATUS: FAILED - phase=LOGGER/LEGACY-RECOVERY - ' + $_.Exception.Message) -ForegroundColor Red
        if ($LegacyTempScript) { Remove-Item -LiteralPath $LegacyTempScript -Force -ErrorAction SilentlyContinue }
        exit 64
    }
}

try {
    $ScriptPath = [System.IO.Path]::GetFullPath($ScriptPath)
    $RepositoryRoot = [System.IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
}
catch {
    Write-Host ('STATUS: FAILED - phase=LOGGER/ARGUMENTS - invalid path: ' + $_.Exception.Message) -ForegroundColor Red
    if ($LegacyTempScript) { Remove-Item -LiteralPath $LegacyTempScript -Force -ErrorAction SilentlyContinue }
    exit 64
}

if (-not (Test-Path -LiteralPath $ScriptPath -PathType Leaf)) {
    Write-Host ('STATUS: FAILED - phase=LOGGER/ARGUMENTS - script not found: ' + $ScriptPath) -ForegroundColor Red
    if ($LegacyTempScript) { Remove-Item -LiteralPath $LegacyTempScript -Force -ErrorAction SilentlyContinue }
    exit 64
}
if (-not (Test-Path -LiteralPath $RepositoryRoot -PathType Container)) {
    Write-Host ('STATUS: FAILED - phase=LOGGER/ARGUMENTS - repository not found: ' + $RepositoryRoot) -ForegroundColor Red
    if ($LegacyTempScript) { Remove-Item -LiteralPath $LegacyTempScript -Force -ErrorAction SilentlyContinue }
    exit 64
}
if ($CaptureStage.ToLowerInvariant() -ne 'fresh') {
    Write-Host ('STATUS: FAILED - phase=LOGGER/ARGUMENTS - unsupported environment stage: ' + $CaptureStage) -ForegroundColor Red
    if ($LegacyTempScript) { Remove-Item -LiteralPath $LegacyTempScript -Force -ErrorAction SilentlyContinue }
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

# No bootstrap data is passed on argv. The child inherits the four FHM_UPGRADE_*
# environment variables and therefore cannot lose a value through quote parsing.
try {
    & $ScriptPath 2>&1 | ForEach-Object {
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

if ($LegacyTempScript) {
    Remove-Item -LiteralPath $LegacyTempScript -Force -ErrorAction SilentlyContinue
}

exit $rc
