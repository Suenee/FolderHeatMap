param(
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
$Repo = Split-Path -Parent $MyInvocation.MyCommand.Path
$Repo = [IO.Path]::GetFullPath($Repo).TrimEnd('\')
$DistEngine = Join-Path $Repo 'dist\FolderHeatMapEngine.exe'

function Get-RegValue([string]$Path, [string]$Name) {
    try { return (Get-ItemProperty -Path $Path -Name $Name -ErrorAction Stop).$Name } catch { return $null }
}

function Resolve-TcIni {
    if ($env:COMMANDER_INI -and (Test-Path -LiteralPath $env:COMMANDER_INI)) { return $env:COMMANDER_INI }
    foreach ($key in @(
        'HKCU:\Software\Ghisler\Total Commander',
        'HKLM:\Software\Ghisler\Total Commander',
        'HKLM:\Software\Wow6432Node\Ghisler\Total Commander')) {
        $value = Get-RegValue $key 'IniFileName'
        if ($value) {
            $expanded = [Environment]::ExpandEnvironmentVariables([string]$value)
            if (Test-Path -LiteralPath $expanded) { return $expanded }
        }
    }
    return (Join-Path $env:APPDATA 'GHISLER\wincmd.ini')
}

function Resolve-LocalRuntimeEngine([string]$TcIni) {
    if (-not $TcIni -or -not (Test-Path -LiteralPath $TcIni)) { return $null }
    $inContentPlugins = $false
    foreach ($raw in Get-Content -LiteralPath $TcIni -ErrorAction SilentlyContinue) {
        $line=[string]$raw
        if ($line -match '^\s*\[([^\]]+)\]\s*$') {
            $inContentPlugins = ($matches[1] -ieq 'ContentPlugins')
            continue
        }
        if (-not $inContentPlugins -or $line -notmatch '=') { continue }
        $value=($line -split '=',2)[1].Trim().Trim('"')
        $value=[Environment]::ExpandEnvironmentVariables($value)
        if ([IO.Path]::GetFileName($value) -ine 'FolderHeatMap.wdx64') { continue }
        try {
            $wdx=[IO.Path]::GetFullPath($value)
            $engine=Join-Path (Split-Path -Parent $wdx) 'FolderHeatMapEngine.exe'
            if (Test-Path -LiteralPath $engine) { return [IO.Path]::GetFullPath($engine) }
        } catch {}
    }
    return $null
}

if ($Install) {
    $runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    New-Item -Path $runKey -Force | Out-Null
    $launcher = $MyInvocation.MyCommand.Path
    $command = 'powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "' + $launcher + '"'
    $command = $command.Replace('\"', '"')
    New-ItemProperty -Path $runKey -Name 'FolderHeatMapEngine' -PropertyType String -Value $command -Force | Out-Null
}

# Avoid a second engine. The engine itself also owns a named mutex, so a race is harmless.
if (Get-Process -Name 'FolderHeatMapEngine' -ErrorAction SilentlyContinue) { exit 0 }

$tcIni = Resolve-TcIni
$Engine = Resolve-LocalRuntimeEngine $tcIni
if (-not $Engine) { $Engine = $DistEngine }
if (-not (Test-Path -LiteralPath $Engine)) { exit 2 }

$settingsDir = Split-Path -Parent $tcIni
if ([string]::IsNullOrWhiteSpace($settingsDir)) { $settingsDir = Join-Path $env:APPDATA 'GHISLER' }
$settings = Join-Path $settingsDir 'FolderHeatMap.ini'
$db = Join-Path $settingsDir 'FolderHeatMap.db'

$arguments = '--db "' + $db + '" --settings "' + $settings + '"'
$arguments = $arguments.Replace('\"', '"')
$process = Start-Process -FilePath $Engine -ArgumentList $arguments -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 300
if ($process.HasExited) { exit 3 }
exit 0
