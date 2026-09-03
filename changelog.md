# Changelog

## Unreleased - Heat model object refactor

- Added the read-only `test-nas-id.cmd` / `FolderHeatMapNasIdTest.exe` diagnostic for comparing mapped-drive and UNC access to the same NAS object. It reports `FileFsObjectIdInformation` volume Object ID, `FILE_ID_INFO` volume serial and 128-bit File ID, `FileFsVolumeInformation` serial/label, remote protocol metadata and the final handle path, then classifies whether both paths expose the same volume and filesystem object. The diagnostic is built automatically whenever the normal FolderHeatMap target is built and does not modify filesystem metadata.
- Rolled back the experimental mapped-network-drive identity hotfix to commit `e28958d82dd22f724aaee8f82d711e7ca363fdb1` after it coincided with a Total Commander WDX loading regression. The proven installer 1.08 x64 registration repair remains; NAS runtime identity support is being reintroduced only after a read-only protocol/filesystem identity diagnostic establishes which stable SMB volume identity the target NAS actually exposes.
- Updated the installer to 1.08 and fixed Total Commander x64 WDX registration repair. `Ensure-WdxRegistration` now always maintains the matching `[ContentPlugins64] <slot>=1` entry, even when the `[ContentPlugins]` path is already correct, and verifies the path/x64 pair before installation may report success. This prevents a valid `FolderHeatMap.wdx64` on disk from being left unloaded by 64-bit Total Commander after an upgrade.
- Updated `upgrade.ps1` metadata to the current FolderHeatMap 1.52 runtime and added an explicit final `VERSION: 1.52` line after both successful/warning completion and failed upgrades. The final status block now makes it immediately visible whether the local deployment is synchronized with the expected project version.
- Fixed the 1.52 runtime-diagnostics build-order regression: `InjectRuntimeSettingsDiagnostics.cmake` no longer rewrites the engine version banner before `InjectDeletionDiagnostics.cmake` consumes its guarded `FolderHeatMap 1.11 engine starting` anchor. Runtime path diagnostics and live logger reinitialization remain enabled, while the existing lifecycle patch pipeline keeps its established ordering.
- Updated `install.cmd` / `install.ps1` to version 1.07 and fixed the PowerShell parser failure `InvalidVariableReferenceWithDrive` introduced by the Total Commander update-status log line. The ambiguous `$after:` interpolation is now written as `${after}:`.
- Added a mandatory parser preflight in `install.cmd` for both `install.ps1` and `repair_custom_columns.ps1`. Parser errors are now reported with the exact script, line, column and message before either helper can run, preventing a syntax defect from reaching the DEPLOY phase as an opaque child-process failure.
- Updated the installer launcher to 1.06 and added an idempotent Total Commander custom-column repair pass. Every `install.cmd` / `upgrade.cmd` run now verifies that exactly one `FolderHeatMap` custom-column view remains, removes duplicate FolderHeatMap entries, compacts the surviving custom-column definitions, and preserves unrelated user-defined custom-column views.
- Added `repair_custom_columns.ps1` as the dedicated internal repair helper. It resolves the active `WINCMD.INI`, stops Total Commander before editing custom-column configuration, restores the previous running state afterwards, and verifies the final FolderHeatMap view count before reporting success.
- Updated the FolderHeatMap installer to version 1.05. The Total Commander upgrade prompt is now emitted as complete ordered console lines with an explicit `ACTION REQUIRED` message before waiting for `Y`/`N`, avoiding the unreadable interleaving previously caused by `Read-Host` when `install.cmd` is invoked from `upgrade.cmd`.
- Added visible liveness output while the Total Commander installer is downloaded and while the official installer process is running, so a long update no longer appears frozen.
- Total Commander upgrades now lock the detected existing installation directory as the explicit installer target. The official installer is launched with `/F "<existing-directory>"`, and FolderHeatMap verifies that the upgraded `TOTALCMD64.EXE`/`TOTALCMD.EXE` in that original directory reaches the expected stable version before accepting the update as successful.
- Before a Total Commander update, the active `WINCMD.INI` is backed up separately with an `fhm-before-tc-update` timestamp. The normal FolderHeatMap integration backup is still created afterwards.
- Added a Total Commander font-profile guard to every `install.cmd` run, including those launched automatically by `upgrade.cmd`. It enforces Microsoft Sans Serif 8 bold for file lists and the main window, Microsoft Sans Serif 8 regular for dialogs, charset 238, and `AutoSizeDialogs=1` (automatic dialog sizing for larger fonts) in the active/existing resolution profile sections.
- The font guard is idempotent and runs after Total Commander is stopped but before FolderHeatMap WDX/custom-column/color/icon integration is repaired, preventing a Total Commander reinstall/update from silently leaving the UI font configuration changed.
- Extended `install.cmd` to launcher revision 1.04 with robust Total Commander executable discovery before the internal installer runs. This fixes older Total Commander installations whose active `WINCMD.INI` is known but whose executable directory is not exposed through the registry paths used by newer releases.
- The CMD launcher now checks the inherited `COMMANDER_PATH`, classic Total Commander locations including `C:\totalcmd`, common Program Files locations, PATH via `where`, and finally the executable path of a currently running `TOTALCMD64` / `TOTALCMD` process. When found, it exports `COMMANDER_PATH` to the internal installer so version detection and the optional official Total Commander upgrade can run normally.
- Extended `install.cmd` / internal installer helper to version 1.03 with an online Total Commander version check against Ghisler's current official download page (`download.htm`).
- The installer reads the version of the actually detected `TOTALCMD64.EXE`/`TOTALCMD.EXE`, compares it with the latest stable Total Commander release, and continues silently when the local installation is current.
- If a newer stable Total Commander is available, the installer asks the user before doing anything. Declining the offer continues FolderHeatMap installation unchanged; Total Commander is never upgraded without explicit confirmation.
- After confirmation, the installer downloads the official x64 Total Commander installer, requires a valid Authenticode signature from `Ghisler Software GmbH`, runs the official installer, verifies the resulting installed version, then resumes FolderHeatMap integration. Failure to perform the online version check itself is non-fatal and is logged as a warning.
- Current official Total Commander release verified while implementing this change: 11.58, published 01.07.2026. The version is not hard-coded; the installer re-reads the official download page on each run.
- Fixed Total Commander detection in `upgrade.ps1` (`1.51-tc-detection-dirty-tree-repair`). The upgrader now expands environment variables/quotes in TC paths and uses the same `%APPDATA%\GHISLER\WINCMD.INI` fallback as the installer, so a fresh machine no longer fails configuration merely because `COMMANDER_INI` and the registry do not expose the active INI path.
- Added common Total Commander executable-location fallbacks to the upgrader when no install path is available from environment variables or registry.
- Refined pre-upgrade dirty-tree detection to distinguish real tracked content edits from CRLF/LF-only materialization. Line-ending-only differences no longer trigger an unnecessary automatic stash.
- When real tracked modifications are present, the upgrader now logs the exact `git status --porcelain` entries before stashing them, making repeated fresh-machine dirty-tree warnings diagnosable instead of opaque.
- Fixed the deploy retry failure where an obsolete Total Commander registration could point to `build\package\FolderHeatMap.wdx64`, causing the upgrader to repeatedly try to copy the staged WDX onto itself and misreport the condition as a temporary file lock. `Copy-FileWithRetry` now canonicalizes source and destination paths and skips an identical source/destination copy immediately.
- Defined `dist\FolderHeatMap.wdx64` as the only stable live WDX registration. `build\Release` remains build output and `build\package` remains temporary staging; neither is used as Total Commander's persistent FolderHeatMap registration.
- The upgrader no longer deploys runtime files back into an arbitrary previously registered FolderHeatMap path. After stable `dist` deployment it runs `install.cmd`, which migrates or creates the WDX registration and repairs the complete Total Commander integration consistently.
- Expanded `install.cmd` / internal `install.ps1` helper to installer version 1.01. `install.cmd` remains the primary user-facing helper entry point; PowerShell is used only as its internal implementation helper.
- The installer now creates or repairs a selectable Total Commander custom-column view named `FolderHeatMap` containing `Heat`, `Visits`, `Last Visit`, `Writes` and `Last Write`.
- The installer now creates or repairs FolderHeatMap text-color rules using the same configured color anchors, smooth interpolation and steps-per-level behavior as the configurator, while preserving unrelated user color filters.
- The installer now regenerates FolderHeatMap heat-colored folder icons and Internal Associations from the same FolderHeatMap color/icon settings.
- The installer creates a timestamped `WINCMD.INI` backup and writes diagnostics to `logs\install.log`; when Total Commander is running it is stopped before configuration changes and restarted once after the complete repair has finished.
- Upgrade packaging now carries `install.cmd`, its internal installer helper, `setup_icons.cmd` and its internal icon helper into the stable distribution alongside the existing support files.
- Updated `README.md` to document the stable `dist` runtime path and the complete `install.cmd` repair workflow. Runtime FolderHeatMap remains version 1.51.
- Added automatic C++ build-environment bootstrap for fresh Windows machines. `upgrade.cmd` now runs the authoritative `ensure_build_tools.ps1` from `origin/devel` before the main upgrade runner.
- If Visual Studio/Build Tools is absent, the dependency bootstrap uses `winget` to install Visual Studio 2022 Build Tools with the `Microsoft.VisualStudio.Workload.VCTools` workload and recommended components, including MSVC, Windows SDK and Visual Studio CMake support. Administrator elevation may be requested by Windows.
- If Visual Studio/Build Tools already exists but the required C++/CMake workload is missing, the bootstrap uses the installed Visual Studio Installer to add the workload instead of installing a second IDE/toolchain.
- Dependency installation is verified by resolving a usable CMake executable after installation. Missing `winget`, installer failure, cancelled elevation or an incomplete toolchain now produce an explicit dependency-bootstrap error instead of reaching the later generic CMake failure.
- Added automatic Git `safe.directory` recovery to `upgrade.cmd` (`1.52-bootstrap-network-safe-directory`) for repositories on NAS, UNC paths and mapped network drives. If Git rejects an existing checkout with `detected dubious ownership`, the launcher now parses Git's own suggested repository-specific safe path, registers only that exact path in the user's global Git configuration, retries repository detection and continues normally.
- `dubious ownership` is no longer treated as "not a Git repository", preventing a valid freshly cloned network checkout from re-entering bootstrap and attempting another clone.
- Documented the network-repository failure mode and added mapped/UNC `dubious ownership` recovery to the mandatory upgrader acceptance checklist in `UPGRADE.md`. Wildcard `safe.directory=*` is explicitly avoided.
- Fixed fresh-machine bootstrap recursion in `upgrade.cmd` (`1.52-bootstrap-current-dir`). The launcher now installs the repository directly into the directory that contains the standalone `upgrade.cmd` instead of appending another `FolderHeatMap` subdirectory.
- Bootstrap now hands control to a temporary copy of the launcher before replacing the standalone `upgrade.cmd`, so the running batch file is never overwritten in place. The temporary runner removes only the standalone launcher, clones `origin/devel` into the now-empty target directory, verifies `.git` and the authoritative cloned `upgrade.cmd`, then hands off to the normal updater.
- Added a bootstrap safety check: the target directory must contain only `upgrade.cmd`. Leftovers from a failed bootstrap, including a nested `FolderHeatMap` directory, cause an explicit error instead of being deleted automatically.
- `--test` is preserved through the fresh-machine bootstrap handoff. Bootstrap diagnostics remain in `FolderHeatMap-bootstrap.log` in the parent directory, while the cloned project continues with its normal `logs\upgrade.log`.
- Added a frozen golden-master reference dataset for the current `Dual-Timescale Activity` heat mathematics.
- Added deterministic, date-independent folder, file and automatic-cooling scenarios covering bursts, short-term decay, long-term habit, idle periods and different user activity rhythms.
- Added `FolderHeatMapHeatReferenceTest` and integrated it into `test.cmd`; the complete output is appended to the final `D:\Temp\FHM\logs\diagnostic-*.log` while remaining visible in the console.
- Added the generic `IHeatModel` contract plus the named `DualTimescaleActivityModel` implementation (`dual_timescale_activity`).
- Moved short-term folder heat, long-term folder habit heat, file heat and automatic cooling-half-life mathematics into the model implementation. The model receives aggregated activity/time inputs and does not access the database, filesystem, Total Commander, icons or presentation state.
- The generated engine now adapts database/time data into model-facing inputs and delegates direct folder/file heat calculations to `DualTimescaleActivityModel`; hierarchy/path aggregation remains engine-level logic.
- The CMake refactor stage is guarded by source anchors and aborts configuration if the verified engine baseline no longer matches the expected heat-math block.
- Changed the golden-reference executable from a duplicate frozen formula implementation to a test of the actual runtime `DualTimescaleActivityModel` against the frozen pre-refactor expected values. This makes any mathematical drift in the production model fail the 24-case golden reference test at `1e-9` tolerance.
- Kept the shared fixture source independent from the current model so the same dataset can later compare additional heat models without changing activity inputs.
- Runtime heat semantics are intentionally unchanged; FolderHeatMap remains version 1.51 until a user-visible model-selection feature is introduced.

