$ErrorActionPreference = 'Stop'

$Version = '1.22'
$Revision = '1.22-independent-tc-navigation'
$Repo = $env:FHM_UPGRADE_REPO
if ([string]::IsNullOrWhiteSpace($Repo)) { $Repo = (Get-Location).ProviderPath }
$Repo = [IO.Path]::GetFullPath($Repo).TrimEnd('\')
$LogsDir = Join-Path $Repo 'logs'
New-Item -ItemType Directory -Path $LogsDir -Force | Out-Null
$Log = Join-Path $LogsDir 'upgrade.log'
$HadWarning = $false
$FailPhase = 'UNKNOWN'

$Utf8 = [Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $Utf8
$OutputEncoding = $Utf8
try { & chcp.com 65001 *> $null } catch { }

[IO.File]::WriteAllText($Log, '', $Utf8)

function Write-Line([string]$Text, [ConsoleColor]$Color = [ConsoleColor]::Gray) {
    [IO.File]::AppendAllText($Log, $Text + [Environment]::NewLine, $Utf8)
    Write-Host $Text -ForegroundColor $Color
}
function Info([string]$Text) { Write-Line $Text Gray }
function Warn([string]$Text) { $script:HadWarning = $true; Write-Line ('WARNING: ' + $Text) Yellow }
function Fail([string]$Phase, [string]$Text) {
    $script:FailPhase = $Phase
    Write-Line ('ERROR: ' + $Text) Red
    throw [InvalidOperationException]::new($Text)
}
function Run-Native {
    param(
        [Parameter(Mandatory=$true)][string]$Phase,
        [Parameter(Mandatory=$true)][string]$Exe,
        [Parameter(Mandatory=$true)][string[]]$ArgumentList,
        [switch]$AllowFailure,
        [switch]$SuppressOutput
    )
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Exe @ArgumentList 2>&1 | ForEach-Object {
            if ($SuppressOutput) { return }
            $line = [string]$_
            if ($line -match '(?i)\b(error|failed|fatal error)\b|\berror\s+(C|LNK)\d+|MSB\d+.*\berror\b') { Write-Line $line Red }
            elseif ($line -match '(?i)\bwarning\b') { Write-Line $line Yellow }
            else { Write-Line $line Gray }
        }
        $rc = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $savedPreference }
    if ($rc -ne 0 -and -not $AllowFailure) { Fail $Phase ("$Exe failed with exit code $rc") }
    return $rc
}
function Is-ProcessRunning([string[]]$Names) {
    foreach ($n in $Names) { if (Get-Process -Name $n -ErrorAction SilentlyContinue) { return $true } }
    return $false
}
function Get-RegValue([string]$Path, [string]$Name) {
    try { return (Get-ItemProperty -Path $Path -Name $Name -ErrorAction Stop).$Name } catch { return $null }
}
function Resolve-CMake {
    $cmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.CMake.Project -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1
        if ($found) { return [string]$found }
    }
    return $null
}
function Resolve-TC {
    $tcPath = $env:COMMANDER_PATH
    $tcIni = $env:COMMANDER_INI
    $keys = @('HKCU:\Software\Ghisler\Total Commander','HKLM:\Software\Ghisler\Total Commander','HKLM:\Software\Wow6432Node\Ghisler\Total Commander')
    if (-not $tcPath) { foreach ($k in $keys) { $v = Get-RegValue $k 'InstallDir'; if ($v) { $tcPath = $v; break } } }
    if (-not $tcIni) { foreach ($k in $keys) { $v = Get-RegValue $k 'IniFileName'; if ($v) { $tcIni = $v; break } }
    }
    $tcExe = $null
    if ($tcPath) { foreach ($name in @('TOTALCMD64.EXE','TOTALCMD.EXE')) { $p = Join-Path $tcPath $name; if (Test-Path $p) { $tcExe = $p; break } } }
    $plugin = $null
    if ($tcIni -and (Test-Path $tcIni)) { foreach ($line in Get-Content -LiteralPath $tcIni) { if ($line -match 'FolderHeatMap\.wdx64' -and $line -match '=') { $plugin = ($line -split '=',2)[1].Trim('"'); break } } }
    $settings = if ($tcIni) { Join-Path (Split-Path -Parent $tcIni) 'FolderHeatMap.ini' } else { $null }
    [pscustomobject]@{ Path=$tcPath; Ini=$tcIni; Exe=$tcExe; Plugin=$plugin; Settings=$settings }
}
function Move-RootLogsIntoLogsDirectory {
    Get-ChildItem -LiteralPath $Repo -Filter '*.log' -File -ErrorAction SilentlyContinue | ForEach-Object {
        $destination = Join-Path $LogsDir $_.Name
        if (Test-Path -LiteralPath $destination) {
            $stamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmssfff')
            $base = [IO.Path]::GetFileNameWithoutExtension($_.Name)
            $destination = Join-Path $LogsDir ("$base-$stamp.log")
        }
        Move-Item -LiteralPath $_.FullName -Destination $destination -Force
        Info ("[LOGS] Moved $($_.Name) -> logs\$([IO.Path]::GetFileName($destination))")
    }
}
function Reset-BootstrapFilesToRemote {
    foreach ($f in @('upgrade.cmd','upgrade.ps1')) {
        & git diff --quiet -- "$f"
        $dirty = ($LASTEXITCODE -ne 0)
        if (-not $dirty) {
            & git diff --cached --quiet -- "$f"
            $dirty = ($LASTEXITCODE -ne 0)
        }
        if ($dirty) {
            Warn "$f changed during/bootstrap before verification; restoring exact origin/devel version."
            Run-Native -Phase 'SELF-UPDATE' -Exe 'git.exe' -ArgumentList @('restore','--source=origin/devel','--staged','--worktree','--',$f) | Out-Null
        }
    }
}

