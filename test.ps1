$ErrorActionPreference = 'Stop'

$TestVersion = '1.32'
$Workspace = 'D:\Temp\FHM'
$RunId = [DateTime]::Now.ToString('yyyyMMdd-HHmmss')
$LogDir = Join-Path $Workspace 'logs'
$LogPath = Join-Path $LogDir ("test-$RunId.log")
$PassCount = 0
$ErrorCount = 0
$WarningCount = 0
$CleanupItems = @()
$Repo = Split-Path -Parent $MyInvocation.MyCommand.Path
$Repo = [IO.Path]::GetFullPath($Repo).TrimEnd('\')
$EngineLog = Join-Path $Repo 'logs\FolderHeatMap.log'

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
    if (-not $full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Safety barrier: write/delete path is outside D:\Temp\FHM: $full"
    }
    return $full
}
function Clean-Workspace {
    Assert-ExactWorkspace
    if (-not (Test-Path -LiteralPath $Workspace)) {
        New-Item -ItemType Directory -Path $Workspace -Force | Out-Null
        $script:CleanupItems = @()
        return
    }
    $items = @(Get-ChildItem -LiteralPath $Workspace -Force -ErrorAction SilentlyContinue)
    $script:CleanupItems = @($items | ForEach-Object { $_.Name })
    foreach ($item in $items) {
        [void](Assert-InWorkspace $item.FullName)
        Remove-Item -LiteralPath $item.FullName -Recurse -Force -ErrorAction Stop
    }
}

function Get-RegValue([string]$Path,[string]$Name) {
    try { return (Get-ItemProperty -Path $Path -Name $Name -ErrorAction Stop).$Name } catch { return $null }
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
            if ($value) { $tcPath = [Environment]::ExpandEnvironmentVariables([string]$value); break }
        }
    }
    if (-not $tcIni) {
        foreach ($key in $keys) {
            $value = Get-RegValue $key 'IniFileName'
            if ($value) { $tcIni = [Environment]::ExpandEnvironmentVariables([string]$value); break }
        }
    }
    $exe = $null
    if ($tcPath) {
        foreach ($name in @('TOTALCMD64.EXE','TOTALCMD.EXE')) {
            $candidate = Join-Path $tcPath $name
            if (Test-Path -LiteralPath $candidate) { $exe = $candidate; break }
        }
    }
    [pscustomobject]@{ Exe=$exe; Ini=$tcIni }
}

$nativeSource = @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class FhmNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct FILE_ID_128 {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst=16)] public byte[] Identifier;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct FILE_ID_INFO {
        public UInt64 VolumeSerialNumber;
        public FILE_ID_128 FileId;
    }

    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern SafeFileHandle CreateFileW(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool GetFileInformationByHandleEx(SafeFileHandle handle, int infoClass, out FILE_ID_INFO info, uint size);

    [DllImport("winsqlite3.dll", CharSet=CharSet.Unicode)]
    public static extern int sqlite3_open16(string filename, out IntPtr db);
    [DllImport("winsqlite3.dll")]
    public static extern int sqlite3_close(IntPtr db);
    [DllImport("winsqlite3.dll", CharSet=CharSet.Unicode)]
    public static extern int sqlite3_prepare16_v2(IntPtr db, string sql, int bytes, out IntPtr stmt, IntPtr tail);
    [DllImport("winsqlite3.dll")]
    public static extern int sqlite3_step(IntPtr stmt);
    [DllImport("winsqlite3.dll")]
    public static extern long sqlite3_column_int64(IntPtr stmt, int col);
    [DllImport("winsqlite3.dll")]
    public static extern IntPtr sqlite3_column_text16(IntPtr stmt, int col);
    [DllImport("winsqlite3.dll")]
    public static extern int sqlite3_column_type(IntPtr stmt, int col);
    [DllImport("winsqlite3.dll")]
    public static extern int sqlite3_finalize(IntPtr stmt);

    public static string FileIdentity(string path, bool directory) {
        const uint FILE_READ_ATTRIBUTES = 0x80;
        const uint SHARE = 1 | 2 | 4;
        const uint OPEN_EXISTING = 3;
        const uint FILE_ATTRIBUTE_NORMAL = 0x80;
        const uint FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;
        using (SafeFileHandle h = CreateFileW(path, FILE_READ_ATTRIBUTES, SHARE, IntPtr.Zero, OPEN_EXISTING, directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL, IntPtr.Zero)) {
            if (h.IsInvalid) return null;
            FILE_ID_INFO info;
            if (!GetFileInformationByHandleEx(h, 18, out info, (uint)Marshal.SizeOf(typeof(FILE_ID_INFO)))) return null;
            if (info.FileId.Identifier == null) return null;
            string hex = BitConverter.ToString(info.FileId.Identifier).Replace("-", "").ToLowerInvariant();
            return info.VolumeSerialNumber.ToString() + ":" + hex;
        }
    }
}
'@
Add-Type -TypeDefinition $nativeSource -Language CSharp