## 1.52 - 03.09.2026

- Fixed the deployed WDX display client exposing only `Heat`, `Visits` and `Writes` while the installer and Total Commander integration referenced `Last Visit`, `Last Write`, `Heat Level` and `Heat Color Step` as well. The active WDX now exposes and reads the complete seven-field runtime cache surface again.
- Restored `Last Visit` and `Last Write` custom-column values and the hidden `Heat Level` / `Heat Color Step` fields used by Total Commander heat-color and folder-icon rules.
- A Total Commander WDX reload now increments the shared settings sequence so the independently running engine treats the reload as a settings boundary instead of continuing indefinitely with startup-time settings.
- Added a guarded build-stage runtime settings patch. When settings are reloaded, the engine now reinitializes the file logger too, so changing Logging mode to `single` or `all` takes effect without requiring the persistent engine process to be killed manually.
- Runtime logs now record the resolved settings and database paths after startup/reload, making mapped-drive and active-INI path problems directly diagnosable from `FolderHeatMap.log`.
- Runtime/project version is now 1.52. Heat mathematics and lifecycle semantics are unchanged.

## 1.51 - 30.08.2026

- Fixed the regression where FolderHeatMap text colors still followed Heat, but Total Commander folder icons remained at the normal yellow icon instead of using the heat-colored icon map.
- Root cause: the deployed configurator expected `setup_icons.ps1` beside `FolderHeatMapConfig.exe`, but the isolated package/deployment did not include that script, and normal upgrades did not re-apply the FolderHeatMap Internal Associations.
- `upgrade.ps1` now packages and deploys `setup_icons.ps1` beside `FolderHeatMapConfig.exe`.
- Every successful upgrade now regenerates the FolderHeatMap heat icon files and Total Commander Internal Associations while Total Commander is stopped, before the application is restarted.
- Icon regeneration failure is now a deployment failure instead of silently leaving a partially installed visual integration.
- Existing user-selected icon artwork and configured heat colors continue to be read from `FolderHeatMap.ini`; the repair does not reset those settings.
- Updated project/upgrader metadata to 1.51 (`1.51-folder-icon-repair`).

