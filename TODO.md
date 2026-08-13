# FolderHeatMap TODO

## Heat reset action

- Add a Total Commander toolbar/user-command action to reset the direct heat of the currently selected file(s) and/or folder(s) to zero.
- Multi-selection must be supported.
- Reset only the selected item's own/direct activity history. It must not suppress or erase inherited heat coming from hot child folders or files below it.
- Recursive reset is optional and only on explicit request. When a selected folder is reset, ask whether to reset just that folder or the complete subtree below it.
- Removing heat from an item/subtree must naturally affect all parent folders, because inherited heat above it has to be recalculated from the activity that remains.
- The command should be suitable for binding to a Total Commander toolbar button and ideally an `em_...` user command.

## Later experiments

- Optional tracking of actual file reads/opens in Total Commander, separate from write-based File Heat.
- Find a less visible way to force Total Commander to fully re-evaluate FolderHeatMap color rules after a heat reset without the current previous-tab/next-tab flicker. Keep the tab-switch workaround as the reliable fallback if no cleaner mechanism exists.
- Decide how FolderHeatMap should handle tracked files and folders when they are deleted or moved to the Recycle Bin, including cleanup of stale database records and the resulting inherited heat of parent folders.
