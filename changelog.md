# Changelog

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
