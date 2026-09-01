$ErrorActionPreference = 'Stop'
$Version = '1.00'
$Repo = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$LogsDir = Join-Path $Repo 'logs'
New-Item -ItemType Directory -Path $LogsDir -Force | Out-Null
$Log = Join-Path $LogsDir 'install.log'
$Utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($Log, '', $Utf8)

function Log([string]$text) { [IO.File]::AppendAllText($Log, $text + [Environment]::NewLine, $Utf8); Write-Host $text }
function Fail([string]$text) { Log ('ERROR: ' + $text); throw $text }
function Expand-Value([string]$value) { if ([string]::IsNullOrWhiteSpace($value)) { return '' }; return [Environment]::ExpandEnvironmentVariables($value.Trim('"')) }
function Get-RegValue([string]$path,[string]$name) { try { return (Get-ItemProperty -LiteralPath $path -Name $name -ErrorAction Stop).$name } catch { return $null } }

function Find-TC {
    $path = Expand-Value $env:COMMANDER_PATH
    $ini = Expand-Value $env:COMMANDER_INI
    $keys = @('HKCU:\Software\Ghisler\Total Commander','HKLM:\Software\Ghisler\Total Commander','HKLM:\Software\Wow6432Node\Ghisler\Total Commander')
    if (-not $path) { foreach ($k in $keys) { $v=Get-RegValue $k 'InstallDir'; if ($v) { $path=Expand-Value $v; break } } }
    if (-not $ini) { foreach ($k in $keys) { $v=Get-RegValue $k 'IniFileName'; if ($v) { $ini=Expand-Value $v; break } } }
    if (-not $ini) { $candidate=Join-Path $env:APPDATA 'GHISLER\WINCMD.INI'; if (Test-Path -LiteralPath $candidate) { $ini=$candidate } }
    $exe=$null
    if ($path) { foreach ($n in @('TOTALCMD64.EXE','TOTALCMD.EXE')) { $p=Join-Path $path $n; if (Test-Path -LiteralPath $p) { $exe=$p; break } } }
    [pscustomobject]@{Path=$path;Ini=$ini;Exe=$exe}
}

Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class FhmInstallIni {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern bool WritePrivateProfileString(string section,string key,string value,string fileName);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern uint GetPrivateProfileString(string section,string key,string def,StringBuilder ret,uint size,string fileName);
}
'@
function Read-Ini([string]$file,[string]$section,[string]$key) { $b=[Text.StringBuilder]::new(32768); [void][FhmInstallIni]::GetPrivateProfileString($section,$key,'',$b,[uint32]$b.Capacity,$file); $b.ToString() }
function Write-Ini([string]$file,[string]$section,[string]$key,[string]$value) { if (-not [FhmInstallIni]::WritePrivateProfileString($section,$key,$value,$file)) { Fail "Could not update [$section] $key in $file" } }

try {
    Log "FolderHeatMap installer $Version"
    $tc=Find-TC
    if (-not $tc.Ini -or -not (Test-Path -LiteralPath $tc.Ini)) { Fail 'Active Total Commander WINCMD.INI could not be located.' }
    $ini=(Resolve-Path -LiteralPath $tc.Ini).Path
    $wdx=Join-Path $Repo 'FolderHeatMap.wdx64'
    if (-not (Test-Path -LiteralPath $wdx)) {
        $candidates=@(Get-ChildItem -LiteralPath $Repo -Filter 'FolderHeatMap.wdx64' -File -Recurse -ErrorAction SilentlyContinue | Sort-Object FullName)
        if ($candidates.Count -eq 0) { Fail 'FolderHeatMap.wdx64 was not found. Run upgrade.cmd first.' }
        $wdx=$candidates[0].FullName
    }
    $wdx=[IO.Path]::GetFullPath($wdx)
    Log "[TC] Configuration: $ini"
    Log "[FHM] WDX:          $wdx"

    $foundKey=$null; $foundPath=$null; $freeKey=$null
    for ($i=0; $i -le 999; $i++) {
        $key=[string]$i; $value=Read-Ini $ini 'ContentPlugins' $key
        if (-not $value) { if ($null -eq $freeKey) { $freeKey=$key }; continue }
        $expanded=Expand-Value $value
        if ([IO.Path]::GetFileName($expanded) -ieq 'FolderHeatMap.wdx64') { $foundKey=$key; $foundPath=$expanded; break }
    }
    $changed=$false
    if ($null -ne $foundKey) {
        if ([string]::Equals([IO.Path]::GetFullPath($foundPath),$wdx,[StringComparison]::OrdinalIgnoreCase)) { Log "[TC] FolderHeatMap WDX is already registered in [ContentPlugins] $foundKey." }
        else {
            $backup="$ini.fhm-install-$(Get-Date -Format 'yyyyMMdd-HHmmss').bak"; Copy-Item -LiteralPath $ini -Destination $backup -Force
            Write-Ini $ini 'ContentPlugins' $foundKey $wdx; $changed=$true
            Log "[TC] Updated FolderHeatMap WDX registration: $foundKey=$wdx"; Log "[TC] Backup: $backup"
        }
    } else {
        if ($null -eq $freeKey) { Fail 'No free [ContentPlugins] slot (0..999) was found.' }
        $backup="$ini.fhm-install-$(Get-Date -Format 'yyyyMMdd-HHmmss').bak"; Copy-Item -LiteralPath $ini -Destination $backup -Force
        Write-Ini $ini 'ContentPlugins' $freeKey $wdx; $changed=$true
        Log "[TC] Registered FolderHeatMap WDX: $freeKey=$wdx"; Log "[TC] Backup: $backup"
    }

    $tcRunning=@(Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue)
    if ($changed -and $tcRunning.Count -gt 0) {
        if (-not $tc.Exe) { Fail 'Total Commander is running, but its executable path could not be resolved for restart.' }
        Log '[TC] Registration changed while Total Commander is running; forcing configuration reload by restart.'
        $tcRunning | Stop-Process -ErrorAction SilentlyContinue
        $deadline=[DateTime]::UtcNow.AddSeconds(15)
        while ((Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 250 }
        if (Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue) { Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue | Stop-Process -Force }
        Start-Sleep -Milliseconds 500
        Start-Process -FilePath $tc.Exe
        Log '[TC] Total Commander restarted.'
    } elseif ($changed) { Log '[TC] Registration changed. Total Commander will load it on next start.' }
    else { Log '[TC] No configuration change was necessary.' }
    Log 'STATUS: SUCCESS'
    exit 0
} catch {
    if ($_.Exception.Message -notlike 'ERROR:*') { Log ('STATUS: FAILED - ' + $_.Exception.Message) }
    exit 1
}