try {
    Set-Location $Repo
    Info '============================================================'
    Info 'FolderHeatMap upgrade diagnostic log'
    Info ("Version:    $Revision")
    Info ('Started:    ' + [DateTime]::Now.ToString('dd.MM.yyyy HH:mm:ss.fff'))
    Info ("Repository: $Repo")
    $branch = (& git branch --show-current 2>$null)
    $commit = (& git rev-parse HEAD 2>$null)
    Info ("Branch:     $branch")
    Info ("Commit:     $commit")
    Info 'Runner:     PowerShell-only; upgrade.cmd is launcher only'
    Info '============================================================'

    $FailPhase = 'SELF-UPDATE'
    if (-not (Get-Command git.exe -ErrorAction SilentlyContinue)) { Fail $FailPhase 'Git was not found in PATH.' }
    Run-Native -Phase $FailPhase -Exe 'git.exe' -ArgumentList @('fetch','origin') | Out-Null
    $currentBranch = (& git branch --show-current).Trim()
    if ($currentBranch -ne 'devel') { Run-Native -Phase $FailPhase -Exe 'git.exe' -ArgumentList @('switch','devel') | Out-Null }

    & git diff --quiet --ignore-submodules --
    $trackedDirty = ($LASTEXITCODE -ne 0)
    & git diff --cached --quiet --ignore-submodules --
    if ($LASTEXITCODE -ne 0) { $trackedDirty = $true }
    if ($trackedDirty) {
        Warn 'Local tracked changes detected; stashing tracked files before update.'
        Run-Native -Phase $FailPhase -Exe 'git.exe' -ArgumentList @('stash','push','-m','FolderHeatMap automatic pre-upgrade stash') | Out-Null
    }

    Run-Native -Phase $FailPhase -Exe 'git.exe' -ArgumentList @('pull','--ff-only','origin','devel') | Out-Null
    Reset-BootstrapFilesToRemote

    $head = (& git rev-parse HEAD 2>$null).Trim()
    $remoteHead = (& git rev-parse origin/devel 2>$null).Trim()
    if (-not $head -or -not $remoteHead -or $head -ne $remoteHead) { Fail $FailPhase 'Local devel HEAD is not identical to origin/devel after update.' }
    foreach ($f in @('upgrade.cmd','upgrade.ps1')) {
        & git diff --quiet -- "$f"
        if ($LASTEXITCODE -ne 0) { Fail $FailPhase "$f has local working-tree changes after self-repair." }
        $headBlob = (& git rev-parse ("HEAD:$f") 2>$null).Trim()
        $remoteBlob = (& git rev-parse ("origin/devel:$f") 2>$null).Trim()
        if (-not $headBlob -or -not $remoteBlob -or $headBlob -ne $remoteBlob) { Fail $FailPhase "$f in HEAD is not identical to origin/devel after update." }
    }
    Info '[BOOTSTRAP] repository launcher and runner verified current.'
    $buildCommit = (& git rev-parse HEAD).Trim()
    Info ("[GIT] Build commit: $buildCommit")

    $tc = Resolve-TC
    if ($tc.Path) { Info ("[TC] Total Commander: $($tc.Path)") }
    if ($tc.Ini) { Info ("[TC] Configuration:   $($tc.Ini)") }
    if ($tc.Plugin) { Info ("[TC] Registered plugin: $($tc.Plugin)") }
    if ($tc.Settings) { Info ("[FHM] Settings:       $($tc.Settings)") }
    Info ("[FHM] Engine log:     $LogsDir\FolderHeatMap.log")
    Info ("[FHM] Upgrade log:    $Log")

    $cmake = Resolve-CMake
    if (-not $cmake) { Fail 'DEPENDENCIES' 'CMake/Visual Studio Build Tools were not found. Install Visual Studio 2022 Build Tools with C++ and CMake support.' }

    if (-not (Test-Path "$Repo\vendor\sqlite\sqlite3.c") -or -not (Test-Path "$Repo\vendor\sqlite\sqlite3.h")) {
        $FailPhase = 'DEPENDENCIES'; Info '[SETUP] Downloading SQLite 3.53.4...'
        $zip = Join-Path $env:TEMP 'sqlite-amalgamation-3530400.zip'; Invoke-WebRequest -UseBasicParsing 'https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip' -OutFile $zip
        $tmp = Join-Path $env:TEMP ('fhm-sqlite-' + [guid]::NewGuid().ToString('N')); Expand-Archive -LiteralPath $zip -DestinationPath $tmp -Force
        $src = Get-ChildItem $tmp -Directory | Select-Object -First 1; New-Item -ItemType Directory -Path "$Repo\vendor\sqlite" -Force | Out-Null
        Copy-Item (Join-Path $src.FullName 'sqlite3.c') "$Repo\vendor\sqlite\sqlite3.c" -Force; Copy-Item (Join-Path $src.FullName 'sqlite3.h') "$Repo\vendor\sqlite\sqlite3.h" -Force
        Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }

    $FailPhase = 'STOP-RUNTIME'; Info '[1/7] Stopping Total Commander and FolderHeatMap engine...'
    foreach ($name in @('FolderHeatMapConfig','FolderHeatMapReset')) { Get-Process -Name $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue }
    $tcWasRunning = Is-ProcessRunning @('TOTALCMD64','TOTALCMD')
    if ($tcWasRunning) {
        foreach ($name in @('TOTALCMD64','TOTALCMD')) {
            $proc = Get-Process -Name $name -ErrorAction SilentlyContinue
            if ($proc) { $proc | Stop-Process -ErrorAction SilentlyContinue }
        }
        $deadline = [DateTime]::UtcNow.AddSeconds(20)
        while ((Is-ProcessRunning @('TOTALCMD64','TOTALCMD')) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 250 }
        if (Is-ProcessRunning @('TOTALCMD64','TOTALCMD')) {
            Warn 'Normal Total Commander close timed out; forcing process termination.'
            foreach ($name in @('TOTALCMD64','TOTALCMD')) { Get-Process -Name $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue }
        }
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ((Is-ProcessRunning @('FolderHeatMapEngine')) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 250 }
    if (Is-ProcessRunning @('FolderHeatMapEngine')) { Warn 'FolderHeatMapEngine did not finish graceful shutdown within 30 seconds; forcing it to stop.'; Get-Process FolderHeatMapEngine -ErrorAction SilentlyContinue | Stop-Process -Force }

    Move-RootLogsIntoLogsDirectory

    $FailPhase = 'CONFIGURATION'; Info '[2/7] Configuring repository-local logging path...'
    if (-not $tc.Settings) { Fail $FailPhase 'FolderHeatMap settings path could not be resolved.' }
    $helper = Join-Path $Repo 'configure_logging_path.ps1'; if (-not (Test-Path $helper)) { Fail $FailPhase 'configure_logging_path.ps1 is missing.' }
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $helper -SettingsIni $tc.Settings -RepositoryRoot $Repo 2>&1 | ForEach-Object { Info ([string]$_) }
        $helperRc = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    if ($helperRc -ne 0) { Fail $FailPhase 'Could not configure FolderHeatMap.log in the repository logs directory.' }

    $FailPhase = 'BUILD'; Info '[3/7] Preparing build...'; $build = Join-Path $Repo 'build'; if (Test-Path $build) { Remove-Item $build -Recurse -Force }
    Info '[4/7] Configuring x64 Release build...'; Run-Native -Phase 'CMAKE-CONFIGURE' -Exe $cmake -ArgumentList @('-S','.','-B','build','-A','x64') | Out-Null
    Info '[5/7] Building FolderHeatMap 1.22 independent TC navigation and tools...'; Run-Native -Phase 'BUILD' -Exe $cmake -ArgumentList @('--build','build','--config','Release','--target','FolderHeatMap','FolderHeatMapEngine','FolderHeatMapConfig','FolderHeatMapReset') | Out-Null

    $artifacts = @('FolderHeatMap.wdx64','FolderHeatMapEngine.exe','FolderHeatMapConfig.exe','FolderHeatMapReset.exe'); foreach ($f in $artifacts) { if (-not (Test-Path (Join-Path "$build\Release" $f))) { Fail 'BUILD' "$f is missing after build." } }
    $FailPhase = 'DIST'; Info '[6/7] Preparing dist package...'; $dist = Join-Path $Repo 'dist'; New-Item -ItemType Directory -Path $dist -Force | Out-Null
    foreach ($f in $artifacts) { Copy-Item (Join-Path "$build\Release" $f) (Join-Path $dist $f) -Force }
    foreach ($f in @('configure.cmd','README.md','TESTING.md')) { if (Test-Path (Join-Path $Repo $f)) { Copy-Item (Join-Path $Repo $f) (Join-Path $dist $f) -Force } }

    $FailPhase = 'DEPLOY'; Info '[7/7] Deploying to Total Commander...'
    if ($tc.Plugin) {
        $pluginFull = [IO.Path]::GetFullPath($tc.Plugin); $distPlugin = [IO.Path]::GetFullPath((Join-Path $dist 'FolderHeatMap.wdx64'))
        if (-not $pluginFull.Equals($distPlugin,[StringComparison]::OrdinalIgnoreCase)) { $pluginDir = Split-Path -Parent $pluginFull; Copy-Item $distPlugin $pluginFull -Force; Copy-Item (Join-Path $dist 'FolderHeatMapEngine.exe') (Join-Path $pluginDir 'FolderHeatMapEngine.exe') -Force; Info ("[TC] Updated WDX and engine in: $pluginDir") } else { Info '[TC] Registered plugin already points to dist; engine is beside the WDX.' }
    }
    if ($tcWasRunning -and $tc.Exe) { Start-Process -FilePath $tc.Exe | Out-Null }

    Write-Line '' Gray; Write-Line "SUCCESS - FolderHeatMap $Version installed." Green
    Info ("WDX:         $dist\FolderHeatMap.wdx64"); Info ("Engine:      $dist\FolderHeatMapEngine.exe"); Info ("Config:      $dist\FolderHeatMapConfig.exe"); Info ("Engine log:  $LogsDir\FolderHeatMap.log"); Info ("Upgrade log: $Log"); Write-Line '' Gray
    if ($HadWarning) { Write-Line 'STATUS: WARNING - phase=COMPLETE' Yellow } else { Write-Line 'STATUS: SUCCESS - phase=COMPLETE' Green }
    exit 0
}
catch {
    if ($FailPhase -eq 'UNKNOWN') { $FailPhase = 'UNEXPECTED' }
    if (-not ($_.Exception -is [InvalidOperationException])) { Write-Line ('ERROR: ' + $_.Exception.Message) Red }
    Write-Line ("STATUS: FAILED - phase=$FailPhase") Red
    exit 1
}
