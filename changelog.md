# Changelog

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