## 1.50 - 29.08.2026

- Fixed the remaining rapid MOVE round-trip gap shown by the 1.49 diagnostics: path/File-ID memory prevented destructive purge, but history could remain stranded on `DST` when the watcher did not observe the unwatched `DST -> SRC` departure.
- Added an arrival lifecycle callback for `FILE_ACTION_ADDED` and `FILE_ACTION_RENAMED_NEW_NAME` events.
- Arrival handling resolves the object's canonical volume + File ID. If the same File ID is already tracked at another path, the database history is migrated to the path where the object physically exists now.
- Added `arrival_identity_reconciled`, `arrival_identity_current`, and `arrival_identity_reconcile_FAILED` lifecycle diagnostics.
- Recycle-bin arrivals are excluded from reconciliation, and genuinely new File IDs are not migrated from unrelated history.
- Kept the 1.49 durable path-to-File-ID memory and the 1.48 surviving-File-ID reconciliation path; no additional timing delays or 1.41-1.43 speculative SLOW behaviors were introduced.
- Updated lifecycle diagnostic metadata to 1.50 and included arrival reconciliation traces in rapid MOVE diagnostics.
- Normalized the tracked `upgrade.ps1` metadata to 1.50 and removed the temporary metadata rewriting workaround from `upgrade.cmd`; the launcher now executes the self-updated authoritative `origin/devel:upgrade.ps1` directly.
- Updated project/runtime/launcher metadata to 1.50 (`1.50-arrival-file-id-reconcile`).

