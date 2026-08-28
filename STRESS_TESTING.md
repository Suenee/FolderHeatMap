# FolderHeatMap lifecycle stress tests

Version 1.38 keeps the baseline, stress, and lifecycle diagnostic suites from 1.37, and changes the test handoff so a completed baseline regression failure no longer prevents the diagnostic stage from running. This is specifically intended to capture evidence for the reproducible file MOVE round-trip mismatch observed in the baseline suite.

Run `test.cmd` from the repository root. The launcher syntax-checks `test.ps1`, `test_stress.ps1`, and `test_lifecycle_diag.ps1` with `System.Management.Automation.Language.Parser` before any test executes. The baseline suite still runs first. When baseline passes, stress runs second and lifecycle diagnostics run afterward. When baseline finishes with an assertion failure, the stress stage is skipped, lifecycle diagnostics still run, and `test.cmd` returns the original baseline failure code after diagnostics complete.

A parser error, missing prerequisite, or other failure that prevents a test script from running safely remains a hard stop. The 1.38 change only preserves diagnostic execution after a completed baseline regression result.

Before destructive test cleanup begins, Total Commander is explicitly navigated to the fixed non-destructive release path `D:\Temp`. Destructive filesystem operations remain hard-limited to the exclusive workspace `D:\Temp\FHM\`.

The original stress suite still covers:

1. Rapid repeated same-volume directory moves between two parents while verifying File ID and complete persistent history.
2. Directory rename with identity/history preservation and old-path cleanup.
3. File rename with identity/history preservation and old-path cleanup.
4. Recursive deletion of a populated heated subtree and verification that descendant folder/file history is purged.
5. Recreation of the same subtree paths with new filesystem identities.
6. Immediate delete/recreate race handling.
7. Move followed immediately by a file write.
8. Moving a parent directory that contains heated descendant directories and files.
9. Controlled FolderHeatMapEngine restart persistence.
10. Workspace reuse cleanup.

The lifecycle diagnostic stage focuses on move/rename convergence and watcher visibility:

- Rapid MOVE is tested twice: a normal 1500 ms cadence and the original 450 ms stress cadence. Each variant waits up to 10 seconds for the complete history signature to converge instead of merely waiting for a destination database row to exist.
- Directory and file RENAME diagnostics print before/after File IDs, full database state, old-path state, and matching engine log lines for the unique rename paths. This makes `RENAMED_OLD_NAME`, `RENAMED_NEW_NAME`, `REMOVED`, and lifecycle migration behavior visible without changing runtime code.
- MOVE plus immediate write is split into independent assertions. The diagnostic first verifies that MOVE alone preserved identity/history, then checks whether an immediate destination write is observed before Total Commander watches the destination, and finally performs a control write after navigating Total Commander to the destination.
- Missing immediate destination-side write observation before destination navigation is reported as a warning rather than automatically being classified as a MOVE lifecycle regression.

Logs are written to:

- `D:\Temp\FHM\logs\stress-YYYYMMDD-HHMMSS.log`
- `D:\Temp\FHM\logs\diagnostic-YYYYMMDD-HHMMSS.log`

PASS results are green, ERROR results are red, warnings are yellow, and headings are cyan. Compare filesystem File IDs, SQLite state, and the relevant `[LIFECYCLE]`, `[DIAG_FS]`, `[FILE_WRITE]`, and `[NAV-TC]` engine log entries before changing runtime lifecycle logic.
