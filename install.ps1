$ErrorActionPreference = 'Stop'
$Version = '1.02'
$Repo = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$LogsDir = Join-Path $Repo 'logs'
New-Item -ItemType Directory -Path $LogsDir -Force | Out-Null
$Log = Join-Path $LogsDir 'install.log'
$Utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($Log, '', $Utf8)

function Log([string]$text) { [IO.File]::AppendAllText($Log, $text + [Environment]::NewLine, $Utf8); Write-Host $text }
function Warn([string]$text) { Log ('WARNING: ' + $text) }
function Fail([string]$text) { Log ('ERROR: ' + $text); throw $text }
function Expand-Value([string]$value) { if ([string]::IsNullOrWhiteSpace($value)) { return '' }; return [Environment]::ExpandEnvironmentVariables($value.Trim('"')) }
function Get-RegValue([string]$path,[string]$name) { try { return (Get-ItemProperty -LiteralPath $path -Name $name -ErrorAction Stop).$name } catch { return $null } }
function Same-Path([string]$a,[string]$b) { if (-not $a -or -not $b) { return $false }; return [string]::Equals([IO.Path]::GetFullPath($a),[IO.Path]::GetFullPath($b),[StringComparison]::OrdinalIgnoreCase) }

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

function Get-TcInstalledVersion([object]$tc) {
    if (-not $tc.Exe -or -not (Test-Path -LiteralPath $tc.Exe)) { return $null }
    try {
        $raw=(Get-Item -LiteralPath $tc.Exe).VersionInfo.ProductVersion
        if (-not $raw) { $raw=(Get-Item -LiteralPath $tc.Exe).VersionInfo.FileVersion }
        if ($raw -match '(\d+\.\d+(?:\.\d+)*)') { return [version]$matches[1] }
    } catch {}
    return $null
}

function Get-LatestTcRelease {
    $url='https://www.ghisler.com/amazons3.php'
    try {
        $response=Invoke-WebRequest -UseBasicParsing -Uri $url -TimeoutSec 15
        $html=[string]$response.Content
        $m=[regex]::Match($html,'(?i)Download\s+Total\s+Commander\s+(\d+\.\d+(?:\.\d+)*)\s+final')
        if (-not $m.Success) { throw 'latest stable version could not be parsed from the official download page' }
        $versionText=$m.Groups[1].Value
        $link=[regex]::Match($html,'(?i)href=["'']([^"'']*tcmd[^"'']*x64\.exe)["'']')
        $download=$null
        if ($link.Success) {
            $download=$link.Groups[1].Value
            if ($download -notmatch '^https?://') { $download=[Uri]::new([Uri]$url,$download).AbsoluteUri }
        } else {
            $compact=$versionText.Replace('.','')
            $download="https://totalcommander.ch/$compact/tcmd${compact}x64.exe"
        }
        return [pscustomobject]@{Version=[version]$versionText;VersionText=$versionText;DownloadUrl=$download;SourceUrl=$url}
    } catch {
        Warn "Could not verify the latest Total Commander version from the official Ghisler download page: $($_.Exception.Message). FolderHeatMap installation will continue."
        return $null
    }
}

