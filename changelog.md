# Changelog

## 1.12 - 20.08.2026

- Added `upgrade.log` in the repository root as a single-run diagnostic log. Every new `upgrade.cmd` run truncates the previous log and records the complete visible bootstrap, configure, build and deploy output.
- Added live console classification through `upgrade_logger.ps1`: normal output is gray, warnings are yellow, compiler/CMake/upgrade errors are red, and the final successful status is green.
- The first diagnostic block records the upgrade revision, start timestamp, repository path, branch and starting commit; after self-update the exact build commit is logged as well.
- `upgrade.log` is covered by the existing `*.log` repository ignore rule and is intended as the quick artifact to upload for troubleshooting a failed or suspicious upgrade.
- The final line of every captured run is now a machine- and human-readable status: `STATUS: SUCCESS`, `STATUS: WARNING`, or `STATUS: FAILED`, with the failed phase included where applicable.
- Added a logger-side fallback status if the child upgrade exits unexpectedly before writing its own final status.
- Preserved the self-updating `upgrade.cmd` guarantee. A compatibility path allows the first launch from an older local upgrader to start the new logger after fetching the current 1.12 script.
- Kept the 1.11 two-worker FAST/SLOW lifecycle, Heat/File Heat, colors, WDX hot path and persistence behavior unchanged.

## 1.11 - 20.08.2026

- Kept the two-worker architecture only: FAST remains interactive/predictive and SLOW owns persistence, file-write observation and lifecycle maintenance.
- Added SLOW-only lifecycle reconciliation using stable volume-local Windows file IDs where supported. Same-volume rename/move preserves the tracked history; recycle-bin moves, deletes and cross-volume moves remove the old-volume history.
- Added a `tracked_objects` persistence index so a missing path can be resolved by file ID on the same volume. Moving a whole tracked directory updates its subtree by SQL prefix operations instead of visiting every descendant record individually.
- Large sets of file lifecycle changes are accumulated and applied in one SQLite transaction rather than thousands of per-file commits.
- RAM/cache lifecycle changes are coalesced into one atomic publication per SLOW batch. Moved paths are remapped, deleted paths are removed, and stale ready batches touching the old/new branches are invalidated.
- Recalculate both sides of a tree mutation: ancestors of the old location lose the removed contribution and ancestors of the new location gain it. Duplicate dirty ancestors are collapsed so each affected branch is recalculated only once per batch.
- Full runtime-cache persistence is now dirty/debounced. SLOW flushes after a short idle delay instead of rewriting the complete cache after every navigation, while graceful shutdown still performs a mandatory final flush.
- Preserved the 1.10 duplicate-navigation suppression, unchanged-batch suppression, single FAST prediction trigger, Heat/File Heat mathematics, Total Commander color/icon behavior and RAM-only WDX foreground path.
- Reworked `upgrade.cmd` bootstrap return handling to remove the spurious post-success `RAP_RC` error. Upgrade-owned error messages are now printed in red, warnings in yellow and successful completion in green.

## 1.10 - 20.08.2026

- Coalesced shared-RAM publication so batches and own-directory snapshots equivalent to the already published state no longer create a new cache generation.
- Suppressed duplicate same-directory navigation notifications before they can enqueue SLOW work or FAST predictions.
- Removed the second scheduling of the same hot-child predictions from the SLOW refresh path. Real navigation remains the authoritative prediction trigger.
- Runtime cache persistence now runs only when the public RAM state actually changed; failed persistence marks the cache dirty again for a later retry.
- Added a 0.001 Heat-equivalence tolerance so tiny cooling-time differences do not create pointless cache generations while Visits, Writes, timestamps, Heat Level and Color Step remain exact change triggers.
- Coalesced shutdown publication/persistence instead of blindly republishing unchanged ready batches.
- Left Heat/File Heat mathematics, Total Commander color/icon behavior, repository-local logging, the WDX foreground hot path and the FAST/SLOW architecture unchanged.
- Added guarded build-time engine source generation in `cmake/GenerateOptimizedEngine.cmake`. The verified `src/EngineApp.cpp` remains untouched; CMake aborts if any expected 1.09 source anchor no longer matches, preventing a partial/stale optimization from being compiled.

## 1.09 - 20.08.2026

- Logging is now strictly repository-local. `upgrade.cmd` writes the absolute repository-root `FolderHeatMap.log` path into `[Logging] Path` in `FolderHeatMap.ini`; the engine has no fallback to the Total Commander profile directory.
- `upgrade.cmd` removes the obsolete profile-local `FolderHeatMap.log` created by the short-lived 1.08 behavior.
- Added repository `*.log` ignore so runtime logs never enter Git history.
- The configurator displays the configured repository log path and still saves `Logging = off/single/all` before Total Commander restarts.
- Reverted the 1.08 staged color cleanup. Neither the logging extension nor `upgrade.cmd` removes or rewrites Total Commander color/icon rules. Existing FolderHeatMap coloring is preserved and the normal configurator remains authoritative for color settings.
- Heat, Visits and Writes remain read-only RAM diagnostics with FAST/SLOW workers and no explicit repaint request.

