# Changelog

## 1.29 - 27.08.2026

- Fixed same-volume moves that could lose visible FolderHeatMap history when a watched object was removed from one parent before the destination parent was reconciled.
- Changed watcher removal handling to identity-first semantics: `FILE_ACTION_REMOVED` is treated as a change hint, not immediate proof that the filesystem object was destroyed.
- Before recursive purge, SLOW now resolves the previously tracked object by `Volume Serial + File ID` and retries briefly to cover source/destination notification races.
- If the same object still exists elsewhere on the same volume, `MoveTrackedObject()` atomically migrates directory Visits/usage, descendant file activity, tracked identities, or file Writes to the new path.
- Recycle-bin moves remain deletion semantics and are intentionally not migrated as normal moves.
- If a move is detected but database migration fails, history is preserved instead of being destructively purged; destination-parent reconciliation is queued as a recovery path.
- The same-volume move build stage now fails configuration when its expected lifecycle anchor is missing instead of silently skipping the protection.
- Added `[LIFECYCLE] move_migrated old/new` and `move_migration FAILED old/new` diagnostics.
- Version updated to 1.29 (`1.29-identity-first-moves`).

## 1.28 - 27.08.2026

- Added live file-write tracking for existing files. The filesystem watcher now subscribes to `FILE_NOTIFY_CHANGE_LAST_WRITE` and `FILE_NOTIFY_CHANGE_SIZE` in addition to name/creation changes.
- `FILE_ACTION_MODIFIED` events for files are forwarded into the engine activity pipeline and persisted through the existing `ObserveFileWrite()` database path.
- Coalesced repeated modification notifications for the same file within one second so one application save does not inflate `Writes` because Windows emitted several filesystem notifications.
- File write updates refresh both the file cache entry and its parent directory heat contribution without incrementing the parent directory `Visits` counter.
- Kept deletion/rename lifecycle handling isolated; file-write tracking is injected by a separate guarded CMake stage so the proven delete lifecycle remains unchanged.
- Added `[FILE_WRITE] accepted`, `[FILE_WRITE] coalesced`, and `[FILE_WRITE] persisted` diagnostics.
- Version updated to 1.28 (`1.28-file-write-tracking`).

## 1.27 - 27.08.2026

- Confirmed with `tasklist /m FolderHeatMap.wdx64` that a restarted `TOTALCMD64.EXE` process was the persistent owner of the live `dist\FolderHeatMap.wdx64` lock during deployment.
- Added a pre-deploy Total Commander guard that rechecks all `TOTALCMD64`/`TOTALCMD` processes immediately before the first live artifact replacement.
- If Total Commander appears again during WDX copy retries, the upgrader now records its PID, force-stops it, waits for process exit, and retries the copy instead of merely waiting on the lock.
- Total Commander is restarted only after the complete deployment has succeeded; a final guard runs before that restart.
- Updated build/runtime version identification to 1.27 (`1.27-tc-deploy-guard`).

## 1.26 - 27.08.2026

- Restored the complete `CMakeLists.txt` build graph after an accidental version-only replacement left the file with only `cmake_minimum_required()` and `project()`, causing CMake configuration to succeed without generating `FolderHeatMap.vcxproj` and MSBuild to fail with `MSB1009`.
- Fixed the runtime engine banner, which was still reporting the historical `1.20 canonical lifecycle engine` baseline even in newer binaries.
- Confirmed from runtime diagnostics that the uploaded engine was not actually 1.20: it already contained the independent `[NAV-TC]` monitor introduced in 1.22; only the startup version string was stale.
- The final TC navigation injection stage now requires the expected 1.20 baseline banner and replaces it with `FolderHeatMap 1.26 engine starting (independent TC navigation)`, making a stale build detectable during CMake configuration instead of silently shipping a misleading banner.
- Hardened `start_engine.ps1` so a newly started engine is checked for immediate process failure.
- Version updated to 1.26 (`1.26-runtime-version-fix`).

## 1.25 - 27.08.2026

- Separated package staging from the live `dist` deployment so the DIST phase never overwrites a loaded Total Commander plugin.
- New artifacts are staged under `build/package` and verified before any live files are replaced.
- Live deployment now retries locked targets for up to 30 seconds and reports lock progress in `logs/upgrade.log`.
- After forced shutdown, the upgrader now verifies that Total Commander and `FolderHeatMapEngine.exe` actually exited before deployment continues.
- A persistent file lock now fails in `DEPLOY`, not `DIST`, with the exact target path and last Windows error.
- Version updated to 1.25 (`1.25-deploy-lock-fix`).

## 1.24 - 26.08.2026

