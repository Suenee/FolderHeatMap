# Changelog

## 1.43 - 28.08.2026

- Fixed the remaining rapid MOVE round-trip history loss in SLOW lifecycle reconciliation.
- When a child is missing from one directory enumeration but the same canonical File ID has already returned to its original tracked relative path, the event is now treated as a stale snapshot instead of a deletion.
- The existing tracked row and Visits/Writes history are preserved unchanged for this returned-object case.
- A surviving File ID at a different relative path still produces a MOVE, while a genuinely missing File ID still produces DELETE semantics.
- Kept the 1.42 bounded File ID resolution grace in SLOW and left working RENAME, ordinary MOVE, file-write tracking and `identity_mismatch_non_destructive` behavior unchanged.
- Updated build/runtime/upgrader metadata to 1.43 (`1.43-rapid-move-returned-object`).

## 1.42 - 28.08.2026

- Added bounded SLOW reconciliation grace for canonical File ID resolution during MOVE/RENAME transitions.
- `OpenFileById` resolution is retried up to 12 times at 250 ms intervals before a missing tracked object can become a destructive DELETE action.
- Kept the FAST navigation path non-blocking; the grace window exists only in SLOW lifecycle reconciliation.
- Version updated to 1.42 (`1.42-rapid-move-reconciliation-grace`).

## 1.41 - 28.08.2026

- Added stale removal protection for rapid MOVE chains when a previous lifecycle task has already migrated the tracked-object row away from the old path.
- If the old path exists again while that stale removal is processed, watcher-side cleanup releases the tombstone and queues canonical SLOW reconciliation instead of immediately resetting history.
- Kept working same-directory RENAME, ordinary MOVE and file-write identity behavior unchanged.
- Version updated to 1.41 (`1.41-rapid-move-stale-task`).

## 1.40 - 28.08.2026

- Fixed same-directory RENAME handling by sending `FILE_ACTION_RENAMED_OLD_NAME` through the same identity-first lifecycle path used for same-volume MOVE.
- File-write handling now records canonical `Volume Serial + File ID` identity before persisting Writes, so recently created/written files are already present in `tracked_objects` before a subsequent MOVE/RENAME can be mistaken for deletion.
- Kept the existing identity-first delete protection: a surviving same-volume File ID migrates history, while genuine deletion and DELETE -> RECREATE with a different File ID remain destructive/new-object semantics.
- Added lifecycle diagnostics for write-driven identity migration and tracking failures.
- Added `upgrade.cmd --test`: tests run automatically only after a successful upgrade/deployment; failed upgrades never launch `test.cmd`, and the final exit code reflects the test result when tests are requested.
- Updated build/runtime/self-updating upgrader metadata to 1.40.
- Version updated to 1.40 (`1.40-identity-first-move-rename`).

## 1.39 - 28.08.2026

- Fixed `FolderHeatMapReset.exe` linker failures by restoring `src/Database.cpp` to the reset target so `Database::Open()` and `Database::~Database()` are linked.
- Kept runtime lifecycle behavior unchanged in this build hotfix.
- Version updated to 1.39 (`1.39-reset-link-hotfix`).

## 1.38 - 28.08.2026

- Changed `test.cmd` so a completed baseline regression failure no longer prevents `test_lifecycle_diag.ps1` from running.
- When the baseline suite returns an assertion failure, the stress stage is skipped, lifecycle diagnostics run immediately, and the launcher finally returns the original baseline failure code.
- This preserves diagnostic evidence for the reproducible file MOVE round-trip mismatch where File ID survives but the baseline file history comparison fails on `DST -> SRC`.
- Parser failures and unsafe prerequisite failures still stop execution before destructive diagnostic work begins.
- FolderHeatMap runtime lifecycle behavior remains unchanged in this release; 1.38 is a diagnostic handoff release intended to identify whether file history is altered during migration or afterward by SLOW reconciliation.
- Updated build/runtime/self-updating upgrader metadata to 1.38.
- Version updated to 1.38 (`1.38-baseline-diagnostic-handoff`).

## 1.37 - 28.08.2026

