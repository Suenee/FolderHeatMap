# Changelog

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
- Saved the complete pre-reset implementation on the `legacy-0.34` branch as a return point and removed the old in-process async cache headers from `devel`.
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

- Added configurable Folder Heat color/icon mapping and File Heat support.
- Added heat reset tooling and Total Commander integration.
- Added asynchronous cache/database groundwork for performance optimization.