## 1.08 - 20.08.2026

- Fixed logging-save ordering in the configurator. `Logging = single/all` is now written to `FolderHeatMap.ini` before Total Commander is restarted, so the newly started engine reads the selected mode immediately.
- Made the log location deterministic and visible in the configurator. `FolderHeatMap.log` is stored next to `FolderHeatMap.ini`, normally in `%APPDATA%\GHISLER\FolderHeatMap.log`.
- The engine now creates and flushes an initial logging-session line immediately on startup when logging is enabled. A navigation or file-write event is no longer required before the log file appears.
- `single` truncates the log for every new engine/TC session; `all` appends to the existing log; `off` does not create or write the log.
- Fixed a staged-testing regression where pressing Save in the configurator reinstalled FolderHeatMap color filters and immediately re-enabled Heat coloring. During the current diagnostic phase Save now removes FolderHeatMap-managed color/icon integration again before TC continues.
- Heat, Visits and Writes remain read-only RAM diagnostics. No repaint request or foreground Heat calculation was added.

## 1.07 - 19.08.2026

- Added `Writes` back to the staged WDX as a read-only shared-RAM diagnostic next to `Heat` and `Visits`. The WDX hot path remains RAM-only and does not perform filesystem work, SQLite access, Heat math, queueing, prediction or repaint requests.
- Fixed File Heat first-observation semantics. The first time a file is discovered, FolderHeatMap now stores only its current `LastWrite` timestamp as a cold baseline with `Writes = 0`; only a later timestamp change counts as the first real write event.
- Kept Total Commander color and heat-icon integration disabled so this release still tests data correctness and cursor stability before visual coloring is restored.
- Added a Logging selector to the configurator with `off`, `single` and `all` modes without rewriting the existing configurator UI implementation.
- `single` truncates `FolderHeatMap.log` when the engine starts, so the file covers one Total Commander/engine session. `all` appends across sessions. `off` performs no file logging.
- The existing background diagnostic log records engine lifecycle, FAST navigation/prediction work, SLOW persistence work, RAM generation publication and runtime-cache persistence, making worker activity inspectable without adding foreground work to Total Commander.
- Preserved the two-speed FAST/SLOW architecture and the one-step-behind display model.

## 1.06 - 19.08.2026

- Fixed a staged-runtime consistency bug where entering a directory updated the database but did not refresh that directory's own RAM cache entry.
- The SLOW worker now recalculates the visited directory itself immediately after persisting the visit and atomically stores that snapshot in shared RAM.
- The child batch for the currently viewed directory remains hidden exactly as before, so this change does not reintroduce progressive panel updates or repaint requests.
- The practical result is one-step-behind behavior instead of potentially many-step-old parent views: after visiting `X`, its parent can read the new `Visits`/`Heat` for `X` the next time that item is normally requested.
- WDX remains read-only and RAM-only. Total Commander color/icon integration remains disabled for this staged test.

## 1.05 - 19.08.2026

- Exposed `Heat` to Total Commander again as a read-only numeric field alongside `Visits`.
- Both fields are served exclusively from the latest complete shared-RAM generation. `ContentGetValueW()` still performs no filesystem access, SQLite access, Heat math, queueing, prediction or repaint request.
- FAST/SLOW workers remain external in `FolderHeatMapEngine.exe` and continue calculating/persisting Heat in the background.
- Total Commander color filters and heat-icon associations remain deliberately disabled. This release tests only whether reading already-computed Heat values from RAM remains visually stable.
- Missing RAM entries return a definitive numeric zero rather than delayed/empty state.
- Acceptance criterion: Heat values become visible after background preparation/later navigation while cursor movement and directory browsing remain as stable as 1.04.

## 1.04 - 19.08.2026

- Reintroduced the full background calculation engine behind the stable 1.03 counter-only WDX baseline.
- Total Commander still sees exactly one FolderHeatMap field: `Visits`. Heat, color, write and date fields remain hidden from TC, and FolderHeatMap color/icon rules remain removed.
- Restored the two-speed worker architecture in the external `FolderHeatMapEngine.exe` process.
- **FAST worker** handles real navigation first and can prepare high-probability follow-up directories in RAM.
- **SLOW worker** handles persistence, file-write observation, durable runtime-cache saves and lower-priority preparation.
- Restored the existing Heat mathematics, inherited heat, optional File Heat calculations, runtime cache persistence and heat-based prediction internally, but none of these results are exposed to Total Commander yet.
- Real navigation always outranks predictive work. Prediction remains background-only and cannot request a TC repaint.
- The WDX foreground path remains unchanged in principle: it reads only `Visits` from shared RAM and performs no Heat calculation, SQLite query, filesystem scan, prediction or repaint operation.
- Added the settings path when launching the background engine so the restored workers use the configured cooling, path contribution, File Heat and logging settings.
- This version is deliberately a hidden-engine test. The acceptance criterion is that cursor/navigation behavior remains as stable and fast as 1.03 while the two workers calculate in the background.