## 1.49 - 29.08.2026

- Fixed the confirmed 1.48 rapid-MOVE regression where second and later removals reached watcher purge with an empty `object_id`, causing `RESET_RECURSIVE` even though the same filesystem File ID survived.
- Restored durable path-to-File-ID memory at anchors compatible with the 1.48 generated lifecycle code.
- A confirmed MOVE now remembers canonical volume, File ID and object type for both old and new endpoints; later removal events can recover that identity even after the tracked database row has already migrated away from the old path.
- The recovered identity feeds the existing 1.48 surviving-File-ID reconciliation path, allowing `queued_identity_reconciled` to migrate database history to the path where the object physically exists now instead of purging it.
- No additional timing delays were added and the unsuccessful 1.41-1.43 SLOW lifecycle experiments remain rolled back.
- Updated project/runtime/launcher metadata to 1.49 (`1.49-rapid-move-identity-memory`).

## 1.48 - 29.08.2026

- Added canonical reconciliation for a surviving queued File ID: when a rapid MOVE round trip leaves the database row on an earlier path, the watcher now looks up the tracked object by volume and File ID and migrates its history to the object's current filesystem path.
- Added `Database::GetTrackedObjectById()` for identity-first reconciliation without relying on a stale path.
- Added explicit `queued_identity_reconciled` and `queued_identity_survived_unreconciled` lifecycle diagnostics.
- Updated the lifecycle diagnostic suite to report test version 1.48 and include the new reconciliation traces.
- Fixed the 1.48 CMake pipeline after the first 1.48 package attempt: the 1.48 same-volume MOVE injector already contains the required rapid round-trip handling, so the obsolete 1.47 `ProtectRapidMoveRoundTrips.cmake` stage is no longer applied a second time. This was a build-time anchor conflict only; the failed package was never deployed.
- Removed the temporary `.test_lifecycle_diag.runtime.ps1` generation and all CMD-to-PowerShell string rewriting. `test.cmd` now syntax-checks and executes the tracked `test_lifecycle_diag.ps1` directly.
- Restored the lifecycle diagnostic script to conventional PowerShell formatting, eliminating the compact-token runtime failures such as `Info'...'` and `Write-LogLine'...'` at their source.
- Baseline assertion failures still skip the stress suite but always hand off to lifecycle diagnostics, while preserving the original baseline exit code.
- Runtime lifecycle behavior is unchanged by these diagnostic-runner fixes.

