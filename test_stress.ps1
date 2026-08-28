$ErrorActionPreference = 'Stop'

$TestVersion = '1.36'
$Workspace = 'D:\Temp\FHM'
$ReleasePath = 'D:\Temp'
$RunId = [DateTime]::Now.ToString('yyyyMMdd-HHmmss')
$LogDir = Join-Path $Workspace 'logs'
$LogPath = Join-Path $LogDir ("stress-$RunId.log")
$Repo = Split-Path -Parent $MyInvocation.MyCommand.Path
$Repo = [IO.Path]::GetFullPath($Repo).TrimEnd('\')
$PassCount = 0
$ErrorCount = 0
$WarningCount = 0
$Utf8 = [Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $Utf8
$OutputEncoding = $Utf8
try { & chcp.com 65001 *> $null } catch { }

function Write-LogLine {
    param(
        [string]$Text,
        [ConsoleColor]$Color = [ConsoleColor]::Gray
    )
    if (-not (Test-Path -LiteralPath $LogDir)) {
        New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
    }
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
    if ($actual -cne 'D:\Temp\FHM') {
        throw "Safety barrier: unexpected workspace '$actual'."
    }
}
function Assert-InWorkspace([string]$Path) {
    $root = [IO.Path]::GetFullPath($Workspace).TrimEnd('\') + '\'
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Safety barrier: path outside D:\Temp\FHM: $full"
    }
    return $full
}
function Get-RegValue([string]$Path, [string]$Name) {
    try { return (Get-ItemProperty -Path $Path -Name $Name -ErrorAction Stop).$Name }
    catch { return $null }
}
function Resolve-TC {
    $tcPath = $env:COMMANDER_PATH
    $tcIni = $env:COMMANDER_INI
    $keys = @(
        'HKCU:\Software\Ghisler\Total Commander',
        'HKLM:\Software\Ghisler\Total Commander',
        'HKLM:\Software\Wow6432Node\Ghisler\Total Commander'
    )
    if (-not $tcPath) {
        foreach ($key in $keys) {
            $value = Get-RegValue $key 'InstallDir'
            if ($value) {
                $tcPath = [Environment]::ExpandEnvironmentVariables([string]$value)
                break
            }
        }
    }
    if (-not $tcIni) {
        foreach ($key in $keys) {
            $value = Get-RegValue $key 'IniFileName'
            if ($value) {
                $tcIni = [Environment]::ExpandEnvironmentVariables([string]$value)
                break
            }
        }
    }
    $exe = $null
    if ($tcPath) {
        foreach ($name in @('TOTALCMD64.EXE', 'TOTALCMD.EXE')) {
            $candidate = Join-Path $tcPath $name
            if (Test-Path -LiteralPath $candidate) {
                $exe = $candidate
                break
            }
        }
    }
    [pscustomobject]@{ Exe = $exe; Ini = $tcIni }
}
function Release-TCWorkspace([string]$TcExe) {
    if (-not (Test-Path -LiteralPath $ReleasePath)) {
        New-Item -ItemType Directory -Path $ReleasePath -Force | Out-Null
    }
    Info ("[TC] Releasing test workspace -> $ReleasePath")
    $process = Start-Process -FilePath $TcExe -ArgumentList @('/O', ('/L="{0}"' -f $ReleasePath)) -PassThru
    if ($process) { $process.WaitForExit(5000) | Out-Null }
    Start-Sleep -Milliseconds 1500
}
function Clean-Workspace {
    Assert-ExactWorkspace
    if (-not (Test-Path -LiteralPath $Workspace)) {
        New-Item -ItemType Directory -Path $Workspace -Force | Out-Null
        return
    }
    foreach ($item in @(Get-ChildItem -LiteralPath $Workspace -Force -ErrorAction SilentlyContinue)) {
        [void](Assert-InWorkspace $item.FullName)
        $removed = $false
        for ($attempt = 1; $attempt -le 20; $attempt++) {
            try {
                Remove-Item -LiteralPath $item.FullName -Recurse -Force -ErrorAction Stop
                $removed = $true
                break
            }
            catch {
                if ($attempt -eq 20) { throw }
                Start-Sleep -Milliseconds 250
            }
        }
        if (-not $removed) { throw "Could not clean workspace item: $($item.FullName)" }
    }
}

$nativeSource = @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class FhmStressNative {
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
    $full = [IO.Path]::GetFullPath($Path).Replace('/', '\').TrimEnd('\')
    if ($full.Length -lt 3 -or $full[1] -ne ':') { throw "Only drive-letter paths are supported: $full" }
    return $full.Substring(3).ToLowerInvariant()
}
function Invoke-SqlRow([string]$Database, [string]$Sql, [string[]]$Columns) {
    [IntPtr]$db = [IntPtr]::Zero
    [IntPtr]$statement = [IntPtr]::Zero
    $rc = [FhmStressNative]::sqlite3_open16($Database, [ref]$db)
    if ($rc -ne 0) { throw "sqlite3_open16 failed rc=$rc" }
    try {
        $rc = [FhmStressNative]::sqlite3_prepare16_v2($db, $Sql, -1, [ref]$statement, [IntPtr]::Zero)
        if ($rc -ne 0) { throw "sqlite prepare failed rc=$rc" }
        $rc = [FhmStressNative]::sqlite3_step($statement)
        if ($rc -ne 100) { return $null }
        $output = [ordered]@{}
        for ($i = 0; $i -lt $Columns.Count; $i++) {
            if ([FhmStressNative]::sqlite3_column_type($statement, $i) -eq 1) {
                $output[$Columns[$i]] = [FhmStressNative]::sqlite3_column_int64($statement, $i)
            }
            else {
                $output[$Columns[$i]] = $null
            }
        }
        return [pscustomobject]$output
    }
    finally {
        if ($statement -ne [IntPtr]::Zero) { [void][FhmStressNative]::sqlite3_finalize($statement) }
        if ($db -ne [IntPtr]::Zero) { [void][FhmStressNative]::sqlite3_close($db) }
    }
}
function Get-FolderState([string]$Database, [string]$Path) {
    $rel = SqlEscape (Relative-Path $Path)
    $sql = "SELECT f.visits,f.last_visit,u.heat_visits,u.recent_visits,u.active_days,u.first_active_day,u.last_active_day,u.last_effective_visit FROM folders f LEFT JOIN folder_usage u ON u.storage_key=f.storage_key WHERE lower(f.relative_path)='$rel' LIMIT 1;"
    return Invoke-SqlRow $Database $sql @('visits','last_visit','heat_visits','recent_visits','active_days','first_active_day','last_active_day','last_effective_visit')
}
function Get-FileState([string]$Database, [string]$Path) {
    $rel = SqlEscape (Relative-Path $Path)
    $sql = "SELECT write_events,last_write,active_days,first_active_day,last_active_day FROM file_activity WHERE lower(relative_path)='$rel' LIMIT 1;"
    return Invoke-SqlRow $Database $sql @('write_events','last_write','active_days','first_active_day','last_active_day')
}
function State-Signature($State, [string[]]$Fields) {
    if ($null -eq $State) { return '<null>' }
    return ($Fields | ForEach-Object { "$_=$($State.$_)" }) -join ';'
}
function Wait-Until([scriptblock]$Condition, [int]$TimeoutMs = 12000, [int]$IntervalMs = 250) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do {
        $value = & $Condition
        if ($value) { return $value }
        Start-Sleep -Milliseconds $IntervalMs
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}
function Navigate-TC([string]$TcExe, [string]$Path) {
    [void](Assert-InWorkspace $Path)
    Info ("[TC] Navigate left panel -> $Path")
    $process = Start-Process -FilePath $TcExe -ArgumentList @('/O', ('/L="{0}"' -f $Path)) -PassThru
    if ($process) { $process.WaitForExit(5000) | Out-Null }
    Start-Sleep -Milliseconds 500
}
function Heat-Directory([string]$TcExe, [string]$Parent, [string]$Path, [int]$Visits = 3) {
    for ($i = 0; $i -lt $Visits; $i++) {
        Navigate-TC $TcExe $Path
        Start-Sleep -Milliseconds 1100
        Navigate-TC $TcExe $Parent
        Start-Sleep -Milliseconds 1100
    }
}
function Heat-File([string]$Path, [int]$Writes = 3) {
    for ($i = 1; $i -le $Writes; $i++) {
        [IO.File]::AppendAllText((Assert-InWorkspace $Path), ("stress-write-$i " + [DateTime]::Now.ToString('O') + "`r`n"), $Utf8)
        Start-Sleep -Milliseconds 1300
    }
}
function Same-State($A, $B, [string[]]$Fields) {
    return ($A -and $B -and (State-Signature $A $Fields) -eq (State-Signature $B $Fields))
}

try {
    Assert-ExactWorkspace
    $tc = Resolve-TC
    if (-not $tc.Exe) { throw 'Total Commander executable not found.' }
    if (-not $tc.Ini) { throw 'Total Commander INI was not found.' }

    Release-TCWorkspace $tc.Exe
    Pass 'Workspace released by Total Commander.'
    Clean-Workspace
    New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
    [IO.File]::WriteAllText($LogPath, '', $Utf8)

    Info '============================================================'
    Info 'FolderHeatMap lifecycle stress regression tests'
    Info ("Test version: $TestVersion")
    Info ('Started:      ' + [DateTime]::Now.ToString('dd.MM.yyyy HH:mm:ss.fff'))
    Info ("Workspace:    $Workspace")
    Info '============================================================'

    $database = Join-Path (Split-Path -Parent $tc.Ini) 'FolderHeatMap.db'
    if (-not (Test-Path -LiteralPath $database)) { throw "FolderHeatMap database not found: $database" }
    if (-not (Get-Process -Name 'FolderHeatMapEngine' -ErrorAction SilentlyContinue)) { throw 'FolderHeatMapEngine is not running.' }
    Pass 'Prerequisites available.'

    $folderFields = @('visits','last_visit','heat_visits','recent_visits','active_days','first_active_day','last_active_day','last_effective_visit')
    $fileFields = @('write_events','last_write','active_days','first_active_day','last_active_day')
    $src = Join-Path $Workspace 'STRESS_SRC'
    $dst = Join-Path $Workspace 'STRESS_DST'
    New-Item -ItemType Directory -Path $src,$dst -Force | Out-Null

    TestHeader 'Rapid same-volume MOVE stress'
    $rapidDir = Join-Path $src 'RAPID_DIR'
    New-Item -ItemType Directory -Path $rapidDir | Out-Null
    Heat-Directory $tc.Exe $src $rapidDir 3
    $rapidState = Wait-Until { Get-FolderState $database $rapidDir }
    $rapidId = [FhmStressNative]::FileIdentity($rapidDir, $true)
    $rapidCurrent = $rapidDir
    for ($i = 1; $i -le 6; $i++) {
        if ($rapidCurrent.StartsWith($src, [StringComparison]::OrdinalIgnoreCase)) {
            $target = Join-Path $dst 'RAPID_DIR'
        }
        else {
            $target = Join-Path $src 'RAPID_DIR'
        }
        Move-Item -LiteralPath (Assert-InWorkspace $rapidCurrent) -Destination (Assert-InWorkspace $target) -Force
        Start-Sleep -Milliseconds 450
        $rapidCurrent = $target
    }
    $rapidAfter = Wait-Until { Get-FolderState $database $rapidCurrent }
    if ($rapidId -eq [FhmStressNative]::FileIdentity($rapidCurrent, $true) -and (Same-State $rapidState $rapidAfter $folderFields)) {
        Pass 'Rapid directory moves preserved identity and history.'
    }
    else {
        ErrorResult 'Rapid directory moves changed identity or history.'
    }

    TestHeader 'Directory and file RENAME'
    $renameDir = Join-Path $src 'RENAME_DIR'
    $renameDirNew = Join-Path $src 'RENAMED_DIR'
    New-Item -ItemType Directory -Path $renameDir | Out-Null
    Heat-Directory $tc.Exe $src $renameDir 3
    $renameDirState = Wait-Until { Get-FolderState $database $renameDir }
    $renameDirId = [FhmStressNative]::FileIdentity($renameDir, $true)
    Move-Item -LiteralPath (Assert-InWorkspace $renameDir) -Destination (Assert-InWorkspace $renameDirNew) -Force
    $renameDirAfter = Wait-Until { Get-FolderState $database $renameDirNew }
    if ($renameDirId -eq [FhmStressNative]::FileIdentity($renameDirNew, $true) -and (Same-State $renameDirState $renameDirAfter $folderFields) -and -not (Get-FolderState $database $renameDir)) {
        Pass 'Directory rename preserved identity/history and removed old DB path.'
    }
    else {
        ErrorResult 'Directory rename regression detected.'
    }

    Navigate-TC $tc.Exe $src
    $renameFile = Join-Path $src 'rename_file.txt'
    $renameFileNew = Join-Path $src 'renamed_file.txt'
    [IO.File]::WriteAllText((Assert-InWorkspace $renameFile), 'rename-test', $Utf8)
    Heat-File $renameFile 3
    $renameFileState = Wait-Until { Get-FileState $database $renameFile }
    $renameFileId = [FhmStressNative]::FileIdentity($renameFile, $false)
    Move-Item -LiteralPath (Assert-InWorkspace $renameFile) -Destination (Assert-InWorkspace $renameFileNew) -Force
    $renameFileAfter = Wait-Until { Get-FileState $database $renameFileNew }
    if ($renameFileId -eq [FhmStressNative]::FileIdentity($renameFileNew, $false) -and (Same-State $renameFileState $renameFileAfter $fileFields) -and -not (Get-FileState $database $renameFile)) {
        Pass 'File rename preserved identity/history and removed old DB path.'
    }
    else {
        ErrorResult 'File rename regression detected.'
    }

    TestHeader 'Delete populated subtree and recreate cold'
    $tree = Join-Path $src 'HOT_TREE'
    $c = Join-Path (Join-Path (Join-Path $tree 'A') 'B') 'C'
    $treeFile = Join-Path $c 'child.txt'
    New-Item -ItemType Directory -Path $c -Force | Out-Null
    [IO.File]::WriteAllText((Assert-InWorkspace $treeFile), 'tree', $Utf8)
    Heat-Directory $tc.Exe (Split-Path -Parent $c) $c 3
    Navigate-TC $tc.Exe $c
    Heat-File $treeFile 3
    $oldCId = [FhmStressNative]::FileIdentity($c, $true)
    $oldChildId = [FhmStressNative]::FileIdentity($treeFile, $false)
    Navigate-TC $tc.Exe $src
    Remove-Item -LiteralPath (Assert-InWorkspace $tree) -Recurse -Force
    $subtreeGone = Wait-Until {
        if (-not (Get-FolderState $database $c) -and -not (Get-FileState $database $treeFile)) { return $true }
        return $null
    }
    if ($subtreeGone) { Pass 'Populated subtree history was purged recursively.' }
    else { ErrorResult 'Populated subtree left stale DB history.' }
    New-Item -ItemType Directory -Path $c -Force | Out-Null
    [IO.File]::WriteAllText((Assert-InWorkspace $treeFile), 'new-tree', $Utf8)
    if ($oldCId -ne [FhmStressNative]::FileIdentity($c, $true) -and $oldChildId -ne [FhmStressNative]::FileIdentity($treeFile, $false)) {
        Pass 'Recreated subtree objects have new identities.'
    }
    else {
        ErrorResult 'Recreated subtree reused an old identity unexpectedly.'
    }

    TestHeader 'Immediate DELETE -> RECREATE race'
    $race = Join-Path $src 'RACE_DIR'
    New-Item -ItemType Directory -Path $race | Out-Null
    Heat-Directory $tc.Exe $src $race 3
    $raceOld = Wait-Until { Get-FolderState $database $race }
    $raceOldId = [FhmStressNative]::FileIdentity($race, $true)
    Navigate-TC $tc.Exe $src
    Remove-Item -LiteralPath (Assert-InWorkspace $race) -Recurse -Force
    New-Item -ItemType Directory -Path (Assert-InWorkspace $race) -Force | Out-Null
    $raceNewId = [FhmStressNative]::FileIdentity($race, $true)
    Navigate-TC $tc.Exe $race
    Start-Sleep -Milliseconds 1500
    $raceNew = Wait-Until { Get-FolderState $database $race }
    if ($raceOldId -ne $raceNewId -and $raceNew -and $raceOld -and $raceNew.visits -lt $raceOld.visits) {
        Pass 'Immediate delete/recreate did not inherit old directory history.'
    }
    else {
        ErrorResult 'Immediate delete/recreate race inherited stale history or identity.'
    }

    TestHeader 'MOVE then immediate file write'
    Navigate-TC $tc.Exe $src
    $moveWrite = Join-Path $src 'move_write.txt'
    $moveWriteDst = Join-Path $dst 'move_write.txt'
    [IO.File]::WriteAllText((Assert-InWorkspace $moveWrite), 'move-write', $Utf8)
    Heat-File $moveWrite 3
    $moveWriteBefore = Wait-Until { Get-FileState $database $moveWrite }
    $moveWriteId = [FhmStressNative]::FileIdentity($moveWrite, $false)
    Move-Item -LiteralPath (Assert-InWorkspace $moveWrite) -Destination (Assert-InWorkspace $moveWriteDst) -Force
    [IO.File]::AppendAllText((Assert-InWorkspace $moveWriteDst), "immediate`r`n", $Utf8)
    $moveWriteAfter = Wait-Until {
        $state = Get-FileState $database $moveWriteDst
        if ($state -and $moveWriteBefore -and $state.write_events -ge ($moveWriteBefore.write_events + 1)) { return $state }
        return $null
    } 15000 250
    if ($moveWriteAfter -and $moveWriteId -eq [FhmStressNative]::FileIdentity($moveWriteDst, $false) -and $moveWriteBefore -and $moveWriteAfter.write_events -ge ($moveWriteBefore.write_events + 1)) {
        Pass 'MOVE plus immediate write preserved old history and added the new write.'
    }
    else {
        ErrorResult 'MOVE plus immediate write lost identity/history or the new write.'
    }

    TestHeader 'MOVE directory with heated descendants'
    $parent = Join-Path $src 'PARENT_TREE'
    $child = Join-Path $parent 'CHILD'
    $childFile = Join-Path $child 'child_hot.txt'
    New-Item -ItemType Directory -Path $child -Force | Out-Null
    [IO.File]::WriteAllText((Assert-InWorkspace $childFile), 'child', $Utf8)
    Heat-Directory $tc.Exe $parent $child 3
    Navigate-TC $tc.Exe $child
    Heat-File $childFile 3
    $childState = Wait-Until { Get-FolderState $database $child }
    $childFileState = Wait-Until { Get-FileState $database $childFile }
    $childId = [FhmStressNative]::FileIdentity($child, $true)
    $childFileId = [FhmStressNative]::FileIdentity($childFile, $false)
    Navigate-TC $tc.Exe $src
    $parentDst = Join-Path $dst 'PARENT_TREE'
    Move-Item -LiteralPath (Assert-InWorkspace $parent) -Destination (Assert-InWorkspace $parentDst) -Force
    $childDst = Join-Path $parentDst 'CHILD'
    $childFileDst = Join-Path $childDst 'child_hot.txt'
    $childAfter = Wait-Until { Get-FolderState $database $childDst }
    $childFileAfter = Wait-Until { Get-FileState $database $childFileDst }
    if ($childId -eq [FhmStressNative]::FileIdentity($childDst, $true) -and $childFileId -eq [FhmStressNative]::FileIdentity($childFileDst, $false) -and (Same-State $childState $childAfter $folderFields) -and (Same-State $childFileState $childFileAfter $fileFields)) {
        Pass 'Moved directory preserved heated descendant identities and histories.'
    }
    else {
        ErrorResult 'Moved directory lost descendant identity or history.'
    }

    TestHeader 'Engine restart persistence'
    $persistDirState = Get-FolderState $database $childDst
    $persistFileState = Get-FileState $database $childFileDst
    $persistDirId = [FhmStressNative]::FileIdentity($childDst, $true)
    $persistFileId = [FhmStressNative]::FileIdentity($childFileDst, $false)
    Get-Process -Name 'FolderHeatMapEngine' -ErrorAction SilentlyContinue | Stop-Process -Force
    $stopped = Wait-Until {
        if (-not (Get-Process -Name 'FolderHeatMapEngine' -ErrorAction SilentlyContinue)) { return $true }
        return $null
    } 5000 200
    $launcher = Join-Path $Repo 'start_engine.ps1'
    if (-not $stopped -or -not (Test-Path -LiteralPath $launcher)) {
        ErrorResult 'Could not safely prepare engine restart test.'
    }
    else {
        Start-Process powershell.exe -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',$launcher) -WindowStyle Hidden | Out-Null
        $started = Wait-Until { Get-Process -Name 'FolderHeatMapEngine' -ErrorAction SilentlyContinue } 10000 250
        if ($started) {
            Start-Sleep -Milliseconds 1500
            if ((Same-State $persistDirState (Get-FolderState $database $childDst) $folderFields) -and (Same-State $persistFileState (Get-FileState $database $childFileDst) $fileFields) -and $persistDirId -eq [FhmStressNative]::FileIdentity($childDst, $true) -and $persistFileId -eq [FhmStressNative]::FileIdentity($childFileDst, $false)) {
                Pass 'Engine restart preserved persistent histories and filesystem identities.'
            }
            else {
                ErrorResult 'Engine restart persistence mismatch.'
            }
        }
        else {
            ErrorResult 'FolderHeatMapEngine did not restart.'
        }
    }

    TestHeader 'Workspace reuse safety'
    $reuse = Join-Path $Workspace 'REUSE_SENTINEL'
    New-Item -ItemType Directory -Path $reuse -Force | Out-Null
    [IO.File]::WriteAllText((Assert-InWorkspace (Join-Path $reuse 'old.txt')), 'old', $Utf8)
    if (Test-Path -LiteralPath $reuse) { Pass 'Workspace reuse fixture created for the next run cleanup check.' }
    else { ErrorResult 'Could not create workspace reuse fixture.' }
}
catch {
    ErrorResult ("Unhandled stress-test exception: $($_.Exception.Message)")
    Info ("[EXCEPTION] " + $_.ScriptStackTrace)
}
finally {
    Write-LogLine ''
    Write-LogLine '============================================================'
    Write-LogLine 'FolderHeatMap lifecycle stress test summary'
    Write-LogLine ("PASS:    $PassCount") Green
    Write-LogLine ("ERROR:   $ErrorCount") $(if ($ErrorCount -gt 0) { [ConsoleColor]::Red } else { [ConsoleColor]::Green })
    Write-LogLine ("WARNING: $WarningCount") $(if ($WarningCount -gt 0) { [ConsoleColor]::Yellow } else { [ConsoleColor]::Gray })
    if ($ErrorCount -eq 0) { Write-LogLine 'RESULT: PASS' Green }
    else { Write-LogLine 'RESULT: ERROR' Red }
    Write-LogLine ("Log: $LogPath") Gray
    Write-LogLine '============================================================'
}

if ($ErrorCount -gt 0) { exit 1 }
exit 0
