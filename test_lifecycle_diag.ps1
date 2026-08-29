$ErrorActionPreference = 'Stop'

$TestVersion = '1.48'
$Workspace = 'D:\Temp\FHM'
$ReleasePath = 'D:\Temp'
$Repo = Split-Path -Parent $MyInvocation.MyCommand.Path
$Repo = [IO.Path]::GetFullPath($Repo).TrimEnd('\')
$EngineLog = Join-Path $Repo 'logs\FolderHeatMap.log'
$RunId = [DateTime]::Now.ToString('yyyyMMdd-HHmmss')
$LogDir = Join-Path $Workspace 'logs'
$LogPath = Join-Path $LogDir ("diagnostic-$RunId.log")
$PassCount = 0
$ErrorCount = 0
$WarningCount = 0
$Utf8 = [Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $Utf8
$OutputEncoding = $Utf8
try { & chcp.com 65001 *> $null } catch { }

function Write-LogLine {
    param([string]$Text, [ConsoleColor]$Color = [ConsoleColor]::Gray)
    if (-not (Test-Path -LiteralPath $LogDir)) { New-Item -ItemType Directory -Path $LogDir -Force | Out-Null }
    [IO.File]::AppendAllText($LogPath, $Text + [Environment]::NewLine, $Utf8)
    Write-Host $Text -ForegroundColor $Color
}
function Info([string]$Text) { Write-LogLine $Text Gray }
function Pass([string]$Text) { $script:PassCount++; Write-LogLine ('[PASS] ' + $Text) Green }
function ErrorResult([string]$Text) { $script:ErrorCount++; Write-LogLine ('[ERROR] ' + $Text) Red }
function Warn([string]$Text) { $script:WarningCount++; Write-LogLine ('[WARNING] ' + $Text) Yellow }
function TestHeader([string]$Text) { Write-LogLine ''; Write-LogLine ('[TEST] ' + $Text) Cyan }

function Assert-ExactWorkspace {
    $actual = [IO.Path]::GetFullPath($Workspace).TrimEnd('\')
    if ($actual -cne 'D:\Temp\FHM') { throw "Safety barrier: unexpected workspace '$actual'." }
}
function Assert-InWorkspace([string]$Path) {
    $root = [IO.Path]::GetFullPath($Workspace).TrimEnd('\') + '\'
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) { throw "Safety barrier: path outside D:\Temp\FHM: $full" }
    return $full
}
function Get-RegValue([string]$Path, [string]$Name) {
    try { return (Get-ItemProperty -Path $Path -Name $Name -ErrorAction Stop).$Name }
    catch { return $null }
}
function Resolve-TC {
    $tcPath = $env:COMMANDER_PATH
    $tcIni = $env:COMMANDER_INI
    $keys = @('HKCU:\Software\Ghisler\Total Commander','HKLM:\Software\Ghisler\Total Commander','HKLM:\Software\Wow6432Node\Ghisler\Total Commander')
    if (-not $tcPath) { foreach ($key in $keys) { $v = Get-RegValue $key 'InstallDir'; if ($v) { $tcPath = [Environment]::ExpandEnvironmentVariables([string]$v); break } } }
    if (-not $tcIni) { foreach ($key in $keys) { $v = Get-RegValue $key 'IniFileName'; if ($v) { $tcIni = [Environment]::ExpandEnvironmentVariables([string]$v); break } } }
    $exe = $null
    if ($tcPath) { foreach ($name in @('TOTALCMD64.EXE','TOTALCMD.EXE')) { $candidate = Join-Path $tcPath $name; if (Test-Path -LiteralPath $candidate) { $exe = $candidate; break } } }
    [pscustomobject]@{ Exe = $exe; Ini = $tcIni }
}
function Navigate-TC([string]$TcExe, [string]$Path, [switch]$AllowReleasePath) {
    if (-not $AllowReleasePath) { [void](Assert-InWorkspace $Path) }
    Info ("[TC] Navigate left panel -> $Path")
    $p = Start-Process -FilePath $TcExe -ArgumentList @('/O',('/L="{0}"' -f $Path)) -PassThru
    if ($p) { $p.WaitForExit(5000) | Out-Null }
    Start-Sleep -Milliseconds 500
}
function Clean-Workspace([string]$TcExe) {
    Assert-ExactWorkspace
    Navigate-TC $TcExe $ReleasePath -AllowReleasePath
    Start-Sleep -Milliseconds 1000
    if (-not (Test-Path -LiteralPath $Workspace)) { New-Item -ItemType Directory -Path $Workspace -Force | Out-Null; return }
    foreach ($item in @(Get-ChildItem -LiteralPath $Workspace -Force -ErrorAction SilentlyContinue)) {
        [void](Assert-InWorkspace $item.FullName)
        for ($attempt = 1; $attempt -le 20; $attempt++) {
            try { Remove-Item -LiteralPath $item.FullName -Recurse -Force -ErrorAction Stop; break }
            catch { if ($attempt -eq 20) { throw }; Start-Sleep -Milliseconds 250 }
        }
    }
}

$nativeSource = @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class FhmLifecycleDiagNative {
    [StructLayout(LayoutKind.Sequential)] public struct FILE_ID_128 { [MarshalAs(UnmanagedType.ByValArray, SizeConst=16)] public byte[] Identifier; }
    [StructLayout(LayoutKind.Sequential)] public struct FILE_ID_INFO { public UInt64 VolumeSerialNumber; public FILE_ID_128 FileId; }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern SafeFileHandle CreateFileW(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetFileInformationByHandleEx(SafeFileHandle handle, int infoClass, out FILE_ID_INFO info, uint size);
    [DllImport("winsqlite3.dll", CharSet=CharSet.Unicode)] public static extern int sqlite3_open16(string filename, out IntPtr db);
    [DllImport("winsqlite3.dll")] public static extern int sqlite3_close(IntPtr db);
    [DllImport("winsqlite3.dll", CharSet=CharSet.Unicode)] public static extern int sqlite3_prepare16_v2(IntPtr db, string sql, int bytes, out IntPtr stmt, IntPtr tail);
    [DllImport("winsqlite3.dll")] public static extern int sqlite3_step(IntPtr stmt);
    [DllImport("winsqlite3.dll")] public static extern long sqlite3_column_int64(IntPtr stmt, int col);
    [DllImport("winsqlite3.dll")] public static extern int sqlite3_column_type(IntPtr stmt, int col);
    [DllImport("winsqlite3.dll")] public static extern int sqlite3_finalize(IntPtr stmt);
    public static string FileIdentity(string path, bool directory) {
        const uint ACCESS=0x80, SHARE=7, OPEN=3, NORMAL=0x80, BACKUP=0x02000000;
        using (SafeFileHandle h=CreateFileW(path, ACCESS, SHARE, IntPtr.Zero, OPEN, directory ? BACKUP : NORMAL, IntPtr.Zero)) {
            if (h.IsInvalid) return null;
            FILE_ID_INFO info;
            if (!GetFileInformationByHandleEx(h, 18, out info, (uint)Marshal.SizeOf(typeof(FILE_ID_INFO))) || info.FileId.Identifier == null) return null;
            return info.VolumeSerialNumber.ToString() + ":" + BitConverter.ToString(info.FileId.Identifier).Replace("-", "").ToLowerInvariant();
        }
    }
}
'@
Add-Type -TypeDefinition $nativeSource -Language CSharp

function SqlEscape([string]$Value) { return $Value.Replace("'", "''") }
function Relative-Path([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path).Replace('/','\').TrimEnd('\')
    if ($full.Length -lt 3 -or $full[1] -ne ':') { throw "Only drive-letter paths are supported: $full" }
    return $full.Substring(3).ToLowerInvariant()
}
function Invoke-SqlRow([string]$Database, [string]$Sql, [string[]]$Columns) {
    [IntPtr]$db = [IntPtr]::Zero
    [IntPtr]$statement = [IntPtr]::Zero
    $rc = [FhmLifecycleDiagNative]::sqlite3_open16($Database,[ref]$db)
    if ($rc -ne 0) { throw "sqlite3_open16 failed rc=$rc" }
    try {
        $rc = [FhmLifecycleDiagNative]::sqlite3_prepare16_v2($db,$Sql,-1,[ref]$statement,[IntPtr]::Zero)
        if ($rc -ne 0) { throw "sqlite prepare failed rc=$rc" }
        $rc = [FhmLifecycleDiagNative]::sqlite3_step($statement)
        if ($rc -ne 100) { return $null }
        $out = [ordered]@{}
        for ($i=0; $i -lt $Columns.Count; $i++) {
            if ([FhmLifecycleDiagNative]::sqlite3_column_type($statement,$i) -eq 1) { $out[$Columns[$i]] = [FhmLifecycleDiagNative]::sqlite3_column_int64($statement,$i) }
            else { $out[$Columns[$i]] = $null }
        }
        return [pscustomobject]$out
    }
    finally {
        if ($statement -ne [IntPtr]::Zero) { [void][FhmLifecycleDiagNative]::sqlite3_finalize($statement) }
        if ($db -ne [IntPtr]::Zero) { [void][FhmLifecycleDiagNative]::sqlite3_close($db) }
    }
}
function Get-FolderState([string]$Database,[string]$Path) {
    $rel = SqlEscape (Relative-Path $Path)
    return Invoke-SqlRow $Database "SELECT f.visits,f.last_visit,u.heat_visits,u.recent_visits,u.active_days,u.first_active_day,u.last_active_day,u.last_effective_visit FROM folders f LEFT JOIN folder_usage u ON u.storage_key=f.storage_key WHERE lower(f.relative_path)='$rel' LIMIT 1;" @('visits','last_visit','heat_visits','recent_visits','active_days','first_active_day','last_active_day','last_effective_visit')
}
function Get-FileState([string]$Database,[string]$Path) {
    $rel = SqlEscape (Relative-Path $Path)
    return Invoke-SqlRow $Database "SELECT write_events,last_write,active_days,first_active_day,last_active_day FROM file_activity WHERE lower(relative_path)='$rel' LIMIT 1;" @('write_events','last_write','active_days','first_active_day','last_active_day')
}
function State-Signature($State,[string[]]$Fields) {
    if ($null -eq $State) { return '<null>' }
    return ($Fields | ForEach-Object { "$_=$($State.$_)" }) -join ';'
}
function Wait-Until([scriptblock]$Condition,[int]$TimeoutMs=10000,[int]$IntervalMs=250) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do { $value = & $Condition; if ($value) { return $value }; Start-Sleep -Milliseconds $IntervalMs } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}
function Heat-Directory([string]$TcExe,[string]$Parent,[string]$Path,[int]$Visits=3) {
    for ($i=0; $i -lt $Visits; $i++) { Navigate-TC $TcExe $Path; Start-Sleep -Milliseconds 1100; Navigate-TC $TcExe $Parent; Start-Sleep -Milliseconds 1100 }
}
function Heat-File([string]$Path,[int]$Writes=3) {
    for ($i=1; $i -le $Writes; $i++) { [IO.File]::AppendAllText((Assert-InWorkspace $Path),("diag-write-$i " + [DateTime]::Now.ToString('O') + "`r`n"),$Utf8); Start-Sleep -Milliseconds 1300 }
}
function Dump-State([string]$Label,[string]$Path,$State,[string]$FileId,[string[]]$Fields) {
    Info ("[DIAG] $Label path=$Path")
    Info ("[DIAG] $Label file_id=$FileId")
    Info ("[DIAG] $Label state=" + (State-Signature $State $Fields))
}
function Dump-EngineTrace([string[]]$Patterns,[int]$FromLine=0) {
    if (-not (Test-Path -LiteralPath $EngineLog)) { Warn "Engine log not found: $EngineLog"; return }
    $lines = @(Get-Content -LiteralPath $EngineLog -ErrorAction SilentlyContinue)
    if ($FromLine -gt 0 -and $FromLine -lt $lines.Count) { $lines = $lines[$FromLine..($lines.Count - 1)] }
    elseif ($FromLine -ge $lines.Count) { $lines = @() }
    foreach ($line in $lines) {
        foreach ($pattern in $Patterns) {
            if ($line -like "*$pattern*") { Info ('[ENGINE] ' + $line); break }
        }
    }
}
function Engine-LineCount {
    if (-not (Test-Path -LiteralPath $EngineLog)) { return 0 }
    return @(Get-Content -LiteralPath $EngineLog -ErrorAction SilentlyContinue).Count
}

try {
    Assert-ExactWorkspace
    $tc = Resolve-TC
    if (-not $tc.Exe) { throw 'Total Commander executable not found.' }
    if (-not $tc.Ini) { throw 'Total Commander INI was not found.' }
    if (-not (Get-Process -Name 'FolderHeatMapEngine' -ErrorAction SilentlyContinue)) { throw 'FolderHeatMapEngine is not running.' }
    Clean-Workspace $tc.Exe
    New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
    [IO.File]::WriteAllText($LogPath,'',$Utf8)

    $database = Join-Path (Split-Path -Parent $tc.Ini) 'FolderHeatMap.db'
    if (-not (Test-Path -LiteralPath $database)) { throw "FolderHeatMap database not found: $database" }
    $folderFields = @('visits','last_visit','heat_visits','recent_visits','active_days','first_active_day','last_active_day','last_effective_visit')
    $fileFields = @('write_events','last_write','active_days','first_active_day','last_active_day')
    $src = Join-Path $Workspace 'DIAG_SRC'
    $dst = Join-Path $Workspace 'DIAG_DST'
    New-Item -ItemType Directory -Path $src,$dst -Force | Out-Null

    Info '============================================================'
    Info 'FolderHeatMap lifecycle diagnostic tests'
    Info ("Test version: $TestVersion")
    Info ('Started:      ' + [DateTime]::Now.ToString('dd.MM.yyyy HH:mm:ss.fff'))
    Info ("Workspace:    $Workspace")
    Info '============================================================'
    Pass 'Diagnostic prerequisites available.'

    foreach ($case in @(@{Name='NORMAL';Delay=1500},@{Name='STRESS';Delay=450})) {
        TestHeader ("Rapid MOVE convergence - $($case.Name) $($case.Delay) ms")
        $name = 'RAPID_' + $case.Name
        $path = Join-Path $src $name
        New-Item -ItemType Directory -Path $path | Out-Null
        Heat-Directory $tc.Exe $src $path 3
        $before = Wait-Until { Get-FolderState $database $path }
        $beforeId = [FhmLifecycleDiagNative]::FileIdentity($path,$true)
        $traceStart = Engine-LineCount
        $current = $path
        for ($i=1; $i -le 6; $i++) {
            $target = if ($current.StartsWith($src,[StringComparison]::OrdinalIgnoreCase)) { Join-Path $dst $name } else { Join-Path $src $name }
            Move-Item -LiteralPath (Assert-InWorkspace $current) -Destination (Assert-InWorkspace $target) -Force
            Start-Sleep -Milliseconds $case.Delay
            $current = $target
        }
        $after = Wait-Until {
            $state = Get-FolderState $database $current
            if ($state -and (State-Signature $state $folderFields) -eq (State-Signature $before $folderFields)) { return $state }
            return $null
        } 10000 250
        $afterId = [FhmLifecycleDiagNative]::FileIdentity($current,$true)
        Dump-State "$($case.Name) BEFORE" $path $before $beforeId $folderFields
        Dump-State "$($case.Name) AFTER" $current $after $afterId $folderFields
        Info ("[DIAG] old_db_path_present=" + [bool](Get-FolderState $database $path))
        Dump-EngineTrace @($name,'DB_DELETE_TRACE','queued_identity_reconciled','queued_identity_survived_unreconciled') $traceStart
        if ($after -and $beforeId -eq $afterId) { Pass "$($case.Name) rapid MOVE converged to the original identity/history." }
        else { ErrorResult "$($case.Name) rapid MOVE did not converge within 10 seconds." }
    }

    TestHeader 'Directory RENAME event diagnostics'
    $renameDir = Join-Path $src 'DIAG_RENAME_DIR'
    $renameDirNew = Join-Path $src 'DIAG_RENAMED_DIR'
    New-Item -ItemType Directory -Path $renameDir | Out-Null
    Heat-Directory $tc.Exe $src $renameDir 3
    $dirBefore = Wait-Until { Get-FolderState $database $renameDir }
    $dirIdBefore = [FhmLifecycleDiagNative]::FileIdentity($renameDir,$true)
    Navigate-TC $tc.Exe $src
    Move-Item -LiteralPath (Assert-InWorkspace $renameDir) -Destination (Assert-InWorkspace $renameDirNew) -Force
    Start-Sleep -Milliseconds 2500
    $dirAfter = Get-FolderState $database $renameDirNew
    $dirOldAfter = Get-FolderState $database $renameDir
    $dirIdAfter = [FhmLifecycleDiagNative]::FileIdentity($renameDirNew,$true)
    Dump-State 'DIR RENAME BEFORE' $renameDir $dirBefore $dirIdBefore $folderFields
    Dump-State 'DIR RENAME NEW' $renameDirNew $dirAfter $dirIdAfter $folderFields
    Info ('[DIAG] DIR RENAME old state=' + (State-Signature $dirOldAfter $folderFields))
    if ($dirIdBefore -eq $dirIdAfter -and $dirAfter -and -not $dirOldAfter -and (State-Signature $dirBefore $folderFields) -eq (State-Signature $dirAfter $folderFields)) { Pass 'Directory rename lifecycle is correct.' }
    else { ErrorResult 'Directory rename lifecycle mismatch confirmed.' }

    TestHeader 'File RENAME event diagnostics'
    Navigate-TC $tc.Exe $src
    $renameFile = Join-Path $src 'diag_rename_file.txt'
    $renameFileNew = Join-Path $src 'diag_renamed_file.txt'
    [IO.File]::WriteAllText((Assert-InWorkspace $renameFile),'rename',$Utf8)
    Heat-File $renameFile 3
    $fileBefore = Wait-Until { Get-FileState $database $renameFile }
    $fileIdBefore = [FhmLifecycleDiagNative]::FileIdentity($renameFile,$false)
    Move-Item -LiteralPath (Assert-InWorkspace $renameFile) -Destination (Assert-InWorkspace $renameFileNew) -Force
    Start-Sleep -Milliseconds 2500
    $fileAfter = Get-FileState $database $renameFileNew
    $fileOldAfter = Get-FileState $database $renameFile
    $fileIdAfter = [FhmLifecycleDiagNative]::FileIdentity($renameFileNew,$false)
    if ($fileIdBefore -eq $fileIdAfter -and $fileAfter -and -not $fileOldAfter -and (State-Signature $fileBefore $fileFields) -eq (State-Signature $fileAfter $fileFields)) { Pass 'File rename lifecycle is correct.' }
    else { ErrorResult 'File rename lifecycle mismatch confirmed.' }

    TestHeader 'MOVE and destination write observability split'
    Navigate-TC $tc.Exe $src
    $moveFile = Join-Path $src 'diag_move_write.txt'
    $moveFileDst = Join-Path $dst 'diag_move_write.txt'
    [IO.File]::WriteAllText((Assert-InWorkspace $moveFile),'move',$Utf8)
    Heat-File $moveFile 3
    $moveBefore = Wait-Until { Get-FileState $database $moveFile }
    $moveIdBefore = [FhmLifecycleDiagNative]::FileIdentity($moveFile,$false)
    Move-Item -LiteralPath (Assert-InWorkspace $moveFile) -Destination (Assert-InWorkspace $moveFileDst) -Force
    $moveOnly = Wait-Until {
        $state = Get-FileState $database $moveFileDst
        if ($state -and (State-Signature $state $fileFields) -eq (State-Signature $moveBefore $fileFields)) { return $state }
        return $null
    } 10000 250
    $moveIdAfter = [FhmLifecycleDiagNative]::FileIdentity($moveFileDst,$false)
    if ($moveOnly -and $moveIdBefore -eq $moveIdAfter) { Pass 'MOVE preserved file identity/history before any destination write.' }
    else { ErrorResult 'MOVE itself lost file identity/history.' }

    $writesBeforeImmediate = if ($moveOnly) { $moveOnly.write_events } else { -1 }
    [IO.File]::AppendAllText((Assert-InWorkspace $moveFileDst),"immediate-unwatched`r`n",$Utf8)
    Start-Sleep -Milliseconds 2500
    $afterImmediate = Get-FileState $database $moveFileDst
    if ($afterImmediate -and $writesBeforeImmediate -ge 0 -and $afterImmediate.write_events -gt $writesBeforeImmediate) { Pass 'Immediate destination write was observed without navigating to DST.' }
    else { Warn 'Immediate destination write was not observed before navigating to DST.' }

    Navigate-TC $tc.Exe $dst
    Start-Sleep -Milliseconds 1300
    $controlBefore = Get-FileState $database $moveFileDst
    [IO.File]::AppendAllText((Assert-InWorkspace $moveFileDst),"control-watched`r`n",$Utf8)
    $controlAfter = Wait-Until {
        $state = Get-FileState $database $moveFileDst
        if ($state -and $controlBefore -and $state.write_events -gt $controlBefore.write_events) { return $state }
        return $null
    } 10000 250
    if ($controlAfter) { Pass 'Destination write is observed after Total Commander navigates to DST.' }
    else { ErrorResult 'Destination write was not observed even while DST was watched.' }
}
catch {
    ErrorResult ("Unhandled diagnostic exception: $($_.Exception.Message)")
    Info ('[EXCEPTION] ' + $_.ScriptStackTrace)
}
finally {
    Write-LogLine ''
    Write-LogLine '============================================================'
    Write-LogLine 'FolderHeatMap lifecycle diagnostic summary'
    Write-LogLine ("Test version: $TestVersion") Cyan
    Write-LogLine ("PASS:    $PassCount") Green
    Write-LogLine ("ERROR:   $ErrorCount") $(if ($ErrorCount -gt 0) { [ConsoleColor]::Red } else { [ConsoleColor]::Green })
    Write-LogLine ("WARNING: $WarningCount") $(if ($WarningCount -gt 0) { [ConsoleColor]::Yellow } else { [ConsoleColor]::Gray })
    if ($ErrorCount -eq 0) { Write-LogLine 'RESULT: PASS' Green } else { Write-LogLine 'RESULT: ERROR' Red }
    Write-LogLine ("Log: $LogPath") Gray
    Write-LogLine '============================================================'
}

if ($ErrorCount -gt 0) { exit 1 }
exit 0