## 1.47 - 29.08.2026

- Fixed the remaining 1.46 rapid MOVE round-trip gap shown by the 1.46 runtime trace: after the first successful MOVE consumed its per-task identity hint, a later removal of the same old path could have no tracked row and therefore reach watcher purge with an empty File ID.
- Added last-confirmed path identity memory independent of individual delete tasks. A successful same-volume MOVE remembers canonical volume, File ID and object type for both endpoints.
- When a later rapid round-trip removal has no tracked row at its old path, the watcher can recover the last confirmed File ID for that path and verify whether that exact object still survives on the volume before allowing destructive reset.
- Genuine DELETE and DELETE -> RECREATE remain destructive when the remembered File ID no longer exists; recycle-bin semantics are unchanged.
- Added a guarded build-stage patch dedicated to the confirmed round-trip race instead of reintroducing the unsuccessful 1.41-1.43 speculative lifecycle changes.
- `test.cmd` now derives the lifecycle diagnostic runtime version from the authoritative CMake project version, preventing copied summaries from reporting an obsolete hard-coded test version during normal `test.cmd` / `upgrade.cmd --test` runs.
- The self-updating launcher now synchronizes release metadata in its temporary PowerShell runner copy, keeping upgrade output aligned with the launcher/project release without modifying the tracked working tree.
- Updated project/runtime/launcher metadata to 1.47 (`1.47-rapid-move-roundtrip-memory`).

## 1.46 - 29.08.2026

- Fixed the confirmed rapid MOVE history-loss path identified by the 1.44/1.45 `[DB_DELETE_TRACE]` diagnostics: a queued watcher purge could become path-only after an earlier MOVE task migrated the tracked row away from the old path.
- Watcher removal tasks now capture the canonical volume, File ID and object type when the removal is observed, and retain that identity until the queued delete task is resolved.
- If the old tracked row is no longer present when the task executes but the captured File ID still exists anywhere on the same volume, the queued task is treated as stale MOVE evidence and is not allowed to call `ResetRecursiveActivity()`.
- Preserved ordinary same-volume MOVE/RENAME migration, genuine DELETE/DELETE -> RECREATE semantics and recycle-bin deletion semantics.
- Rolled back the speculative 1.41, 1.42 and 1.43 lifecycle behavior changes after diagnostics showed they did not address the actual destructive path: removed the path-exists stale-row workaround, the additional SLOW OpenFileById retry grace, and the same-path stale-snapshot exception.
- Restored the proven 1.40 SLOW lifecycle decision model while retaining the later diagnostic File ID propagation and `[DB_DELETE_TRACE]` instrumentation.
- Added `[LIFECYCLE] queued_identity_survived ...` diagnostics when a queued watcher delete is cancelled because its captured File ID is still alive.

