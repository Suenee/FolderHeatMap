# FolderHeatMap

FolderHeatMap is an open-source Total Commander content plugin that builds a visual heat map of folders based on how recently, frequently and regularly they are used.

The project targets Total Commander on Windows 10+ and intentionally tracks directories only.

## Core idea

Only folders that are actually used are stored. A folder that has never been seen has an implicit heat value of `0` and does not need a database record.

The plugin exposes these fields to Total Commander:

- `Heat` - continuous temperature value from 0 to 7
- `Visits` - raw directory entries, useful for diagnostics
- `Last Visit`
- `Heat Level` - integer level 0-7
- `Heat Color Step` - internal helper for visualization

Raw history is kept separate from the scoring model so the heat algorithm can evolve without throwing away folder history.

## Intelligent heat model

Heat is not a simple visit counter. The current model combines three signals:

### Recent heat

Recent heat represents what the user is actively working on now.

- Repeated entries within 90 seconds still increase the diagnostic `Visits` counter, but they do not increase heat.
- An 8-hour inactivity gap starts a new work session for that folder.
- Effective visits use logarithmic / diminishing returns, so the first meaningful visits matter much more than the 20th repetition.
- Recent heat cools substantially faster than long-term habit heat.

### Habit heat

Habit heat represents folders which are part of the user's normal workflow.

- It grows from distinct active days, not from rapid repeated clicking.
- It takes several different working days to build a strong habit.
- Regularity matters: a folder used almost every day becomes warmer than one used occasionally.
- Habit heat cools much more slowly than recent heat.

Recent and habit heat are combined so that either can make a folder warm, but level 7 is normally reached only when a folder is both strongly active now and an established part of the user's workflow. This keeps the hottest color meaningful.

### Automatic cooling

In automatic mode FolderHeatMap observes the user's global rhythm over the last 60 days.

A user active almost every day gets a shorter half-life, while a user who works with Total Commander only occasionally gets a longer half-life. Until enough behavior has been observed, the model uses a neutral 30-day bootstrap value.

Manual mode allows the user to set a fixed half-life in days instead.

### Hot path inheritance

Optionally, parent folders inherit part of the hottest descendant's heat:

```text
inherited heat = descendant heat × path contribution ^ depth
```

Only the hottest descendant path is used. Heat from many mildly warm children is deliberately not added together, because that could make a parent artificially hot.

This allows a useful project path to remain visible even when the user jumps directly to a deep folder instead of navigating through every parent.

## Temperature scale and colors

FolderHeatMap uses eight user-facing temperature points numbered `0` through `7`.

- `0` means no FolderHeatMap color override. Total Commander keeps its normal/default color.
- `1` through `7` are user-configurable color anchors.
- Intermediate shades are interpolated only between adjacent anchors.
- The algorithm never invents colors outside the configured gradient.

Conceptually:

```text
0          1          2          3          4          5          6          7
DEFAULT -> cool ----------------------------------------------------------> hot
```

For example, heat `4.37` lies 37% of the way between the colors configured for anchors `4` and `5`.

The color model applies to folders only, so existing file-type colors in Total Commander are preserved.

FolderHeatMap generates native Total Commander saved-search color filters in `wincmd.ini`. Existing user-created color filters are preserved and appended after the FolderHeatMap rules.

## Settings

Run `FolderHeatMapConfig.exe` or `configure.cmd` to open the settings window.

The settings UI is intentionally English-only because FolderHeatMap is designed as an open-source project and does not carry a translation layer.

The window provides:

- automatic or manual cooling
- manual half-life
- hot descendant / path heat toggle
- path contribution percentage
- smooth color transitions
- number of intermediate color steps
- seven configurable color anchors
- `Save` and `Close`

`Save` writes FolderHeatMap settings, safely closes Total Commander if it is running, regenerates the native Total Commander color rules, verifies the result, and starts Total Commander again when appropriate.

### Opening settings from Total Commander

The WDX content-plugin interface does not currently provide a native configuration callback/button for a plugin. Therefore FolderHeatMap cannot add its settings window directly to Total Commander's Content Plugins dialog through the WDX API.

The practical integration is to launch `FolderHeatMapConfig.exe` (or `configure.cmd`) from a Total Commander toolbar button, Start-menu entry, or user command.

## Path identity

Drive letters are not part of the permanent folder identity.

A local folder is identified internally by:

```text
<VolumeID> + <relative path inside the volume>
```

Example:

```text
E:\WORK\GitHub\FolderHeatMap
```

may be stored conceptually as:

```text
VolumeID = \\?\Volume{persistent-volume-id}\
Path     = WORK\GitHub\FolderHeatMap
```

If Windows later mounts the same removable volume as `G:`, the FolderHeatMap history remains attached to the same folder.

Local volumes and network/UNC paths use separate identity strategies.

## Storage

FolderHeatMap uses SQLite with WAL mode and `synchronous=NORMAL` for lightweight embedded persistence.

The database keeps raw visits plus compact usage state used by the intelligent model. Existing databases are upgraded in place by creating the additional usage table; old visit history is retained and bootstrapped into the new scoring model.

## Target

- Total Commander 11.58 x64
- Windows 10+
- WDX content plugin
- C++ / Win32 API
- SQLite persistence

## Current milestone

The implementation now proves that:

1. Total Commander can query `Heat`, `Visits`, `Last Visit` and heat-level fields through WDX.
2. Folder identity survives removable-drive letter changes.
3. Folder usage is recorded while navigating in Total Commander.
4. Heat values can be sorted and visualized through native Total Commander color rules.
5. The settings application can generate and verify a smooth 0-7 heat color map automatically.
