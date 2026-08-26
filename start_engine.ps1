param(
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
$Repo = Split-Path -Parent $MyInvocation.MyCommand.Path
$Repo = [IO.Path]::GetFullPath($Repo).TrimEnd('\')
$Engine = Join-Path $Repo 'dist\FolderHeatMapEngine.exe'

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

if ($Install) {
    $runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    New-Item -Path $runKey -Force | Out-Null
    $launcher = $MyInvocation.MyCommand.Path
    $command = 'powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "' + $launcher + '"'
    $command = $command.Replace('\"', '"')
    New-ItemProperty -Path $runKey -Name 'FolderHeatMapEngine' -PropertyType String -Value $command -Force | Out-Null
}

if (-not (Test-Path -LiteralPath $Engine)) { exit 2 }

# Avoid a second engine. The engine itself also owns a named mutex, so a race is harmless.
if (Get-Process -Name 'FolderHeatMapEngine' -ErrorAction SilentlyContinue) { exit 0 }

$tcIni = Resolve-TcIni
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