## 1.03 - 19.08.2026

- Tightened the diagnostic baseline after confirming that Total Commander still evaluated old FolderHeatMap color/icon integration even though Heat math had been disabled.
- The WDX now exposes exactly one field: `Visits`. `Heat`, `Heat Level`, `Heat Color Step`, `Writes`, and date fields no longer exist in the diagnostic plugin.
- Added `cleanup_tc_integration.ps1`, invoked automatically by `upgrade.cmd` while Total Commander is stopped. It removes only FolderHeatMap-managed color filters, saved searches, and heat-icon associations from `wincmd.ini` while preserving unrelated user color rules and associations.
- The cleanup creates a timestamped backup of `wincmd.ini` before changing anything.
- Explicit Total Commander refresh no longer counts as a visit; only `contst_readnewdir` is forwarded to the counter engine.
- No coloring, icon heat mapping, Heat mathematics, prediction, directory scanning, or repaint request remains active in this baseline.
- Goal: establish whether raw visit counting alone can run with completely stable cursor/navigation behavior before any other layer is reintroduced.

## 1.02 - 19.08.2026

- Deliberately reduced the runtime to a counter-only diagnostic baseline after persistent Total Commander cursor repaint/flicker problems in the full Heat runtime.
- Saved the complete 1.01 implementation on `legacy-1.01` before simplifying `devel`.
- Disabled all Heat mathematics, inherited Heat, File Heat scanning, prediction, FAST/SLOW scheduling, frozen-view logic and background repaint behavior.
- The WDX now returns stable zero for `Heat`, `Heat Level`, `Heat Color Step` and `Writes`; date fields remain empty. Existing Total Commander Heat color rules therefore have no active Heat value to color.
- Only the `Visits` field reads dynamic data. It is served from a tiny shared-memory counter map and does not perform SQLite or filesystem work inside `ContentGetValueW()`.
- The background engine now does exactly one job: receive `contst_readnewdir`, increment the raw visit counter in SQLite, and publish the updated visit count for that folder into RAM.
- No directory enumeration, descendant traversal or file timestamp inspection occurs in the engine.
- This build is intended as a clean performance/visual-stability baseline. New features will be reintroduced one layer at a time only after the cursor remains completely stable.

## 1.01 - 19.08.2026

- Added a frozen per-directory WDX view. On `contst_readnewdir`, the plugin captures exactly one complete shared-RAM generation before notifying the background engine about the new navigation step.
- `ContentGetValueW()` now reads only from that frozen view for the entire directory visit. Worker activity, predictive prefetch and persistence can continue changing background RAM without changing what Total Commander sees mid-visit.
- This formalizes the one-step-behind model: the user sees the last complete state that existed when the directory was entered; newer calculations become eligible on a later directory entry.
- No background task requests repainting. The only in-place refresh path is an explicit user refresh, which still swaps one complete snapshot rather than publishing per-item updates.
- The goal is zero progressive recoloring and zero cursor repaint storms while keeping already-predicted directories effectively instantaneous.

## 1.00 - 18.08.2026

- Architecture reset. Preserved the existing Folder Heat/File Heat mathematics, settings, configurator, reset tooling, colors and icon integration while replacing the runtime path completely.
- Saved the complete pre-reset implementation on `legacy-0.34` branch as a return point and removed the old in-process async cache headers from `devel`.
- Reduced the WDX plugin to a dumb read-only shared-memory client. `ContentGetValueW()` no longer performs filesystem work, SQLite access, heat calculations, background queueing or cache mutation.
- Added `FolderHeatMapEngine.exe` as a separate background process. All filesystem analysis, heat calculation, prediction and database persistence now live outside the Total Commander process.
- Added double-buffered shared RAM with reader counters and atomic buffer switching. Total Commander always reads one complete cache generation; partially calculated data is never exposed.
- SQLite is now explicitly persistence/backup for runtime state. Added a `runtime_cache` table which stores the latest complete RAM cache and restores it when the engine starts.
- Split database access into separate read and write connections so FAST calculation is not serialized by the application-level mutex used for SLOW persistence writes.
- Added FAST and SLOW worker roles. FAST handles real navigation and heat-based predictions; SLOW records visits/file writes, durable persistence and lower-priority maintenance.
- A calculated snapshot for the currently viewed directory stays hidden. It becomes public only after the user leaves, so background completion cannot progressively recolor the current panel.
- Added initial predictive prefetch: the hottest immediate child directories are prepared while the FAST worker is idle. Real navigation always has priority over prediction.
- Added graceful shutdown. The final WDX client requests engine shutdown; predictive work is discarded, FAST finishes interactive work and can assist with the remaining SLOW persistence queue, completed RAM state is saved to SQLite, and only then does the engine exit.
- Added engine file logging with `off`, `single` and `all` modes. Logging is disabled by default and can be enabled through the `[Logging]` section of `FolderHeatMap.ini`.
- Rebuilt `upgrade.cmd` for the new architecture. It relaunches itself after `git pull`, gracefully closes Total Commander and waits for the engine to drain before rebuilding, builds and deploys both the WDX and engine, and only force-stops an engine after a 30-second shutdown timeout.
- Updated CI packaging to include `FolderHeatMapEngine.exe` and the reset utility.

