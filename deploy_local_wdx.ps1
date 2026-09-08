$ErrorActionPreference = 'Stop'
$Version = '1.00'

$Root = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$Repo = if (Test-Path -LiteralPath (Join-Path $Root '.git')) { $Root } elseif (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $Root) '.git')) { [IO.Path]::GetFullPath((Split-Path -Parent $Root)).TrimEnd('\') } else { $Root }
$LogsDir = Join-Path $Repo 'logs'
New-Item -ItemType Directory -Path $LogsDir -Force | Out-Null
$Log = Join-Path $LogsDir 'local_wdx_test.log'
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
function Same-Path([string]$A,[string]$B) {
    if (-not $A -or -not $B) { return $false }
    return [string]::Equals([IO.Path]::GetFullPath($A),[IO.Path]::GetFullPath($B),[StringComparison]::OrdinalIgnoreCase)
}
function Find-TC {
    $path=Expand-Value $env:COMMANDER_PATH
    $ini=Expand-Value $env:COMMANDER_INI
    $keys=@('HKCU:\Software\Ghisler\Total Commander','HKLM:\Software\Ghisler\Total Commander','HKLM:\Software\Wow6432Node\Ghisler\Total Commander')
    if (-not $path) { foreach($k in $keys){$v=Get-RegValue $k 'InstallDir'; if($v){$path=Expand-Value $v; break}} }
    if (-not $ini) { foreach($k in $keys){$v=Get-RegValue $k 'IniFileName'; if($v){$ini=Expand-Value $v; break}} }
    if (-not $ini) {
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
function Stop-TC {
    $running=@(Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue)
    if ($running.Count -eq 0) { return $false }
    Log ('[TC] Stopping Total Commander before local WDX test deployment. PID(s): ' + (($running | ForEach-Object {$_.Id}) -join ','))
    $running | Stop-Process -ErrorAction SilentlyContinue
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    while ((Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 250 }
    if (Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue) { Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue | Stop-Process -Force }
    if (Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue) { Fail 'Total Commander could not be stopped safely.' }
    return $true
}

try {
    Log "FolderHeatMap local WDX deployment test $Version"

    $sourceCandidates=@(
        (Join-Path $Root 'dist\FolderHeatMap.wdx64'),
        (Join-Path $Root 'FolderHeatMap.wdx64'),
        (Join-Path $Repo 'dist\FolderHeatMap.wdx64')
    )
    $source=$sourceCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $source) { Fail 'FolderHeatMap.wdx64 source was not found in dist or beside the helper.' }
    $source=[IO.Path]::GetFullPath($source)

    if (-not (Test-Path -LiteralPath 'D:\')) { Fail 'D: drive is not available; the requested D:\Temp local WDX test cannot be performed.' }
    $targetDir='D:\Temp\FolderHeatMap'
    $target=Join-Path $targetDir 'FolderHeatMap.wdx64'
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null

    $tc=Find-TC
    if (-not $tc.Ini -or -not (Test-Path -LiteralPath $tc.Ini)) { Fail 'Active Total Commander WINCMD.INI could not be located.' }
    $ini=[IO.Path]::GetFullPath($tc.Ini)
    $wasRunning=Stop-TC

    Log "[WDX] Source: $source"
    Log "[WDX] Local test target: $target"
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

    $backup="$ini.fhm-before-local-wdx-test-$(Get-Date -Format 'yyyyMMdd-HHmmss').bak"
    Copy-Item -LiteralPath $ini -Destination $backup -Force
    Log "[TC] Backup: $backup"

    Write-Ini $ini 'ContentPlugins' $slot $target
    Write-Ini $ini 'ContentPlugins64' $slot '1'

    $verifyPath=Expand-Value (Read-Ini $ini 'ContentPlugins' $slot)
    $verify64=Read-Ini $ini 'ContentPlugins64' $slot
    if (-not (Same-Path $verifyPath $target) -or $verify64 -ne '1') { Fail 'Total Commander WDX registration verification failed after local deployment.' }
    Log "[TC] FolderHeatMap WDX redirected to local test path: [ContentPlugins] $slot=$target"
    Log "[TC] 64-bit registration verified: [ContentPlugins64] $slot=1"

    if ($wasRunning) {
        if (-not $tc.Exe -or -not (Test-Path -LiteralPath $tc.Exe)) { Fail 'Total Commander was running, but its executable could not be resolved for restart.' }
        Start-Process -FilePath $tc.Exe | Out-Null
        Log '[TC] Total Commander restarted with the local WDX registration.'
    }

    Log 'STATUS: SUCCESS'
    exit 0
} catch {
    if ($_.Exception.Message -notlike 'ERROR:*') { Log ('STATUS: FAILED - ' + $_.Exception.Message) }
    exit 1
}
