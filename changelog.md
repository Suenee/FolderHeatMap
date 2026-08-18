# Changelog

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
