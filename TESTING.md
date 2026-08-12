# FolderHeatMap - first WDX test

Target: Total Commander 11.58 x64 on Windows 10+.

This is an early functional prototype. Activity is stored only in RAM and is reset when Total Commander unloads the plugin or exits.

## Install

1. Download the `FolderHeatMap-WDX64-test` artifact from the latest successful GitHub Actions build on the `devel` branch.
2. Extract `FolderHeatMap.wdx64` to a permanent plugin directory, for example:
   `C:\Tools\TotalCommander\Plugins\wdx\FolderHeatMap\FolderHeatMap.wdx64`
3. In Total Commander open Configuration -> Options -> Plugins -> Content plugins (.WDX) -> Configure.
4. Add `FolderHeatMap.wdx64`.

## Create a test custom columns view

Create a Custom columns view and add these FolderHeatMap fields:

- Heat
- Visits
- Last Visit
- Heat Level

The fields are intentionally returned only for directories.

## Test procedure

1. Switch a panel to the custom columns view containing FolderHeatMap fields.
2. Enter several directories and return to their parent directories.
3. Revisit one directory several times.
4. Verify that `Visits` increases for visited directories and that untouched directories remain at `0`.
5. Verify that `Heat` and `Heat Level` increase with repeated visits.
6. Click the Heat column header. The default sort direction should put hotter folders first.
7. If available, repeat the test with a removable drive, reconnecting the same volume under a different drive letter. The same relative folder should retain its identity during the same Total Commander/plugin lifetime.

## Important prototype limitation

The official WDX state callback `contst_readnewdir` is emitted when Total Commander reads a file list in Custom columns or Thumbnails view. It is not emitted merely by switching from Full view into Custom columns view. This first test therefore focuses on navigation while the FolderHeatMap custom view is active.

Persistence (SQLite), configuration UI, color anchors 0-7 and generated Total Commander color rules come after this navigation/identity test is confirmed.
