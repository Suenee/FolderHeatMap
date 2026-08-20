param(
    [Parameter(Mandatory = $true)]
    [string]$SettingsIni,

    [Parameter(Mandatory = $true)]
    [string]$RepositoryRoot
)

$ErrorActionPreference = 'Stop'

$settingsFull = [System.IO.Path]::GetFullPath($SettingsIni)
$repoFull = [System.IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
$logPath = Join-Path $repoFull 'FolderHeatMap.log'

$settingsDir = Split-Path -Parent $settingsFull
if (-not (Test-Path -LiteralPath $settingsDir)) {
    New-Item -ItemType Directory -Path $settingsDir -Force | Out-Null
}
if (-not (Test-Path -LiteralPath $settingsFull)) {
    New-Item -ItemType File -Path $settingsFull -Force | Out-Null
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

# Remove the obsolete profile-local log created by the short-lived 1.08 build.
$legacyLog = Join-Path $settingsDir 'FolderHeatMap.log'
if (-not [string]::Equals($legacyLog, $logPath, [System.StringComparison]::OrdinalIgnoreCase) -and
    (Test-Path -LiteralPath $legacyLog)) {
    Remove-Item -LiteralPath $legacyLog -Force
    Write-Host "Removed legacy profile log: $legacyLog"
}

Write-Host "FolderHeatMap log path: $logPath"
