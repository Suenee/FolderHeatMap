param(
    [Parameter(Mandatory = $true)]
    [string]$SettingsIni,

    [Parameter(Mandatory = $true)]
    [string]$RepositoryRoot
)

$ErrorActionPreference = 'Stop'

$settingsFull = [System.IO.Path]::GetFullPath($SettingsIni)
$repoFull = [System.IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
$logsDir = Join-Path $repoFull 'logs'
$logPath = Join-Path $logsDir 'FolderHeatMap.log'

$settingsDir = Split-Path -Parent $settingsFull
if (-not (Test-Path -LiteralPath $settingsDir)) {
    New-Item -ItemType Directory -Path $settingsDir -Force | Out-Null
}
if (-not (Test-Path -LiteralPath $settingsFull)) {
    New-Item -ItemType File -Path $settingsFull -Force | Out-Null
}
if (-not (Test-Path -LiteralPath $logsDir)) {
    New-Item -ItemType Directory -Path $logsDir -Force | Out-Null
}

Add-Type @'
using System.Runtime.InteropServices;
public static class FolderHeatMapIniNative {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool WritePrivateProfileString(string section, string key, string value, string fileName);
}
'@

if (-not [FolderHeatMapIniNative]::WritePrivateProfileString('Logging', 'Path', $logPath, $settingsFull)) {
    throw "Could not write [Logging] Path to $settingsFull"
}
[FolderHeatMapIniNative]::WritePrivateProfileString($null, $null, $null, $settingsFull) | Out-Null

# Migrate historical runtime logs into the permanent repository-local log directory.
$legacyLogs = @(
    (Join-Path $repoFull 'FolderHeatMap.log'),
    (Join-Path $settingsDir 'FolderHeatMap.log')
)
foreach ($legacyLog in $legacyLogs) {
    if (-not [string]::Equals($legacyLog, $logPath, [System.StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $legacyLog)) {
        $destination = Join-Path $logsDir ([IO.Path]::GetFileName($legacyLog))
        if (Test-Path -LiteralPath $destination) {
            $stamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmss')
            $destination = Join-Path $logsDir ("FolderHeatMap-$stamp.log")
        }
        Move-Item -LiteralPath $legacyLog -Destination $destination -Force
        Write-Host "Moved legacy log: $legacyLog -> $destination"
    }
}

Write-Host "FolderHeatMap log path: $logPath"
