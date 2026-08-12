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
- Heat-colored folder icons (green -> yellow -> orange -> red) using Total Commander capabilities, after File Heat behavior is stable.
- Folder icon colors should be configurable similarly to the current heat color map in the configurator, not hard-coded.
