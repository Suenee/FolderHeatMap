# FolderHeatMap TODO

## Heat reset action

- Add a Total Commander toolbar/user-command action to reset the direct heat of the currently selected file(s) and/or folder(s) to zero.
- Multi-selection must be supported.
- Reset only the selected item's own/direct activity history. It must not suppress or erase inherited heat coming from hot child folders or files below it.
- Recursive reset is optional and only on explicit request. When a selected folder is reset, ask whether to reset just that folder or the complete subtree below it.
- Removing heat from an item/subtree must naturally affect all parent folders, because inherited heat above it has to be recalculated from the activity that remains.
- The command should be suitable for binding to a Total Commander toolbar button and ideally an `em_...` user command.

## Runtime 1.00 follow-up

- Verify that the dumb shared-memory WDX removes all cursor repaint/flicker and that plugin ON/OFF navigation latency is effectively indistinguishable.
- Persist the published RAM cache generation itself in SQLite, not only the underlying activity history, so a new Total Commander/engine process can restore the last complete cache immediately after startup.
- Add an optional single atomic repaint only when the user remains in one directory long enough for a complete newer batch to finish. Never publish partial results and never perform progressive recoloring.
- Extend FAST/SLOW scheduling metrics so FAST can temporarily assist SLOW during graceful shutdown after interactive/predictive work is no longer needed.

## Later experiments

- Optional tracking of actual file reads/opens in Total Commander, separate from write-based File Heat.
- Find a less visible way to force Total Commander to fully re-evaluate FolderHeatMap color rules after a heat reset without the current previous-tab/next-tab flicker. Keep the tab-switch workaround as the reliable fallback if no cleaner mechanism exists.
- Decide how FolderHeatMap should handle tracked files and folders when they are deleted or moved to the Recycle Bin, including cleanup of stale database records and the resulting inherited heat of parent folders.
- Predictive navigation prefetch: learn transitions between folders (for example A -> B) in addition to individual Heat. Use transition history, recent visits and Heat to estimate which folder the user is likely to open next and precompute/cache its complete stable snapshot in the background.
- Predictive work must always have lower priority than real user navigation. It must never slow foreground browsing; queued predictions should yield immediately when the user opens another folder.
