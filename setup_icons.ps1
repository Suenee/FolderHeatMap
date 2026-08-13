param(
    [switch]$Remove
)

$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FhmIni {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool WritePrivateProfileString(string section, string key, string value, string fileName);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
    public static extern uint GetPrivateProfileString(string section, string key, string def, System.Text.StringBuilder ret, uint size, string fileName);
}
'@

function Read-IniValue([string]$section, [string]$key, [string]$default = '') {
    $sb = New-Object System.Text.StringBuilder 8192
    [void][FhmIni]::GetPrivateProfileString($section, $key, $default, $sb, [uint32]$sb.Capacity, $script:WincmdIni)
    $sb.ToString()
}

function Write-IniValue([string]$section, [string]$key, [AllowNull()][string]$value) {
    if (-not [FhmIni]::WritePrivateProfileString($section, $key, $value, $script:WincmdIni)) {
        throw "Could not update [$section] $key in $script:WincmdIni"
    }
}

function Expand-Value([string]$value) {
    if ([string]::IsNullOrWhiteSpace($value)) { return '' }
    [Environment]::ExpandEnvironmentVariables($value.Trim('"'))
}

function Find-WincmdIni {
    $candidate = Expand-Value $env:COMMANDER_INI
    if ($candidate -and (Test-Path -LiteralPath $candidate)) { return (Resolve-Path -LiteralPath $candidate).Path }

    foreach ($regPath in @('HKCU:\Software\Ghisler\Total Commander','HKLM:\Software\Ghisler\Total Commander')) {
        try {
            $value = (Get-ItemProperty -LiteralPath $regPath -Name IniFileName -ErrorAction Stop).IniFileName
            $candidate = Expand-Value $value
            if ($candidate -and (Test-Path -LiteralPath $candidate)) { return (Resolve-Path -LiteralPath $candidate).Path }
        } catch {}
    }

    $candidate = Join-Path $env:APPDATA 'GHISLER\wincmd.ini'
    if (Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    throw 'Could not locate Total Commander wincmd.ini.'
}

function Read-FhmColor([int]$level) {
    $defaults = @(0, 7915600, 5954690, 4645320, 4312565, 3644410, 3955445, 7882485)
    $settingsIni = Join-Path (Split-Path -Parent $script:WincmdIni) 'FolderHeatMap.ini'
    if (-not (Test-Path -LiteralPath $settingsIni)) { return [uint32]$defaults[$level] }
    $sb = New-Object System.Text.StringBuilder 64
    [void][FhmIni]::GetPrivateProfileString('Colors', "Color$level", [string]$defaults[$level], $sb, [uint32]$sb.Capacity, $settingsIni)
    $parsed = 0L
    if ([Int64]::TryParse($sb.ToString(), [ref]$parsed)) { return [uint32]$parsed }
    [uint32]$defaults[$level]
}

function Write-HeatFolderIcon([string]$path, [uint32]$colorRef) {
    Add-Type -AssemblyName System.Drawing
    $r = [int]($colorRef -band 0xff)
    $g = [int](($colorRef -shr 8) -band 0xff)
    $b = [int](($colorRef -shr 16) -band 0xff)

    $bmp = New-Object System.Drawing.Bitmap -ArgumentList 32,32,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $gfx.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $gfx.Clear([System.Drawing.Color]::Transparent)
        $base = [System.Drawing.Color]::FromArgb(255,$r,$g,$b)
        $dark = [System.Drawing.Color]::FromArgb(255,[Math]::Max(0,$r-65),[Math]::Max(0,$g-65),[Math]::Max(0,$b-65))
        $light = [System.Drawing.Color]::FromArgb(255,[Math]::Min(255,$r+55),[Math]::Min(255,$g+55),[Math]::Min(255,$b+55))
        $shadow = New-Object System.Drawing.SolidBrush -ArgumentList ([System.Drawing.Color]::FromArgb(70,0,0,0))
        $fill = New-Object System.Drawing.SolidBrush -ArgumentList $base
        $tab = New-Object System.Drawing.SolidBrush -ArgumentList $light
        $pen = New-Object System.Drawing.Pen -ArgumentList $dark,1.4
        try {
            $gfx.FillRectangle($shadow, 4,10,25,18)
            $gfx.FillRectangle($tab, 3,6,11,7)
            $gfx.FillRectangle($fill, 3,10,26,17)
            $gfx.DrawRectangle($pen, 3,10,25,16)
            $gfx.DrawLine($pen,3,10,3,7)
            $gfx.DrawLine($pen,3,7,13,7)
            $gfx.DrawLine($pen,13,7,16,10)
            $highlight = New-Object System.Drawing.Pen -ArgumentList ([System.Drawing.Color]::FromArgb(150,255,255,255)),1
            try { $gfx.DrawLine($highlight,5,12,26,12) } finally { $highlight.Dispose() }
        } finally {
            $shadow.Dispose(); $fill.Dispose(); $tab.Dispose(); $pen.Dispose()
        }

        $png = New-Object System.IO.MemoryStream
        try {
            $bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
            $data = $png.ToArray()
            $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
            $bw = New-Object System.IO.BinaryWriter -ArgumentList $fs
            try {
                $bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]1)
                $bw.Write([byte]32); $bw.Write([byte]32); $bw.Write([byte]0); $bw.Write([byte]0)
                $bw.Write([uint16]1); $bw.Write([uint16]32)
                $bw.Write([uint32]$data.Length); $bw.Write([uint32]22)
                $bw.Write($data)
            } finally { $bw.Dispose(); $fs.Dispose() }
        } finally { $png.Dispose() }
    } finally { $gfx.Dispose(); $bmp.Dispose() }
}

