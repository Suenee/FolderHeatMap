# Changelog

## 1.48 - 29.08.2026

- Added canonical reconciliation for a surviving queued File ID: when a rapid MOVE round trip leaves the database row on an earlier path, the watcher now looks up the tracked object by volume and File ID and migrates its history to the object's current filesystem path.
- Added `Database::GetTrackedObjectById()` for identity-first reconciliation without relying on a stale path.
- Added explicit `queued_identity_reconciled` and `queued_identity_survived_unreconciled` lifecycle diagnostics.
- Updated the lifecycle diagnostic suite to report test version 1.48 and include the new reconciliation traces.
- Fixed the 1.48 CMake pipeline after the first 1.48 package attempt: the 1.48 same-volume MOVE injector already contains the required rapid round-trip handling, so the obsolete 1.47 `ProtectRapidMoveRoundTrips.cmake` stage is no longer applied a second time. This was a build-time anchor conflict only; the failed package was never deployed.
- Updated build/runtime/launcher metadata to 1.48 (`1.48-surviving-file-id-reconcile`).

## 1.47 - 29.08.2026

- Fixed the remaining 1.46 rapid MOVE round-trip gap shown by the 1.46 runtime trace: after the first successful MOVE consumed its per-task identity hint, a later removal of the same old path could have no tracked row and therefore reach watcher purge with an empty File ID.
- Added last-confirmed path identity memory independent of individual delete tasks. A successful same-volume MOVE remembers the canonical volume, File ID and object type for both endpoints.
- When a later rapid round-trip removal has no tracked row at its old path, the watcher can recover the last confirmed File ID for that path and verify whether that exact object still survives on the volume before allowing destructive reset.
- Genuine DELETE and DELETE -> RECREATE remain destructive when the remembered File ID no longer exists; recycle-bin semantics are unchanged.
- Added a guarded build-stage patch dedicated to the confirmed round-trip race instead of reintroducing the unsuccessful 1.41-1.43 speculative lifecycle changes.
- `test.cmd` now derives the lifecycle diagnostic runtime version from the authoritative CMake project version, preventing copied summaries from reporting an obsolete hard-coded test version during normal `test.cmd` / `upgrade.cmd --test` runs.
- The self-updating launcher now synchronizes release metadata in its temporary PowerShell runner copy, keeping upgrade output aligned with the launcher/project release without modifying the tracked working tree.
- Updated build/runtime/launcher metadata to 1.47 (`1.47-rapid-move-roundtrip-memory`).

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