function Offer-TcUpdate([object]$tc) {
    $installed=Get-TcInstalledVersion $tc
    if (-not $installed) { Warn 'Installed Total Commander version could not be determined; online update check skipped.'; return $tc }
    $latest=Get-LatestTcRelease
    if (-not $latest) { return $tc }
    Log "[TC] Installed version: $installed"
    Log "[TC] Latest stable version: $($latest.VersionText) (official Ghisler download page)"
    if ($installed -ge $latest.Version) { Log '[TC] Total Commander is up to date.'; return $tc }

    Write-Host ''
    Write-Host "A newer stable Total Commander is available: $installed -> $($latest.VersionText)" -ForegroundColor Yellow
    $answer=Read-Host 'Upgrade Total Commander now? [Y/N]'
    if ($answer -notmatch '^(?i)y(?:es)?$') { Log '[TC] Total Commander update declined; continuing FolderHeatMap installation.'; return $tc }

    $wasRunning=@(Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue).Count -gt 0
    if ($wasRunning) { [void](Stop-TC $tc) }
    $installer=Join-Path $env:TEMP ("FolderHeatMap-tcmd-$($latest.VersionText)-x64.exe")
    Log "[TC] Downloading official Total Commander $($latest.VersionText) installer..."
    try { Invoke-WebRequest -UseBasicParsing -Uri $latest.DownloadUrl -OutFile $installer -TimeoutSec 120 }
    catch { Fail "Could not download the official Total Commander installer: $($_.Exception.Message)" }
    if (-not (Test-Path -LiteralPath $installer)) { Fail 'Total Commander installer download did not produce a file.' }
    $signature=Get-AuthenticodeSignature -FilePath $installer
    if ($signature.Status -ne 'Valid') { Remove-Item -LiteralPath $installer -Force -ErrorAction SilentlyContinue; Fail "Downloaded Total Commander installer has invalid Authenticode signature: $($signature.Status)." }
    Log "[TC] Installer signature valid: $($signature.SignerCertificate.Subject)"
    Log '[TC] Starting Total Commander upgrade installer. Existing Total Commander configuration is preserved by the official installer.'
    $process=Start-Process -FilePath $installer -Wait -PassThru
    $rc=$process.ExitCode
    Remove-Item -LiteralPath $installer -Force -ErrorAction SilentlyContinue
    if ($rc -ne 0) { Fail "Total Commander installer returned exit code $rc." }
    $refreshed=Find-TC
    $after=Get-TcInstalledVersion $refreshed
    if (-not $after) { Fail 'Total Commander upgrade completed, but the installed version could not be verified.' }
    if ($after -lt $latest.Version) { Fail "Total Commander upgrade completed, but version $after is still older than $($latest.VersionText)." }
    Log "[TC] Total Commander upgraded successfully to $after."
    if ($wasRunning) { $script:TcWasRunningBeforeInstall=$true }
    return $refreshed
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
function Read-Ini([string]$file,[string]$section,[string]$key,[string]$default='') { $b=[Text.StringBuilder]::new(32768); [void][FhmInstallIni]::GetPrivateProfileString($section,$key,$default,$b,[uint32]$b.Capacity,$file); $b.ToString() }
function Write-Ini([string]$file,[string]$section,[string]$key,[AllowNull()][string]$value) { if (-not [FhmInstallIni]::WritePrivateProfileString($section,$key,$value,$file)) { Fail "Could not update [$section] $key in $file" } }

function Stop-TC([object]$tc) {
    $running=@(Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue)
    if ($running.Count -eq 0) { return $false }
    if (-not $tc.Exe) { Fail 'Total Commander is running, but its executable path could not be resolved for restart.' }
    Log '[TC] Total Commander is running; stopping it before configuration repair.'
    $running | Stop-Process -ErrorAction SilentlyContinue
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    while ((Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 250 }
    if (Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue) { Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue | Stop-Process -Force }
    if (Get-Process TOTALCMD64,TOTALCMD -ErrorAction SilentlyContinue) { Fail 'Total Commander could not be stopped for configuration repair.' }
    return $true
}

function Ensure-WdxRegistration([string]$ini,[string]$wdx) {
    $foundKey=$null; $foundPath=$null; $freeKey=$null
    for ($i=0; $i -le 999; $i++) {
        $key=[string]$i; $value=Read-Ini $ini 'ContentPlugins' $key
        if (-not $value) { if ($null -eq $freeKey) { $freeKey=$key }; continue }
        $expanded=Expand-Value $value
        if ([IO.Path]::GetFileName($expanded) -ieq 'FolderHeatMap.wdx64') { $foundKey=$key; $foundPath=$expanded; break }
    }
    if ($null -ne $foundKey) {
        if (Same-Path $foundPath $wdx) { Log "[TC] FolderHeatMap WDX already points to dist in [ContentPlugins] $foundKey."; return $false }
        Write-Ini $ini 'ContentPlugins' $foundKey $wdx
        Log "[TC] Repaired FolderHeatMap WDX registration: $foundKey=$wdx"
        return $true
    }
    if ($null -eq $freeKey) { Fail 'No free [ContentPlugins] slot (0..999) was found.' }
    Write-Ini $ini 'ContentPlugins' $freeKey $wdx
    Log "[TC] Registered FolderHeatMap WDX: $freeKey=$wdx"
    return $true
}

function Ensure-CustomColumns([string]$ini) {
    $title='FolderHeatMap'
    $rawTitles=Read-Ini $ini 'CustomFields' 'Titles'
    $titles=if ([string]::IsNullOrEmpty($rawTitles)) { @() } else { @($rawTitles -split '\|',-1) }
    $slot=0
    for ($i=0;$i -lt $titles.Count;$i++) { if ($titles[$i] -eq $title) { $slot=$i+1; break } }
    if ($slot -eq 0) {
        $maxSlot=0
        for ($i=1;$i -le 999;$i++) {
            if ((Read-Ini $ini 'CustomFields' "Widths$i") -or (Read-Ini $ini 'CustomFields' "Headers$i") -or (Read-Ini $ini 'CustomFields' "Contents$i")) { $maxSlot=$i }
        }
        $slot=[Math]::Max($maxSlot,$titles.Count)+1
        if ($slot -gt 999) { Fail 'No free Total Commander custom-column view slot was found.' }
        $list=[Collections.Generic.List[string]]::new(); foreach($t in $titles){[void]$list.Add($t)}
        while ($list.Count -lt ($slot-1)) { [void]$list.Add('') }
        [void]$list.Add($title)
        Write-Ini $ini 'CustomFields' 'Titles' ($list -join '|')
        Log "[TC] Added custom-column view title '$title' as view $slot."
    }
    $widths='180,45,55,60,95,60,95'
    $headers='Heat\nVisits\nLast Visit\nWrites\nLast Write'
    $contents='[=folderheatmap.Heat]\n[=folderheatmap.Visits]\n[=folderheatmap.Last Visit]\n[=folderheatmap.Writes]\n[=folderheatmap.Last Write]'
    $options='-1|0|96'
    $changed=$false
    foreach($pair in @(@("Widths$slot",$widths),@("Headers$slot",$headers),@("Contents$slot",$contents),@("Options$slot",$options))) {
        if ((Read-Ini $ini 'CustomFields' $pair[0]) -ne $pair[1]) { Write-Ini $ini 'CustomFields' $pair[0] $pair[1]; $changed=$true }
    }
    if ($changed) { Log "[TC] Created/repaired FolderHeatMap custom-column view $slot (Heat, Visits, Last Visit, Writes, Last Write)." }
    else { Log "[TC] FolderHeatMap custom-column view $slot is already correct." }
    return $changed
}

function Color-Component([uint32]$c,[int]$shift) { return [int](($c -shr $shift) -band 0xff) }
function Interpolate-Color([uint32]$a,[uint32]$b,[double]$t) {
    $r=[Math]::Max(0,[Math]::Min(255,[int][Math]::Round((Color-Component $a 0)+((Color-Component $b 0)-(Color-Component $a 0))*$t)))
    $g=[Math]::Max(0,[Math]::Min(255,[int][Math]::Round((Color-Component $a 8)+((Color-Component $b 8)-(Color-Component $a 8))*$t)))
    $bl=[Math]::Max(0,[Math]::Min(255,[int][Math]::Round((Color-Component $a 16)+((Color-Component $b 16)-(Color-Component $a 16))*$t)))
    return [uint32]($r -bor ($g -shl 8) -bor ($bl -shl 16))
}
function Read-FhmColor([string]$settings,[int]$level) {
    $defaults=@(0,7915600,5954690,4645320,4312565,3644410,3955445,7882485)
    if (-not (Test-Path -LiteralPath $settings)) { return [uint32]$defaults[$level] }
    $raw=Read-Ini $settings 'Colors' "Color$level" ([string]$defaults[$level]); $parsed=0L
    if ([Int64]::TryParse($raw,[ref]$parsed)) { return [uint32]$parsed }
    return [uint32]$defaults[$level]
}
function Managed-SearchName([int]$index) { return ('FolderHeatMap Heat {0:000}' -f $index) }
function Install-ColorRules([string]$ini,[string]$settings) {
    $existing=@()
    for($i=1;$i -le 999;$i++) {
        $base="ColorFilter$i"; $filter=Read-Ini $ini 'Colors' $base
        if ($filter -and $filter -notlike '>FolderHeatMap Heat *') { $existing += [pscustomobject]@{Filter=$filter;Color=(Read-Ini $ini 'Colors' ($base+'Color'));Dark=(Read-Ini $ini 'Colors' ($base+'ColorDark'))} }
    }
    for($i=1;$i -le 999;$i++) { $base="ColorFilter$i"; Write-Ini $ini 'Colors' $base $null; Write-Ini $ini 'Colors' ($base+'Color') $null; Write-Ini $ini 'Colors' ($base+'ColorDark') $null }
    for($i=1;$i -le 128;$i++) { $name=Managed-SearchName $i; foreach($suffix in @('_SearchFor','_SearchIn','_SearchText','_SearchFlags','_plugin')) { Write-Ini $ini 'searches' ($name+$suffix) $null } }
    $smooth=1; $steps=4
    if (Test-Path -LiteralPath $settings) { [void][int]::TryParse((Read-Ini $settings 'Colors' 'Smooth' '1'),[ref]$smooth); [void][int]::TryParse((Read-Ini $settings 'Colors' 'StepsPerLevel' '4'),[ref]$steps) }
    if ($smooth -eq 0) { $steps=1 } else { $steps=[Math]::Max(1,[Math]::Min(16,$steps)) }
    $colors=@(0); for($level=1;$level -le 7;$level++){ $colors += (Read-FhmColor $settings $level) }
    $rule=1; $managed=0; $epsilon=0.001
    $entries=@()
    $entries += [pscustomobject]@{Threshold=7.0-$epsilon;Color=$colors[7]}
    for($level=6;$level -ge 1;$level--) {
        for($s=$steps-1;$s -ge 0;$s--) { $pos=[double]$s/$steps; $entries += [pscustomobject]@{Threshold=$level+$pos-$epsilon;Color=(Interpolate-Color $colors[$level] $colors[$level+1] $pos)} }
    }
    foreach($entry in $entries) {
        $managed++; $name=Managed-SearchName $managed
        Write-Ini $ini 'searches' ($name+'_SearchFor') ''
        Write-Ini $ini 'searches' ($name+'_SearchIn') ''
        Write-Ini $ini 'searches' ($name+'_SearchText') ''
        Write-Ini $ini 'searches' ($name+'_SearchFlags') '0|002002000020|||||||||0000|||'
        Write-Ini $ini 'searches' ($name+'_plugin') ('folderheatmap.Heat > '+$entry.Threshold.ToString('0.000',[Globalization.CultureInfo]::InvariantCulture))
        Write-Ini $ini 'Colors' "ColorFilter$rule" ('>'+$name)
        Write-Ini $ini 'Colors' "ColorFilter${rule}Color" ([string][uint32]$entry.Color)
        $rule++
    }
    foreach($old in $existing) {
        if($rule -gt 999){break}; Write-Ini $ini 'Colors' "ColorFilter$rule" $old.Filter
        if($old.Color){Write-Ini $ini 'Colors' "ColorFilter${rule}Color" $old.Color}; if($old.Dark){Write-Ini $ini 'Colors' "ColorFilter${rule}ColorDark" $old.Dark}; $rule++
    }
    Write-Ini $ini 'FolderHeatMap' 'ManagedColorRuleCount' ([string]$managed); Write-Ini $ini 'FolderHeatMap' 'ManagedColorRuleStart' '1'
    Log "[TC] Installed FolderHeatMap text-color integration ($managed managed heat rules)."
    return $true
}

try {
    Log "FolderHeatMap installer $Version"
    $script:TcWasRunningBeforeInstall=$false
    $tc=Find-TC
    if (-not $tc.Ini -or -not (Test-Path -LiteralPath $tc.Ini)) { Fail 'Active Total Commander WINCMD.INI could not be located.' }
    if (-not $tc.Exe) { Warn 'Total Commander executable could not be located; version check and automatic update are unavailable.' }
    else { $tc=Offer-TcUpdate $tc }
    $ini=(Resolve-Path -LiteralPath $tc.Ini).Path
    $dist=Join-Path $Repo 'dist'
    $wdx=Join-Path $dist 'FolderHeatMap.wdx64'
    if (-not (Test-Path -LiteralPath $wdx)) { Fail "Stable deployed WDX was not found at '$wdx'. Run upgrade.cmd first." }
    $wdx=[IO.Path]::GetFullPath($wdx)
    $settings=Join-Path (Split-Path -Parent $ini) 'FolderHeatMap.ini'
    $setupIcons=Join-Path $Repo 'setup_icons.ps1'
    if (-not (Test-Path -LiteralPath $setupIcons)) { Fail 'setup_icons.ps1 is missing; folder icon integration cannot be installed.' }

    Log "[TC] Configuration: $ini"
    Log "[FHM] Stable WDX:    $wdx"
    $runningAtRepair=Stop-TC $tc
    $tcWasRunning=$script:TcWasRunningBeforeInstall -or $runningAtRepair

    $backup="$ini.fhm-install-$(Get-Date -Format 'yyyyMMdd-HHmmss').bak"
    Copy-Item -LiteralPath $ini -Destination $backup -Force
    Log "[TC] Backup:        $backup"

    [void](Ensure-WdxRegistration $ini $wdx)
    [void](Ensure-CustomColumns $ini)
    [void](Install-ColorRules $ini $settings)

    Log '[TC] Installing FolderHeatMap heat-colored folder icons and Internal Associations...'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $setupIcons 2>&1 | ForEach-Object { Log ([string]$_) }
    if ($LASTEXITCODE -ne 0) { Fail "Folder icon integration failed with exit code $LASTEXITCODE." }
    Log '[TC] Folder icon integration installed.'

    if ($tcWasRunning) { Start-Process -FilePath $tc.Exe; Log '[TC] Total Commander restarted once after complete integration repair.' }
    else { Log '[TC] Total Commander was not running; the repaired integration will load on next start.' }
    Log 'STATUS: SUCCESS'
    exit 0
} catch {
    if ($_.Exception.Message -notlike 'ERROR:*') { Log ('STATUS: FAILED - ' + $_.Exception.Message) }
    exit 1
}