$script:WincmdIni = Find-WincmdIni
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = "$script:WincmdIni.fhm-icons-$stamp.bak"
Copy-Item -LiteralPath $script:WincmdIni -Destination $backup -Force

# Remove previous FolderHeatMap-managed icon associations without touching the user's own associations.
for ($i=1; $i -le 999; $i++) {
    $base = "Filter$i"
    $value = Read-IniValue 'Associations' $base ''
    if ($value -like '>FolderHeatMap Icon *') {
        Write-IniValue 'Associations' $base $null
        Write-IniValue 'Associations' "$base.icon" $null
    }
}
for ($level=1; $level -le 7; $level++) {
    $name = "FolderHeatMap Icon $level"
    foreach ($suffix in @('_SearchFor','_SearchIn','_SearchText','_SearchFlags','_plugin')) {
        Write-IniValue 'searches' ($name + $suffix) $null
    }
}

if ($Remove) {
    Write-Host "FolderHeatMap icon associations removed. Backup: $backup"
    exit 0
}

$iconDir = Join-Path (Split-Path -Parent $script:WincmdIni) 'FolderHeatMapIcons'
New-Item -ItemType Directory -Path $iconDir -Force | Out-Null
for ($level=1; $level -le 7; $level++) {
    Write-HeatFolderIcon (Join-Path $iconDir "heat-$level.ico") (Read-FhmColor $level)
}

# Find the first block of seven unused association slots. Existing associations are left untouched.
$start = 1
while ($start -le 993) {
    $free = $true
    for ($j=0; $j -lt 7; $j++) {
        if (Read-IniValue 'Associations' ("Filter" + ($start+$j)) '') { $free = $false; break }
    }
    if ($free) { break }
    $start++
}
if ($start -gt 993) { throw 'No free block of seven Internal Association slots was found.' }

# High heat must be tested first because the first matching internal association wins.
$slot = $start
for ($level=7; $level -ge 1; $level--) {
    $name = "FolderHeatMap Icon $level"
    Write-IniValue 'searches' ($name + '_SearchFor') ''
    Write-IniValue 'searches' ($name + '_SearchIn') ''
    Write-IniValue 'searches' ($name + '_SearchText') ''
    # Explicit Directory attribute is required by Total Commander 11.50+ for folder icons.
    Write-IniValue 'searches' ($name + '_SearchFlags') '0|002002000020||||||||22221|0000|||'
    $threshold = ([double]$level - 0.001).ToString('0.000', [Globalization.CultureInfo]::InvariantCulture)
    Write-IniValue 'searches' ($name + '_plugin') "folderheatmap.Heat > $threshold"

    $base = "Filter$slot"
    Write-IniValue 'Associations' $base (">$name")
    Write-IniValue 'Associations' ($base + '.icon') (Join-Path $iconDir "heat-$level.ico")
    $slot++
}

Write-Host "FolderHeatMap heat-colored folder icons installed."
Write-Host "wincmd.ini: $script:WincmdIni"
Write-Host "Backup:     $backup"
Write-Host "Icons:      $iconDir"
Write-Host 'Restart Total Commander to load the new Internal Associations.'
