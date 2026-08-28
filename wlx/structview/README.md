# structview — Unified Structured Text Plugin

A WLX plugin for [Double Commander](https://doublecmd.github.io/) that views and edits structured text files: **JSON**, **XML**, **INI**, **CBOR**, **YAML**, and **TOML**.

This plugin ships as **two independent native builds** — one for DC's **GTK3** build (`wlxbase_gtk`), one for its **Qt6** build (`wlxbase_wlqt`). Both share the same `structview_core` format-parsing engines (JSON/XML/INI), but a couple of features differ — see [Feature Differences](#feature-differences-gtk3-vs-qt6). Install whichever one matches your Double Commander build — they cannot be mixed.

## Features (both variants, unless noted below)

- **JSON**: Flattens top-level arrays of objects into a grid. Columns = union of all keys. Nested values shown as compact JSON. Full roundtrip serialization preserving types.
- **CBOR**: Binary JSON format parsed and mapped identically to the JSON grid view.
- **XML**: Auto-detects repeating child elements as rows. Attributes shown as `@attr` columns. Non-tabular XML falls back to Name/Value layout.
- **INI**: Section navigation list on the left, 2-column Key/Value grid on the right. Sections switch without losing edits.
- **YAML**: Displays mappings and sequences, natively supporting nested tree building.
- **TOML**: Supports parsing tables and arrays of tables into structured grids.
- **Find** with scope filtering (All Cells, Current Column, Current Row) — read-only find, no replace (despite the panel being named "Find/Replace" internally, no format engine wires up a replace path on either toolkit).
- **Full undo/redo** (Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y)
- **Save** (Ctrl+S) writes back to the original file
- **Open Externally** — launches the file in the system's default external application (see shortcut difference below)
- **Word wrap** and **grid lines** toggles

![Screenshot 1](structview_json.png)

![Screenshot 2](structview_xml.png)

![Screenshot 3](structview_cbor_edit.png)

## Feature Differences (GTK3 vs Qt6)

|                          | GTK3                                                                                                                                                                   | Qt6                                                                              |
| ------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| Open Externally shortcut | **`Ctrl+E`** — DC's GTK3 build treats `Ctrl+O` as a hardcoded hotkey that a plugin's key-snooper cannot preempt, so this variant deliberately uses a different binding | **`Ctrl+O`**                                                                     |
| Encoding auto-detection  | **Not implemented** — files are read as-is, no non-UTF-8 encoding conversion                                                                                           | Detects file encoding via `EncodingUtils`/enca and auto-converts non-UTF-8 files |

Everything else — JSON/CBOR/XML/INI/YAML/TOML parsing and editing, Find, undo/redo, Save, Word wrap/grid lines — is implemented equivalently in both variants via the shared `structview_core` library.

## Architecture

```
structview_core (static library, Qt-free)
    ├── TextFormatEngine        → Abstract parser base + factory
    │   ├── JsonEngine
    │   ├── XmlEngine
    │   └── IniEngine
    │
structview_qt6.wlx (shared library)
    ├── wlx_entry.cpp            → WLX C interface (ListLoad, etc.)
    ├── StructViewWidget         → Main widget assembly (Qt6)
    └── wlxbase_wlqt (static library)
        ├── FocusManager
        ├── EditableGridWidget   (GridMode::MemoryDocument)
        ├── PluginToolBar
        ├── ScopedFindReplacePanel
        └── EncodingUtils
    │
structview_gtk3.wlx (shared library)
    ├── src/gtk3/plugin_gtk3.cpp → WLX C interface + GTK3 widget assembly
    └── wlxbase_gtk (static library)
        ├── GtkFocusManager
        ├── GtkEditableGridWidget
        ├── GtkPluginToolBar
        └── GtkFindReplacePanel
```

### Adding a New Format

1. Create `src/NewFormatEngine.cpp` with a class inheriting `TextFormatEngine` (in `structview_core`, shared by both toolkits)
2. Implement `loadInto()`, `serialize()`, `formatName()`
3. Add a factory function `createNewFormatEngine()` and wire it in the factory switch
4. Add the source file to `CMakeLists.txt`

## Building

### Prerequisites (both variants)

- CMake ≥ 3.20
- C++17 compiler

### GTK3 variant

Additional prerequisites: GTK3 (`gtk+-3.0`) development packages.

```bash
cd wlx/structview
mkdir build && cd build
cmake ..
make -j$(nproc) structview_gtk3
```

Output: `structview_gtk3.wlx`

### Qt6 variant

Additional prerequisites: Qt6 (Core, Gui, Widgets, Xml).

```bash
cd wlx/structview
mkdir build && cd build
cmake ..
make -j$(nproc) structview_qt6
```

Output: `structview_qt6.wlx`

## Installation

Copy the `.wlx` file matching your Double Commander build (`structview_gtk3.wlx` or `structview_qt6.wlx`) to your Double Commander plugins directory and configure the detect string:

```
EXT="JSON" | EXT="XML" | EXT="INI" | EXT="CBOR" | EXT="YAML" | EXT="TOML"
```

## Keyboard Shortcuts

| Shortcut              | Action            | GTK3 | Qt6 |
| --------------------- | ----------------- |:----:|:---:|
| Ctrl+S                | Save file         | ✅    | ✅   |
| Ctrl+Z                | Undo              | ✅    | ✅   |
| Ctrl+Shift+Z / Ctrl+Y | Redo              | ✅    | ✅   |
| Ctrl+F                | Toggle Find panel | ✅    | ✅   |
| Ctrl+C                | Copy selection    | ✅    | ✅   |
| Ctrl+V                | Paste             | ✅    | ✅   |
| Ctrl+E                | Open Externally   | ✅    | —   |
| Ctrl+O                | Open Externally   | —    | ✅   |

## Future

- Additional structured text formats
