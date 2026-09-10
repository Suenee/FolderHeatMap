$ErrorActionPreference = 'Stop'
$Version = '1.54'

function Expand-Value([string]$value) {
    if ([string]::IsNullOrWhiteSpace($value)) { return '' }
    return [Environment]::ExpandEnvironmentVariables($value.Trim('"'))
}

function Get-RegValue([string]$path,[string]$name) {
    try { return (Get-ItemProperty -LiteralPath $path -Name $name -ErrorAction Stop).$name }
    catch { return $null }
}

Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class FhmCustomColumnsIni {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern bool WritePrivateProfileString(string section,string key,string value,string fileName);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern uint GetPrivateProfileString(string section,string key,string def,StringBuilder ret,uint size,string fileName);
}
'@

function Read-Ini([string]$file,[string]$section,[string]$key,[string]$default='') {
    $buffer=[Text.StringBuilder]::new(32768)
    [void][FhmCustomColumnsIni]::GetPrivateProfileString($section,$key,$default,$buffer,[uint32]$buffer.Capacity,$file)
    return $buffer.ToString()
}

function Write-Ini([string]$file,[string]$section,[string]$key,[AllowNull()][string]$value) {
    if (-not [FhmCustomColumnsIni]::WritePrivateProfileString($section,$key,$value,$file)) {
        throw "Could not update [$section] $key in $file"
    }
}

function Resolve-TcIni {
    $ini=Expand-Value $env:COMMANDER_INI
    if ($ini -and (Test-Path -LiteralPath $ini)) { return [IO.Path]::GetFullPath($ini) }

    foreach ($key in @(
        'HKCU:\Software\Ghisler\Total Commander',
        'HKLM:\Software\Ghisler\Total Commander',
        'HKLM:\Software\Wow6432Node\Ghisler\Total Commander'
    )) {
        $value=Get-RegValue $key 'IniFileName'
        if ($value) {
            $candidate=Expand-Value $value
            if (Test-Path -LiteralPath $candidate) { return [IO.Path]::GetFullPath($candidate) }
        }
    }

    if ($env:APPDATA) {
        $candidate=Join-Path $env:APPDATA 'GHISLER\WINCMD.INI'
        if (Test-Path -LiteralPath $candidate) { return [IO.Path]::GetFullPath($candidate) }
    }
    throw 'Active Total Commander WINCMD.INI could not be located.'
}

function Stop-TcIfRunning {
    $running=@(Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue)
    if ($running.Count -eq 0) { return $false }
    $running | Stop-Process -ErrorAction SilentlyContinue
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    while ((Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue) -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 250
    }
    if (Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue) {
        Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue | Stop-Process -Force
    }
    if (Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue) {
        throw 'Total Commander could not be stopped before custom-column repair.'
    }
    return $true
}

