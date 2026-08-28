# Changelog

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
- Updated build/runtime/self-updating upgrader metadata to 1.46 (`1.46-queued-file-id-rapid-move`).

## 1.45 - 29.08.2026

- Added the lifecycle diagnostic test version to the final summary so copied or partial logs can be identified without their header.
- Kept runtime lifecycle behavior unchanged; this release only improved test-result identification while retaining the 1.44 destructive lifecycle tracing.
- Updated build/runtime/launcher metadata to 1.45 (`1.45-diagnostic-version-summary`).

## 1.44 - 28.08.2026

- Added diagnostic-only tracing for every destructive runtime lifecycle path involved in the rapid MOVE investigation without changing MOVE/RENAME/DELETE behavior.
- Watcher-side recursive purges now emit `[DB_DELETE_TRACE] source=watcher_purge` with the queued path, canonical volume/relative path and tracked File ID when available immediately before `ResetRecursiveActivity()`.
- SLOW canonical reconciliation now propagates File IDs through `LifecycleResult` and emits `[DB_DELETE_TRACE] source=slow_reconcile action=DELETE` for every applied destructive lifecycle action.
- `test_lifecycle_diag.ps1` is now version 1.44 and captures engine-log lines produced during each RAPID_NORMAL / RAPID_STRESS case so the diagnostic log itself identifies which destructive path removed history.
- Kept the 1.43 runtime lifecycle decisions unchanged; this release is intended to identify the actual deletion source before another behavioral fix is attempted.
- Corrected the historical 1.29 changelog wording: recycle-bin moves are deletion semantics and are intentionally not migrated as normal moves.
- Updated build/runtime/upgrader metadata to 1.44 (`1.44-destructive-lifecycle-trace`).

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