- Added `test_lifecycle_diag.ps1`, a third-stage diagnostic suite that runs after the existing stress suite to separate real FolderHeatMap lifecycle regressions from test assumptions before runtime lifecycle code is changed.
- Added two rapid MOVE convergence profiles: a normal 1500 ms cadence and the original 450 ms stress cadence. Both wait up to 10 seconds for the complete persistent history signature to converge instead of merely waiting for a destination database row to exist.
- Added detailed directory and file RENAME diagnostics with before/after File IDs, full SQLite history signatures, old-path state, and matching engine log lines for the unique rename paths.
- Split MOVE plus destination-write diagnostics into independent checks: MOVE identity/history preservation, an immediate destination write before Total Commander watches the destination, and a control write after navigating Total Commander to the destination.
- An unobserved immediate destination write outside the active watcher scope is reported as a warning instead of automatically being classified as a MOVE lifecycle regression.
- `test.cmd` now syntax-checks all three PowerShell test files. Baseline failure still stops the suite immediately; lifecycle diagnostics run after stress even when the stress stage reports errors so evidence is not lost.
- `upgrade.ps1` now packages and deploys `test_lifecycle_diag.ps1` together with the existing test tools.
- FolderHeatMap runtime lifecycle behavior is unchanged in this release; the purpose of 1.37 is diagnostic confirmation before any rename or move-race runtime fix.
- Version updated to 1.37 (`1.37-lifecycle-diagnostic-tests`).

## 1.36 - 28.08.2026

