# FolderHeatMap lifecycle stress tests

Version 1.34 adds a second automated regression stage, `test_stress.ps1`. It runs only after the proven baseline `test.ps1` suite completes successfully.

Run `test.cmd` from the repository root. The launcher first parses both `test.ps1` and `test_stress.ps1` with `System.Management.Automation.Language.Parser`. Any syntax error stops execution before either test stage starts.

All destructive filesystem operations are hard-limited to the exclusive workspace `D:\Temp\FHM\`. The stress suite may stop and restart `FolderHeatMapEngine.exe` for the persistence scenario, but it does not delete or move filesystem objects outside the test workspace.

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

The stress log is written to `D:\Temp\FHM\logs\stress-YYYYMMDD-HHMMSS.log`. As with the baseline suite, PASS results are green, ERROR results are red, warnings are yellow, and headings are cyan.

A stress failure should be diagnosed as either a test-runner issue or a FolderHeatMap runtime lifecycle regression. In particular, compare filesystem File IDs, SQLite state, and the relevant `[LIFECYCLE]`, `[DIAG_FS]`, `[FILE_WRITE]`, and `[NAV-TC]` engine log entries before changing runtime lifecycle logic.
