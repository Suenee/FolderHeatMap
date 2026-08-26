# Changelog

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
- Added stable per-volume filesystem object ID validation. `same path + same ID` keeps history; `same path + different ID` is treated as a new filesystem object and the stale history is never exposed.
- Added protection for deletes performed outside Total Commander or outside the currently watched directory: a later identity mismatch immediately returns a cold snapshot and schedules stale subtree cleanup.
- Kept same-volume rename/move history preservation through the existing File ID lifecycle logic. Cross-volume moves remain intentionally treated as delete + new object.
- Changed lifecycle reconciliation ordering so old delete/move actions are applied before current observations. This prevents a recreated object at the same path from inheriting history from the deleted object.
- Recursive lifecycle cleanup now also removes matching `tracked_objects` rows, not only folder/file activity.
- Retained cooling/expiry as the final garbage-collection fallback for stale records that are never encountered again.
- Upgrader and build version updated to 1.17 (`1.17-delete-lifecycle`).

## 1.14 - 20.08.2026

- Replaced CMD/PowerShell bootstrap argument passing with environment-only transport (`FHM_UPGRADE_INTERNAL`, `FHM_UPGRADE_STAGE`, `FHM_UPGRADE_REPO`, `FHM_UPGRADE_SCRIPT`). Current bootstrap execution passes no repository path or stage token on argv.
- Root cause of the 1.13 `args=2` failure was the Windows command-line quoting edge case where a quoted directory ending in `\` can consume the closing quote / following token when forwarded across process boundaries. The repository path is now normalized without a trailing separator before bootstrap handoff.
- Simplified self-update flow: the local launcher silently fetches `origin/devel`, extracts both the newest `upgrade_logger.ps1` and newest `upgrade.cmd` to TEMP, then the logger runs that fresh upgrader. The real upgrade therefore never needs to update the batch file it is currently executing.
- Added legacy recovery in `upgrade_logger.ps1` for already-installed 1.12/1.13 launchers. If positional arguments arrive malformed or incomplete, the logger ignores the malformed repository argument, resolves the Git repository from the working directory, extracts the newest `upgrade.cmd` from `origin/devel`, and switches to the environment-only transport automatically.
- Fixed PowerShell script-scope vs function-scope `$args` handling in legacy recovery by capturing script arguments once in `$ScriptArgs`.
- Removed all PowerShell `param()` binding from the bootstrap logger. This eliminates collisions with PowerShell automatic variables and prevents interactive `Supply values...` prompts.
- Added explicit bootstrap validation for Git working tree, extracted TEMP file existence/size, repository resolution, local-vs-remote `upgrade.cmd` hash, and captured environment state.
- Bootstrap failures now also write a single-run `upgrade.log` with a final `STATUS: FAILED - phase=SELF-UPDATE/BOOTSTRAP` line even if the logger itself cannot start.
- Reviewed remaining PowerShell calls in `upgrade.cmd`: the bootstrap path no longer passes quoted trailing-directory arguments; the remaining logging-path helper receives a normalized `%CD%` path and a file path, and inline PowerShell commands do not use bootstrap parameter binding.
- Preserved console classification (gray normal, yellow warning, red error, green final success), single-run `upgrade.log`, and the 1.11 FAST/SLOW engine/lifecycle behavior.

## 1.13 - 20.08.2026

- Hardened upgrade bootstrap and diagnostics.

## 1.12 - 20.08.2026

- Improved upgrade self-update handling.

## 1.11 - 20.08.2026

- Introduced optimized FAST/SLOW engine architecture.
