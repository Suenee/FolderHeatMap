# FolderHeatMap

FolderHeatMap is a Total Commander content plugin that builds a visual heat map of folders based on how frequently and how recently they are used.

The project is designed for Total Commander on Windows 10+ and starts with directory activity only. Files may be considered later, but they are intentionally out of scope for the first versions.

## Core idea

Only folders that are actually used are stored. A folder that has never been seen has an implicit heat value of `0` and does not need a database record.

The plugin should expose heat-related metadata to Total Commander so that folders can be visually distinguished and sorted by usage temperature.

Planned fields include:

- `Heat`
- `Visits`
- `Last Visit`
- `Heat Level`

The exact scoring algorithm is intentionally separated from the raw activity data so it can evolve without losing history.

## Path identity

Drive letters are not part of the permanent folder identity.

A folder is identified internally by:

```text
<VolumeID> + <relative path inside the volume>
```

Example:

```text
E:\WORK\GitHub\FolderHeatMap
```

may be stored conceptually as:

```text
VolumeID = {persistent-volume-id}
Path     = WORK\GitHub\FolderHeatMap
```

If Windows later mounts the same removable volume as `G:`, the FolderHeatMap history remains attached to the same folder.

Local volumes and network/UNC paths will use separate identity strategies.

## Storage

The initial plan is to use SQLite as a lightweight embedded database.

The database stores only used folders and their raw activity data. Derived heat values can be calculated from this data.

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

## First milestone

The first implementation should prove four things:

1. Total Commander can query a custom `Heat` field through a WDX plugin.
2. Folder identity survives removable-drive letter changes.
3. Folder usage can be recorded reliably when navigating in Total Commander.
4. The resulting heat value can be used for sorting and visual differentiation inside Total Commander.
