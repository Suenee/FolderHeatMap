# FolderHeatMap - WDX test

Target: Total Commander 11.58 x64 on Windows 10+.

This build stores folder activity persistently in an embedded SQLite database and adds a graphical configuration utility.

## Upgrade and build

1. Pull the latest `devel` branch.
2. Run `upgrade.cmd`.
3. The script builds `dist\FolderHeatMap.wdx64` and `dist\FolderHeatMapConfig.exe`.
4. If Total Commander was running, the upgrade script closes it for deployment and starts it again afterwards.

## Configuration GUI

Run `configure.cmd` from the repository root.

The GUI currently controls:

- automatic or manual cooling,
- manual cooling half-life in days,
- whether heat propagates from hot descendants to their parent path,
- path propagation decay percentage per tree level,
- color anchors 1-7,
- smooth color transitions and the number of intermediate steps.

Heat level 0 never receives a FolderHeatMap color override. Total Commander keeps its normal color for it.

The current WDX interface has no standard callback for opening a content-plugin configuration dialog from Total Commander's WDX plugin list, so the graphical settings utility is a companion executable. `configure.cmd` is the one-click launcher.

## Custom columns

The FolderHeatMap view can use these fields:

- Heat
- Visits
- Last Visit
- Heat Level
- Heat Color Step (technical field used by generated Total Commander color rules)

Fields are intentionally returned only for directories.

## Color-map test

1. Run `configure.cmd`.
2. Keep the default 1-7 colors for the first test.
3. Keep `Smooth transitions` enabled and `4` intermediate steps per level.
4. Click `Použít a aktualizovat TC`.
5. The utility writes FolderHeatMap saved searches and color filters into the active `wincmd.ini` while preserving existing non-FolderHeatMap color filters.
6. If Total Commander is running, the utility restarts it so the new color rules are loaded.
7. Return to a directory containing folders with different Heat values.
8. Verify that unused/zero-heat folders keep their original Total Commander color and hotter folders move through the configured color gradient.

## Path heat test

1. Enable `Zohlednit horké podadresáře` and set path decay to 50%.
2. Make a deep child folder hot by visiting it several times, preferably by jumping directly to it.
3. Return to its parent, grandparent, and higher levels.
4. Verify that `Visits` only reflects direct visits, while `Heat` on the ancestors receives a progressively weaker inherited contribution.
5. Disable path heat in the GUI and verify that the ancestors return to their direct-only Heat values.

## Cooling test

Manual mode: set a chosen half-life in days and verify that the calculation uses that value.

Automatic mode: FolderHeatMap records active days. Until at least 7 active days have been observed it uses a 30-day bootstrap half-life. After that it adapts the effective half-life to the user's observed activity frequency, clamped between 7 and 180 calendar days.

## Persistence test

1. Navigate through several directories in the FolderHeatMap custom-columns view.
2. Revisit one directory several times and note its `Visits` value.
3. Close all Total Commander windows.
4. Start Total Commander again and return to the same parent directory.
5. Verify that the previous `Visits`, `Last Visit` and heat history are still present.
6. Enter the directory once more and verify that `Visits` continues from the stored value instead of starting from zero.

## Removable-drive identity test

1. Visit a directory on a removable local volume and note its `Visits` value.
2. Disconnect and reconnect the same volume under a different drive letter if possible.
3. Open the same relative path.
4. Verify that the old history is found. Local folder identity is based on volume GUID plus relative path, not the drive letter.
