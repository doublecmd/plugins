# officeview

A Double Commander WLX lister plugin that previews Microsoft Office and
OpenDocument files (Word/Excel/PowerPoint and their legacy/macro-enabled
variants, plus ODT/ODS/ODP) directly in Double Commander's Quick View panel
and file lister, without opening a separate editor. It can also preview
native Google Docs/Sheets/Slides files under an `rclone` mount (see
[Google Drive support](#google-drive-support-via-rclone) below).

This plugin ships as **two independent native builds** — one for DC's
**GTK3** build, one for its **Qt6** build. Both share the same
`officeview_core` (`src/core/OfficeCore.*` — the x2t/LOK engine selection,
size-limit enforcement, and Google Drive/rclone handling) and have
equivalent feature sets across the board: engine auto-detection, PDF
click-and-drag text selection, LOK select-all copy, zoom, the sheet-tab
bar, and rclone/Google Drive export are all implemented identically in
both variants. Install whichever one matches your Double Commander
build — they cannot be mixed.

## What it does

- Converts Microsoft-format documents (legacy binary, OOXML, and
  macro-enabled OOXML) to PDF via EuroOffice's or OnlyOffice's headless
  `x2t` converter, then renders that PDF with MuPDF -- including real
  click-and-drag text selection with highlighting, and copy (Ctrl+C,
  right-click, or Double Commander's own Copy command).
- Renders OpenDocument files (ODT/ODS/ODP) directly via LibreOfficeKit
  (LOK), LibreOffice's own embeddable rendering component -- no PDF
  round-trip for this format family.
- Spreadsheets (XLSX/XLSM via the PDF path, ODS via LOK) get a sheet-tab
  bar for switching between sheets. A sheet that needs more than one page
  is paginated properly, not squeezed onto a single page.
- Zoom in/out (`Ctrl +`/`Ctrl -`/`Ctrl 0`, or `Ctrl+scroll wheel`) on both
  rendering paths.
- Per-extension file size limits, so a very large document is skipped
  instead of stalling the preview panel.
- Exports and previews native Google Docs/Sheets/Slides files mounted via
  `rclone` (see below) -- these don't have real content on disk until
  exported, and the plugin handles that transparently.
- Any format family (OOXML, ODF, legacy MS, or Google Drive exports) can
  be turned off entirely via config.

Supported extensions (also what the plugin reports to Double Commander via
its detect string, i.e. which files it offers to preview): `DOC`, `DOCX`,
`DOCM`, `XLS`, `XLSX`, `XLSM`, `PPT`, `PPTX`, `PPTM`, `ODT`, `ODS`, `ODP`.
Template variants (`DOT`, `DOTX`, `DOTM`, `XLT`, `XLTX`, `XLTM`, `POT`,
`POTX`, `POTM`, `OTT`, `OTS`, `OTP`) are intentionally not included.

## Building and Installation

### Prerequisites (both variants)
* CMake 3.16+
* C++17 compiler
* MuPDF, FreeType, HarfBuzz (fetched/built automatically as part of the `officeview_core` static library)

### GTK3 variant

Additional prerequisites: GTK3 (`gtk+-3.0`) development packages.

```bash
cd wlx/officeview
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc) officeview_gtk3
```

Output: `officeview_gtk3.wlx`

### Qt6 variant

Additional prerequisites: Qt6 Development Libraries (`Qt6Core`, `Qt6Gui`, `Qt6Widgets`).

```bash
cd wlx/officeview
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc) officeview_qt6
```

Output: `officeview_qt6.wlx`

### Installation

Copy the `.wlx` file matching your Double Commander build to its plugin directory and register it under **Options -> Plugins -> WLX (Lister Plugins)**.

## Finding an office suite, and what happens if more than one is installed

On first use, the plugin looks for:

- **EuroOffice or OnlyOffice** (for the Microsoft-format/PDF-rendering
  path): checks `/opt/euro-office/desktopeditors/converter/x2t` and
  `/opt/onlyoffice/desktopeditors/converter/x2t`. If **both** are present,
  **EuroOffice is preferred**.
- **LibreOffice** (for the ODF/LOK path, and as a fallback for Microsoft
  formats if neither EuroOffice nor OnlyOffice is found, or if PDF
  conversion fails for a specific file): checks the `LO_PATH` environment
  variable first, then `/usr/lib/libreoffice/program`,
  `/usr/lib64/libreoffice/program`, and `/opt/libreoffice/program`, in
  that order.

Whatever is found is written to `officeview.conf` (see below) the first
time the plugin runs, so this detection only happens once -- after that,
the plugin just reads the config. Delete the relevant config values (or
the whole file) to force it to re-detect.

**ODF (ODT/ODS/ODP) always uses LibreOffice**, regardless of what's
detected for the other formats. This is deliberate, not a fallback:
LibreOffice's ODF rendering fidelity is meaningfully better than x2t's for
this format family.

## Google Drive support (via rclone)

Google Docs, Sheets, and Slides are native Google formats -- when `rclone`
mounts a Google Drive remote (`rclone mount gdrive: ~/GoogleDrive`), it
can't materialize their content as a real file on read the way it does for
an actual `.docx` sitting in Drive. What you see on disk is a **0-byte
stub** with a `.docx`/`.xlsx`/`.pptx` (or `.odt`/`.ods`/`.odp`, depending
on your `--drive-export-formats` rclone setting) extension -- reading it
directly just gets you nothing.

`rclone` can still export the real content on demand via its `copyto`
command, which makes a Google Drive API call and writes back a real
Office/ODF file. **This plugin requires `rclone` to already be installed,
in `PATH`, and to have a working Google Drive remote configured and
mounted** -- it doesn't set any of that up itself. When it encounters a
0-byte file under a live `fuse.rclone` mount (detected via `/proc/mounts`),
it runs the equivalent of:

```bash
rclone copyto "gdrive:Documents/Resume/Peter_Lupo_Resume_Old.docx" /tmp/downloaded.docx
```

...and previews the downloaded file instead of the stub. This can take a
few seconds (it's a real network call to Google), and the resulting file
still goes through the normal size-limit check (see below) and the
same OOXML/ODF rendering path as any other file of that extension --
Google Drive exports are just an independently-configurable "format
family" for engine selection purposes (`EngineForGDrive`), not a separate
rendering path.

If a 0-byte file is **not** under an `rclone` mount, or the `rclone`
export fails (not installed, network error, remote not configured), the
plugin fails gracefully with a short message instead of trying to render
an empty file.

## Configuration

Config file: `~/.config/doublecmd/officeview.conf` (INI format, created/
migrated automatically the first time the plugin loads a file).

```ini
[Paths]
LibreOfficePath=/usr/lib/libreoffice/program
EuroOfficePath=/opt/euro-office/desktopeditors
OnlyOfficePath=

; Valid values: EuroOffice, OnlyOffice, LibreOffice, or
; Disabled (skip this format family entirely, showing a short
; message instead of attempting to render it).
[Engines]
EngineForOOXML=EuroOffice
EngineForODF=LibreOffice
EngineForLegacyMS=EuroOffice
; Native Google Docs/Sheets/Slides, exported on the fly via
; rclone (requires an rclone mount and the rclone binary --
; see README.md).
EngineForGDrive=EuroOffice

; Size limit in bytes. Files larger than this are not opened at
; all -- the plugin doesn't attempt to process them. Set a value
; to -1 to effectively disable the plugin for that extension.
; (0 is a valid, if impractical, limit -- only 0-byte files would
; pass -- so it's no longer the disable sentinel: a 0-byte file
; can legitimately be an unmaterialized rclone/Google Drive stub
; that the plugin is about to export and re-check the size of,
; not something to reject outright.)
[FileSizeLimits]
DOC=3145728
DOCX=3145728
DOCM=3145728
XLS=3145728
XLSX=3145728
XLSM=3145728
PPT=3145728
PPTX=3145728
PPTM=3145728
ODT=3145728
ODS=3145728
ODP=3145728
```

### `[Paths]`

Where each office suite's install lives. Set these manually if
auto-detection picked the wrong install (e.g. a non-standard LibreOffice
location), or to point at a suite that isn't in one of the default search
paths. `LO_PATH` (environment variable) always takes priority over
`LibreOfficePath` if both are set.

### `[Engines]`

Which engine each format family uses:

- `EngineForOOXML` -- for DOCX/XLSX/PPTX/DOCM/XLSM/PPTM. Valid values:
  `EuroOffice`, `OnlyOffice`, `LibreOffice`, or `Disabled`.
- `EngineForODF` -- for ODT/ODS/ODP. In practice this should stay
  `LibreOffice` (see above), but the setting exists if you want to
  experiment with routing ODF through x2t instead, or set it to
  `Disabled`.
- `EngineForLegacyMS` -- for DOC/XLS/PPT. Same valid values as
  `EngineForOOXML`.
- `EngineForGDrive` -- for files exported from a native Google Docs/
  Sheets/Slides stub via `rclone` (see above). Same valid values. Defaults
  to whatever `EngineForOOXML` resolved to, but is independently
  configurable -- you might reasonably want a different engine for
  Google Drive exports than for local OOXML files.

Setting any of these to **`Disabled`** turns off that entire format family:
matching files show a short "disabled" message instead of any attempt to
render them, and no conversion/download work happens at all.

Edit these directly to force a specific engine per format family instead
of relying on auto-detection.

### `[FileSizeLimits]`

One entry per extension, in **bytes**. A file larger than its extension's
limit is not opened or processed at all -- the plugin declines up front,
before any conversion work happens (for a Google Drive export, this check
runs against the downloaded file's real size, right after the download),
and shows a short message explaining why instead of attempting to render
it. Set an extension's value to **`-1`** to disable previewing for that
extension entirely, regardless of size.

## Known limitations

- **Text selection on the LibreOffice/ODF path is whole-document/
  whole-part, not click-and-drag.** The Microsoft-format/MuPDF path
  supports real click-and-drag text selection with highlighting (MuPDF's
  structured-text API exposes per-glyph positions). LibreOfficeKit's
  copy API doesn't give the plugin that same level of control, so
  ODT/ODS/ODP copy (Ctrl+C, right-click, or Double Commander's Copy
  command) always copies the entire current page/sheet's text, not just
  a selected portion.
- **Legacy `.xls` files don't get a sheet-tab bar.** Sheet-tab extraction
  works by reading the sheet list out of the file's own XML (xlsx/xlsm
  are zip archives with XML inside; ODS the same). Legacy `.xls` is the
  old BIFF binary format, not a zip/XML file, so this extraction can't
  work on it without a real binary format parser. `.xls` files still
  render correctly -- multi-page pagination works fine -- just without a
  way to jump between sheets; you only see whichever sheet the file
  itself opens to.
- **Google Drive support requires `rclone` to be installed and already
  configured/mounted.** The plugin only detects and uses an existing
  mount (via `/proc/mounts`) and calls `rclone copyto` to export a stub's
  content -- it doesn't install, configure, or mount `rclone` itself.