## 0.34 - 18.08.2026

- Changed missing Heat/Heat Level/Heat Color Step values from `ft_fieldempty` to definitive numeric zero values, matching the previously verified zero-work diagnostic behavior that did not cause Total Commander repaint storms.
- Normalized snapshot and directory cache keys case-insensitively, converted `/` to `\\`, and removed trailing separators except for drive roots so equivalent Windows paths cannot miss an already prepared snapshot.
- Kept `ContentGetValueW()` strictly RAM-only and preserved the stable batch rule: a background batch cannot become visible during the current directory visit.
- File entries with File Heat disabled now also return stable numeric zero for Heat-related fields instead of an empty value.
- The goal of this release is to eliminate repeated TC color/icon reevaluation and cursor flicker caused by unresolved/empty WDX values while retaining the eventually-consistent batch architecture.

## 0.33 - 18.08.2026

- Replaced per-item asynchronous snapshot refreshes with stable per-directory batch snapshots.
- `ContentGetValueW()` is now a strict RAM-only hot path: no filesystem access, SQLite calls, Heat calculation, queueing or delayed retries happen while Total Commander asks for a displayed value.
- Entering a directory promotes only the last fully completed snapshot from an earlier visit. A new snapshot is calculated independently in the background and stays hidden until a later visit.
- Background calculation enumerates the directory once, calculates all immediate items, and atomically publishes the complete batch only after every item has been processed.
- Removed progressive per-item recoloring and `ft_delayed` refresh behavior from normal browsing. Missing snapshot data is left empty for the current visit instead of causing incremental repainting.
- Directory visit recording is now started from the background batch worker as well, keeping foreground navigation free from folder identity and filesystem work.
- File metadata already returned by directory enumeration is reused for File Heat instead of re-reading each file timestamp separately.
- This release deliberately favors visual stability and navigation latency over immediate Heat freshness: the displayed Heat may be one visit behind while background data catches up.

## 0.32 - 18.08.2026

- Added an in-memory per-path WDX snapshot cache so repeated Heat, Visits, Writes, Heat Level and color queries are served from RAM instead of recalculating the same item multiple times.
- Changed delayed evaluation to stale-while-revalidate behavior: if a previous snapshot exists, Total Commander receives it immediately while a background worker refreshes it asynchronously.
- Added a dedicated snapshot refresh worker with duplicate suppression so repeated requests for the same path do not create redundant work.
- Kept `ft_delayed` only for a path that has no snapshot yet; subsequent visits reuse the last known visual state immediately.
- Refreshing Total Commander reloads settings and clears snapshots so the new configuration is recalculated cleanly.
- The goal of this release is to remove the visible green-to-heat recoloring and cursor redraw flicker introduced by the first `CONTENT_DELAYIFSLOW` implementation while keeping navigation responsive.

## 0.31 - 18.08.2026

- Restored the full Folder Heat and File Heat engine after the zero-work WDX performance diagnostic build.
- Enabled the existing asynchronous SQLite/cache facade for the WDX plugin while keeping reset operations on the synchronous database implementation.
- Added Total Commander `CONTENT_DELAYIFSLOW` handling: expensive WDX values return `ft_delayed` during foreground evaluation and are calculated when Total Commander retries them in the background.
- Removed per-value INI reloads from the WDX hot path. Settings are loaded at plugin initialization and reloaded on Total Commander refresh.
- Kept directory visit recording asynchronous through the cache/database worker.
- Preserved the existing heat mathematics, inherited heat, File Heat, color levels, icon integration, configurator, and reset tool.

## 0.30

- Added configurable Folder Heat/File Heat support.
- Added heat reset tooling and Total Commander integration.
- Added asynchronous cache/database groundwork for performance optimization.