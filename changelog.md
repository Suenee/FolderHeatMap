# Changelog

## 1.26 - 27.08.2026

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