- Fixed the remaining self-update failure where `git stash` could report success while `upgrade.cmd` still appeared locally modified because of Windows line-ending materialization.
- Bootstrap files `upgrade.cmd` and `upgrade.ps1` are no longer treated as user state during local-change detection.
- Real tracked edits outside the bootstrap files are preserved through the managed pre-upgrade stash.
- After preservation, the tracked installation tree is synchronized deterministically to `origin/devel`; untracked runtime data remains untouched.
- Kept the authoritative temporary `origin/devel:upgrade.ps1` runner model and post-sync `HEAD == origin/devel` verification.
- Version updated to 1.24 (`1.24-bootstrap-sync-fix`).

## 1.23 - 26.08.2026

- Fixed the self-update bootstrap loop that could report `upgrade.cmd has local working-tree changes after self-repair` after a successful pull.
- Removed bootstrap working-tree repair/verification for `upgrade.cmd`; the launcher is no longer treated as a byte-identical runtime artifact after repository mutation.
- The authoritative upgrader remains the temporary `upgrade.ps1` extracted from `origin/devel` before execution, matching the documented upgrade protocol.
- Self-update verification now relies on `HEAD == origin/devel` plus Git blob identity for `upgrade.ps1`, avoiding CRLF/LF false positives on Windows working-tree files.
- Kept `.gitattributes` CRLF rules for Windows scripts unchanged.
- Version updated to 1.23 (`1.23-bootstrap-protocol-fix`).

## 1.22 - 26.08.2026

- Added an independent native Win32 Total Commander navigation monitor in `FolderHeatMapEngine.exe`.
- The engine now reads the left and right Total Commander panel paths through `WM_USER+50` and `GetWindowTextW`, so visit counting no longer depends on FolderHeatMap WDX columns being visible.
- Panel paths are sampled every 100 ms with `SendMessageTimeoutW` protection, avoiding any filesystem scan in the navigation sensor itself.
- Left and right panels are tracked independently; only a real path change creates a navigation candidate.
- Kept the one-visit-per-path-per-whole-second debounce to suppress rapid repeated Enter/technical duplicates without hiding later real revisits.
- Added `[NAV-TC] LEFT/RIGHT accepted ...` diagnostics. The WDX `ContentSendStateInformation` path remains temporarily as a diagnostic/reference channel for A/B verification.
- WDX unloading no longer requests engine shutdown. WDX is now treated as a display/cache client rather than the owner of engine lifetime.
- Added `start_engine.ps1` and automatic HKCU startup registration so the engine can run independently even when Total Commander never loads the FolderHeatMap content plugin view.
- `upgrade.cmd` now installs/starts the independent engine after a successful upgrade and keeps bootstrap failures under `logs/upgrade.log`.
- Upgrader console output is switched to UTF-8 to prevent Czech MSBuild text from being decoded through a mismatched console code page.
- Kept the 1.20 canonical filesystem lifecycle, watcher-delete handling, root-volume safety barrier and centralized `logs/` layout unchanged.
- Version updated to 1.22 (`1.22-independent-tc-navigation`).

## 1.21 - 26.08.2026

- Replaced permanent same-path navigation suppression with a per-path one-second debounce.
- A repeated visit to the same directory is now counted again when it occurs in a different whole second, even if the previous accepted directory path is identical.
- Multiple callbacks or repeated Enter presses targeting the same path within the same second are treated as one visit and do not inflate heat.
- Added explicit navigation diagnostics: `[NAV] accepted` and `[NAV] debounced same path within second`.
- Navigation counting remains independent of whether Total Commander currently displays FolderHeatMap content columns.
- Kept the 1.20 canonical filesystem lifecycle, watcher delete handling, root-volume safety barrier, and centralized `logs/` layout unchanged.
- Version updated to 1.21 (`1.21-navigation-debounce`).

## 1.20 - 26.08.2026

- Promoted the canonical native Win32 File ID from diagnostic validation into the authoritative SLOW lifecycle reconciliation path for same-volume move/rename and external delete/recreate detection.
- Confirmed lifecycle semantics remain: same volume + same File ID preserves history across rename/move; same path + different File ID is a new filesystem object and stale history is removed before the new observation is stored.
- Added safe cleanup of obsolete pre-1.19 tracked-object rows for volume roots. Only the stale `tracked_objects` identity row is removed; root heat/history and file activity are never reset by this migration.
- Kept watcher-confirmed `REMOVED` as the immediate fast delete signal with tombstone + prioritized SLOW subtree cleanup.
- Kept the hard drive-root tombstone/purge barrier and 10 MiB per-run engine-log safety cap.
- Centralized runtime logs under repository-local `logs/`: `logs/FolderHeatMap.log`, `logs/upgrade.log`, and migrated root-level `*.log` files.
- Added explicit `logs/` Git ignore protection; runtime logs remain outside version control.
- Upgrade metadata and build version updated to 1.20 (`1.20-canonical-lifecycle`).

## 1.19 - 25.08.2026

