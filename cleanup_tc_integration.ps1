param(
    [Parameter(Mandatory=$false)][string]$WincmdIni = $env:COMMANDER_INI
)

$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FhmIniCleanup {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool WritePrivateProfileString(string section, string key, string value, string fileName);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
    public static extern uint GetPrivateProfileString(string section, string key, string def, System.Text.StringBuilder ret, uint size, string fileName);
}
'@

function Expand-Value([string]$value) {
    if ([string]::IsNullOrWhiteSpace($value)) { return '' }
    [Environment]::ExpandEnvironmentVariables($value.Trim('"'))
}

function Find-WincmdIni {
    $candidate = Expand-Value $script:WincmdIni
    if ($candidate -and (Test-Path -LiteralPath $candidate)) { return (Resolve-Path -LiteralPath $candidate).Path }
    foreach ($regPath in @('HKCU:\Software\Ghisler\Total Commander','HKLM:\Software\Ghisler\Total Commander')) {
        try {
            $candidate = Expand-Value (Get-ItemProperty -LiteralPath $regPath -Name IniFileName -ErrorAction Stop).IniFileName
            if ($candidate -and (Test-Path -LiteralPath $candidate)) { return (Resolve-Path -LiteralPath $candidate).Path }
        } catch {}
    }
    $candidate = Join-Path $env:APPDATA 'GHISLER\wincmd.ini'
    if (Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    throw 'Could not locate Total Commander wincmd.ini.'
}

function Read-Ini([string]$section, [string]$key) {
    $sb = New-Object System.Text.StringBuilder 8192
    [void][FhmIniCleanup]::GetPrivateProfileString($section, $key, '', $sb, [uint32]$sb.Capacity, $script:Ini)
    $sb.ToString()
}

function Write-Ini([string]$section, [string]$key, [AllowNull()][string]$value) {
    if (-not [FhmIniCleanup]::WritePrivateProfileString($section, $key, $value, $script:Ini)) {
        throw "Could not update [$section] $key"
    }
}

$script:Ini = Find-WincmdIni
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = "$script:Ini.fhm-counter-only-$stamp.bak"
Copy-Item -LiteralPath $script:Ini -Destination $backup -Force

# Remove only FolderHeatMap-managed color filters while preserving all user color filters.
$kept = @()
for ($i = 1; $i -le 999; $i++) {
    $base = "ColorFilter$i"
    $filter = Read-Ini 'Colors' $base
    if (-not $filter) { continue }
    if ($filter -like '>FolderHeatMap Heat *') { continue }
    $kept += [pscustomobject]@{
        Filter = $filter
        Color = Read-Ini 'Colors' ($base + 'Color')
        ColorDark = Read-Ini 'Colors' ($base + 'ColorDark')
    }
}
for ($i = 1; $i -le 999; $i++) {
    $base = "ColorFilter$i"
    Write-Ini 'Colors' $base $null
    Write-Ini 'Colors' ($base + 'Color') $null
    Write-Ini 'Colors' ($base + 'ColorDark') $null
}
$i = 1
foreach ($rule in $kept) {
    $base = "ColorFilter$i"
    Write-Ini 'Colors' $base $rule.Filter
    if ($rule.Color) { Write-Ini 'Colors' ($base + 'Color') $rule.Color }
    if ($rule.ColorDark) { Write-Ini 'Colors' ($base + 'ColorDark') $rule.ColorDark }
    $i++
}

# Remove all saved searches created by FolderHeatMap color and icon integration.
for ($i = 1; $i -le 128; $i++) {
    $name = 'FolderHeatMap Heat ' + $i.ToString('000')
    foreach ($suffix in @('_SearchFor','_SearchIn','_SearchText','_SearchFlags','_plugin')) {
        Write-Ini 'searches' ($name + $suffix) $null
    }
}
for ($level = 1; $level -le 7; $level++) {
    $name = "FolderHeatMap Icon $level"
    foreach ($suffix in @('_SearchFor','_SearchIn','_SearchText','_SearchFlags','_plugin')) {
        Write-Ini 'searches' ($name + $suffix) $null
    }
}

# Remove only FolderHeatMap internal icon associations.
for ($i = 1; $i -le 999; $i++) {
    $base = "Filter$i"
    $value = Read-Ini 'Associations' $base
    if ($value -like '>FolderHeatMap Icon *') {
        Write-Ini 'Associations' $base $null
        Write-Ini 'Associations' ($base + '.icon') $null
    }
}

# Remove obsolete FolderHeatMap-managed metadata. Existing user settings remain untouched.
Write-Ini 'FolderHeatMap' 'ManagedColorRuleCount' $null
Write-Ini 'FolderHeatMap' 'ManagedColorRuleStart' $null
[void][FhmIniCleanup]::WritePrivateProfileString($null, $null, $null, $script:Ini)

Write-Host 'FolderHeatMap TC color/icon integration removed for counter-only diagnostic mode.'
Write-Host "wincmd.ini: $script:Ini"
Write-Host "Backup:     $backup"