## 1.45 - 29.08.2026

- Added lifecycle diagnostic test-version visibility to the final summary so copied test results can be matched to the exact diagnostic revision.
- Kept runtime lifecycle behavior unchanged from 1.44.

## 1.44 - 29.08.2026

- Added diagnostic-only destructive lifecycle tracing to identify which code path actually removes rapid-MOVE history.
- SLOW applied DELETE actions now retain their canonical File ID in `LifecycleChange` and emit `[DB_DELETE_TRACE] source=slow_reconcile action=DELETE ...` before database deletion.
- Watcher-side recursive purge now emits `[DB_DELETE_TRACE] source=watcher_purge action=RESET_RECURSIVE ...` immediately before destructive reset.
- Rapid MOVE diagnostics now capture trace offsets per case and print only current-case lifecycle/destructive traces, including `DB_DELETE_TRACE`.
- Runtime lifecycle behavior is otherwise unchanged from 1.43; this release is intentionally diagnostic rather than another speculative fix.

## 1.43 - 29.08.2026

- Added SLOW same-ID/same-path stale snapshot protection during directory reconciliation.
- Rolled back in 1.46 after diagnostics showed it did not address the actual destructive path.

## 1.42 - 29.08.2026

- Added additional SLOW File-ID reconciliation grace/retry before destructive lifecycle actions.
- Rolled back in 1.46 after diagnostics showed it did not address the actual destructive path.

## 1.41 - 29.08.2026

- Added path-existence protection when a queued removal no longer had a tracked row at the old path.
- Rolled back in 1.46 after diagnostics showed it did not address the actual destructive path.

## 1.40 - 29.08.2026

- Added identity-first MOVE/RENAME runtime repair and `upgrade.cmd --test`.

## 1.39 - 29.08.2026

- Fixed FolderHeatMapReset linkage by restoring `src/Database.cpp` to the reset target.

## 1.38 - 29.08.2026

- Added baseline-failure diagnostic handoff: lifecycle diagnostics still run after assertion failures while preserving the baseline exit code.

## 1.37 - 29.08.2026

- Added diagnostic-only lifecycle tests for rapid MOVE, rename and MOVE/write timing.

## 1.36 - 29.08.2026

- Reworked the lifecycle stress runner for conventional PowerShell runtime safety.

## 1.35 - 29.08.2026

- Added Total Commander workspace release before stress cleanup and corrected runner summary handling.

## 1.34 - 29.08.2026

- Added lifecycle stress regression coverage for rapid MOVE/rename/delete/recreate/restart scenarios.

## 1.33 - 29.08.2026

- Added DELETE -> RECREATE regression coverage for directories and files.

## 1.32 - 29.08.2026

- Added PowerShell parser preflight to the regression launcher and fixed test-runner syntax handling.

## 1.31 - 29.08.2026

- Added automated same-volume MOVE regression coverage.

## 1.30 - 29.08.2026

- Added engine deployment lock protection and post-deploy verification.

## 1.29 - 29.08.2026

- Added identity-first same-volume MOVE protection using canonical volume + File ID.
- Same-volume moves preserve history by migrating tracked paths instead of treating the old path as a deletion.
- Recycle-bin destinations remain deletion semantics.

## 1.28 - 29.08.2026

- Added file-write tracking using filesystem notifications and one-second coalescing.

## 1.27 - 29.08.2026

- Added Total Commander pre-deployment lock guard.

## 1.26 - 29.08.2026

- Restored the complete CMake build graph and fixed stale runtime version banner injection.

## 1.25 - 29.08.2026

- Added staged package deployment with retry handling.

## 1.24 - 29.08.2026

- Added deterministic bootstrap synchronization to `origin/devel`.

## 1.23 - 29.08.2026

- Fixed bootstrap self-update protocol.

## 1.22 - 29.08.2026

- Added independent Total Commander panel navigation monitoring using `WM_USER + 50` left/right path controls.

## 1.21 - 29.08.2026

- Internal maintenance release.

## 1.20 - 29.08.2026

- Added canonical filesystem identity diagnostics.

## 1.19 - 29.08.2026

- Internal maintenance release.

## 1.18 - 29.08.2026

- Internal maintenance release.

## 1.17 - 29.08.2026

- Internal maintenance release.

## 1.16 - 29.08.2026

- Internal maintenance release.

## 1.15 - 29.08.2026

- Earlier project history retained in repository history.
