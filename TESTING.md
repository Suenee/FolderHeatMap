# FolderHeatMap - WDX test

Target: Total Commander 11.58 x64 on Windows 10+.

This build stores folder activity persistently in an embedded SQLite database.

## Upgrade and build

1. Pull the latest `devel` branch.
2. Run `upgrade.cmd`.
3. The script automatically prepares the pinned SQLite amalgamation dependency and builds `dist\FolderHeatMap.wdx64`.

## Install / replace plugin

Use `dist\FolderHeatMap.wdx64` as the installed Total Commander content plugin. If Total Commander has the old DLL loaded, close Total Commander before replacing the file and start it again afterwards.

## Custom columns

The FolderHeatMap view can use these fields:

- Heat
- Visits
- Last Visit
- Heat Level

Fields are intentionally returned only for directories.

## Persistence test

1. Navigate through several directories in the FolderHeatMap custom-columns view.
2. Revisit one directory several times and note its `Visits` value.
3. Close all Total Commander windows.
4. Start Total Commander again and return to the same parent directory.
5. Verify that the previous `Visits`, `Last Visit` and heat history are still present.
6. Enter the directory once more and verify that `Visits` continues from the stored value instead of starting from zero.

## Removable-drive identity test

1. Visit a directory on a removable local volume and note its `Visits` value.
2. Close Total Commander and disconnect the drive.
3. Reconnect the same volume under a different drive letter if possible.
4. Start Total Commander and open the same relative path.
5. Verify that the old history is found. Local folder identity is based on volume GUID plus relative path, not the drive letter.

## Navigation limitation under test

The WDX callback `contst_readnewdir` is emitted when Total Commander reads a file list in Custom columns or Thumbnails view. It is not emitted merely by switching from Full view into Custom columns view. FolderHeatMap currently counts navigation observed through this callback.

Color anchors 0-7 and generated Total Commander color rules are the next visualization milestone.
