# sourceview — GtkSourceView-based Text Editor Plugin

A WLX (Lister) plugin for [Double Commander](https://doublecmd.github.io/) that opens files as a full, editable text editor in the Quick View panel, powered by GTK's own `GtkSourceView 4`. It's not a read-only viewer — it edits any text file, and syntax highlighting is one feature of that editor, applied automatically for languages GtkSourceView recognizes.

This is the GTK3 counterpart to [kate](../kate/README.md) (which does the same job on the Qt6/KDE build of Double Commander via `KTextEditor`) — same overall design, but the two are independent implementations on different toolkits, so their exact feature sets differ; see below for what this plugin specifically supports.

## Features
- Full read/write editing of any text file, not just recognized source code — syntax highlighting is layered on top when GtkSourceView recognizes the file's language, but plain text files open and edit the same way.
- Syntax highlighting for every language GtkSourceView 4 ships a definition for; file-type detection is driven directly off GtkSourceView's own registered languages, so the plugin picks up whatever language set is vendored in without a hardcoded extension list.
- Save, Save As, Save Copy As (write a copy in a different encoding without changing the open file's tracked encoding), Save With Encoding, and Reload.
- Automatic encoding detection on load, with manual re-encoding available via Save With Encoding.
- Undo/Redo, Select All, Find/Replace, and Go to Line.
- Word-completion suggestions drawn from words already present in the buffer.
- Line numbers, current-line highlighting, right margin, line marks, and whitespace display, each independently toggleable.
- Toggleable style scheme picker (GtkSourceView's syntax color themes).
- Auto-indent, indent-on-tab, insert-spaces-instead-of-tabs, and smart Home/End.
- Toolbar with Save, Undo/Redo, Find/Replace, Word Wrap, and Read-Only toggle.
- Status bar showing cursor position, detected language, edit mode, and encoding.
- Printing support.
- Disk-change detection: warns when the open file changes on disk outside the editor.
- Case transforms (upper/lower/title/sentence/camel case) on selected text.

## Requirements
- `GTK3`
- `GtkSourceView 4` and `libxml2` — both built from source by this plugin's own CMake build (via `ExternalProject_Add`), so no separate system package is required for them.

## Building
```bash
mkdir build && cd build
cmake ..
make
```

## Installation
Copy `sourceview_gtk3.wlx` to `~/.config/doublecmd/plugins/wlx/` and add it via Double Commander's Options -> Plugins -> WLX.
