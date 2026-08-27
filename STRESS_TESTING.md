# FolderHeatMap lifecycle stress tests

Version 1.35 keeps the second automated regression stage, `test_stress.ps1`, and fixes the handoff from the baseline test suite. The stress stage runs only after the proven baseline `test.ps1` suite completes successfully.

Run `test.cmd` from the repository root. The launcher first parses both `test.ps1` and `test_stress.ps1` with `System.Management.Automation.Language.Parser`. Any syntax error stops execution before either test stage starts.

Before stress cleanup begins, Total Commander is explicitly navigated to the fixed non-destructive release path `D:\Temp`. The runner waits for the panel/watcher handoff and then removes the old contents of `D:\Temp\FHM`. Cleanup retries transiently locked workspace items for up to five seconds. The release navigation is the only stress-start operation outside the sandbox and is never destructive.

All destructive filesystem operations remain hard-limited to the exclusive workspace `D:\Temp\FHM\`. The stress suite may stop and restart `FolderHeatMapEngine.exe` for the persistence scenario, but it does not delete, rename, move, or modify filesystem objects outside the test workspace.

The stress suite covers:

1. Rapid repeated same-volume directory moves between two parents while verifying File ID and complete persistent history.
2. Directory rename with identity/history preservation and old-path cleanup.
3. File rename with identity/history preservation and old-path cleanup.
4. Recursive deletion of a populated heated subtree and verification that descendant folder/file history is purged.
5. Recreation of the same subtree paths with new filesystem identities.
6. Immediate delete/recreate race handling, where the replacement object is created before normal SLOW cleanup latency can be assumed complete.
7. Move followed immediately by a file write, verifying that migrated history is retained and the new destination-side write is added.
8. Moving a parent directory that contains heated descendant directories and files, verifying descendant histories follow their filesystem objects.
9. Controlled FolderHeatMapEngine restart, verifying persistent Visits/Writes history and filesystem identities remain intact after restart.
10. A workspace-reuse sentinel left intentionally for the following run so startup cleanup can be exercised again.

The stress log is written to `D:\Temp\FHM\logs\stress-YYYYMMDD-HHMMSS.log`. PASS results are green, ERROR results are red, warnings are yellow, and headings are cyan. Version 1.35 also fixes the final PASS/ERROR output calls so PowerShell does not concatenate the `Write-LogLine` function name with the result text at runtime.

A stress failure should be diagnosed as either a test-runner issue or a FolderHeatMap runtime lifecycle regression. In particular, compare filesystem File IDs, SQLite state, and the relevant `[LIFECYCLE]`, `[DIAG_FS]`, `[FILE_WRITE]`, and `[NAV-TC]` engine log entries before changing runtime lifecycle logic.
