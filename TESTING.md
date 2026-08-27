# FolderHeatMap test protocol

Target: Total Commander 11.58 x64 on Windows 10+.

FolderHeatMap stores activity persistently in SQLite. Since 1.22, `FolderHeatMapEngine.exe` is an independent background process: it observes Total Commander panel navigation directly through native Win32 controls, while the WDX plugin is primarily a cache/display client.

## Automated MOVE regression test (1.31)

Run `test.cmd` from the repository root. The launcher executes `test.ps1` and uses `D:\Temp\FHM\` as the exclusive test workspace.

The test runner deliberately removes all existing contents of `D:\Temp\FHM\` at startup so stale data from an interrupted previous run cannot affect the next run. It never writes, moves, renames, or deletes test filesystem objects outside that workspace. Any future test that needs a destructive filesystem operation elsewhere on `D:\` must stop and require explicit user approval before that operation is implemented or performed.

The current automated suite:

1. Verifies Total Commander, `FolderHeatMapEngine.exe`, and the persistent SQLite database are available.
2. Creates `SRC`, `DST`, `HOT_DIR`, and `hot_file.txt` under `D:\Temp\FHM\`.
3. Drives real Total Commander left-panel navigation through `/O /L=...` to create directory Visits through the independent TC navigation monitor.
4. Performs three real filesystem writes while the source directory is watched and verifies the persistent `file_activity.write_events` counter.
5. Captures native `Volume Serial + FILE_ID_128` identities before the move.
6. Moves the directory and file from `SRC` to `DST` on the same `D:` volume using normal filesystem MOVE operations.
7. Reads FolderHeatMap SQLite state through the Windows `winsqlite3.dll` API and verifies the complete relevant persistent history is attached to the destination path without changes.
8. Verifies the old source path no longer retains active FolderHeatMap history.
9. Verifies `[LIFECYCLE] move_migrated old/new` diagnostics in `logs\FolderHeatMap.log`.
10. Moves both objects back from `DST` to `SRC` and verifies history and File IDs survive the round trip.

Console output is intentionally compact: successful assertions are green `[PASS]`, failures are red `[ERROR]`, warnings are yellow, and test headings are cyan. A failure does not intentionally stop later independent assertions unless a prerequisite or unhandled exception makes continuation unsafe.

Each run writes a detailed diagnostic log to `D:\Temp\FHM\logs\test-YYYYMMDD-HHMMSS.log`. Test data are kept after the run for diagnosis and are removed automatically at the beginning of the next run.

The test runner treats database values as the authoritative automated assertion for Visits/Writes history. Visible WDX Heat/color remains a separate UI-level check because the displayed heat value is calculated from persistent activity and current cooling settings.

## Upgrade and build

1. Run `upgrade.cmd` from the repository root.
2. Verify the final line is `STATUS: SUCCESS - phase=COMPLETE` (or an explicitly understood warning).
3. The script builds `dist\FolderHeatMap.wdx64`, `dist\FolderHeatMapEngine.exe`, `dist\FolderHeatMapConfig.exe` and `dist\FolderHeatMapReset.exe`.
4. The upgrader registers `start_engine.ps1` under the current user's Windows startup and starts the independent engine immediately.
5. All runtime/upgrade logs remain under `logs\` and are ignored by Git.

## Independent navigation test (1.22)

Purpose: prove that Visits are recorded even when no FolderHeatMap custom columns are visible.

1. Start Total Commander in a normal view which does **not** display Heat/Visits/Writes.
2. Choose a test directory and note its Visits value before the test (you may briefly enable the FolderHeatMap view to read it, then return to the normal view).
3. With FolderHeatMap columns hidden, enter the test directory, leave it, wait at least one whole second, and repeat this at least four times.
4. Re-enable the FolderHeatMap columns only after all visits are complete.
5. Verify that Visits increased by the accepted visits performed while the columns were hidden.
6. Inspect `logs\FolderHeatMap.log`. Each independent panel navigation should appear as `[NAV-TC] LEFT accepted ...` or `[NAV-TC] RIGHT accepted ...`.
7. WDX diagnostic callbacks may appear separately as `[DIAG_TC]`; they must not be required for Visits to increase.

## File write tracking test (1.28)

Purpose: prove that writes to existing files update `Writes` and file heat without relying on a Total Commander refresh callback.

1. Enter a test directory and choose an existing text/document file whose current `Writes` value is known.
2. Open the file in an editor, change its content, save once, and keep Total Commander in the same directory.
3. Verify `logs\FolderHeatMap.log` contains `[DIAG_FS] action=MODIFIED`, then `[FILE_WRITE] accepted` and `[FILE_WRITE] persisted` for that file.
4. Refresh the Total Commander view if required for display only; verify `Writes` increased by one and file heat/color reacts.
5. Save the same file again after more than one second and verify another write is counted.
6. Perform an application save that produces several filesystem notifications close together. `[FILE_WRITE] coalesced` may appear, but one logical save must not inflate `Writes` several times.
7. Confirm the parent directory `Visits` value did not increase merely because a child file was saved.

## One-second debounce test

1. Keep a test directory selected and trigger rapid repeated navigation/Enter actions within the same whole second.
2. Verify that the same path is counted at most once for that second.
3. Repeat the same visit in a later second and verify that it is counted again.
4. Normal revisits such as `A -> B -> A` must continue to count when they occur in different seconds.

## Two-panel test

1. Put different directories in the left and right Total Commander panels.
2. Navigate independently in the left panel, then in the right panel.
3. Verify that `[NAV-TC] LEFT` and `[NAV-TC] RIGHT` log entries correspond to the correct sides.
4. Changing one panel must not create a false visit for the unchanged panel.

## Engine lifetime test

1. Confirm `FolderHeatMapEngine.exe` is running.
2. Switch Total Commander from a FolderHeatMap custom-column view to a normal view.
3. Confirm the engine remains running.
4. Close Total Commander completely and confirm the engine may remain sleeping in the background.
5. Start Total Commander again without selecting a FolderHeatMap view and perform the independent navigation test again.
6. After a Windows sign-out/sign-in or reboot, verify that the engine starts automatically from the current-user startup registration.

## Configuration GUI

Run `configure.cmd` from the repository root.

The GUI controls cooling, path heat contribution, file contribution where enabled, and Total Commander color mapping. Heat level 0 never receives a FolderHeatMap color override; Total Commander keeps its normal color.

## Color-map test

1. Run `configure.cmd`.
2. Keep the default 1-7 colors for the first test.
3. Keep smooth transitions enabled.
4. Apply the settings and refresh/restart Total Commander as requested by the configurator.
5. Return to a directory containing folders with different Heat values.
6. Verify that unused/zero-heat folders keep their original Total Commander color and hotter folders move through the configured gradient.

## Path heat test

1. Enable descendant/path heat and choose a path-decay value.
2. Make a deep child folder hot by visiting it several times.
3. Return to its parent, grandparent and higher levels.
4. Verify that Visits reflects direct visits while Heat on ancestors can receive progressively weaker inherited contribution.
5. Disable path heat and verify ancestors return to direct-only Heat.

## Persistence test

1. Navigate through several directories and revisit one directory several times.
2. Note its Visits value.
3. Close Total Commander.
4. Start Total Commander again.
5. Verify that Visits/Heat persist and continue from the stored value.

## Canonical identity: rename test

1. Create and visit a test directory several times.
2. Rename it on the same volume.
3. Verify that the renamed directory keeps the same Visits/Heat history.
4. Repeat with a file that already has several Writes; rename it and verify Writes/Heat are preserved.
5. The canonical Volume Serial + 128-bit File ID must remain the same before and after the rename.

## Identity-first same-volume move test (1.29)

Purpose: verify that a source-side `REMOVED` event cannot destroy history before the same File ID is reconciled at its destination.

1. Create `D:\FHM_MOVE_SOURCE` and `D:\FHM_MOVE_TARGET`.
2. Create `D:\FHM_MOVE_SOURCE\HOT_DIR`, visit it repeatedly, and note its Visits/Heat.
3. Create `D:\FHM_MOVE_SOURCE\hot_file.txt`, save it repeatedly with more than one second between logical writes, and note its Writes/Heat.
4. Move `HOT_DIR` to `D:\FHM_MOVE_TARGET\HOT_DIR` without changing volume.
5. Move `hot_file.txt` to `D:\FHM_MOVE_TARGET\hot_file.txt` without changing volume.
6. Verify directory Visits/Heat and file Writes/Heat are preserved at the destination.
7. Verify the log contains `[LIFECYCLE] move_migrated old` and `[LIFECYCLE] move_migrated new` for the moved objects, or an equivalent SLOW lifecycle move action.
8. Verify the Volume Serial and File ID are identical before and after each move.
9. Verify the old source paths no longer retain visible history/cache entries.

## Move race stress test (1.29)

1. Heat a directory containing several heated files.
2. Move the entire directory between two parent directories on the same volume several times in quick succession.
3. Refresh Total Commander after the moves.
4. Verify the directory and descendant file histories remain attached to the same filesystem objects rather than resetting or duplicating.
5. The log must not contain `purge_subtree completed` for an object that is simultaneously resolved under a new same-volume path with the same File ID.

## Canonical identity: external delete/recreate test

1. Create and heat a test directory.
2. Delete it outside Total Commander (for example from a command prompt).
3. Recreate the same path as a new directory.
4. Navigate so SLOW lifecycle reconciliation observes it.
5. Verify the recreated filesystem object starts cold and does not inherit the deleted object's history.
6. Repeat with a file that has non-zero Writes; a newly created file at the same path with a different File ID must start with new history.

## Delete watcher test

1. Create and heat a test directory.
2. Delete it through Total Commander.
3. Verify an immediate watcher `REMOVED`/tombstone path appears in the log.
4. Recreate the same directory name and verify it starts cold.
5. Drive roots such as `D:\` must never be tombstoned or recursively purged.
6. A move to `$Recycle.Bin` is intentionally treated as deletion and must not preserve the object's active history as a normal move.
