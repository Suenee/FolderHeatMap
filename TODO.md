# FolderHeatMap TODO

## After File Heat is stabilized

- Add a Total Commander toolbar/user-command action to reset the direct heat of the currently selected file(s) and/or folder(s) to zero.
- Multi-selection must be supported.
- Reset only the selected item's own/direct activity history. It must not suppress or erase inherited heat coming from hot child folders or files below it.
- The command should be suitable for binding to a Total Commander toolbar button (and ideally an `em_...` user command).

## Later experiments

- Optional tracking of actual file reads/opens in Total Commander, separate from write-based File Heat.
- Heat-colored folder icons (green -> yellow -> orange -> red) using Total Commander capabilities, after File Heat behavior is stable.
