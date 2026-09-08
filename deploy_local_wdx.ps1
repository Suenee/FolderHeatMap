$ErrorActionPreference = 'Stop'
$Version = '1.54'

$Root = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$Repo = if (Test-Path -LiteralPath (Join-Path $Root '.git')) { $Root } elseif (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $Root) '.git')) { [IO.Path]::GetFullPath((Split-Path -Parent $Root)).TrimEnd('\') } else { $Root }
$LogsDir = Join-Path $Repo 'logs'
New-Item -ItemType Directory -Path $LogsDir -Force | Out-Null
$Log = Join-Path $LogsDir 'wdx_deploy.log'
$Utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($Log, '', $Utf8)

function Log([string]$Text) {
    [IO.File]::AppendAllText($Log, $Text + [Environment]::NewLine, $Utf8)
    Write-Host $Text
}
function Fail([string]$Text) { Log ('ERROR: ' + $Text); throw $Text }
function Expand-Value([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return '' }
    return [Environment]::ExpandEnvironmentVariables($Value.Trim().Trim('"'))
}
function Get-RegValue([string]$Path,[string]$Name) {
    try { return (Get-ItemProperty -LiteralPath $Path -Name $Name -ErrorAction Stop).$Name } catch { return $null }
}
function Same-Path([string]$A,[string]$B) {
    if (-not $A -or -not $B) { return $false }
    return [string]::Equals([IO.Path]::GetFullPath($A),[IO.Path]::GetFullPath($B),[StringComparison]::OrdinalIgnoreCase)
}
function Get-DriveType([string]$Path) {
    try {
        $root=[IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Path))
        if (-not $root) { return [IO.DriveType]::Unknown }
        return ([IO.DriveInfo]::new($root)).DriveType
    } catch { return [IO.DriveType]::Unknown }
}
function Test-DirectoryWritable([string]$Path) {
    try {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
        $probe=Join-Path $Path ('.fhm-write-test-' + [guid]::NewGuid().ToString('N') + '.tmp')
        [IO.File]::WriteAllText($probe,'ok',[Text.Encoding]::ASCII)
        Remove-Item -LiteralPath $probe -Force
        return $true
    } catch { return $false }
}
function Find-TC {
    $path=Expand-Value $env:COMMANDER_PATH
    $ini=Expand-Value $env:COMMANDER_INI
    $keys=@('HKCU:\Software\Ghisler\Total Commander','HKLM:\Software\Ghisler\Total Commander','HKLM:\Software\Wow6432Node\Ghisler\Total Commander')
    if (-not $path) { foreach($k in $keys){$v=Get-RegValue $k 'InstallDir'; if($v){$path=Expand-Value $v; break}} }
    if (-not $ini) { foreach($k in $keys){$v=Get-RegValue $k 'IniFileName'; if($v){$ini=Expand-Value $v; break}} }
    if (-not $ini -and $env:APPDATA) {
        $candidate=Join-Path $env:APPDATA 'GHISLER\WINCMD.INI'
        if (Test-Path -LiteralPath $candidate) { $ini=$candidate }
    }
    $exe=$null
    if ($path) { foreach($name in @('TOTALCMD64.EXE','TOTALCMD.EXE')){$candidate=Join-Path $path $name; if(Test-Path -LiteralPath $candidate){$exe=$candidate; break}} }
    if (-not $exe) {
        $running=Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue | Where-Object {$_.Path} | Select-Object -First 1
        if ($running) { $exe=$running.Path; $path=Split-Path -Parent $exe }
    }
    [pscustomobject]@{Path=$path;Ini=$ini;Exe=$exe}
}
function Resolve-RuntimeTarget([object]$Tc) {
    $tcCandidate=$null
    if ($Tc.Path) {
        $driveType=Get-DriveType $Tc.Path
        if ($driveType -ne [IO.DriveType]::Network -and $driveType -ne [IO.DriveType]::Unknown) {
            $tcCandidate=Join-Path $Tc.Path 'Plugins\wdx\FolderHeatMap'
            if (Test-DirectoryWritable $tcCandidate) {
                return [pscustomobject]@{Directory=[IO.Path]::GetFullPath($tcCandidate);Reason='local writable Total Commander plugin directory'}
            }
            Log "[WDX] Total Commander plugin directory is not writable without elevation: $tcCandidate"
        } else {
            Log "[WDX] Total Commander itself is on a non-local drive; its plugin directory will not be used as FolderHeatMap runtime: $($Tc.Path)"
        }
    }
    if (-not $env:LOCALAPPDATA) { Fail 'LOCALAPPDATA is unavailable and no safe writable local Total Commander plugin directory was found.' }
    $fallback=Join-Path $env:LOCALAPPDATA 'FolderHeatMap\Plugins\wdx'
    if (-not (Test-DirectoryWritable $fallback)) { Fail "Stable local FolderHeatMap runtime directory is not writable: $fallback" }
    return [pscustomobject]@{Directory=[IO.Path]::GetFullPath($fallback);Reason='LOCALAPPDATA fallback'}
}
function Stop-TC {
    $running=@(Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue)
    if ($running.Count -eq 0) { return $false }
    Log ('[TC] Stopping Total Commander before WDX runtime deployment. PID(s): ' + (($running | ForEach-Object {$_.Id}) -join ','))
    $running | Stop-Process -ErrorAction SilentlyContinue
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    while ((Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 250 }
    if (Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue) { Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue | Stop-Process -Force }
    if (Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue) { Fail 'Total Commander could not be stopped safely.' }
    return $true
}
function Cleanup-DiagnosticRuntime {
    $oldDir='D:\Temp\FolderHeatMap'
    $oldFile=Join-Path $oldDir 'FolderHeatMap.wdx64'
    if (Test-Path -LiteralPath $oldFile) {
        Remove-Item -LiteralPath $oldFile -Force
        Log "[CLEANUP] Removed diagnostic WDX: $oldFile"
    }
    if (Test-Path -LiteralPath $oldDir) {
        $remaining=@(Get-ChildItem -LiteralPath $oldDir -Force -ErrorAction SilentlyContinue)
        if ($remaining.Count -eq 0) {
            Remove-Item -LiteralPath $oldDir -Force
            Log "[CLEANUP] Removed empty diagnostic directory: $oldDir"
        } else {
            Log "[CLEANUP] Diagnostic directory contains other files and was preserved: $oldDir"
        }
    }
}

Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class FhmLocalWdxIni {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern bool WritePrivateProfileString(string section,string key,string value,string fileName);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern uint GetPrivateProfileString(string section,string key,string def,StringBuilder ret,uint size,string fileName);
}
'@
function Read-Ini([string]$File,[string]$Section,[string]$Key,[string]$Default='') {
    $buffer=[Text.StringBuilder]::new(32768)
    [void][FhmLocalWdxIni]::GetPrivateProfileString($Section,$Key,$Default,$buffer,[uint32]$buffer.Capacity,$File)
    return $buffer.ToString()
}
function Write-Ini([string]$File,[string]$Section,[string]$Key,[AllowNull()][string]$Value) {
    if (-not [FhmLocalWdxIni]::WritePrivateProfileString($Section,$Key,$Value,$File)) { Fail "Could not update [$Section] $Key in $File" }
}

try {
    Log "FolderHeatMap stable local WDX deployment $Version"
    $sourceCandidates=@(
        (Join-Path $Root 'dist\FolderHeatMap.wdx64'),
        (Join-Path $Root 'FolderHeatMap.wdx64'),
        (Join-Path $Repo 'dist\FolderHeatMap.wdx64')
    )
    $source=$sourceCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $source) { Fail 'FolderHeatMap.wdx64 source was not found in dist or beside the helper.' }
    $source=[IO.Path]::GetFullPath($source)

    $tc=Find-TC
    if (-not $tc.Ini -or -not (Test-Path -LiteralPath $tc.Ini)) { Fail 'Active Total Commander WINCMD.INI could not be located.' }
    $ini=[IO.Path]::GetFullPath($tc.Ini)
    $runtime=Resolve-RuntimeTarget $tc
    $targetDir=$runtime.Directory
    $target=Join-Path $targetDir 'FolderHeatMap.wdx64'
    $wasRunning=Stop-TC

    Log "[WDX] Source: $source"
    Log "[WDX] Runtime target: $target"
    Log "[WDX] Runtime selection: $($runtime.Reason)"
    Copy-Item -LiteralPath $source -Destination $target -Force
    if (-not (Test-Path -LiteralPath $target)) { Fail "Local WDX copy was not created: $target" }
    $sourceHash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    $targetHash=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if ($sourceHash -ne $targetHash) { Fail 'Local WDX copy hash does not match the deployed dist WDX.' }
    Log "[WDX] SHA256 verified: $targetHash"

    $slot=$null
    $free=$null
    for($i=0;$i -le 999;$i++) {
        $key=[string]$i
        $value=Read-Ini $ini 'ContentPlugins' $key
        if (-not $value) { if ($null -eq $free) { $free=$key }; continue }
        $expanded=Expand-Value $value
        if ([IO.Path]::GetFileName($expanded) -ieq 'FolderHeatMap.wdx64') { $slot=$key; break }
    }
    if ($null -eq $slot) {
        if ($null -eq $free) { Fail 'No free [ContentPlugins] slot was found.' }
        $slot=$free
    }

    $backup="$ini.fhm-before-wdx-runtime-$(Get-Date -Format 'yyyyMMdd-HHmmss').bak"
    Copy-Item -LiteralPath $ini -Destination $backup -Force
    Log "[TC] Backup: $backup"
    Write-Ini $ini 'ContentPlugins' $slot $target
    Write-Ini $ini 'ContentPlugins64' $slot '1'

    $verifyPath=Expand-Value (Read-Ini $ini 'ContentPlugins' $slot)
    $verify64=Read-Ini $ini 'ContentPlugins64' $slot
    if (-not (Same-Path $verifyPath $target) -or $verify64 -ne '1') { Fail 'Total Commander WDX registration verification failed after local deployment.' }
    Log "[TC] FolderHeatMap WDX registered at stable local runtime: [ContentPlugins] $slot=$target"
    Log "[TC] 64-bit registration verified: [ContentPlugins64] $slot=1"

    Cleanup-DiagnosticRuntime

    if ($wasRunning) {
        if (-not $tc.Exe -or -not (Test-Path -LiteralPath $tc.Exe)) { Fail 'Total Commander was running, but its executable could not be resolved for restart.' }
        Start-Process -FilePath $tc.Exe | Out-Null
        Log '[TC] Total Commander restarted with the stable local WDX registration.'
    }

    Log 'STATUS: SUCCESS'
    exit 0
} catch {
    if ($_.Exception.Message -notlike 'ERROR:*') { Log ('STATUS: FAILED - ' + $_.Exception.Message) }
    exit 1
}
