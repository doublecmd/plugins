# CSV/TSV Table Grid Lister Plugin for Double Commander (Linux/Wayland)

A WLX (Lister) plugin for Double Commander to visualize, navigate, edit, and export **CSV** and **TSV** files in a clean, interactive spreadsheet-like grid.

This plugin is a port of the original work by **j2969719** ([https://github.com/j2969719/doublecmd-plugins](https://github.com/j2969719/doublecmd-plugins)).

This plugin ships as **two independent native builds** — one for DC's **GTK3** build, one for its **Qt6** build. They share the same CSV/TSV parsing core and most editing/undo/find-replace features, but the GTK3 variant has a smaller feature set than the Qt6 variant (see [Feature Differences](#feature-differences-gtk3-vs-qt6)). Install whichever one matches your Double Commander build — they cannot be mixed.

---

## Screenshots (Qt6 variant)

### Toolbar and Header Row Toggle
![Toolbar and Header Row Toggle](csvview1.png)

### Custom Right-Click Context Menu
![Custom Right-Click Context Menu](csvview2.png)

---

## Features (both variants, unless noted in Feature Differences)

- **Spreadsheet Grid View**: Displays CSV/TSV data in an organized grid table with adjustable row and column headers.
- **Double-Quote Parsing**: Correctly handles double-quoted fields containing commas, tabs, or newlines, conforming to standard CSV RFC behaviors.
- **Inline Editing**: Modify cell contents directly inside Lister by double-clicking any cell.
- **Undo / Redo Support**: Full edit history (`Ctrl+Z` / `Ctrl+Y`) for cell editing, row/column operations, and sorting.
- **Column Context Menu**: Right-click headers to copy, paste, insert empty columns, or delete column selections.
- **Row Insertion**: Insert empty rows above or below the current selection.
- **Text / Source View Mode**: Switch between the spreadsheet grid and a raw text preview with word wrap.
- **Open Externally**: Launch the file in the system's default external application directly from the toolbar.
- **Smart Focus Management**: Seamlessly yields keyboard and mouse focus to Double Commander when clicking outside the plugin, ensuring file selection changes and arrow-key pane navigation work flawlessly.

---

### Header Row Toggle

A checkable **Header Row** button is shown in the toolbar (enabled by default).

- **On (default)**: The first line of the file is treated as column headers. It is displayed in the table header row (not as a data row). Sort arrows appear on the header. Copy operations include the header line.
- **Off**: The first line is treated as a regular data row and appears at index 0. Columns display default numeric labels. Copy operations do not include a header line.

Toggling this button automatically reloads and re-parses the file.

---

### Copying

- Press **`Ctrl+C`** to copy the currently selected cells as **TSV** (Tab Separated Values) to the clipboard.
- Right-click to open the context menu and choose **Copy Selection as TSV** or **Copy Selection as CSV**.

**Header inclusion rules:**
- If **Header Row** is **on**: the column headers of the selected columns are prepended as the first line of the copied text.
- If **Header Row** is **off**: only the selected cell values are copied, with no header line.

---

### Pasting & Row Insertion

- **Insert Empty Row**: Right-click → **Insert Empty Row Above** or **Insert Empty Row Below** to add a blank row.
- **Qt6 only**: **`Ctrl+V`** (or right-click → **Insert Row from Clipboard Above/Below**) inserts rows straight from clipboard content — see [Feature Differences](#feature-differences-gtk3-vs-qt6).

---

### Deleting Rows

- Press **`Delete`** (or right-click → **Delete Selected Rows**) to remove all selected rows from the grid.
- Multiple non-contiguous rows can be selected and deleted in one operation.

---

### Undo & Redo

- Press **`Ctrl+Z`** to undo the last edit, insertion, deletion, sorting, or column move.
- Press **`Ctrl+Y`** (or **`Ctrl+Shift+Z`**) to redo an undone action.
- A **dirty indicator** (`✓` / `●`) on the toolbar shows whether there are unsaved edits in the undo history.

---

### Column Manipulation & Sorting

- **Sorting**: Click any column header to sort the table data by that column. Click again to toggle between ascending and descending order.
- **Column Context Menu**: Right-click a column header to access column-specific options: copy column selection, paste column selection, insert empty columns, delete selected columns.
- **Qt6 only**: **Drag-and-Drop Reordering** — drag any column header horizontally to reorder columns, with full undo/redo support.

---

### Source Text Mode & External Apps

- **Toggle Text Mode**: Click the **Text Mode** button to view the raw, unparsed text content of the file. Toggle **Word Wrap** to wrap lines.
- **Open Externally**: Click the **Open Externally** button to open the file in the default system editor/application.

---

### Save & Reload

- **`Ctrl+S`** or click **Save** to save all changes back to the original file. Works correctly whether or not a cell is being edited — if a cell editor is active it is committed first; otherwise the file is saved directly without disturbing Double Commander's focus.
- **Save As...** to export to a different file path or format (Qt6 only, see Feature Differences).
- **Reload** to discard unsaved changes and re-read the file from disk.

---

### Find & Replace

Press **`Ctrl+F`** (to find), **`Ctrl+R`** (to replace), or click the **Find/Replace** toolbar button to open the inline Find/Replace panel. Hitting **`Escape`** closes it.

* **Search Options**: Match Case, Match Entire Cell, Regular Expression.
* **Scope Options**: All Cells, Selected Cells, Current Column, Current Row.
* **Action Buttons**: Find Next / Find Prev, Replace, Replace All (single undo macro).
* **Automatic Quoting Safeguard**: if a replacement introduces the separator character (e.g. inserting `,` in a CSV or `\t` in a TSV), the plugin automatically wraps the cell value in double quotes and escapes existing quotes correctly, fully integrated into the Undo/Redo stack.

---

### Classic Lister Search

Press **`F7`** (or use Double Commander's built-in search) to search for substrings across all cells using the classic dialog.

---

### TSV Support

Works with both `.csv` and `.tsv` files. The separator is auto-detected from content (trying `,`, `;`, `\t` in order). If auto-detection is ambiguous, the file extension is used as a fallback: `.tsv` → tab, `.csv` → comma.

---

## Feature Differences (GTK3 vs Qt6)

| | GTK3 | Qt6 |
|---|---|---|
| Persisted settings (`ListSetDefaultParams`) | **Not implemented** — the GTK3 target's `ListSetDefaultParams` is a no-op stub; no settings are read from or written to an ini file, so all options below are Qt6-only | Reads/writes `csvview.ini` (see [Configuration](#configuration-qt6-only)) |
| Encoding auto-detection (Enca) | **Not implemented** — no character-set detection at all | Detects file character-set encodings (Cyrillic, UTF-8, Latin, etc.) via an embedded Enca engine |
| Column drag-and-drop reordering | **Not implemented** | Drag any column header to reorder, with undo/redo support |
| Insert Row from Clipboard | **Not implemented** — only empty-row insertion (Insert Row Above/Below) is available | `Ctrl+V` / context menu inserts clipboard rows directly, with header-line deduplication and column-count validation |
| Save As... | Not present in the toolbar | Present, exports to a different file path/format |
| Auto-resize columns to content | Not configurable (no ini) | `resize_columns` setting |
| Grid line drawing toggle | Not configurable (no ini) | `draw_grid` setting |

Everything else — grid editing, undo/redo, header toggle, sorting, column context menu (copy/paste/insert/delete), Find & Replace, classic Lister search, TSV/CSV auto-detection, Open Externally, focus handling — is implemented equivalently in both variants.

---

## Building and Installation

### Prerequisites (both variants)
* CMake 3.16+
* C++17 compiler

### GTK3 variant

Additional prerequisites: GTK3 (`gtk+-3.0`) development packages.

```bash
cd wlx/csvview
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc) csvview_gtk3
```

Output: `csvview_gtk3.wlx`

### Qt6 variant

Additional prerequisites: Qt6 Development Libraries (`Qt6Core`, `Qt6Gui`, `Qt6Widgets`, `Qt6PrintSupport`).

```bash
cd wlx/csvview
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_QT6=ON ..
make -j$(nproc) csvview_qt6
```

Output: `csvview_qt6.wlx`

### Installation

1. In Double Commander, open **Options** → **Plugins** → **WLX**.
2. Click **Add** and select the `.wlx` file matching your DC build (`csvview_gtk3.wlx` or `csvview_qt6.wlx`).
3. Ensure the detect string is configured as:
   ```
   (EXT="CSV" | EXT="TSV") & SIZE<30000000
   ```

---

## Configuration (Qt6 only)

The Qt6 variant's configuration is stored in `csvview.ini`, in the directory Double Commander hands the plugin at load time (its `DefaultIniName`), under the `[csvview]` section:

| Key | Type | Description |
|---|---|---|
| `enca` | bool | Enable Enca character encoding auto-detection |
| `resize_columns` | bool | Auto-resize column widths to fit contents |
| `enca_readall` | bool | Read the entire file for encoding detection (slower but more accurate) |
| `doublequoted` | bool | Handle RFC-compliant double-quoted CSV fields |
| `draw_grid` | bool | Draw grid lines between cells |
| `enca_lang` | string | Locale hint for Enca (e.g. `ru`, `cs`) |

The GTK3 variant has no ini file — none of these are configurable there.
