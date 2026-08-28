#pragma once

#include <gtk/gtk.h>
#include <vector>
#include <string>
#include <functional>

namespace GtkWlPlugin {

class GtkFocusManager;

/// GTK counterpart to QtWlPlugin::EditableGridWidget — a GtkTreeView (all
/// columns G_TYPE_STRING) wrapper with undo/redo (via GtkFocusManager's
/// command stack), tab-separated copy/paste, and row insert/delete. Scope
/// note: does not port EditableGridWidget's drag-to-reorder-columns,
/// filter-row, or theme-toggle context-menu integration — those are
/// secondary UI chrome, not the undo/redo + editing functionality this
/// was built to bring to parity.
///
/// Also intentionally NOT ported: insertColumns/deleteSelectedColumns/
/// copyColumnSelection/pasteColumnSelectionAt. Qt's versions are driven by
/// QHeaderView's column-selection UI; GtkTreeViewColumn headers have no
/// equivalent selection API, and no plugin here has ever built
/// click-to-select-column UI, so those methods would have no way to be
/// invoked -- dead API surface, not a real gap. setFilterRow and
/// setThemeToggleEnabled/setExtraContextMenuCallback are similarly out of
/// scope: they hook into Qt's FilterRowWidget and right-click context menu
/// (showRowContextMenu/showColumnContextMenu), neither of which exists on
/// the GTK side at all.
class GtkEditableGridWidget {
public:
    /// Takes ownership of building a GtkTreeView with `columnCount`
    /// string columns inside a GtkScrolledWindow.
    GtkEditableGridWidget(int columnCount, GtkFocusManager *fm);
    ~GtkEditableGridWidget();

    GtkEditableGridWidget(const GtkEditableGridWidget &) = delete;
    GtkEditableGridWidget &operator=(const GtkEditableGridWidget &) = delete;

    /// The GtkScrolledWindow containing the tree view — add this to your
    /// layout.
    GtkWidget *widget() const { return m_scrolled; }
    GtkWidget *treeView() const { return m_treeView; }
    GtkListStore *store() const { return m_store; }

    void setColumnTitle(int col, const std::string &title);
    void setColumnEditable(int col, bool editable);

    /// Replace all row data. Does not go through the undo stack (matches
    /// EditableGridWidget's behavior on a fresh load).
    void setRowData(const std::vector<std::vector<std::string>> &rows);
    /// Appends rows without clearing existing ones first -- for
    /// incremental/lazy-loaded data sources (see dbview's GTK3 UI) where
    /// re-populating the whole store on every fetched chunk would defeat
    /// the point of not materializing everything upfront.
    void appendRows(const std::vector<std::vector<std::string>> &rows);
    std::vector<std::vector<std::string>> rowData() const;

    int rowCount() const;
    int columnCount() const { return m_columnCount; }

    // --- Data operations (undo-tracked) ---
    void copySelection(char separator = '\t');
    void pasteSelection(char separator = '\t');
    /// Same as pasteSelection(), but pastes starting at a given row instead
    /// of the current selection (e.g. from a programmatic Replace-driven
    /// paste rather than an interactive one).
    void pasteSelectionAt(int atRow, char separator = '\t');
    void insertRows(int count, int atRow);
    void deleteSelectedRows();

    // --- Appearance ---
    void setShowGrid(bool show);

    /// Wraps long cell text across multiple lines instead of clipping it.
    /// Previously omitted from this widget as "not achievable in GTK", which
    /// left csvview's and structview's Word Wrap buttons toggling only their
    /// optional Show Text panel -- never the grid the user is actually
    /// looking at, so the button appeared to do nothing at all. dbview had
    /// already proven the cell-renderer approach works; this lifts it out of
    /// dbview so every consumer gets it.
    ///
    /// Each column wraps at the width it CURRENTLY has. Toggling this never
    /// resizes a column -- it is a text property, not a layout command. That
    /// is a deliberate product decision, and it has a visible consequence: a
    /// column auto-sized to its own content is by definition wide enough for
    /// one line, so wrapping it changes nothing until the column is narrowed.
    /// Narrow a column and its text wraps immediately (the width is tracked
    /// live). Do not "fix" that by pinning columns to a computed width --
    /// that was tried, and it resized columns the user never asked to resize.
    void setWordWrap(bool wrap);
    bool wordWrap() const { return m_wordWrap; }

    /// Programmatic single-cell edit (e.g. from Replace/Replace All),
    /// pushed through the same undo stack as interactive edits.
    void setCellValue(int row, int col, const std::string &text);
    std::string cellValue(int row, int col) const;

    /// Selects (and scrolls to) a single cell — used to highlight Find
    /// results.
    void selectCell(int row, int col);

    bool isDirty() const { return m_dirty; }
    void setDirty(bool dirty);
    /// Called whenever a cell edit, insert, or delete happens (after undo
    /// tracking). Use this to trigger a "save" affordance.
    void setDirtyChangedCallback(std::function<void(bool)> cb) { m_dirtyChangedCb = std::move(cb); }

private:
    void onCellEdited(int col, const std::string &pathStr, const std::string &newText);
    static void cellEditedTrampoline(GtkCellRendererText *, gchar *path, gchar *newText, gpointer data);

    GtkFocusManager *m_fm;
    GtkWidget *m_scrolled;
    GtkWidget *m_treeView;
    GtkListStore *m_store;
    int m_columnCount;
    bool m_dirty = false;
    std::function<void(bool)> m_dirtyChangedCb;

    // Column-edit trampoline context (one per column, freed with the widget)
    void applyWrapToColumn(GtkTreeViewColumn *col, int budget);
    static void columnWidthChangedTrampoline(GObject *col, GParamSpec *pspec, gpointer data);


    /// Forces GtkTreeView to discard cached row heights and re-measure. Needed
    /// after anything that changes how tall a row's text renders.
    void refreshRowHeights();
    static gboolean rowHeightRefreshIdle(gpointer data);

    // lastBudget: width this column's wrap was last applied at. Used to ignore
    // notify::width events that report a width we have already handled, which
    // is what kept the refresh loop alive.
    struct ColCtx { GtkEditableGridWidget *self; int col; int lastBudget = -1; };
    std::vector<ColCtx *> m_colContexts;
    bool m_wordWrap = false;
    bool m_applyingWrap = false; // re-entrancy guard for notify::width
    guint m_rowHeightRefreshId = 0; // pending idle row-height re-measure
};

} // namespace GtkWlPlugin
