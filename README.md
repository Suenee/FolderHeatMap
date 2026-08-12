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

Heat is not a simple visit counter. The current model combines three signals: recent work, long-term habit and optional hot-path inheritance.

### Recent heat

Recent heat represents what the user is actively working on now. Rapid repeated entries are protected by a configurable cooldown (default 90 seconds): raw `Visits` still increases, but Heat does not. A configurable inactivity gap (default 8 hours) starts a new recent-work session. Effective visits use logarithmic diminishing returns, so early meaningful visits matter much more than repeated re-entry.

### Habit heat

Habit heat represents folders which are part of the user's normal workflow. It grows from distinct active days, takes several working days to mature, rewards regularity and cools much more slowly than recent heat.

Recent and habit heat are combined so that either can make a folder warm, but level 7 is normally reached only when a folder is both strongly active now and an established part of the user's workflow.

### Automatic cooling

In automatic mode FolderHeatMap observes the user's global rhythm over the last 60 days. A user active almost every day gets a shorter half-life, while an occasional user gets a longer half-life. Until enough behavior has been observed, the model uses a neutral 30-day bootstrap value.

Manual mode allows a fixed cooling half-life from 1 to 365 days.

### Hot path inheritance

Optionally, parent folders inherit part of the hottest descendant's heat:

```text
inherited heat = descendant heat × path contribution ^ depth
```

Only the hottest descendant path is used. Heat from many mildly warm children is deliberately not added together.

## Temperature scale and colors

FolderHeatMap uses eight user-facing temperature points numbered `0` through `7`.

- `0` means no FolderHeatMap color override and previews Total Commander's current base text color.
- `1` through `7` are user-configurable color anchors.
- Intermediate shades are interpolated only between adjacent anchors.
- The algorithm never invents colors outside the configured gradient.

Conceptually:

```text
0          1          2          3          4          5          6          7
DEFAULT -> cool ----------------------------------------------------------> hot
```

The color model applies to folders only, so existing file-type colors in Total Commander are preserved. FolderHeatMap generates native Total Commander saved-search color filters in `wincmd.ini` and keeps existing user-created color filters after its own rules.

## Settings

Run `FolderHeatMapConfig.exe` or `configure.cmd` to open the settings window. The UI is intentionally English-only.

The compact settings window provides automatic/manual cooling, a 1-365 day manual half-life, hot-path inheritance, a 0-100% path contribution slider, configurable repeat-visit cooldown (0-600 seconds), configurable session reset (1-24 hours), smooth color interpolation, 1-16 intermediate steps, a read-only Level 0 base-color preview, and editable Levels 1-7. Both the level button and its color square open the color picker. Detailed explanations are available through tooltips and the `Help` button.

`Save` writes FolderHeatMap settings, safely closes Total Commander when necessary, regenerates and verifies the Total Commander color rules, and restarts Total Commander when appropriate. `Cancel` closes the configurator without saving current changes.

The configurator refuses to run without a readable and writable Total Commander `wincmd.ini`. If automatic detection fails, it explains where Total Commander shows the active INI path and opens a file picker so the user can select it.

### Opening settings from Total Commander

The WDX content-plugin interface does not provide a native configuration callback/button. The practical integration is therefore to launch `FolderHeatMapConfig.exe` from a Total Commander toolbar button, Start-menu entry, keyboard shortcut or user command.

## Path identity

Drive letters are not part of the permanent folder identity. A local folder is identified internally by a persistent volume ID plus its relative path, so removable-drive history survives drive-letter changes. Local volumes and network/UNC paths use separate identity strategies.

## Storage

FolderHeatMap uses SQLite with WAL mode and `synchronous=NORMAL` for lightweight embedded persistence. The database keeps raw visits plus compact usage state used by the intelligent model. Existing databases are upgraded in place.

## Target

- Total Commander 11.58 x64
- Windows 10+
- WDX content plugin
- C++ / Win32 API
- SQLite persistence

## Current milestone

The implementation now proves that Total Commander can query and sort heat fields through WDX, directory usage can be recorded persistently, folder identity can survive drive-letter changes, smooth native Total Commander color rules can visualize Heat, and the standalone settings application can configure both appearance and the core timing behavior of the intelligent heat model.
