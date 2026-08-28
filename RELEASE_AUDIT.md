# Release v0.01 zip audit (2026-08-26)

Every zip attached to [v0.01](https://github.com/pplupo/doublecmd_plugins/releases/tag/v0.01)
was downloaded and listed with `unzip -l` (no extraction) to check whether it actually
contains its plugin binary. Root causes below were found in the build run's log
(`gh run view 32974297597 --repo pplupo/doublecmd_plugins --log`).

**All five gaps below have since been fixed** (in the commit that added this note) and
verified with a real local build of each plugin. `v0.01`'s zips still reflect the broken
state described here until the workflow is re-run against that tag.

## Zips that were missing their plugin binary (now fixed)

| Zip | Contained | Root cause | Fix |
|---|---|---|---|
| `exif` | only `README.txt`, `license.txt`, `exif.lng` — no `exif.wdx` | Compile error: `exif.c:292: error: conflicting types for 'strlcpy'`. Ubuntu 24.04's glibc now declares `strlcpy` itself, which conflicted with the plugin's own private helper of the same name. | Renamed the plugin's helper to `exif_strlcpy` throughout `exif.c`. Verified with a direct `gcc -c` compile. |
| `rclone` | only `rclone.ico`, `COPYING.txt` — no `rclone.wfx` | Two separate bugs: (1) `rclone.lpi`'s `IncludeFiles`/`OtherUnitFiles` pointed `..\..\..\..\sdk` — one `..` too many, resolving outside the repo entirely instead of to `sdk/`. (2) Once that was fixed, a second real error surfaced: `FsExtractCustomIconW`'s `TheIcon: PWfxIcon` parameter type doesn't exist in this repo's `sdk/wfxplugin.pas`, which only has the older `var TheIcon: hicon` icon-extraction signature — the code was written against a newer WFX SDK than what's vendored here. | Fixed the include path to `..\..\..\sdk` (3 levels). Changed the signature to `var TheIcon: hicon` and simplified the body to always return `FS_ICON_USEDEFAULT` (the plugin never had working icon-loading code to produce a real `hicon` handle, so it now explicitly defers to DC's default icon instead of a struct-typed dead end). Verified with a full `lazbuild --build-mode=Release` producing `rclone.wfx`. |
| `officeview` | only `README.md` — no `officeview_qt6.wlx`/`officeview_gtk3.wlx` | `CMake Error: The current CMakeCache.txt directory ... is different than the directory ... where CMakeCache.txt was created.` **Cause: `wlx/officeview/build/CMakeCache.txt` was committed to git**, baked with an absolute path from a local dev machine (`/home/pplupo/repos/plugins/...`). When CI checked out the repo, that stale cache collided with the runner's own checkout path and CMake refused to proceed. Confirmed via `git ls-files` that `wlx/officeview/build/` was the only plugin's build dir with `CMakeCache.txt` actually tracked (other plugins had stray tracked files under `build/` too, but not their `CMakeCache.txt`, so they didn't hit this exact failure). | `git rm -r --cached` on every tracked `build/` directory repo-wide (`wlx/csvview`, `wlx/kate`, `wlx/kpartview`, `wlx/officeview`, `wlx/structview`, `wlx/wayland_qt_base`, `wfx/rclone` — 763 tracked files total) and added/extended a `build/` `.gitignore` entry in each of those plugin dirs. |
| `logview` | only `README.md`, `logviewer.png`, `color_editor.png`, a stray `double commander wlx log viewer.md` — no `logviewer.wlx` | Compile error: `LogViewerWidget.cpp:398: error: 'beginFilterChange' was not declared in this scope` (and 3 more of the same). `QSortFilterProxyModel::beginFilterChange()`/`endFilterChange()` are Qt 6.7+ API; `ubuntu-24.04`'s `qt6-base-dev` ships Qt 6.4.2. | Replaced all four `beginFilterChange()`/`endFilterChange()` pairs with `invalidateFilter()`, the pre-6.7 equivalent. Verified with a full CMake build in the same GTK3/Qt6 dev container used throughout this project (Qt 6.4.2), producing `logviewer.wlx`. |
| `mdk` | only `proposal.md`, `README.md` — no `mdk_qt6.wlx` | `fatal error: mdk/c/Player.h: No such file or directory`. The MDK SDK (`https://github.com/wang-bin/mdk-sdk`) is a third-party SDK `mdk_core` needs only the **headers** from at compile time (`libmdk.so` itself is `dlopen()`'d at runtime, see `MdkEngine.cpp`); it isn't an apt package and CI never fetched it. | `wlx/mdk/CMakeLists.txt` now uses `FetchContent` to download the pinned `mdk-sdk-linux-x64.tar.xz` v0.38.0 release tarball (SHA256-verified) and points `MDK_SDK_INCLUDE` at its `include/` dir, but only when a local `~/mdk-sdk/include` isn't already present (a dev's own manual SDK install still takes priority). Verified with a full CMake build (forcing the fetch path via `-DMDK_SDK_INCLUDE=/nonexistent`), producing `mdk_qt6.wlx`. |

## Confirmed correct (zip contains its plugin binary)

`crx_wdx`, `csvview` (both `_gtk3`/`_qt6`), `dbview` (both), `diagramview` (both), `diskdir`,
`fileinfo`, `gstplayer`, `gvfs`, `kate`, `kpartview`, `markdownview` (both),
`mpv_wayland` (both), `ooinfo`, `ooxml`, `similarity`, `sourceview`, `structview` (both),
`xpi_wdx`.

## Correctly binary-less (not a bug)

`mediainfo` and `translitwdx` are Lua scripts run by a Lua-hosting plugin, not
compiled `.wdx` binaries — their zips containing only `.lua`/text files is expected.
