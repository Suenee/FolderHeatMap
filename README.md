# FolderHeatMap

FolderHeatMap is a Total Commander content plugin that builds a visual heat map of folders based on how frequently and how recently they are used.

The project is designed for Total Commander on Windows 10+ and starts with directory activity only. Files are intentionally out of scope for the first versions.

## Core idea

Only folders that are actually used are stored. A folder that has never been seen has an implicit heat value of `0` and does not need a database record.

The plugin exposes heat-related metadata to Total Commander so that folders can be visually distinguished and sorted by usage temperature.

Planned fields include:

- `Heat` - continuous temperature value
- `Visits`
- `Last Visit`
- `Heat Level` - integer level 0-7

The scoring algorithm is intentionally separated from the raw activity data so it can evolve without losing history.

## Temperature scale and colors

FolderHeatMap uses eight user-facing temperature points numbered `0` through `7`.

- `0` means no FolderHeatMap color override. Total Commander keeps its normal/default color.
- `1` through `7` are user-configurable color anchor points.
- The default palette will progress from cool/low activity to hot/high activity.
- Intermediate colors are calculated only between adjacent user-defined anchors. The algorithm must never invent colors outside the configured gradient.

Conceptually:

```text
0          1          2          3          4          5          6          7
DEFAULT -> cool ----------------------------------------------------------> hot
```

The internal heat score may be continuous. For example, a temperature of `4.37` lies 37% of the way between the colors configured for anchors `4` and `5`.

This design deliberately avoids automatically darkening folders into colors which could disappear against a user's Total Commander background.

The color model applies to folders only, so existing file-type colors in Total Commander remain untouched.

### Total Commander integration note

A WDX content plugin can expose numeric fields which Total Commander can use in custom columns, sorting, searching and "Colors by file type" rules. WDX itself does not directly paint each row with an arbitrary per-item color. FolderHeatMap will therefore keep heat calculation separate from the visualization adapter. The planned visualization layer can generate Total Commander color rules from the configured anchors and interpolated steps while the WDX plugin remains the source of the heat values.

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

The target storage engine is SQLite as a lightweight embedded database.

The database stores only used folders and their raw activity data. Derived heat values are calculated from this data.

A preliminary data model is:

```text
volumes
-------
volume_id
last_mount_point
label
last_seen

paths
-----
volume_id
relative_path
visits
last_visit
```

## Target

- Total Commander 11.58 x64
- Windows 10+
- WDX content plugin
- C++ / Win32 API
- SQLite persistence

## First milestone

The first implementation should prove four things:

1. Total Commander can query custom `Heat`, `Visits`, `Last Visit` and `Heat Level` fields through the WDX plugin.
2. Folder identity survives removable-drive letter changes.
3. Folder usage can be recorded reliably when navigating in Total Commander.
4. Heat values can be used for sorting and Total Commander color rules.

The first code prototype intentionally starts with in-memory activity data. SQLite persistence follows after the WDX navigation and identity behavior has been verified inside Total Commander.
