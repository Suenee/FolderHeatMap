# FolderHeatMap

FolderHeatMap is an open-source Total Commander content plugin that builds a visual heat map of folders and, optionally, files based on how recently, frequently and regularly they are used or changed.

The project targets Total Commander on Windows 10+.

## Core idea

Only items with useful activity history need persistent records. An unseen item has an implicit heat value of `0`.

The plugin exposes these fields to Total Commander:

- `Heat` - continuous temperature value from 0 to 7
- `Visits` - raw directory entries, useful for diagnostics
- `Last Visit`
- `Heat Level` - integer level 0-7
- `Heat Color Step` - internal helper for visualization
- `Writes` - tracked file modification count
- `Last Write`

Raw history is kept separate from the scoring model so the heat algorithm can evolve without throwing away usage history.

## Runtime architecture 1.00

FolderHeatMap 1.00 deliberately separates Total Commander from all expensive work.

`FolderHeatMap.wdx64` is a dumb read-only client. Its hot path reads only the latest complete generation from shared RAM. It performs no filesystem analysis, SQLite queries, heat calculations or background scheduling while Total Commander asks for a displayed value.

`FolderHeatMapEngine.exe` runs separately from Total Commander and owns all analysis. It uses two worker roles:

- **FAST worker** - real user navigation first, then high-probability predictive prefetch.
- **SLOW worker** - persistence, file-write observation, durable rebuilds and lower-priority preparation.

Real navigation always outranks prediction. The current directory never receives partially calculated values. A completed batch for the directory being viewed remains hidden until the user leaves; it can then be used immediately on a later visit. Predicted folders may already be complete before the user opens them.

Shared RAM is double-buffered and switched atomically. Total Commander therefore sees one complete cache generation or another, never an in-progress generation.

SQLite is persistence/backup, not the foreground data source. The engine stores both activity history and the latest complete runtime cache so a later process can restore useful values without rebuilding everything from zero.

When the final Total Commander client closes, the engine enters graceful shutdown. Predictive work is discarded, FAST can help drain remaining persistence work, completed RAM state is written to SQLite, and only then does the engine exit.

## Intelligent heat model

Heat is not a simple visit counter. The current model combines recent work, long-term habit and optional hot-path inheritance.

### Recent heat

Recent heat represents what the user is actively working on now. Rapid repeated entries are protected by a configurable cooldown (default 90 seconds): raw `Visits` still increases, but Heat does not. A configurable inactivity gap (default 8 hours) starts a new recent-work session. Effective visits use logarithmic diminishing returns, so early meaningful visits matter much more than repeated re-entry.

### Habit heat

Habit heat represents folders which are part of the user's normal workflow. It grows from distinct active days, takes several working days to mature, rewards regularity and cools much more slowly than recent heat.

Recent and habit heat are combined so that either can make a folder warm, but level 7 is normally reached only when a folder is both strongly active now and an established part of the user's workflow.

### Automatic cooling

In automatic mode FolderHeatMap observes the user's global rhythm over the last 60 days. A user active almost every day gets a shorter half-life, while an occasional user gets a longer half-life. Until enough behavior has been observed, the model uses a neutral 30-day bootstrap value.

Manual mode allows a fixed cooling half-life from 1 to 365 days.

### Hot path inheritance

Optionally, parent folders inherit heat from active descendants with configurable path decay. Descendant influence becomes weaker with distance from the parent.

### File Heat

Optional File Heat uses observed file modification timestamps and write history. File Heat can also contribute to the temperature of parent folders. Expensive file observation happens in the background engine, never in the WDX foreground path.

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

## Settings

Run `FolderHeatMapConfig.exe` or `configure.cmd` to open the settings window. The UI is intentionally English-only.

The compact settings window provides automatic/manual cooling, a 1-365 day manual half-life, hot-path inheritance, 0-100% path contribution, File Heat controls, configurable repeat-visit cooldown, session reset, smooth color interpolation, intermediate color steps, a read-only Level 0 base-color preview, editable Levels 1-7 and configurable folder icons.

`Save` writes FolderHeatMap settings, safely closes Total Commander when necessary, regenerates and verifies the Total Commander color/icon rules, and restarts Total Commander when appropriate. `Cancel` closes the configurator without saving current changes.

The configurator refuses to run without a readable and writable Total Commander `wincmd.ini`. If automatic detection fails, it explains where Total Commander shows the active INI path and opens a file picker so the user can select it.

### Logging

The background engine supports file logging through `FolderHeatMap.ini`:

```ini
[Logging]
Mode=off
```

Allowed values are `off`, `single` and `all`. `single` starts a fresh log for the current engine run; `all` appends subsequent runs to the same log. The default is `off`. The log file is `FolderHeatMap.log` beside `FolderHeatMap.ini`.

### Opening settings from Total Commander

The WDX content-plugin interface does not provide a native configuration callback/button. The practical integration is therefore to launch `FolderHeatMapConfig.exe` from a Total Commander toolbar button, Start-menu entry, keyboard shortcut or user command.

## Path identity

Drive letters are not part of the permanent folder identity. A local folder is identified internally by a persistent volume ID plus its relative path, so removable-drive history survives drive-letter changes. Local volumes and network/UNC paths use separate identity strategies.

## Storage

FolderHeatMap uses SQLite with WAL mode and `synchronous=NORMAL`. SQLite stores activity history plus the latest complete runtime-cache generation. Existing databases are upgraded in place.

## Upgrade

Run only:

```bat
upgrade.cmd
```

The script updates `devel`, relaunches the freshly pulled upgrader, prepares dependencies, builds the WDX, background engine, configurator and reset utility, stops Total Commander for atomic deployment when necessary, deploys `FolderHeatMapEngine.exe` beside the registered WDX and restarts Total Commander if it had been running.

## Target

- Total Commander x64
- Windows 10+
- WDX content plugin
- C++ / Win32 API
- SQLite persistence

## Legacy return point

The pre-1.00 runtime is preserved on branch `legacy-0.34`. The 1.00 architecture intentionally does not reuse the old in-process async/cache runtime.