function Resolve-TcExe {
    $path=Expand-Value $env:COMMANDER_PATH
    if ($path) {
        foreach ($name in @('TOTALCMD64.EXE','TOTALCMD.EXE')) {
            $candidate=Join-Path $path $name
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    return $null
}

$ini=Resolve-TcIni
$tcExe=Resolve-TcExe
$wasRunning=Stop-TcIfRunning
try {
    Write-Host "[TC] FolderHeatMap custom-column repair $Version"
    $rawTitles=Read-Ini $ini 'CustomFields' 'Titles'
    $titles=if ([string]::IsNullOrEmpty($rawTitles)) { @() } else { @([regex]::Split($rawTitles,'\|')) }

    # Total Commander stores the currently selected custom-column view by numeric slot
    # in [left]/[right] SpecialView. Remember its title before compaction so unrelated
    # user views keep working even if their slot number changes.
    $panelViews=@{}
    foreach ($section in @('left','right')) {
        $rawSpecial=Read-Ini $ini $section 'SpecialView' '0'
        $special=0
        if ([int]::TryParse($rawSpecial,[ref]$special) -and $special -gt 0 -and $special -le $titles.Count) {
            $activeTitle=$titles[$special-1]
            if (-not [string]::IsNullOrWhiteSpace($activeTitle)) {
                $panelViews[$section]=[pscustomobject]@{ Slot=$special; Title=$activeTitle }
            }
        }
    }

    $views=[Collections.Generic.List[object]]::new()
    for ($i=0; $i -lt $titles.Count; $i++) {
        $title=$titles[$i]
        if ([string]::IsNullOrWhiteSpace($title)) { continue }
        $slot=$i+1
        [void]$views.Add([pscustomobject]@{
            Title=$title
            Widths=Read-Ini $ini 'CustomFields' "Widths$slot"
            Headers=Read-Ini $ini 'CustomFields' "Headers$slot"
            Contents=Read-Ini $ini 'CustomFields' "Contents$slot"
            Options=Read-Ini $ini 'CustomFields' "Options$slot"
        })
    }

    $kept=[Collections.Generic.List[object]]::new()
    $seenFolderHeatMap=$false
    $removed=0
    foreach ($view in $views) {
        if ($view.Title -ieq 'FolderHeatMap') {
            if ($seenFolderHeatMap) {
                $removed++
                continue
            }
            $seenFolderHeatMap=$true
            $view.Widths='180,45,55,60,95,60,95'
            $view.Headers='Heat\nVisits\nLast Visit\nWrites\nLast Write'
            $view.Contents='[=folderheatmap.Heat]\n[=folderheatmap.Visits]\n[=folderheatmap.Last Visit]\n[=folderheatmap.Writes]\n[=folderheatmap.Last Write]'
            $view.Options='-1|0|96'
        }
        [void]$kept.Add($view)
    }

    if (-not $seenFolderHeatMap) {
        [void]$kept.Add([pscustomobject]@{
            Title='FolderHeatMap'
            Widths='180,45,55,60,95,60,95'
            Headers='Heat\nVisits\nLast Visit\nWrites\nLast Write'
            Contents='[=folderheatmap.Heat]\n[=folderheatmap.Visits]\n[=folderheatmap.Last Visit]\n[=folderheatmap.Writes]\n[=folderheatmap.Last Write]'
            Options='-1|0|96'
        })
    }

    $maxSlot=[Math]::Max($titles.Count,$kept.Count)
    for ($slot=1; $slot -le $maxSlot; $slot++) {
        foreach ($prefix in @('Widths','Headers','Contents','Options')) {
            Write-Ini $ini 'CustomFields' ($prefix+[string]$slot) $null
        }
    }

    $newTitles=@($kept | ForEach-Object { $_.Title })
    Write-Ini $ini 'CustomFields' 'Titles' ($newTitles -join '|')
    for ($i=0; $i -lt $kept.Count; $i++) {
        $slot=$i+1
        $view=$kept[$i]
        Write-Ini $ini 'CustomFields' "Widths$slot" $view.Widths
        Write-Ini $ini 'CustomFields' "Headers$slot" $view.Headers
        Write-Ini $ini 'CustomFields' "Contents$slot" $view.Contents
        Write-Ini $ini 'CustomFields' "Options$slot" $view.Options
    }

    $count=@($kept | Where-Object { $_.Title -ieq 'FolderHeatMap' }).Count
    if ($count -ne 1) { throw "Custom-column repair verification failed: FolderHeatMap view count is $count instead of 1." }

    # The FolderHeatMap custom-column view is a diagnostic/helper view only. If a panel
    # was left in that view, return it to standard Details. For any other custom view,
    # preserve the user's choice and remap its slot if compaction changed the numbering.
    foreach ($section in @('left','right')) {
        if (-not $panelViews.ContainsKey($section)) { continue }
        $state=$panelViews[$section]
        if ($state.Title -ieq 'FolderHeatMap') {
            Write-Ini $ini $section 'ViewMode' '0'
            Write-Ini $ini $section 'SpecialView' '0'
            Write-Ini $ini $section 'ShowAllDetails' '1'
            Write-Host "[TC] $section panel: FolderHeatMap helper columns disabled; standard Details view restored."
            continue
        }

        $newSlot=0
        for ($i=0; $i -lt $newTitles.Count; $i++) {
            if ($newTitles[$i] -ieq $state.Title) { $newSlot=$i+1; break }
        }
        if ($newSlot -gt 0 -and $newSlot -ne $state.Slot) {
            Write-Ini $ini $section 'SpecialView' ([string]$newSlot)
            Write-Host "[TC] $section panel: preserved custom view '$($state.Title)' after slot compaction ($($state.Slot) -> $newSlot)."
        }
    }

    if ($removed -gt 0) {
        Write-Host "[TC] Removed $removed duplicate FolderHeatMap custom-column view(s); exactly one remains."
    } else {
        Write-Host '[TC] FolderHeatMap custom-column view is unique; no duplicates found.'
    }
} finally {
    if ($wasRunning -and $tcExe -and (Test-Path -LiteralPath $tcExe)) {
        Start-Process -FilePath $tcExe | Out-Null
        Write-Host '[TC] Total Commander restarted after custom-column repair.'
    }
}