- Rewrote `test_stress.ps1` into normal, readable PowerShell without compressed command/operator formatting that could parse successfully but fail at runtime.
- Fixed `Wait-Until` so successful conditions use the explicit runtime-safe form `return $value` instead of the invalid concatenated token `return$value`.
- Expanded helper functions, loops, branches and test stages into explicit statements with normal spacing to remove the same class of latent runtime tokenization errors throughout the stress runner.
- Preserved all 1.34/1.35 lifecycle stress scenarios and the Total Commander workspace-release safety handoff without changing FolderHeatMap runtime lifecycle behavior.
- Added explicit prerequisite validation for the Total Commander INI before the stress suite derives the FolderHeatMap database path.
- Kept destructive filesystem operations strictly limited to `D:\Temp\FHM\` and retained bounded cleanup retries.
- Updated build, runtime banner and self-updating upgrade metadata to 1.36.
- Version updated to 1.36 (`1.36-stress-runner-runtime-safety`).

## 1.35 - 28.08.2026

- Fixed the stress-stage startup failure where the baseline test left Total Commander inside `D:\Temp\FHM\SRC`, causing Windows to reject workspace cleanup because the directory was still in use.
- The stress runner now navigates Total Commander to the fixed release path `D:\Temp` before cleaning `D:\Temp\FHM`, waits for panel/watcher handoff, and reports a dedicated PASS when the workspace has been released.
- Added bounded retry handling for transiently locked workspace items during stress cleanup while keeping all destructive operations strictly inside `D:\Temp\FHM\`.
- Fixed the stress summary output so `Write-LogLine 'RESULT: PASS'` and `Write-LogLine 'RESULT: ERROR'` are valid runtime calls instead of being concatenated into a nonexistent command name.
- Kept the existing baseline and lifecycle stress scenarios unchanged after the corrected handoff.
- Updated build/runtime/upgrader metadata to 1.35 and included `STRESS_TESTING.md` in staged/deployed documentation.
- Version updated to 1.35 (`1.35-stress-workspace-release`).

## 1.34 - 28.08.2026

- Added `test_stress.ps1`, a second-stage lifecycle stress regression suite that runs after the proven baseline tests.
- Added rapid repeated same-volume MOVE coverage to exercise watcher/SLOW race handling while preserving File ID and persistent history.
- Added automated directory and file RENAME coverage, including old-path cleanup assertions.
- Added populated-subtree DELETE coverage and same-path subtree recreation checks with new filesystem identities.
- Added an immediate DELETE -> RECREATE race test to verify that a new object cannot inherit stale history while SLOW cleanup is still converging.
- Added MOVE -> immediate write coverage to verify that migrated file history is retained and the first destination-side write is appended rather than lost or reset.
- Added directory MOVE coverage with heated descendant directories and files, verifying descendant File IDs and Visits/Writes histories migrate with the subtree.
- Added an engine restart persistence check that verifies persistent histories and filesystem identities survive a controlled `FolderHeatMapEngine.exe` restart.
- Added a workspace reuse fixture for validating clean-start behavior on the following test run.
- `test.cmd` now syntax-checks both PowerShell test files before either test stage begins and stops immediately on any parser error or baseline failure.
- `upgrade.ps1` packages and deploys `test_stress.ps1` together with the existing test tools while retaining the proven self-update and deployment safeguards.
- All destructive filesystem stress operations remain hard-limited to `D:\Temp\FHM\`.
- Version updated to 1.34 (`1.34-lifecycle-stress-tests`).

## 1.33 - 28.08.2026

- Restored the proven 1.32 `upgrade.ps1` and `upgrade.cmd` logic after an attempted 1.33 edit unintentionally simplified the established deployment/bootstrap safeguards.
- Extended `test.ps1` with automated directory DELETE -> RECREATE coverage inside the exclusive `D:\Temp\FHM\` sandbox.
- Extended `test.ps1` with automated file DELETE -> RECREATE coverage inside the same sandbox.
- The new tests verify that deletion removes the old persistent history, recreation produces a different `Volume Serial + FILE_ID_128` identity, and the new object starts with fresh Visits/Writes rather than inheriting stale history.
- Existing same-volume MOVE round-trip coverage remains unchanged and runs before the new delete/recreate assertions.
- Kept the parser preflight in `test.cmd`, the hard sandbox path barrier, detailed per-run logs, and all proven upgrader self-update/deployment guards.
- Version updated to 1.33 (`1.33-delete-recreate-tests`).

## 1.32 - 28.08.2026

- Fixed two malformed PowerShell string expressions in `test.ps1` that caused the automated regression runner to fail during parsing before any tests could start.
- Bumped the automated test runner metadata to 1.32.
- Added a syntax preflight to `test.cmd` using `System.Management.Automation.Language.Parser` so parser errors are reported in red with exact line/column information before the test workspace is touched.
- The preflight now uses explicit token/error variables for the parser API and exits with code 2 when syntax validation fails.
- Kept all existing test safety guarantees: the runner may only create, move, modify or delete content under `D:\Temp\FHM\`.
- Version updated to 1.32 (`1.32-test-parser-preflight`).

## 1.31 - 28.08.2026

- Added `test.cmd` and `test.ps1` as a repeatable automated regression suite for FolderHeatMap same-volume MOVE behavior.
- Standardized the exclusive test workspace to `D:\Temp\FHM\`; each run logs and then removes all previous workspace contents before creating a clean test tree, while destructive filesystem operations outside that tree are prohibited by a hard safety check.
- Added real Total Commander navigation driving through `/O /L=...` so automated directory heating exercises the independent TC navigation monitor rather than synthesizing Visits directly in the database.
- Added real file-write preparation with spacing beyond the one-second coalescing window.
- Added native Windows `Volume Serial + FILE_ID_128` capture before/after moves and round trips.
- Added direct read-only SQLite verification through the Windows `winsqlite3.dll` API, including directory Visits/usage fields and file Writes/activity fields before and after migration.
- Added assertions that old source database paths no longer retain active history and that `[LIFECYCLE] move_migrated old/new` diagnostics are emitted.
- Console test results are reported continuously as green `[PASS]`, red `[ERROR]`, yellow warnings, with detailed per-run diagnostics retained under `D:\Temp\FHM\logs\`.
- Test data are intentionally preserved after a run for diagnosis and automatically cleared at the beginning of the next run.
- Version updated to 1.31 (`1.31-automated-move-tests`).

## 1.30 - 27.08.2026

- Fixed deployment failures where `FolderHeatMapEngine.exe` restarted after the initial STOP-RUNTIME phase and then locked its own live executable in `dist`.
- Added an authoritative pre-deploy engine guard that checks for running `FolderHeatMapEngine` processes immediately before live artifact replacement, logs their PID(s), force-stops them, and verifies process exit.
- Added per-copy engine guarding for every live `FolderHeatMapEngine.exe` replacement, including external plugin directories when the registered WDX is not using repository `dist` directly.
- Added a post-deploy engine guard so the engine cannot race the final verification/restart sequence.
- The engine is restarted explicitly only after the complete deployment succeeds; Total Commander is restarted afterward when it was running before upgrade.
- Extended `Copy-FileWithRetry` with a dedicated engine guard, matching the existing Total Commander WDX lock protection.
- Version updated to 1.30 (`1.30-engine-deploy-guard`).

## 1.29 - 27.08.2026

- Fixed same-volume moves that could lose visible FolderHeatMap history when a watched object was removed from one parent before the destination parent was reconciled.
- Changed watcher removal handling to identity-first semantics: `FILE_ACTION_REMOVED` is treated as a change hint, not immediate proof that the filesystem object was destroyed.
- Before recursive purge, SLOW now resolves the previously tracked object by `Volume Serial + File ID` and retries briefly to cover source/destination notification races.
- If the same object still exists elsewhere on the same volume, `MoveTrackedObject()` atomically migrates directory Visits/usage, descendant file activity, tracked identities, or file Writes to the new path.
- Recycle-bin moves remain deletion semantics and are intentionally migrated as normal moves.
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
- Kept confirmed watcher `REMOVED` events active as the authoritative fast delete signal, including immediate tombstone invalidation and prioritized SLOW subtree cleanup.
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