function SqlEscape([string]$Value) { return $Value.Replace("'", "''") }
function Invoke-SqlRow([string]$Database,[string]$Sql,[string[]]$Columns) {
    [IntPtr]$db = [IntPtr]::Zero
    [IntPtr]$st = [IntPtr]::Zero
    $rc = [FhmNative]::sqlite3_open16($Database, [ref]$db)
    if ($rc -ne 0) { throw "sqlite3_open16 failed rc=$rc db=$Database" }
    try {
        $rc = [FhmNative]::sqlite3_prepare16_v2($db, $Sql, -1, [ref]$st, [IntPtr]::Zero)
        if ($rc -ne 0) { throw "sqlite3_prepare16_v2 failed rc=$rc sql=$Sql" }
        $rc = [FhmNative]::sqlite3_step($st)
        if ($rc -ne 100) { return $null }
        $out = [ordered]@{}
        for ($i=0; $i -lt $Columns.Count; $i++) {
            $type = [FhmNative]::sqlite3_column_type($st, $i)
            if ($type -eq 1) { $out[$Columns[$i]] = [FhmNative]::sqlite3_column_int64($st, $i) }
            elseif ($type -eq 3) {
                $ptr = [FhmNative]::sqlite3_column_text16($st, $i)
                $out[$Columns[$i]] = if ($ptr -eq [IntPtr]::Zero) { $null } else { [Runtime.InteropServices.Marshal]::PtrToStringUni($ptr) }
            } else { $out[$Columns[$i]] = $null }
        }
        return [pscustomobject]$out
    } finally {
        if ($st -ne [IntPtr]::Zero) { [void][FhmNative]::sqlite3_finalize($st) }
        if ($db -ne [IntPtr]::Zero) { [void][FhmNative]::sqlite3_close($db) }
    }
}
function Relative-Path([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path).Replace('/','\').TrimEnd('\')
    if ($full.Length -lt 3 -or $full[1] -ne ':') { throw "Only drive-letter paths are supported by this test: $full" }
    return $full.Substring(3).ToLowerInvariant()
}
function Get-FolderState([string]$Database,[string]$Path) {
    $rel = SqlEscape (Relative-Path $Path)
    $sql = "SELECT f.visits,f.last_visit,u.heat_visits,u.recent_visits,u.active_days,u.first_active_day,u.last_active_day,u.last_effective_visit FROM folders f LEFT JOIN folder_usage u ON u.storage_key=f.storage_key WHERE lower(f.relative_path)='$rel' LIMIT 1;"
    Invoke-SqlRow $Database $sql @('visits','last_visit','heat_visits','recent_visits','active_days','first_active_day','last_active_day','last_effective_visit')
}
function Get-FileState([string]$Database,[string]$Path) {
    $rel = SqlEscape (Relative-Path $Path)
    $sql = "SELECT write_events,last_write,active_days,first_active_day,last_active_day FROM file_activity WHERE lower(relative_path)='$rel' LIMIT 1;"
    Invoke-SqlRow $Database $sql @('write_events','last_write','active_days','first_active_day','last_active_day')
}
function State-Signature($State,[string[]]$Fields) {
    if ($null -eq $State) { return '<null>' }
    return ($Fields | ForEach-Object { "$_=$($State.$_)" }) -join ';'
}
function Wait-Until([scriptblock]$Condition,[int]$TimeoutMs=10000,[int]$IntervalMs=200) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do {
        $value = & $Condition
        if ($value) { return $value }
        Start-Sleep -Milliseconds $IntervalMs
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}
function Navigate-TC([string]$TcExe,[string]$Path) {
    [void](Assert-InWorkspace $Path)
    $args = @('/O', ('/L="{0}"' -f $Path))
    Info ("[TC] Navigate left panel -> $Path")
    $p = Start-Process -FilePath $TcExe -ArgumentList $args -PassThru
    if ($p) { $p.WaitForExit(5000) | Out-Null }
    Start-Sleep -Milliseconds 500
}
function Ensure-TC([string]$TcExe,[string]$InitialPath) {
    if (-not (Get-Process -Name @('TOTALCMD64','TOTALCMD') -ErrorAction SilentlyContinue)) {
        Info '[TC] Total Commander is not running; starting it for navigation tests.'
        Start-Process -FilePath $TcExe -ArgumentList @(('/L="{0}"' -f $InitialPath)) | Out-Null
        Start-Sleep -Seconds 2
    }
    Navigate-TC $TcExe $InitialPath
}
function Get-EngineLogTail([long]$StartLength) {
    if (-not (Test-Path -LiteralPath $EngineLog)) { return '' }
    $stream = [IO.File]::Open($EngineLog,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    try {
        if ($StartLength -lt 0 -or $StartLength -gt $stream.Length) { $StartLength = 0 }
        [void]$stream.Seek($StartLength,[IO.SeekOrigin]::Begin)
        $reader = New-Object IO.StreamReader($stream,$Utf8,$true,4096,$true)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}
function Wait-EngineLog([long]$StartLength,[string]$Pattern,[int]$TimeoutMs=10000) {
    Wait-Until { $tail = Get-EngineLogTail $StartLength; if ($tail -match $Pattern) { return $tail }; return $null } $TimeoutMs 250
}
function Log-EngineTrace([string]$Text) {
    if ([string]::IsNullOrWhiteSpace($Text)) { return }
    $lines = @($Text -split "`r?`n" | Where-Object { $_ -match '(?i)D:\\Temp\\FHM|\[LIFECYCLE\]|\[FILE_WRITE\]|\[DIAG_FS\]|\[NAV-TC\]' } | Select-Object -Last 80)
    foreach ($line in $lines) { if ($line) { Info ("[ENGINE-TRACE] $line") } }
}

try {
    Assert-ExactWorkspace
    Clean-Workspace
    New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
    [IO.File]::WriteAllText($LogPath,'',$Utf8)

    Info '============================================================'
    Info 'FolderHeatMap automated regression tests'
    Info ("Test version: $TestVersion")
    Info ('Started:      ' + [DateTime]::Now.ToString('dd.MM.yyyy HH:mm:ss.fff'))
    Info ("Run ID:       $RunId")
    Info ("Repository:   $Repo")
    Info ("Workspace:    $Workspace")
    Info ("Engine log:   $EngineLog")
    Info '============================================================'

    TestHeader 'Prerequisites and clean workspace'
    Pass 'Safety barrier locked to D:\Temp\FHM.'
    if ($CleanupItems.Count -gt 0) {
        Info ("[WORKSPACE] Previous content found: " + ($CleanupItems -join ', '))
        Pass ("Removed $($CleanupItems.Count) previous workspace item(s).")
    } else {
        Info '[WORKSPACE] No previous content found.'
        Pass 'Workspace started clean.'
    }

    $tc = Resolve-TC
    if (-not $tc.Exe -or -not (Test-Path -LiteralPath $tc.Exe)) { throw 'Total Commander executable was not found.' }
    Pass ("Total Commander found: $($tc.Exe)")
    if (-not $tc.Ini) { throw 'Total Commander INI was not found.' }
    $tcIni = [Environment]::ExpandEnvironmentVariables([string]$tc.Ini)
    $settingsDir = Split-Path -Parent $tcIni
    if (-not $settingsDir) { throw "Could not derive FolderHeatMap data directory from TC INI: $tcIni" }
    $database = Join-Path $settingsDir 'FolderHeatMap.db'
    if (-not (Test-Path -LiteralPath $database)) { throw "FolderHeatMap database not found: $database" }
    Pass ("FolderHeatMap database found: $database")
    if (-not (Get-Process -Name 'FolderHeatMapEngine' -ErrorAction SilentlyContinue)) { throw 'FolderHeatMapEngine is not running.' }
    Pass 'FolderHeatMapEngine is running.'

    $src = Join-Path $Workspace 'SRC'
    $dst = Join-Path $Workspace 'DST'
    $hotDir = Join-Path $src 'HOT_DIR'
    $hotDirDst = Join-Path $dst 'HOT_DIR'
    $file = Join-Path $src 'hot_file.txt'
    $fileDst = Join-Path $dst 'hot_file.txt'
    New-Item -ItemType Directory -Path $src,$dst,$hotDir -Force | Out-Null
    [IO.File]::WriteAllText((Assert-InWorkspace $file),"FolderHeatMap automated test $RunId`r`n",$Utf8)
    Pass 'Test tree created.'

    Ensure-TC $tc.Exe $src

    TestHeader 'Directory navigation heat preparation'
    $navLogStart = if (Test-Path -LiteralPath $EngineLog) { (Get-Item -LiteralPath $EngineLog).Length } else { 0 }
    for ($i=1; $i -le 4; $i++) {
        Navigate-TC $tc.Exe $hotDir
        Start-Sleep -Milliseconds 1100
        Navigate-TC $tc.Exe $src
        Start-Sleep -Milliseconds 1100
        Info ("[NAV] Completed visit cycle $i/4")
    }
    $folderBefore = Wait-Until { $s=Get-FolderState $database $hotDir; if ($s -and $s.visits -ge 4) { return $s }; return $null } 12000 300
    if ($folderBefore) {
        Pass ("Directory history prepared: Visits=$($folderBefore.visits), heat_visits=$($folderBefore.heat_visits)")
    } else {
        ErrorResult 'Directory Visits did not reach the expected value after TC navigation.'
        $folderBefore = Get-FolderState $database $hotDir
    }
    $navTail = Get-EngineLogTail $navLogStart
    if ($navTail -match ([regex]::Escape('NAV-TC') + '.*' + [regex]::Escape($hotDir))) { Pass 'Engine log confirms independent TC navigation for HOT_DIR.' } else { ErrorResult 'Engine log does not contain the expected NAV-TC entry for HOT_DIR.' }
    Log-EngineTrace $navTail
    $dirIdBefore = [FhmNative]::FileIdentity($hotDir,$true)
    if ($dirIdBefore) { Pass ("Directory File ID acquired: $dirIdBefore") } else { ErrorResult 'Could not acquire directory Volume Serial + File ID.' }

    TestHeader 'File write heat preparation'
    Navigate-TC $tc.Exe $src
    Start-Sleep -Milliseconds 500
    $writeLogStart = if (Test-Path -LiteralPath $EngineLog) { (Get-Item -LiteralPath $EngineLog).Length } else { 0 }
    $fileInitial = Get-FileState $database $file
    $initialWrites = if ($fileInitial) { [long]$fileInitial.write_events } else { 0 }
    for ($i=1; $i -le 3; $i++) {
        [IO.File]::AppendAllText((Assert-InWorkspace $file),("write-$i " + [DateTime]::Now.ToString('O') + "`r`n"),$Utf8)
        Info ("[WRITE] Logical write $i/3 completed.")
        Start-Sleep -Milliseconds 1300
    }
    $fileBefore = Wait-Until { $s=Get-FileState $database $file; if ($s -and $s.write_events -ge ($initialWrites + 3)) { return $s }; return $null } 12000 300
    if ($fileBefore) {
        Pass ("File history prepared: Writes=$($fileBefore.write_events)")
    } else {
        ErrorResult ("File Writes did not increase by three logical writes. Initial=$initialWrites")
        $fileBefore = Get-FileState $database $file
    }
    $writeTail = Get-EngineLogTail $writeLogStart
    if ($writeTail -match ([regex]::Escape('FILE_WRITE') + '.*' + [regex]::Escape($file))) { Pass 'Engine log confirms file-write tracking for hot_file.txt.' } else { ErrorResult 'Engine log does not contain expected FILE_WRITE diagnostics for hot_file.txt.' }
    Log-EngineTrace $writeTail
    $fileIdBefore = [FhmNative]::FileIdentity($file,$false)
    if ($fileIdBefore) { Pass ("File File ID acquired: $fileIdBefore") } else { ErrorResult 'Could not acquire file Volume Serial + File ID.' }

    TestHeader 'Same-volume directory MOVE SRC -> DST'
    Navigate-TC $tc.Exe $src
    $logStart = if (Test-Path -LiteralPath $EngineLog) { (Get-Item -LiteralPath $EngineLog).Length } else { 0 }
    Move-Item -LiteralPath (Assert-InWorkspace $hotDir) -Destination (Assert-InWorkspace $hotDirDst) -Force
    Pass 'Directory filesystem move completed.'
    $dirIdAfter = [FhmNative]::FileIdentity($hotDirDst,$true)
    if ($dirIdBefore -and $dirIdAfter -and $dirIdBefore -eq $dirIdAfter) { Pass 'Directory Volume Serial + File ID preserved across MOVE.' }
    else { ErrorResult ("Directory identity changed across MOVE. before='$dirIdBefore' after='$dirIdAfter'") }
    $folderAfter = Wait-Until { Get-FolderState $database $hotDirDst } 12000 300
    $folderFields = @('visits','last_visit','heat_visits','recent_visits','active_days','first_active_day','last_active_day','last_effective_visit')
    if ($folderBefore -and $folderAfter -and (State-Signature $folderBefore $folderFields) -eq (State-Signature $folderAfter $folderFields)) {
        Pass ("Directory history preserved: " + (State-Signature $folderAfter $folderFields))
    } else {
        ErrorResult ("Directory history mismatch after MOVE. before=[" + (State-Signature $folderBefore $folderFields) + "] after=[" + (State-Signature $folderAfter $folderFields) + "]")
    }
    if (Get-FolderState $database $hotDir) { ErrorResult 'Old directory DB path still has active history after MOVE.' } else { Pass 'Old directory DB path no longer has active history.' }
    $moveLog = Wait-EngineLog $logStart ([regex]::Escape('move_migrated old') + '.*' + [regex]::Escape($hotDir)) 12000
    if ($moveLog) { Pass 'Lifecycle log confirms directory move_migrated old/new handling.'; Log-EngineTrace $moveLog }
    else { ErrorResult 'Lifecycle log did not confirm directory move_migrated handling.'; Log-EngineTrace (Get-EngineLogTail $logStart) }

    TestHeader 'Same-volume file MOVE SRC -> DST'
    Navigate-TC $tc.Exe $src
    $logStart = if (Test-Path -LiteralPath $EngineLog) { (Get-Item -LiteralPath $EngineLog).Length } else { 0 }
    Move-Item -LiteralPath (Assert-InWorkspace $file) -Destination (Assert-InWorkspace $fileDst) -Force
    Pass 'File filesystem move completed.'
    $fileIdAfter = [FhmNative]::FileIdentity($fileDst,$false)
    if ($fileIdBefore -and $fileIdAfter -and $fileIdBefore -eq $fileIdAfter) { Pass 'File Volume Serial + File ID preserved across MOVE.' }
    else { ErrorResult ("File identity changed across MOVE. before='$fileIdBefore' after='$fileIdAfter'") }
    $fileAfter = Wait-Until { Get-FileState $database $fileDst } 12000 300
    $fileFields = @('write_events','last_write','active_days','first_active_day','last_active_day')
    if ($fileBefore -and $fileAfter -and (State-Signature $fileBefore $fileFields) -eq (State-Signature $fileAfter $fileFields)) {
        Pass ("File history preserved: " + (State-Signature $fileAfter $fileFields))
    } else {
        ErrorResult ("File history mismatch after MOVE. before=[" + (State-Signature $fileBefore $fileFields) + "] after=[" + (State-Signature $fileAfter $fileFields) + "]")
    }
    if (Get-FileState $database $file) { ErrorResult 'Old file DB path still has active history after MOVE.' } else { Pass 'Old file DB path no longer has active history.' }
    $moveLog = Wait-EngineLog $logStart ([regex]::Escape('move_migrated old') + '.*' + [regex]::Escape($file)) 12000
    if ($moveLog) { Pass 'Lifecycle log confirms file move_migrated old/new handling.'; Log-EngineTrace $moveLog }
    else { ErrorResult 'Lifecycle log did not confirm file move_migrated handling.'; Log-EngineTrace (Get-EngineLogTail $logStart) }

    TestHeader 'MOVE back DST -> SRC'
    Navigate-TC $tc.Exe $dst
    $dirStateAtDst = Get-FolderState $database $hotDirDst
    $fileStateAtDst = Get-FileState $database $fileDst
    Move-Item -LiteralPath (Assert-InWorkspace $hotDirDst) -Destination (Assert-InWorkspace $hotDir) -Force
    Start-Sleep -Milliseconds 800
    $folderBack = Wait-Until { Get-FolderState $database $hotDir } 12000 300
    if ($dirStateAtDst -and $folderBack -and (State-Signature $dirStateAtDst $folderFields) -eq (State-Signature $folderBack $folderFields)) { Pass 'Directory history preserved on MOVE back.' }
    else { ErrorResult 'Directory history was not preserved on MOVE back.' }

    Navigate-TC $tc.Exe $dst
    Move-Item -LiteralPath (Assert-InWorkspace $fileDst) -Destination (Assert-InWorkspace $file) -Force
    Start-Sleep -Milliseconds 800
    $fileBack = Wait-Until { Get-FileState $database $file } 12000 300
    if ($fileStateAtDst -and $fileBack -and (State-Signature $fileStateAtDst $fileFields) -eq (State-Signature $fileBack $fileFields)) { Pass 'File history preserved on MOVE back.' }
    else { ErrorResult 'File history was not preserved on MOVE back.' }

    $dirIdBack = [FhmNative]::FileIdentity($hotDir,$true)
    $fileIdBack = [FhmNative]::FileIdentity($file,$false)
    if ($dirIdBefore -eq $dirIdBack) { Pass 'Directory File ID still matches original after round trip.' } else { ErrorResult 'Directory File ID differs after round trip.' }
    if ($fileIdBefore -eq $fileIdBack) { Pass 'File File ID still matches original after round trip.' } else { ErrorResult 'File File ID differs after round trip.' }

} catch {
    ErrorResult ("Unhandled test exception: $($_.Exception.Message)")
    Info ("[EXCEPTION] " + $_.ScriptStackTrace)
} finally {
    Write-LogLine ''
    Write-LogLine '============================================================'
    Write-LogLine 'FolderHeatMap automated test summary'
    Write-LogLine ("PASS:    $PassCount") Green
    Write-LogLine ("ERROR:   $ErrorCount") $(if ($ErrorCount -gt 0) { [ConsoleColor]::Red } else { [ConsoleColor]::Green })
    Write-LogLine ("WARNING: $WarningCount") $(if ($WarningCount -gt 0) { [ConsoleColor]::Yellow } else { [ConsoleColor]::Gray })
    if ($ErrorCount -eq 0) { Write-LogLine 'RESULT: PASS' Green } else { Write-LogLine 'RESULT: ERROR' Red }
    Write-LogLine ("Log: $LogPath") Gray
    Write-LogLine 'Test data are intentionally kept for diagnosis until the next test run.' Gray
    Write-LogLine '============================================================'
}

if ($ErrorCount -gt 0) { exit 1 }
exit 0