- Reworked filesystem identity around one canonical native Win32 primitive: `CreateFileW` + `GetFileInformationByHandleEx(FileIdInfo)` using `FILE_ID_INFO`.
- Canonical identity now carries both the 64-bit volume serial and the full 128-bit `FILE_ID_128`; the existing tracked-object database continues to store the per-volume 128-bit File ID while volume identity remains separate.
- Directory handles use `FILE_FLAG_BACKUP_SEMANTICS` and `FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`.
- A zero 128-bit File ID is treated as unsupported/invalid and can never trigger lifecycle invalidation or deletion.
- Added detailed non-destructive identity diagnostics showing path, stored File ID, current volume serial and current full 128-bit File ID, plus explicit match/unavailable states.
- Identity mismatch remains diagnostic-only. Watcher-confirmed `REMOVED` remains the only active destructive delete signal while the canonical identity primitive is being verified.
- Kept the 1.18 drive-root tombstone/purge safety barrier and 10 MiB per-run log cap.
- Added a dedicated build injection stage for canonical identity diagnostics so the identity experiment is isolated from the proven watcher-delete lifecycle.

## 1.18 - 25.08.2026

- Hotfixed the 1.17 lifecycle regression where a false identity mismatch on a healthy volume root could enter a destructive `identity_mismatch -> tombstone -> purge -> persist` loop, repeatedly clearing runtime data and flooding the engine log.
- Changed filesystem identity mismatch handling to diagnostic-only. A mismatch is logged as `identity_mismatch_non_destructive` but cannot tombstone, purge, or reset history until File ID semantics are verified separately.
- Added a hard safety barrier that refuses tombstone and subtree purge operations for drive roots such as `D:\`.
- Kept confirmed watcher `REMOVED` events active as the authoritative fast-delete path, including immediate tombstone invalidation and prioritized SLOW subtree cleanup.
- Kept same-volume move/rename protection and delete-queue coalescing from 1.17.
- Added a 10 MiB per-run safety cap to `FolderHeatMap.log`; once reached, engine logging disables itself for the remainder of that run instead of growing without bound.
- Version updated to 1.18 (`1.18-safe-lifecycle`).

## 1.17 - 24.08.2026

- Promoted the proven 1.16 filesystem diagnostics into the real deletion lifecycle while keeping the detailed diagnostic log active for verification.
- Added an immediate in-memory tombstone barrier for confirmed `REMOVED` filesystem events. The removed path/subtree is invalidated from published RAM immediately; recursive database cleanup is deferred to SLOW.
- Added a prioritized SLOW `purge_subtree` queue. The urgent path is O(1)-style invalidation/tombstoning; physical cleanup of folder history, file activity and tracked object identities runs afterward without blocking the watcher.
- Coalesced nested tombstones so a higher removed branch replaces descendant tombstones instead of creating redundant subtree cleanup work.
- Added same-volume move/rename protection so a watcher `REMOVED` event is not allowed to erase history when the same filesystem object still exists elsewhere on the same volume.
- Kept the detailed deletion diagnostics active (`[DIAG_FS]`, `[DIAG_DELETE]`, `[DIAG_SLOW]`, `[LIFECYCLE]`) so real-world delete timing and SLOW-worker load remain observable.
- Added deletion lifecycle coverage for directory trees: the highest removed branch can invalidate descendants immediately while recursive cleanup is deferred to SLOW.
- Version updated to 1.17 (`1.17-delete-lifecycle`).

## 1.16 - 24.08.2026

- Added diagnostic-only filesystem deletion instrumentation before changing deletion semantics.
- Added watcher logging for filesystem actions (`ADDED`, `REMOVED`, `MODIFIED`, `RENAMED_OLD_NAME`, `RENAMED_NEW_NAME`) including path, relative path, timestamps, and whether the object still exists when the event is processed.
- Added delete diagnostics showing whether a `REMOVED` event is observed before or after the filesystem object is already gone.
- Added SLOW-worker queue diagnostics at delete time so deletion latency can be correlated with worker load.
- Added diagnostics for delete/recreate timing and same-path reuse without changing the existing cleanup behavior.
- This release intentionally focuses on logging and measurement only; no new deletion behavior is enabled by the diagnostics themselves.

## 1.15 - 20.08.2026

- Replaced the previous mixed batch/PowerShell bootstrap argument passing with environment-only transport (`FHM_UPGRADE_INTERNAL`, `FHM_UPGRADE_STAGE`, `FHM_UPGRADE_REPO`, `FHM_UPGRADE_SCRIPT`).
- Added single-run `upgrade.log` diagnostics with gray informational output, yellow warnings, red errors and a final colored status line.
- Added self-update validation against `origin/devel` before build/deployment.
- Added robust Total Commander/engine shutdown handling and dependency/build diagnostics.
