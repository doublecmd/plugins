# wayland_qt_base

A reusable component library for building Qt6 Wayland WLX plugins for [Double Commander](https://doublecmd.sourceforge.io/).

The library codifies battle-tested patterns that evolved across `csvview`, `logview`, and `kate` — focus management, keyboard shortcut handling, toolbar integration, grid editing, find/replace, and encoding detection — into a clean, decoupled foundation for new plugins.

> **Existing plugins are not modified by this library.** This is a new foundation for future plugin development.

---

## Table of Contents

- [Architecture](#architecture)
- [Requirements](#requirements)
- [Building](#building)
- [Integration](#integration)
- [Components](#components)
  - [FocusManager](#focusmanager)
  - [PluginToolBar](#plugintoolbar)
  - [EditableGridWidget](#editablegridwidget)
  - [FindReplacePanel](#findreplacepanel)
  - [ScopedFindReplacePanel](#scopedfindreplacepanel)
- [Utilities](#utilities)
  - [EncodingUtils](#encodingutils)
- [Examples](#examples)
- [Design Decisions](#design-decisions)
- [Directory Layout](#directory-layout)

---

## Architecture

Each component depends only on `FocusManager`. A plugin that needs just a toolbar doesn't pull in grid or find/replace. `EncodingUtils` is a standalone utility with no component dependencies.

```
                   ┌──────────────────────┐
                   │    FocusManager      │
                   │  (core framework)    │
                   │  shortcut registry   │
                   │  optional undo/redo  │
                   └──────┬───┬───┬───────┘
                          │   │   │
              ┌───────────┘   │   └───────────┐
              │               │               │
    ┌─────────▼──┐  ┌────────▼────────┐  ┌───▼──────────────┐
    │PluginToolBar│  │EditableGridWidget│  │ FindReplacePanel │
    │(focus-safe) │  │ (grid + undo)   │  │  (base, no scope)│
    └────────────┘  └─────────────────┘  └───────┬──────────┘
                                                  │ extends
                                         ┌───────▼───────────────┐
                                         │ScopedFindReplacePanel  │
                                         │(adds scope combo box) │
                                         └───────────────────────┘

    ┌─────────────┐
    │EncodingUtils │  (standalone utility — no component deps)
    └─────────────┘
```

**Namespace:** `QtWlPlugin`

---

## Requirements

- **CMake** ≥ 3.16
- **Qt 6** (Core, Gui, Widgets)
- **GLib 2.0** (for encoding conversion via `g_convert_with_fallback`)
- **C++20** compiler
- **Internet connection** on first build (to download enca 1.19)

The [enca](https://cihar.com/software/enca/) library is downloaded and statically built automatically during the CMake configure step.

---

## Building

### Standalone

```bash
cd wlx/wayland_qt_base
mkdir build && cd build
cmake ..
make -j$(nproc)
```

This produces `libwayland_qt_base.a` — a static library. Each consumer plugin links it in and produces a self-contained `.wlx`.

### As part of a plugin build

In your plugin's `CMakeLists.txt`:

```cmake
add_subdirectory(../../wayland_qt_base wayland_qt_base)
target_link_libraries(my_plugin PRIVATE wayland_qt_base)
```

The public include directory (`include/`) is automatically added, so you can include headers as:

```cpp
#include <wlxbase_wlqt/FocusManager.h>
#include <wlxbase_wlqt/PluginToolBar.h>
// etc.
```

---

## Integration

A minimal plugin using this library:

```cpp
#include <wlxbase_wlqt/FocusManager.h>
#include <wlxbase_wlqt/PluginToolBar.h>

class MyPluginWidget : public QWidget {
    Q_OBJECT
public:
    MyPluginWidget(QWidget *parent = nullptr) : QWidget(parent) {
        auto *view = new QTextEdit(this);
        auto *layout = new QVBoxLayout(this);

        // 1. Create FocusManager (required for all components)
        m_fm = new QtWlPlugin::FocusManager(this, view, this);

        // 2. Create a focus-safe toolbar
        m_toolbar = new QtWlPlugin::PluginToolBar(m_fm, this);
        m_toolbar->addToolAction("Save", QKeySequence(Qt::CTRL | Qt::Key_S));

        // 3. Register plugin-specific shortcuts
        m_fm->registerShortcut(
            QKeySequence(Qt::CTRL | Qt::Key_W),
            QtWlPlugin::FocusManager::Always,
            [this]() { close(); return true; }
        );

        layout->addWidget(m_toolbar);
        layout->addWidget(view);
    }

private:
    QtWlPlugin::FocusManager *m_fm;
    QtWlPlugin::PluginToolBar *m_toolbar;
};
```

---

## Components

### FocusManager

**Header:** `<wlxbase_wlqt/FocusManager.h>`

The core framework that every plugin using this library needs. It solves the fundamental problem of a Qt widget embedded in a non-Qt host application: focus activation, deactivation, bounce prevention, and keyboard shortcut dispatch.

#### The Problem It Solves

When a Qt plugin widget is embedded inside Double Commander (a non-Qt application), focus management becomes complex:

- Clicking inside the plugin must activate it; clicking outside must deactivate it.
- Programmatic focus changes (e.g. tab switching in DC) must not accidentally activate the plugin.
- Newly added child widgets must not steal focus from the host.
- Keyboard shortcuts must only fire when the plugin is active, and some should be suppressed when the user is editing text in an input field.

FocusManager handles all of this through a single `qApp` event filter.

#### Constructor

```cpp
QtWlPlugin::FocusManager(QWidget *pluginRoot, QWidget *primaryView, QObject *parent = nullptr);
```

- `pluginRoot` — the top-level widget of the plugin (typically `this` in the plugin's main widget).
- `primaryView` — the main content widget that should receive focus by default (e.g. a `QTableView`, `QTextEdit`, `QTreeView`).

The constructor installs the event filter on `qApp` and sets `pluginRoot`'s focus proxy to `primaryView`.

#### Activation

| Method | Description |
|--------|-------------|
| `bool isActive() const` | Returns whether the plugin is currently active. |
| `void setActive(bool)` | Manually activate/deactivate. Emits `activated()` or `deactivated()`. When deactivating, clears focus and returns it to the host. |

**Signals:** `activated()`, `deactivated()`

Activation is normally handled automatically by the event filter (click inside → activate, click outside → deactivate), but `setActive()` is available for edge cases.

#### Input Widget Tracking

The FocusManager distinguishes between "structural" widgets (buttons, headers, etc.) and "input" widgets (text fields, cell editors) to determine when shortcuts should be suppressed.

| Method | Description |
|--------|-------------|
| `void addInputWidget(QWidget *w)` | Register a widget as an input widget (e.g. a QLineEdit in a find panel). |
| `void removeInputWidget(QWidget *w)` | Unregister an input widget. |
| `bool isInputWidget(QWidget *w) const` | Returns `true` if `w` is registered or is a descendant of `primaryView` (but not `primaryView` itself — this catches dynamically created cell editors). |
| `QWidget *activeInput() const` | Returns the currently focused input widget, or `nullptr`. |

**Signals:** `inputWidgetEntered(QWidget *w)`, `inputWidgetExited()`

#### Focus Proxy

When showing a secondary panel (e.g. find/replace), the plugin's focus proxy should be redirected so keyboard events reach the panel's input fields.

| Method | Description |
|--------|-------------|
| `void setFocusProxy(QWidget *proxy)` | Redirect `pluginRoot`'s focus proxy to `proxy`. |
| `void resetFocusProxy()` | Reset focus proxy back to `primaryView`. |
| `void restoreViewFocus()` | Explicitly set focus to `primaryView`. |

#### Shortcut Registration

Replaces the hardcoded if-chains in `eventFilter` with a declarative registry.

```cpp
enum ShortcutContext { WhenNoInput, Always };
using ShortcutId = int;

ShortcutId registerShortcut(const QKeySequence &keys, ShortcutContext ctx,
                            std::function<bool()> handler);
void unregisterShortcut(ShortcutId id);
```

- **`WhenNoInput`** — the shortcut only fires when no input widget has focus. Use for navigation keys, copy/paste, delete, etc.
- **`Always`** — the shortcut fires even when the user is editing text. Use for Ctrl+S (save), Ctrl+F (find), Ctrl+W (close), etc.

The handler returns `true` to consume the event, `false` to let it propagate.

#### Optional Undo/Redo

```cpp
void setUndoStack(QUndoStack *stack);
QUndoStack *undoStack() const;
```

When a non-null `QUndoStack` is set, FocusManager automatically registers three shortcuts (all with `WhenNoInput` context):

| Shortcut | Action |
|----------|--------|
| `Ctrl+Z` | `stack->undo()` |
| `Ctrl+Shift+Z` | `stack->redo()` |
| `Ctrl+Y` | `stack->redo()` |

Calling `setUndoStack(nullptr)` unregisters them. The consumer retains full access to the stack for pushing custom undo commands.

#### Saved Focus Widget

```cpp
void saveFocusWidget(QWidget *w);
```

Store a reference to the host application's focus widget so the plugin can restore focus to it when deactivating. This is useful when the host tells the plugin which widget had focus before the plugin was activated.

#### Event Filter Internals

The event filter handles five event types:

1. **`MouseButtonPress`** — geometry-based click detection. Click inside plugin → activate. Click outside → deactivate.
2. **`FocusIn`** — tracks which input widget has focus. Ignores programmatic focus changes (`OtherFocusReason`) when inactive.
3. **`KeyPress`** — iterates the shortcut registry, matches key sequences, checks context, dispatches handlers.
4. **`ChildAdded`** — sets `Qt::NoFocus` on newly created child widgets to prevent focus theft.
5. **`QApplication::focusChanged`** — connected in the constructor. Detects focus leaving the plugin hierarchy. When focus enters the plugin while inactive, bounces it back to the host via `QTimer::singleShot(0, ...)`.

---

### PluginToolBar

**Header:** `<wlxbase_wlqt/PluginToolBar.h>`

A `QToolBar` subclass that integrates with `FocusManager` to prevent focus issues caused by toolbar interaction.

#### The Problem It Solves

In a Qt plugin embedded in a non-Qt host, clicking a toolbar button transfers focus to the button widget. Without intervention, this causes the host to think the plugin lost focus and deactivate it. PluginToolBar prevents this by:

1. Setting `Qt::NoFocus` on all action widgets (buttons, combo boxes, etc.).
2. Restoring focus to `primaryView` after every action trigger via `QTimer::singleShot`.
3. Applying compact default styling.

#### API

```cpp
explicit PluginToolBar(FocusManager *fm, QWidget *parent = nullptr);

QAction *addToolAction(const QString &text,
                       const QKeySequence &shortcut = {},
                       int ctx = 0 /* FocusManager::WhenNoInput */);
```

- `addToolAction` creates a `QAction`, adds it to the toolbar, and optionally registers a keyboard shortcut through `FocusManager`. The shortcut text is appended to the tooltip automatically.
- Any dynamically added widgets are also enforced to `NoFocus` via an `actionEvent` override.

---

### EditableGridWidget

**Header:** `<wlxbase_wlqt/EditableGridWidget.h>`

A `QWidget` wrapping a caller-injected `QTableView` with full editing capabilities, undo/redo support, drag-to-move, and context menus. **Format-agnostic** — it provides no file I/O, no parsing, no encoding detection.

Because `QTableWidget` is a subclass of `QTableView`, you can inject either:
- A `QTableWidget` for simple item-based workflows
- A plain `QTableView` with any `QAbstractItemModel` (e.g. `QStandardItemModel`, `QSqlTableModel`) for model-based workflows

All internal data access goes through `QAbstractItemModel` / `QModelIndex` — zero `QTableWidgetItem` dependencies inside the grid.

#### GridMode

The constructor requires a `GridMode` enum that determines the undo/memory strategy:

| Mode | Undo Strategy | Sorting | Best For |
|------|--------------|---------|----------|
| `GridMode::MemoryDocument` | Full `QUndoStack` tracking. Sort stores before/after snapshots in RAM. | Snapshot-based undo | `QTableWidget`, `QStandardItemModel`, in-memory data |
| `GridMode::LiveDatabase` | Bypasses `QUndoStack` for data mutations. Sort issues `model->sort()` (SQL `ORDER BY`) with no RAM snapshot. | Direct `model->sort()` | `QSqlTableModel`, transactional models |

Both modes share context menus, drag-to-reorder, focus management, keyboard shortcuts, and the `WrapAnywhereDelegate`.

#### Constructor (Dependency Injection)

```cpp
// The caller creates and configures the view, then hands it to the grid.
explicit EditableGridWidget(QTableView *view, GridMode mode, FocusManager *fm, QWidget *parent = nullptr);
```

**In-memory document (full undo):**
```cpp
auto *tw = new QTableWidget();
auto *grid = new QtWlPlugin::EditableGridWidget(tw, QtWlPlugin::GridMode::MemoryDocument, fm, this);
```

**Live database (transactional):**
```cpp
auto *tv = new QTableView();
tv->setModel(sqlModel);
auto *grid = new QtWlPlugin::EditableGridWidget(tv, QtWlPlugin::GridMode::LiveDatabase, fm, this);
```

#### Features

| Feature | Details |
|---------|---------|
| **Undo/Redo** | `QUndoStack` registered with FocusManager. In `MemoryDocument` mode: four undo command types (`EditCellCommand`, `RowColCommand`, `DataSnapshotCommand`, `SectionMoveCommand`). In `LiveDatabase` mode: data mutations bypass the undo stack — the transactional model handles reverts. |
| **Cell editing** | Enter to open editor, Escape to cancel (reverts to pre-edit value), Up/Down to navigate between cells while editing, Enter to commit and advance right (wraps to next row). |
| **Arrow navigation** | Left at column 0 wraps to the last column of the previous row. Right at last column wraps to column 0 of the next row. |
| **Copy/Paste** | `copySelection(separator)` copies selected cells via `model()->data()`. `pasteSelection()` inserts clipboard rows — wrapped in undo commands in `MemoryDocument`, direct model calls in `LiveDatabase`. |
| **Insert/Delete** | `insertRows()`, `deleteSelectedRows()`, `insertColumns()`, `deleteSelectedColumns()` — undo-wrapped in `MemoryDocument`, direct `model->insertRows()`/`removeRows()` in `LiveDatabase`. |
| **Drag-to-move** | Multi-select row or column drag via headers. Uses a debounce timer to coalesce Qt's per-section `sectionMoved` signals into a single undo command. |
| **Column sorting** | Click header once to arm, click again to sort. `MemoryDocument`: stores full data snapshot for undo. `LiveDatabase`: issues `model->sort()` (SQL `ORDER BY`) with zero RAM overhead. |
| **Word wrap** | `WrapAnywhereDelegate` enables character-level text wrapping (not just word boundaries). Toggle with `setWordWrap(bool)`. |
| **Context menus** | Right-click on the table or vertical header → row operations. Right-click on horizontal header → column operations. Includes insert from clipboard. |
| **Dirty tracking** | `isDirty()` / `dirtyChanged(bool)` signal based on `QUndoStack::isClean()`. |

#### API

```cpp
explicit EditableGridWidget(QTableView *view, GridMode mode, FocusManager *fm, QWidget *parent = nullptr);

// Access
QTableView *view() const;     // Returns the injected view (may be QTableView or QTableWidget)
GridMode mode() const;        // Returns the active mode
QUndoStack *undoStack() const;

// Row operations
void copySelection(char separator = '\t');
QString getSelectionAsText(char separator = '\t');
void pasteSelection();
void pasteSelectionAt(int atRow);
void insertRows(int count, int atRow);
void deleteSelectedRows();

// Column operations
void copyColumnSelection(char separator = '\t');
void pasteColumnSelectionAt(int atCol);
void insertColumns(int count, int atCol);
void deleteSelectedColumns();

// Appearance
void setWordWrap(bool wrap);
bool wordWrap() const;
void setShowGrid(bool show);

// State
bool isDirty() const;

// Signals
void dirtyChanged(bool dirty);
```

The consumer provides a `QTableView` (or `QTableWidget`) with a model already set. All grid operations go through `view()->model()` using `QModelIndex`.

#### Registered Shortcuts

These are automatically registered with `FocusManager` (all `WhenNoInput`):

| Shortcut | Action |
|----------|--------|
| `Ctrl+C` | Copy selection as TSV |
| `Ctrl+V` | Paste from clipboard |
| `Delete` | Delete selected rows |
| `Enter` / `Return` | Edit current cell |
| `↑` `↓` `←` `→` | Navigate with right-wrap |
| `Ctrl+Z` | Undo (via FocusManager) |
| `Ctrl+Shift+Z` / `Ctrl+Y` | Redo (via FocusManager) |

---

### FindReplacePanel

**Header:** `<wlxbase_wlqt/FindReplacePanel.h>`

The **base class** for find & replace UI. Provides inputs, match options, and action buttons but has **no scope concept** — scope is entirely the consumer's responsibility.

#### UI Layout

```
┌──────────────────────────────────────────────────────────────┐
│ Find: [_______________]  Replace: [_______________]          │
│ ☐ Match Case  ☐ Match Entire Cell  ☐ Regex                  │
│ [Find Prev] [Find Next] [Replace] [Replace All]  status  ✕  │
└──────────────────────────────────────────────────────────────┘
```

#### API

```cpp
explicit FindReplacePanel(FocusManager *fm, QWidget *parent = nullptr);

// Configuration
void setReplaceEnabled(bool enabled);  // hide replace row for read-only views

// State
QString findText() const;
QString replaceText() const;
bool matchCase() const;
bool matchEntireCell() const;
bool useRegex() const;

// Status feedback
void setStatusText(const QString &text);

// Visibility
void showPanel(bool show);
bool isPanelVisible() const;

// Signals
void findRequested(bool forward);  // true = next, false = previous
void replaceRequested();
void replaceAllRequested();
void panelClosed();
```

#### Focus Integration

- When shown, `setFocusProxy` redirects to the find input and selects all text.
- When hidden, `resetFocusProxy` and `restoreViewFocus` return focus to the primary view.
- Find and replace inputs are registered as input widgets with FocusManager.
- `Ctrl+F` and `Ctrl+R` toggle panel visibility (registered as `Always` shortcuts).

#### Subclass Extension

The `optionsRow()` layout is exposed to subclasses for inserting additional widgets:

```cpp
protected:
    QHBoxLayout *optionsRow() const;
```

---

### ScopedFindReplacePanel

**Header:** `<wlxbase_wlqt/ScopedFindReplacePanel.h>`

Extends `FindReplacePanel` with a configurable scope combo box. The consumer provides scope labels and reads the current selection in their signal handlers.

```cpp
explicit ScopedFindReplacePanel(FocusManager *fm, QWidget *parent = nullptr);

void setScopes(const QStringList &scopes);  // e.g. {"All Cells", "Current Column"}
QString currentScope() const;
```

#### Usage

```cpp
auto *panel = new QtWlPlugin::ScopedFindReplacePanel(m_fm, this);
panel->setScopes({"All Cells", "Selected Cells", "Current Column", "Current Row"});

connect(panel, &QtWlPlugin::FindReplacePanel::findRequested,
    this, [this, panel](bool forward) {
        QString scope = panel->currentScope();
        // ... implement search logic using scope
    }
);
```

---

## Utilities

### EncodingUtils

**Header:** `<wlxbase_wlqt/EncodingUtils.h>`

Static utility class for encoding detection (via [enca](https://cihar.com/software/enca/)) and encoding conversion (via GLib's `g_convert_with_fallback`). No instance needed — all methods are static.

#### API

```cpp
// Detect encoding of raw bytes. Language hint improves accuracy for
// specific scripts (e.g. "ru" for Russian, "zh" for Chinese).
static QString detectEncoding(const QByteArray &data, const QString &language = {});

// Detect encoding from a file (reads up to sampleSize bytes).
static QString detectFileEncoding(const QString &filePath, const QString &language = {},
                                  int sampleSize = 4096, bool readAll = false);

// Convert from a detected encoding to UTF-8.
static QByteArray toUtf8(const QByteArray &data, const QString &fromEncoding);

// Convert a UTF-8 QString to a target encoding.
static QByteArray fromUtf8(const QString &text, const QString &toEncoding);

// One-shot: detect encoding and decode to QString.
static QString decodeToString(const QByteArray &data, const QString &language = {});

// Check runtime availability of enca.
static bool isEncaAvailable();
```

#### Usage

```cpp
// Detect and decode a file
QFile f("/path/to/file.txt");
f.open(QFile::ReadOnly);
QByteArray raw = f.readAll();
f.close();

QString encoding = QtWlPlugin::EncodingUtils::detectEncoding(raw, "ru");
// encoding → "windows-1251"

QString text = QtWlPlugin::EncodingUtils::decodeToString(raw, "ru");
// text → decoded UTF-8 QString

// Convert back for saving
QByteArray encoded = QtWlPlugin::EncodingUtils::fromUtf8(text, "windows-1251");
```

#### Supported Encodings

Detection accuracy depends on the language hint. enca supports:

| Language | Code | Scripts |
|----------|------|---------|
| Belarusian | `be` | CP1251, ISO-8859-5, IBM866, KOI8-UNI, etc. |
| Bulgarian | `bg` | CP1251, ISO-8859-5, ECMA-113, etc. |
| Chinese | `zh` | BIG5, GBK, GB2312, HZ, etc. |
| Croatian | `hr` | CP1250, ISO-8859-2, ISO-8859-16, etc. |
| Czech | `cs` | CP1250, ISO-8859-2, IBM852, KEYBCS2, etc. |
| Estonian | `et` | CP1257, ISO-8859-4, ISO-8859-13, etc. |
| Hungarian | `hu` | CP1250, ISO-8859-2, IBM852, etc. |
| Latvian | `lv` | CP1257, ISO-8859-4, ISO-8859-13, etc. |
| Lithuanian | `lt` | CP1257, ISO-8859-4, ISO-8859-13, etc. |
| Polish | `pl` | CP1250, ISO-8859-2, ISO-8859-16, etc. |
| Russian | `ru` | CP1251, KOI8-R, ISO-8859-5, IBM866, etc. |
| Slovak | `sk` | CP1250, ISO-8859-2, IBM852, KEYBCS2, etc. |
| Slovene | `sl` | CP1250, ISO-8859-2, IBM852, etc. |
| Ukrainian | `uk` | CP1251, KOI8-U, ISO-8859-5, IBM866, etc. |

Use `"__"` or an empty string to let enca auto-detect from the system locale.

---

## Examples

### Plugin with toolbar + find/replace (no grid)

```cpp
#include <wlxbase_wlqt/FocusManager.h>
#include <wlxbase_wlqt/PluginToolBar.h>
#include <wlxbase_wlqt/ScopedFindReplacePanel.h>

class TextViewerWidget : public QWidget {
    Q_OBJECT
public:
    TextViewerWidget(QWidget *parent = nullptr) : QWidget(parent) {
        auto *view = new QTextEdit(this);
        auto *layout = new QVBoxLayout(this);

        m_fm = new QtWlPlugin::FocusManager(this, view, this);

        m_toolbar = new QtWlPlugin::PluginToolBar(m_fm, this);
        m_toolbar->addToolAction("Print", QKeySequence(Qt::CTRL | Qt::Key_P));

        m_findReplace = new QtWlPlugin::ScopedFindReplacePanel(m_fm, this);
        m_findReplace->setScopes({"Entire Document", "Selection"});
        m_findReplace->setReplaceEnabled(false);  // read-only viewer

        connect(m_findReplace, &QtWlPlugin::FindReplacePanel::findRequested,
            this, &TextViewerWidget::onFind);

        layout->addWidget(m_toolbar);
        layout->addWidget(view, 1);
        layout->addWidget(m_findReplace);
    }

private slots:
    void onFind(bool forward) {
        QString query = m_findReplace->findText();
        QString scope = m_findReplace->currentScope();
        // ... implement search
    }

private:
    QtWlPlugin::FocusManager *m_fm;
    QtWlPlugin::PluginToolBar *m_toolbar;
    QtWlPlugin::ScopedFindReplacePanel *m_findReplace;
};
```

### Plugin with editable grid + undo (QTableWidget)

```cpp
#include <wlxbase_wlqt/FocusManager.h>
#include <wlxbase_wlqt/PluginToolBar.h>
#include <wlxbase_wlqt/EditableGridWidget.h>
#include <QTableWidget>

class DataEditorWidget : public QWidget {
    Q_OBJECT
public:
    DataEditorWidget(QWidget *parent = nullptr) : QWidget(parent) {
        auto *layout = new QVBoxLayout(this);

        // Inject a QTableWidget for item-based convenience
        auto *table = new QTableWidget();
        m_fm = new QtWlPlugin::FocusManager(this, table, this);
        m_grid = new QtWlPlugin::EditableGridWidget(
            table, QtWlPlugin::GridMode::MemoryDocument, m_fm, this);

        m_toolbar = new QtWlPlugin::PluginToolBar(m_fm, this);
        auto *actSave = m_toolbar->addToolAction("Save",
            QKeySequence(Qt::CTRL | Qt::Key_S),
            QtWlPlugin::FocusManager::Always);
        connect(actSave, &QAction::triggered, this, &DataEditorWidget::onSave);

        connect(m_grid, &QtWlPlugin::EditableGridWidget::dirtyChanged,
            this, [this](bool dirty) {
                setWindowTitle(dirty ? "Data Editor *" : "Data Editor");
            });

        layout->addWidget(m_toolbar);
        layout->addWidget(m_grid, 1);
        loadData();
    }

private:
    void loadData() {
        // You can still access QTableWidget-specific methods on the original pointer
        auto *tw = qobject_cast<QTableWidget*>(m_grid->view());
        tw->setRowCount(100);
        tw->setColumnCount(5);
        // ... populate cells with QTableWidgetItem
    }

    void onSave() {
        // ... write data in your format
        m_fm->undoStack()->setClean();
    }

    QtWlPlugin::FocusManager *m_fm;
    QtWlPlugin::PluginToolBar *m_toolbar;
    QtWlPlugin::EditableGridWidget *m_grid;
};
```

### Plugin with editable grid + SQL model (QTableView)

```cpp
#include <wlxbase_wlqt/FocusManager.h>
#include <wlxbase_wlqt/PluginToolBar.h>
#include <wlxbase_wlqt/EditableGridWidget.h>
#include <QTableView>
#include <QSqlTableModel>

class SqlEditorWidget : public QWidget {
    Q_OBJECT
public:
    SqlEditorWidget(QSqlDatabase db, QWidget *parent = nullptr) : QWidget(parent) {
        auto *layout = new QVBoxLayout(this);

        // Inject a QTableView with a SQL model
        auto *sqlView = new QTableView();
        auto *sqlModel = new QSqlTableModel(this, db);
        sqlModel->setTable("users");
        sqlModel->select();
        sqlView->setModel(sqlModel);

        m_fm = new QtWlPlugin::FocusManager(this, sqlView, this);
        m_grid = new QtWlPlugin::EditableGridWidget(
            sqlView, QtWlPlugin::GridMode::LiveDatabase, m_fm, this);

        // The grid handles all focus, context menus, and shortcuts.
        // Sorting uses model->sort() → SQL ORDER BY (no RAM snapshot).
        // Insert/delete call model directly (no undo stack wrapping).
        // model->setData() triggers SQL UPDATEs via QSqlTableModel.

        layout->addWidget(m_grid, 1);
    }

private:
    QtWlPlugin::FocusManager *m_fm;
    QtWlPlugin::EditableGridWidget *m_grid;
};
```

### Encoding detection on file load

```cpp
#include <wlxbase_wlqt/EncodingUtils.h>

void MyPlugin::loadFile(const QString &path) {
    QFile file(path);
    file.open(QFile::ReadOnly);
    QByteArray raw = file.readAll();
    file.close();

    QString encoding = QtWlPlugin::EncodingUtils::detectEncoding(raw);
    if (encoding.isEmpty())
        encoding = "UTF-8";  // fallback

    QByteArray utf8 = QtWlPlugin::EncodingUtils::toUtf8(raw, encoding);
    QString content = QString::fromUtf8(utf8);

    m_detectedEncoding = encoding;  // store for save
    // ... display content
}

void MyPlugin::saveFile(const QString &path) {
    QString content = /* ... get content ... */;
    QByteArray encoded = QtWlPlugin::EncodingUtils::fromUtf8(content, m_detectedEncoding);

    QFile file(path);
    file.open(QFile::WriteOnly);
    file.write(encoded);
    file.close();
}
```

---

## Design Decisions

### Shortcut Registry over Hardcoded eventFilter

Existing plugins (`csvview`, `logview`, `kate`) each contain near-identical `eventFilter` implementations with large if-chains mapping key combinations to actions. This library replaces that pattern with a declarative `registerShortcut()` API. New plugins never need to subclass or override `eventFilter` for keyboard shortcuts.

### Static Library

Each consumer plugin links `libwayland_qt_base.a` and produces a self-contained `.wlx` with no runtime dependency on a separate shared object. This simplifies deployment — just ship the `.wlx` file.

### Optional Undo

Not all plugins need undo/redo (e.g. a read-only log viewer). Calling `setUndoStack()` is optional. When set, undo shortcuts are auto-registered; when cleared, they're auto-unregistered. The consumer always has direct access to the `QUndoStack` for pushing custom commands, checking `isClean()`, etc.

### FindReplacePanel Hierarchy

The base class `FindReplacePanel` has zero scope awareness. This is intentional — a text editor plugin's "scope" concept (selection, whole file) is fundamentally different from a grid plugin's (all cells, current column, current row). The base class provides the UI and signals; the consumer implements matching.

`ScopedFindReplacePanel` adds a scope combo box for plugins that want predefined scope options. It inserts into the base's `optionsRow()` layout, so there's no UI duplication.

### Model-Based, Format-Agnostic Grid

`EditableGridWidget` accepts a `QTableView*` via dependency injection and performs all data operations through `QAbstractItemModel` (`model()->data()`, `model()->setData()`, `model()->insertRows()`, etc.). This means the same grid code works identically whether the underlying model is a `QTableWidget`'s internal model, a `QStandardItemModel`, a `QSqlTableModel`, or any custom `QAbstractItemModel`. The consumer is responsible for file I/O, encoding, and format-specific quoting — the grid knows nothing about data formats.

### GridMode: MemoryDocument vs LiveDatabase

The `GridMode` enum forces the downstream developer to explicitly acknowledge the memory paradigm of the data they are loading. `MemoryDocument` enables full undo/redo tracking — every edit, insert, delete, and sort is wrapped in a `QUndoCommand` backed by in-memory data snapshots. `LiveDatabase` bypasses all memory-intensive operations: sorting delegates to `model->sort()` (which translates to SQL `ORDER BY` for `QSqlTableModel`), and structural mutations (insert/delete) call the model directly without undo wrappers. This prevents the catastrophic RAM explosion that would occur if a 5-million-row SQLite table were snapshotted into `QVariantList` objects for undo support. Both modes share identical context menus, drag-to-reorder, focus proxying, keyboard shortcuts, and the `WrapAnywhereDelegate`.

### enca as Build Dependency

The encoding detection library [enca](https://cihar.com/software/enca/) is downloaded and statically compiled during the CMake configure step. This mirrors the pattern used by `csvview` and avoids requiring enca to be installed system-wide. The static link means no runtime dependency.

---

## Directory Layout

```
wlx/wlxbase_wlqt/
├── CMakeLists.txt
├── README.md
├── include/
│   └── wlxbase_wlqt/
│       ├── EditableGridWidget.h
│       ├── EncodingUtils.h
│       ├── FindReplacePanel.h
│       ├── FocusManager.h
│       ├── PluginToolBar.h
│       └── ScopedFindReplacePanel.h
└── src/
    ├── EditableGridWidget.cpp
    ├── EncodingUtils.cpp
    ├── FindReplacePanel.cpp
    ├── FocusManager.cpp
    ├── PluginToolBar.cpp
    └── ScopedFindReplacePanel.cpp
```
