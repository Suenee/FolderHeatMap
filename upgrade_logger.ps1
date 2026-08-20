$ErrorActionPreference = 'Stop'
$ScriptArgs = @($args)

# FolderHeatMap upgrade logger
# The bootstrap payload is environment-only. The logger deliberately does not
# use PowerShell param() binding for bootstrap data.

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
    return $null
}

# Recovery for short-lived 1.12/1.13 launchers.
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

        # IMPORTANT: git.exe is a native process. Redirect its stdout directly
        # to a file so PowerShell never decodes/re-encodes the batch source.
        # This preserves CRLF and all CMD labels exactly.
        $LegacyTempScript = Join-Path $env:TEMP ('FolderHeatMap-upgrade-recovered-' + [guid]::NewGuid().ToString('N') + '.cmd')
        $spec = 'origin/devel:upgrade.cmd'
        $psi = [System.Diagnostics.ProcessStartInfo]::new()
        $psi.FileName = 'git.exe'
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $true
        $psi.Arguments = '-C "' + $legacyRepo + '" show "' + $spec + '"'
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $p = [System.Diagnostics.Process]::Start($psi)
        $fs = [System.IO.File]::Open($LegacyTempScript, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
        try { $p.StandardOutput.BaseStream.CopyTo($fs) } finally { $fs.Dispose() }
        $stderr = $p.StandardError.ReadToEnd()
        $p.WaitForExit()
        if ($p.ExitCode -ne 0) { throw ('could not read origin/devel:upgrade.cmd: ' + $stderr.Trim()) }
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
    exit 64
}
if (-not (Test-Path -LiteralPath $RepositoryRoot -PathType Container)) {
    Write-Host ('STATUS: FAILED - phase=LOGGER/ARGUMENTS - repository not found: ' + $RepositoryRoot) -ForegroundColor Red
    exit 64
}
if ($CaptureStage.ToLowerInvariant() -ne 'fresh') {
    Write-Host ('STATUS: FAILED - phase=LOGGER/ARGUMENTS - unsupported environment stage: ' + $CaptureStage) -ForegroundColor Red
    exit 64
}

$logPath = Join-Path $RepositoryRoot 'upgrade.log'
[System.IO.File]::WriteAllText($logPath, '', [System.Text.UTF8Encoding]::new($false))

function Write-ClassifiedLine {
    param([AllowEmptyString()][string]$Line)
    [System.IO.File]::AppendAllText($logPath, $Line + [Environment]::NewLine, [System.Text.UTF8Encoding]::new($false))
    if ($Line -match '^STATUS:\s+SUCCESS') { Write-Host $Line -ForegroundColor Green }
    elseif ($Line -match '^STATUS:\s+WARNING' -or $Line -match '(?i)\bWARNING\b') { Write-Host $Line -ForegroundColor Yellow }
    elseif ($Line -match '^STATUS:\s+FAILED' -or $Line -match '(?i)\b(ERROR|FAILED|FATAL ERROR)\b' -or $Line -match '(?i)MSB\d+.*\berror\b' -or $Line -match '(?i)\berror\s+(C|LNK)\d+') { Write-Host $Line -ForegroundColor Red }
    else { Write-Host $Line -ForegroundColor Gray }
}

# A .cmd file must be executed by cmd.exe. PowerShell's call operator is not a
# reliable batch-file host for this bootstrap and caused "batch label specified"
# failures. Pass the batch path through COMSPEC and quote only the script path.
try {
    $cmdLine = '"' + $ScriptPath + '"'
    & $env:ComSpec /d /s /c $cmdLine 2>&1 | ForEach-Object {
        Write-ClassifiedLine ([string]$_)
    }
    $rc = $LASTEXITCODE
}
catch {
    Write-ClassifiedLine ('ERROR: Logger could not execute upgrade.cmd through cmd.exe: ' + $_.Exception.Message)
    $rc = 65
}
if ($null -eq $rc) { $rc = 65 }

$lastLine = ''
if (Test-Path -LiteralPath $logPath) { $lastLine = Get-Content -LiteralPath $logPath -Tail 1 -ErrorAction SilentlyContinue }
if ($lastLine -notmatch '^STATUS:\s+(SUCCESS|WARNING|FAILED)') {
    $fallback = "STATUS: FAILED - phase=LOGGER/UNEXPECTED-EXIT - exit_code=$rc"
    Write-ClassifiedLine $fallback
    if ($rc -eq 0) { $rc = 1 }
}

if ($LegacyTempScript) { Remove-Item -LiteralPath $LegacyTempScript -Force -ErrorAction SilentlyContinue }
exit $rc
