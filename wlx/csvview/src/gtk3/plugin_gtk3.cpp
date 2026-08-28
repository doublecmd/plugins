/*
 * CSV/TSV WLX plugin for Double Commander — GTK3 UI layer.
 *
 * Full-featured build on wlxbase_gtk: GtkFocusManager (shortcuts, undo/redo
 * stack), GtkEditableGridWidget (copy/paste, insert/delete rows), and
 * GtkScopedFindReplacePanel (find/replace with a column-scope selector) —
 * brought to toolbar/feature parity with the Qt6 build's PluginToolBar
 * (Save, Save As, Undo, Redo, Print, Reload, Header Row, Find/Replace, Show
 * Text, Line Wrap, Open Externally) plus a row-number gutter column, which
 * this GTK3 build was previously missing entirely. CsvCore (src/core/)
 * provides the actual CSV tokenizing/serialization, same as before.
 *
 * Separator is now auto-detected from the file content (matching the Qt6
 * build: try ',' ';' '\t' in order on the first line, keep the first that
 * yields more than one column, else fall back on the file extension) --
 * previously this GTK build had a manual delimiter dropdown that doesn't
 * exist in the Qt6 toolbar at all.
 */

#include <gtk/gtk.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <regex>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <utility>
#include <set>

#include "wlxplugin.h"
#include "CsvCore.h"
#include "wlxbase_gtk/GtkFocusManager.h"
#include "wlxbase_gtk/GtkPluginToolBar.h"
#include "wlxbase_gtk/GtkEditableGridWidget.h"
#include "wlxbase_gtk/GtkScopedFindReplacePanel.h"

using namespace GtkWlPlugin;

namespace {

struct CsvGtkState {
    GtkWidget *root = nullptr;
    GtkWidget *gridSlot = nullptr; // holds st->grid->widget(), swapped on reload
    std::unique_ptr<GtkFocusManager> fm;
    std::unique_ptr<GtkPluginToolBar> toolbar;
    std::unique_ptr<GtkEditableGridWidget> grid;
    std::unique_ptr<GtkScopedFindReplacePanel> findPanel;

    GtkWidget *dirtyLabel = nullptr;
    GtkWidget *undoBtn = nullptr;
    GtkWidget *redoBtn = nullptr;
    GtkWidget *headerToggle = nullptr;
    GtkWidget *findToggle = nullptr;
    GtkWidget *textToggle = nullptr;
    GtkWidget *wrapToggle = nullptr;

    // "Show Text" mode: a plain GtkTextView shown in place of the grid.
    GtkWidget *textScroll = nullptr;
    GtkWidget *textView = nullptr;
    bool showingText = false;
    bool wordWrap = false;

    std::string currentFile;
    char separator = ',';
    bool firstLineAsHeader = true;
    std::vector<bool> columnWasQuoted;

    // Find state
    int findRow = 0, findCol = 0;

    // Column selection (click a column header to select it -- highlighted
    // visually since GtkTreeSelection has no native column-selection
    // concept, only row selection). Supports Ctrl-click (toggle) and
    // Shift-click (range from columnSelectAnchor) like row selection does.
    // Mutually exclusive with row selection: selecting a column clears row
    // selection and vice versa (suppressRowSelectionSync guards against the
    // "clear rows" step re-triggering the "clear columns" handler).
    std::set<int> selectedColumns;
    int columnSelectAnchor = -1;
    bool suppressRowSelectionSync = false;
};

std::string readFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::vector<std::string> splitLines(const std::string &data)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= data.size()) {
        size_t pos = data.find('\n', start);
        if (pos == std::string::npos) {
            if (start < data.size()) lines.push_back(data.substr(start));
            break;
        }
        lines.push_back(data.substr(start, pos - start + 1));
        start = pos + 1;
    }
    return lines;
}

bool endsWithNoCase(const std::string &s, const std::string &suffix)
{
    if (s.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(), [](char a, char b) {
        return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
    });
}

// Mirrors the Qt6 build's detection: try ',' ';' '\t' in turn on the first
// line, keep the first that yields more than one field; fall back to the
// file extension (.tsv -> tab, else comma) if none of them do.
char detectSeparator(const std::string &firstLine, const std::string &path)
{
    static const char candidates[] = {',', ';', '\t'};
    for (char c : candidates) {
        auto fields = CsvCore::parseLine(firstLine, c);
        if (fields.size() > 1) return c;
    }
    return endsWithNoCase(path, ".tsv") ? '\t' : ',';
}

// Renders the 1-based row index into the leading gutter column -- purely
// cosmetic, not backed by any model column, so it doesn't touch
// GtkEditableGridWidget at all.
void rowNumberCellDataFunc(GtkTreeViewColumn *, GtkCellRenderer *cell,
                            GtkTreeModel *model, GtkTreeIter *iter, gpointer)
{
    GtkTreePath *path = gtk_tree_model_get_path(model, iter);
    gint *indices = gtk_tree_path_get_indices(path);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", indices[0] + 1);
    g_object_set(cell, "text", buf, nullptr);
    gtk_tree_path_free(path);
}

void addRowNumberColumn(GtkWidget *treeView)
{
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, "foreground", "#888888", nullptr);
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("#", renderer, nullptr);
    gtk_tree_view_column_set_cell_data_func(col, renderer, rowNumberCellDataFunc, nullptr, nullptr);
    gtk_tree_view_column_set_resizable(col, FALSE);
    gtk_tree_view_column_set_min_width(col, 32);
    gtk_tree_view_insert_column(GTK_TREE_VIEW(treeView), col, 0);
}

// A naked std::pair<CsvGtkState*, int> trips up the preprocessor when used
// directly inside a G_CALLBACK(...) macro invocation -- G_CALLBACK only
// balances parens when splitting its argument list, so the comma inside
// the template argument list gets misparsed as an extra macro argument
// (same class of bug fixed earlier for the column-edit renderer context
// below). Route through a type alias instead.
using ColSelCtx = std::pair<CsvGtkState *, int>;

// Tints every cell in a selected column (CsvGtkState's own
// selectedColumns, set by clicking column headers below) so the selection
// is visible -- GtkTreeSelection only tracks row selection natively, there
// is no built-in concept of a selected column to render for us.
void columnHighlightCellDataFunc(GtkTreeViewColumn *, GtkCellRenderer *cell,
                                  GtkTreeModel *, GtkTreeIter *, gpointer userData)
{
    auto *ctx = static_cast<ColSelCtx *>(userData);
    if (ctx->first->selectedColumns.count(ctx->second))
        g_object_set(cell, "cell-background", "#3465A4", "cell-background-set", TRUE, nullptr);
    else
        g_object_set(cell, "cell-background-set", FALSE, nullptr);
}

// Makes every data column header clickable to select (and visually
// highlight) that whole column -- addresses "can't select columns" for the
// column-operations context menu. Supports Ctrl-click (toggle a column
// in/out of the selection) and Shift-click (range from the last-clicked
// column), matching how row multi-selection already works. Selecting a
// column always clears row selection (and vice versa, via the
// GtkTreeSelection "changed" handler below) -- the two selection modes are
// mutually exclusive, not independent. Must run after addRowNumberColumn()
// so the "+1" gutter offset below is correct (matches the ordering
// requirement documented at loadFile()/rebuildGridColumns()'s own
// addRowNumberColumn() call sites), and before attachContextMenus() so its
// right-click handler on the same header buttons gets a chance to run
// after this one -- this handler only consumes (returns TRUE) primary-
// button clicks, leaving secondary-button (right-click) events to fall
// through to attachContextMenus()'s handler connected afterward.
void setupColumnSelection(CsvGtkState *st)
{
    GtkWidget *treeView = st->grid->treeView();
    int dataColCount = st->grid->columnCount();
    for (int c = 0; c < dataColCount; ++c) {
        GtkTreeViewColumn *col = gtk_tree_view_get_column(GTK_TREE_VIEW(treeView), c + 1); // +1: skip the "#" gutter
        if (!col) continue;
        gtk_tree_view_column_set_clickable(col, TRUE);

        // Two SEPARATE allocations, one per destroy-notify site below --
        // GTK invokes both independently during teardown (the column's own
        // cell-data-func destroy-notify, and the button's signal-closure
        // destroy-notify), so sharing one ctx between them double-frees it
        // (reproducibly crashed with "double free detected in tcache 2" on
        // ListCloseWindow, no interaction needed to trigger it).
        GList *renderers = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(col));
        for (GList *l = renderers; l; l = l->next)
            gtk_tree_view_column_set_cell_data_func(col, GTK_CELL_RENDERER(l->data),
                columnHighlightCellDataFunc, new ColSelCtx(st, c), +[](gpointer data) { delete static_cast<ColSelCtx *>(data); });
        g_list_free(renderers);

        GtkWidget *button = gtk_tree_view_column_get_button(col);
        if (!button) continue;
        auto *ctx = new ColSelCtx(st, c);
        g_signal_connect_data(button, "button-press-event", G_CALLBACK(+[](GtkWidget *, GdkEventButton *event, gpointer data) -> gboolean {
            if (event->button != GDK_BUTTON_PRIMARY) return FALSE; // let the right-click menu handler run instead
            auto *ctx = static_cast<ColSelCtx *>(data);
            CsvGtkState *st = ctx->first;
            int col = ctx->second;
            bool ctrl = (event->state & GDK_CONTROL_MASK) != 0;
            bool shift = (event->state & GDK_SHIFT_MASK) != 0;

            if (shift && st->columnSelectAnchor >= 0) {
                int lo = std::min(st->columnSelectAnchor, col);
                int hi = std::max(st->columnSelectAnchor, col);
                if (!ctrl) st->selectedColumns.clear();
                for (int i = lo; i <= hi; ++i) st->selectedColumns.insert(i);
            } else if (ctrl) {
                if (st->selectedColumns.count(col)) st->selectedColumns.erase(col);
                else st->selectedColumns.insert(col);
                st->columnSelectAnchor = col;
            } else {
                st->selectedColumns = {col};
                st->columnSelectAnchor = col;
            }

            st->suppressRowSelectionSync = true;
            gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(st->grid->treeView())));
            st->suppressRowSelectionSync = false;

            gtk_widget_queue_draw(st->grid->treeView());
            return TRUE;
        }), ctx, +[](gpointer data, GClosure *) { delete static_cast<ColSelCtx *>(data); }, (GConnectFlags)0);
    }

    g_signal_connect_data(gtk_tree_view_get_selection(GTK_TREE_VIEW(treeView)), "changed",
        G_CALLBACK(+[](GtkTreeSelection *sel, gpointer data) {
            auto *st = static_cast<CsvGtkState *>(data);
            if (st->suppressRowSelectionSync || st->selectedColumns.empty()) return;
            if (gtk_tree_selection_count_selected_rows(sel) > 0) {
                st->selectedColumns.clear();
                st->columnSelectAnchor = -1;
                gtk_widget_queue_draw(st->grid->treeView());
            }
        }), st, nullptr, (GConnectFlags)0);
}

void refreshUndoRedoSensitivity(CsvGtkState *st)
{
    if (st->undoBtn) gtk_widget_set_sensitive(st->undoBtn, st->fm->canUndo());
    if (st->redoBtn) gtk_widget_set_sensitive(st->redoBtn, st->fm->canRedo());
}

void updateDirtyLabel(CsvGtkState *st, bool dirty)
{
    if (st->dirtyLabel) gtk_label_set_text(GTK_LABEL(st->dirtyLabel), dirty ? "●" : "✓");
    refreshUndoRedoSensitivity(st);
}

std::string joinRowPlain(const std::vector<std::string> &row, char sep)
{
    std::string out;
    for (size_t c = 0; c < row.size(); ++c) {
        if (c) out += sep;
        out += row[c];
    }
    return out;
}

void updateTextView(CsvGtkState *st)
{
    if (!st->textView || !st->grid) return;
    std::string plain;
    if (st->firstLineAsHeader) {
        std::vector<std::string> titles;
        for (int c = 0; c < st->grid->columnCount(); ++c) {
            GtkTreeViewColumn *col = gtk_tree_view_get_column(GTK_TREE_VIEW(st->grid->treeView()), c + 1); // +1: row-number gutter
            const char *title = col ? gtk_tree_view_column_get_title(col) : "";
            titles.push_back(title ? title : "");
        }
        plain += joinRowPlain(titles, st->separator) + "\n";
    }
    for (const auto &row : st->grid->rowData())
        plain += joinRowPlain(row, st->separator) + "\n";

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(st->textView));
    gtk_text_buffer_set_text(buf, plain.c_str(), -1);
}

void attachContextMenus(CsvGtkState *st); // defined below loadFile(); wires row/column right-click menus

void loadFile(CsvGtkState *st, const std::string &path)
{
    st->currentFile = path;
    std::string data = readFile(path);
    std::vector<std::string> lines = splitLines(data);

    st->separator = detectSeparator(lines.empty() ? std::string() : lines[0], path);

    std::vector<std::vector<CsvCore::Field>> rows;
    int colCount = 1;
    for (auto &line : lines) {
        auto fields = CsvCore::parseLine(line, st->separator);
        colCount = std::max(colCount, (int)fields.size());
        rows.push_back(std::move(fields));
    }

    // Rebuild the grid widget with the right column count.
    GtkWidget *oldGridWidget = st->grid ? st->grid->widget() : nullptr;
    st->grid = std::make_unique<GtkEditableGridWidget>(colCount, st->fm.get());
    st->grid->setDirtyChangedCallback([st](bool dirty) { updateDirtyLabel(st, dirty); });

    std::vector<std::vector<std::string>> tableRows;
    size_t startRow = st->firstLineAsHeader ? 1 : 0;
    st->columnWasQuoted.assign(colCount, false);
    for (int c = 0; c < colCount; ++c) {
        std::string title = (st->firstLineAsHeader && !rows.empty() && c < (int)rows[0].size())
                                 ? rows[0][c].text
                                 : ("Column " + std::to_string(c + 1));
        st->grid->setColumnTitle(c, title);
    }
    // Must come after the title-setting loop above: addRowNumberColumn()
    // inserts a column at treeview position 0, which shifts every data
    // column's position by one. setColumnTitle(c, ...) addresses columns
    // by raw treeview position (via gtk_tree_view_get_column), so calling
    // this first (as it used to be) made every title-setting call one
    // column off -- overwriting the "#" gutter's own title with the first
    // data column's title, and cascading every title one column to the
    // right of where its data actually lives.
    addRowNumberColumn(st->grid->treeView());
    st->selectedColumns.clear(); // column identities changed, drop any stale selection
    st->columnSelectAnchor = -1;
    setupColumnSelection(st);
    for (size_t r = startRow; r < rows.size(); ++r) {
        std::vector<std::string> row(colCount, "");
        for (int c = 0; c < colCount && c < (int)rows[r].size(); ++c) {
            row[c] = rows[r][c].text;
            if (rows[r][c].wasQuoted) st->columnWasQuoted[c] = true;
        }
        tableRows.push_back(std::move(row));
    }
    st->grid->setRowData(tableRows);

    if (oldGridWidget) {
        GtkWidget *parent = gtk_widget_get_parent(oldGridWidget);
        gtk_container_remove(GTK_CONTAINER(parent), oldGridWidget);
        gtk_box_pack_start(GTK_BOX(parent), st->grid->widget(), TRUE, TRUE, 0);
        gtk_widget_show_all(st->grid->widget());
        if (st->showingText) gtk_widget_hide(st->grid->widget());
    }

    st->fm->clearUndoStack();
    updateDirtyLabel(st, false);
    if (st->showingText) updateTextView(st);
    attachContextMenus(st);
}

// --- Row/column context menus ---------------------------------------
//
// GtkEditableGridWidget (shared with dbview/structview) exposes row
// insert/delete but has a fixed column count set at construction --
// there's no "insert/delete column" API on it, unlike its Qt6 sibling
// EditableGridWidget. Rather than extend the shared widget (a
// cross-cutting change affecting every consumer, out of scope for a
// csvview-only feature-parity pass), column operations here just rebuild
// the grid in place from the current in-memory row data with the column
// list mutated -- the same rebuild shape loadFile() already does when
// the column count changes on reload, just driven from the live grid's
// own data instead of re-parsing the file.

std::vector<std::string> currentColumnTitles(CsvGtkState *st)
{
    std::vector<std::string> titles;
    for (int c = 0; c < st->grid->columnCount(); ++c) {
        GtkTreeViewColumn *col = gtk_tree_view_get_column(GTK_TREE_VIEW(st->grid->treeView()), c + 1);
        const char *title = col ? gtk_tree_view_column_get_title(col) : "";
        titles.push_back(title ? title : "");
    }
    return titles;
}

// Rebuilds st->grid with `newTitles.size()` columns, mapping each new
// row via `transformRow` (which receives the *old* row and must return
// a row sized to newTitles.size()).
void rebuildGridColumns(CsvGtkState *st, const std::vector<std::string> &newTitles,
                         const std::function<std::vector<std::string>(const std::vector<std::string> &)> &transformRow)
{
    auto oldRows = st->grid->rowData();
    std::vector<std::vector<std::string>> newRows;
    newRows.reserve(oldRows.size());
    for (auto &row : oldRows)
        newRows.push_back(transformRow(row));

    int newColCount = (int)newTitles.size();
    GtkWidget *oldGridWidget = st->grid->widget();
    st->grid = std::make_unique<GtkEditableGridWidget>(newColCount, st->fm.get());
    st->grid->setDirtyChangedCallback([st](bool dirty) { updateDirtyLabel(st, dirty); });
    for (int c = 0; c < newColCount; ++c)
        st->grid->setColumnTitle(c, newTitles[c]);
    // Same ordering requirement as loadFile() -- see the comment there.
    addRowNumberColumn(st->grid->treeView());
    st->selectedColumns.clear();
    st->columnSelectAnchor = -1;
    setupColumnSelection(st);
    st->grid->setRowData(newRows);
    st->grid->setDirty(true);

    GtkWidget *parent = gtk_widget_get_parent(oldGridWidget);
    gtk_container_remove(GTK_CONTAINER(parent), oldGridWidget);
    gtk_box_pack_start(GTK_BOX(parent), st->grid->widget(), TRUE, TRUE, 0);
    gtk_widget_show_all(st->grid->widget());
    if (st->showingText) { gtk_widget_hide(st->grid->widget()); updateTextView(st); }

    updateDirtyLabel(st, true);
    attachContextMenus(st);
}

// N-aware column insert/delete, driven by the current header selection
// (st->selectedColumns) when the right-clicked column is part of it, or a
// single-column {col} set otherwise -- see showColumnContextMenu().
void insertColumnsAt(CsvGtkState *st, int atCol, int count)
{
    auto titles = currentColumnTitles(st);
    atCol = std::max(0, std::min(atCol, (int)titles.size()));
    for (int i = 0; i < count; ++i)
        titles.insert(titles.begin() + atCol, "Column " + std::to_string(atCol + i + 1));
    rebuildGridColumns(st, titles, [atCol, count](const std::vector<std::string> &row) {
        std::vector<std::string> out = row;
        for (int i = 0; i < count; ++i)
            out.insert(out.begin() + std::min(atCol, (int)out.size()), "");
        return out;
    });
}

void deleteColumns(CsvGtkState *st, const std::set<int> &cols)
{
    auto titles = currentColumnTitles(st);
    if (cols.empty() || (int)cols.size() >= (int)titles.size()) return; // must keep at least 1 column
    std::vector<int> sorted(cols.begin(), cols.end());
    std::sort(sorted.rbegin(), sorted.rend()); // descending so earlier indices stay valid while erasing
    for (int c : sorted)
        if (c >= 0 && c < (int)titles.size()) titles.erase(titles.begin() + c);
    rebuildGridColumns(st, titles, [sorted](const std::vector<std::string> &row) {
        std::vector<std::string> out = row;
        for (int c : sorted)
            if (c >= 0 && c < (int)out.size()) out.erase(out.begin() + c);
        return out;
    });
}

void copyToClipboard(const std::string &text)
{
    GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(cb, text.c_str(), (gint)text.size());
}

void showRowContextMenu(CsvGtkState *st, GdkEventButton *event)
{
    GtkTreePath *path = nullptr;
    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(st->grid->treeView()),
        (gint)event->x, (gint)event->y, &path, nullptr, nullptr, nullptr);
    int clickedRow = path ? gtk_tree_path_get_indices(path)[0] : -1;
    if (path) gtk_tree_path_free(path);

    auto data = st->grid->rowData();

    // The real selection range (via GtkTreeSelection directly, not
    // GtkEditableGridWidget's own API -- it doesn't expose this, but the
    // underlying GtkTreeView is directly reachable), so "N rows" actions
    // reflect an actual multi-row selection rather than just "the row
    // that was right-clicked".
    GList *selRows = gtk_tree_selection_get_selected_rows(
        gtk_tree_view_get_selection(GTK_TREE_VIEW(st->grid->treeView())), nullptr);
    int minRow = -1, maxRow = -1, numRows = 0;
    for (GList *l = selRows; l; l = l->next) {
        int idx = gtk_tree_path_get_indices(static_cast<GtkTreePath *>(l->data))[0];
        if (minRow < 0 || idx < minRow) minRow = idx;
        if (idx > maxRow) maxRow = idx;
        ++numRows;
    }
    g_list_free_full(selRows, (GDestroyNotify)gtk_tree_path_free);
    // No selection -- fall back to the clicked row, same as before.
    if (numRows == 0 && clickedRow >= 0) { minRow = maxRow = clickedRow; numRows = 1; }

    GtkWidget *menu = gtk_menu_new();
    auto addItem = [&](const std::string &label, std::function<void()> action) -> GtkWidget * {
        GtkWidget *item = gtk_menu_item_new_with_label(label.c_str());
        auto *cb = new std::function<void()>(std::move(action));
        g_signal_connect_data(item, "activate", G_CALLBACK(+[](GtkMenuItem *, gpointer d) {
            (*static_cast<std::function<void()> *>(d))();
        }), cb, +[](gpointer d, GClosure *) { delete static_cast<std::function<void()> *>(d); }, (GConnectFlags)0);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        return item;
    };
    auto addSeparator = [&]() { gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new()); };

    if (clickedRow >= 0 && clickedRow < (int)data.size()) {
        addItem("Copy Row", [st, clickedRow]() {
            copyToClipboard(joinRowPlain(st->grid->rowData()[clickedRow], '\t'));
        });
        addSeparator();
    }
    addItem("Copy Selection as TSV", [st]() { st->grid->copySelection('\t'); });
    addItem("Copy Selection as CSV", [st]() { st->grid->copySelection(','); });
    addSeparator();
    if (numRows > 0) {
        addItem(numRows == 1 ? "Delete Row" : "Delete " + std::to_string(numRows) + " Rows",
            [st]() { st->grid->deleteSelectedRows(); }); // already N-aware: reads the current selection itself
        addSeparator();
        std::string suffix = numRows == 1 ? "" : (" (" + std::to_string(numRows) + ")");
        addItem("Insert " + std::to_string(numRows) + " Row" + (numRows == 1 ? "" : "s") + " Above" + suffix,
            [st, numRows, minRow]() { st->grid->insertRows(numRows, minRow); });
        addItem("Insert " + std::to_string(numRows) + " Row" + (numRows == 1 ? "" : "s") + " Below" + suffix,
            [st, numRows, maxRow]() { st->grid->insertRows(numRows, maxRow + 1); });
    }

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
}

void showColumnContextMenu(CsvGtkState *st, int col, GdkEventButton *event)
{
    // If the clicked column is part of a multi-column selection, operate
    // on the whole selection; otherwise fall back to just the clicked
    // column (right-click without a prior header click/selection).
    std::set<int> cols = st->selectedColumns.count(col) ? st->selectedColumns : std::set<int>{col};
    int numCols = (int)cols.size();
    int lo = *cols.begin(), hi = *cols.rbegin();

    GtkWidget *menu = gtk_menu_new();
    auto addItem = [&](const std::string &label, std::function<void()> action) -> GtkWidget * {
        GtkWidget *item = gtk_menu_item_new_with_label(label.c_str());
        auto *cb = new std::function<void()>(std::move(action));
        g_signal_connect_data(item, "activate", G_CALLBACK(+[](GtkMenuItem *, gpointer d) {
            (*static_cast<std::function<void()> *>(d))();
        }), cb, +[](gpointer d, GClosure *) { delete static_cast<std::function<void()> *>(d); }, (GConnectFlags)0);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        return item;
    };

    std::string suffix = numCols == 1 ? "" : (" (" + std::to_string(numCols) + ")");
    addItem("Insert " + std::to_string(numCols) + " Column" + (numCols == 1 ? "" : "s") + " Left" + suffix,
        [st, numCols, lo]() { insertColumnsAt(st, lo, numCols); });
    addItem("Insert " + std::to_string(numCols) + " Column" + (numCols == 1 ? "" : "s") + " Right" + suffix,
        [st, numCols, hi]() { insertColumnsAt(st, hi + 1, numCols); });
    if (st->grid->columnCount() > numCols) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        addItem(numCols == 1 ? "Delete Column" : "Delete " + std::to_string(numCols) + " Columns",
            [st, cols]() { deleteColumns(st, cols); });
    }

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
}

gboolean onGridButtonPress(GtkWidget *, GdkEventButton *event, gpointer userData)
{
    if (event->button != GDK_BUTTON_SECONDARY) return FALSE;
    auto *st = static_cast<CsvGtkState *>(userData);
    showRowContextMenu(st, event);
    return TRUE;
}

// Attaches right-click handlers to the tree view body (row menu) and to
// every column header's own button widget (column menu) -- must be
// re-called every time st->grid is replaced with a new
// GtkEditableGridWidget instance (reload, column insert/delete), since
// each new grid has fresh GtkTreeViewColumn/button objects.
void attachContextMenus(CsvGtkState *st)
{
    // Preprocessor macro-expansion of G_CALLBACK()/g_signal_connect_data()
    // doesn't understand C++ template angle brackets -- a raw,
    // un-parenthesized comma inside a std::pair<A, B> template argument
    // list used directly as a macro argument gets misparsed as an extra
    // macro argument. Using an alias sidesteps that entirely (same fix
    // as the one in wlx/sourceview/src/EditorWidget.cpp's case-conversion menu).
    using ColCtx = std::pair<CsvGtkState *, int>;

    g_signal_connect(st->grid->treeView(), "button-press-event", G_CALLBACK(onGridButtonPress), st);

    // Column 0 is the row-number gutter -- not a real data column, no
    // column-operations menu on it.
    for (int c = 0; c < st->grid->columnCount(); ++c) {
        GtkTreeViewColumn *tvc = gtk_tree_view_get_column(GTK_TREE_VIEW(st->grid->treeView()), c + 1);
        GtkWidget *button = tvc ? gtk_tree_view_column_get_button(tvc) : nullptr;
        if (!button) continue;
        auto *colCtx = new ColCtx(st, c);
        g_signal_connect_data(button, "button-press-event", G_CALLBACK(+[](GtkWidget *, GdkEventButton *event, gpointer data) -> gboolean {
            if (event->button != GDK_BUTTON_SECONDARY) return FALSE;
            auto *ctx = static_cast<ColCtx *>(data);
            showColumnContextMenu(ctx->first, ctx->second, event);
            return TRUE;
        }), colCtx, +[](gpointer d, GClosure *) { delete static_cast<ColCtx *>(d); }, (GConnectFlags)0);
    }
}

void saveFile(CsvGtkState *st, const std::string &path, char separator)
{
    if (path.empty() || !st->grid) return;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return;

    int colCount = st->grid->columnCount();

    if (st->firstLineAsHeader) {
        std::vector<std::string> escaped;
        for (int c = 0; c < colCount; ++c) {
            GtkTreeViewColumn *col = gtk_tree_view_get_column(GTK_TREE_VIEW(st->grid->treeView()), c + 1);
            const char *title = col ? gtk_tree_view_column_get_title(col) : "";
            escaped.push_back(CsvCore::escapeField(title ? title : "", separator,
                                                    c < (int)st->columnWasQuoted.size() && st->columnWasQuoted[c]));
        }
        out << CsvCore::joinRow(escaped, separator) << "\n";
    }

    for (const auto &row : st->grid->rowData()) {
        std::vector<std::string> escaped;
        for (int c = 0; c < colCount; ++c) {
            escaped.push_back(CsvCore::escapeField(c < (int)row.size() ? row[c] : "", separator,
                                                    c < (int)st->columnWasQuoted.size() && st->columnWasQuoted[c]));
        }
        out << CsvCore::joinRow(escaped, separator) << "\n";
    }
}

void onSaveClicked(CsvGtkState *st)
{
    saveFile(st, st->currentFile, st->separator);
    updateDirtyLabel(st, false);
}

// Swaps the Save As dialog's proposed filename extension to match whichever
// format filter (CSV/TSV) the user just picked -- previously the typed/
// pre-filled name kept whatever extension it started with regardless of
// filter choice, so saving as TSV via the CSV-suffixed default name (or
// vice versa) silently produced a mismatched extension.
struct SaveAsFilterCtx { GtkFileFilter *csvFilter; GtkFileFilter *tsvFilter; };

void onSaveAsFilterChanged(GObject *chooserObj, GParamSpec *, gpointer data)
{
    auto *ctx = static_cast<SaveAsFilterCtx *>(data);
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(chooserObj);
    GtkFileFilter *chosen = gtk_file_chooser_get_filter(chooser);
    const char *newExt = (chosen == ctx->tsvFilter) ? ".tsv" : ".csv";

    gchar *currentName = gtk_file_chooser_get_current_name(chooser);
    if (!currentName) return;
    std::string name = currentName;
    g_free(currentName);

    if (endsWithNoCase(name, ".csv") || endsWithNoCase(name, ".tsv"))
        name = name.substr(0, name.size() - 4);
    name += newExt;
    gtk_file_chooser_set_current_name(chooser, name.c_str());
}

void onSaveAsClicked(CsvGtkState *st)
{
    GtkWidget *toplevel = gtk_widget_get_toplevel(st->root);
    GtkWidget *dlg = gtk_file_chooser_dialog_new("Save As",
        GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    if (!st->currentFile.empty())
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dlg), st->currentFile.c_str());

    GtkFileFilter *csvFilter = gtk_file_filter_new();
    gtk_file_filter_set_name(csvFilter, "CSV - Comma Separated (*.csv)");
    gtk_file_filter_add_pattern(csvFilter, "*.csv");
    GtkFileFilter *tsvFilter = gtk_file_filter_new();
    gtk_file_filter_set_name(tsvFilter, "TSV - Tab Separated (*.tsv)");
    gtk_file_filter_add_pattern(tsvFilter, "*.tsv");
    if (st->separator == '\t') {
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), tsvFilter);
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), csvFilter);
    } else {
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), csvFilter);
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), tsvFilter);
    }

    SaveAsFilterCtx filterCtx{csvFilter, tsvFilter};
    g_signal_connect(dlg, "notify::filter", G_CALLBACK(onSaveAsFilterChanged), &filterCtx);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        GtkFileFilter *chosen = gtk_file_chooser_get_filter(GTK_FILE_CHOOSER(dlg));
        char sep = (chosen == tsvFilter) ? '\t' : ',';
        saveFile(st, filename, sep);
        st->currentFile = filename;
        st->separator = sep;
        updateDirtyLabel(st, false);
        if (st->showingText) updateTextView(st);
        g_free(filename);
    }
    gtk_widget_destroy(dlg);
}

void showSeparatorMismatchDialog(CsvGtkState *st)
{
    bool isCsvExt = endsWithNoCase(st->currentFile, ".csv");
    bool isTsvExt = endsWithNoCase(st->currentFile, ".tsv");
    if (!((isCsvExt && st->separator == '\t') || (isTsvExt && st->separator == ','))) return;

    const char *msg = isCsvExt
        ? "This .csv file appears to use tab separators instead of commas."
        : "This .tsv file appears to use comma separators instead of tabs.";
    GtkWidget *toplevel = gtk_widget_get_toplevel(st->root);
    GtkWidget *dlg = gtk_message_dialog_new(GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr,
        GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "%s", msg);
    gtk_window_set_title(GTK_WINDOW(dlg), "Separator Mismatch");
    gtk_dialog_add_button(GTK_DIALOG(dlg), "Ignore", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dlg), "Fix Separator", 1);
    gtk_dialog_add_button(GTK_DIALOG(dlg), "Rename Extension", 2);
    int response = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);

    if (response == 1) {
        // Rewrite the raw bytes on disk, swapping the separator outside quotes.
        char oldSep = st->separator;
        char newSep = isCsvExt ? ',' : '\t';
        std::string raw = readFile(st->currentFile);
        bool inQuote = false;
        for (char &ch : raw) {
            if (ch == '"') inQuote = !inQuote;
            else if (!inQuote && ch == oldSep) ch = newSep;
        }
        std::ofstream out(st->currentFile, std::ios::binary | std::ios::trunc);
        out << raw;
        out.close();
        loadFile(st, st->currentFile);
    } else if (response == 2) {
        std::string newPath = st->currentFile.substr(0, st->currentFile.find_last_of('.')) +
                               (isCsvExt ? ".tsv" : ".csv");
        if (std::rename(st->currentFile.c_str(), newPath.c_str()) == 0)
            loadFile(st, newPath);
    }
}

// --- Print (monospace, tab-joined rows paginated by line count) ---

struct PrintCtx {
    std::vector<std::string> lines;
    int linesPerPage = 1;
};

void onPrintBeginPrint(GtkPrintOperation *op, GtkPrintContext *ctx, gpointer data)
{
    auto *pc = static_cast<PrintCtx *>(data);
    double pageHeight = gtk_print_context_get_height(ctx);
    PangoLayout *layout = gtk_print_context_create_pango_layout(ctx);
    pango_layout_set_font_description(layout, pango_font_description_from_string("Monospace 9"));
    pango_layout_set_text(layout, "Ag", -1);
    int lineHeightPx;
    pango_layout_get_pixel_size(layout, nullptr, &lineHeightPx);
    g_object_unref(layout);
    pc->linesPerPage = std::max(1, (int)(pageHeight / std::max(1, lineHeightPx)));
    int pages = ((int)pc->lines.size() + pc->linesPerPage - 1) / pc->linesPerPage;
    gtk_print_operation_set_n_pages(op, std::max(1, pages));
}

void onPrintDrawPage(GtkPrintOperation *, GtkPrintContext *ctx, gint pageNr, gpointer data)
{
    auto *pc = static_cast<PrintCtx *>(data);
    cairo_t *cr = gtk_print_context_get_cairo_context(ctx);
    PangoLayout *layout = gtk_print_context_create_pango_layout(ctx);
    pango_layout_set_font_description(layout, pango_font_description_from_string("Monospace 9"));

    int start = pageNr * pc->linesPerPage;
    int end = std::min((int)pc->lines.size(), start + pc->linesPerPage);
    double y = 0;
    for (int i = start; i < end; ++i) {
        pango_layout_set_text(layout, pc->lines[i].c_str(), -1);
        cairo_move_to(cr, 0, y);
        pango_cairo_show_layout(cr, layout);
        int h;
        pango_layout_get_pixel_size(layout, nullptr, &h);
        y += h;
    }
    g_object_unref(layout);
}

void onPrintClicked(CsvGtkState *st)
{
    if (!st->grid) return;
    auto pc = std::make_unique<PrintCtx>();
    if (st->firstLineAsHeader) {
        std::vector<std::string> titles;
        for (int c = 0; c < st->grid->columnCount(); ++c) {
            GtkTreeViewColumn *col = gtk_tree_view_get_column(GTK_TREE_VIEW(st->grid->treeView()), c + 1);
            const char *title = col ? gtk_tree_view_column_get_title(col) : "";
            titles.push_back(title ? title : "");
        }
        pc->lines.push_back(joinRowPlain(titles, '\t'));
    }
    for (const auto &row : st->grid->rowData())
        pc->lines.push_back(joinRowPlain(row, '\t'));

    GtkPrintOperation *op = gtk_print_operation_new();
    g_signal_connect(op, "begin-print", G_CALLBACK(onPrintBeginPrint), pc.get());
    g_signal_connect(op, "draw-page", G_CALLBACK(onPrintDrawPage), pc.get());
    GtkWidget *toplevel = gtk_widget_get_toplevel(st->root);
    gtk_print_operation_run(op, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
        GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr, nullptr);
    g_object_unref(op);
}

void onOpenExternallyClicked(CsvGtkState *st)
{
    if (st->currentFile.empty()) return;
    GtkWidget *toplevel = gtk_widget_get_toplevel(st->root);
    auto showError = [&](const std::string &msg) {
        GtkWidget *dlg = gtk_message_dialog_new(GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr,
            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Could not open file externally.\n\n%s", msg.c_str());
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
    };

    // g_app_info_launch_default_for_uri() reports success as soon as it
    // has spawned SOMETHING -- confirmed live that it returns TRUE with no
    // GError even when the resolved default-app command doesn't actually
    // exist (a stale/uninstalled default-app association), because the
    // exec() failure happens in the spawned process, not synchronously
    // here. Resolving the GAppInfo ourselves first and checking its
    // executable is actually on PATH catches that case explicitly instead
    // of silently doing nothing.
    gboolean uncertain = FALSE;
    gchar *contentType = g_content_type_guess(st->currentFile.c_str(), nullptr, 0, &uncertain);
    GAppInfo *appInfo = contentType ? g_app_info_get_default_for_type(contentType, FALSE) : nullptr;
    if (contentType) g_free(contentType);

    if (!appInfo) {
        showError("No default application is associated with this file type.");
        return;
    }
    const char *executable = g_app_info_get_executable(appInfo);
    gchar *resolvedPath = executable ? g_find_program_in_path(executable) : nullptr;
    if (!resolvedPath) {
        showError(std::string("The associated application (\"") + (executable ? executable : "?") +
                   "\") is not installed or not on PATH.");
        g_object_unref(appInfo);
        return;
    }
    g_free(resolvedPath);

    GError *error = nullptr;
    GList *files = g_list_append(nullptr, g_file_new_for_path(st->currentFile.c_str()));
    bool ok = g_app_info_launch(appInfo, files, nullptr, &error);
    g_list_free_full(files, g_object_unref);
    g_object_unref(appInfo);
    if (!ok) {
        showError(error ? error->message : "Unknown error.");
        if (error) g_error_free(error);
    }
}

void onToggleShowText(CsvGtkState *st, bool active)
{
    st->showingText = active;
    if (!st->grid) return;
    if (active) {
        updateTextView(st);
        gtk_widget_hide(st->grid->widget());
        // gtk_widget_show_all() is NOT the fix here despite the name: when
        // no_show_all is set on a widget, show_all() skips that widget
        // ENTIRELY -- including when called directly on it, not just when
        // recursing into it as a descendant -- so the previous
        // gtk_widget_show_all(st->textScroll) was a complete no-op and
        // textScroll (and the GtkTextView inside it, which as a result had
        // never once been individually shown since creation) stayed
        // invisible. gtk_widget_show() ignores no_show_all entirely, so
        // show both the container and its child explicitly instead.
        gtk_widget_show(st->textScroll);
        gtk_widget_show(st->textView);
        if (st->findPanel->isPanelVisible()) st->findPanel->showPanel(false);
    } else {
        gtk_widget_hide(st->textScroll);
        gtk_widget_show(st->grid->widget());
    }
}

void onToggleWordWrap(CsvGtkState *st, bool active)
{
    st->wordWrap = active;
    // The grid is the primary view -- wrapping only the optional Show Text
    // panel (which is hidden by default) is why this button read as doing
    // nothing at all.
    if (st->grid)
        st->grid->setWordWrap(active);
    if (st->textView)
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(st->textView), active ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
}

// --- Find/Replace matching, honoring the scope combo (All Cells / Current Column) ---

bool cellMatches(const std::string &text, const std::string &query, bool matchCase, bool entireCell, bool useRegex)
{
    if (query.empty()) return false;
    if (useRegex) {
        try {
            auto flags = std::regex::ECMAScript;
            if (!matchCase) flags |= std::regex::icase;
            std::regex re(query, flags);
            return entireCell ? std::regex_match(text, re) : std::regex_search(text, re);
        } catch (...) {
            return false;
        }
    }
    std::string t = text, q = query;
    if (!matchCase) {
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
        std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return std::tolower(c); });
    }
    return entireCell ? (t == q) : (t.find(q) != std::string::npos);
}

void doFind(CsvGtkState *st, bool forward)
{
    if (!st->grid) return;
    std::string query = st->findPanel->findText();
    if (query.empty()) return;

    auto data = st->grid->rowData();
    int rows = (int)data.size();
    int cols = st->grid->columnCount();
    if (rows == 0 || cols == 0) return;

    bool matchCase = st->findPanel->matchCase();
    bool entireCell = st->findPanel->matchEntireCell();
    bool useRegex = st->findPanel->useRegex();
    std::string scope = st->findPanel->currentScope();

    int total = rows * cols;
    int cur = st->findRow * cols + st->findCol;

    for (int step = 1; step <= total; ++step) {
        int idx = forward ? (cur + step) % total : ((cur - step) % total + total) % total;
        int r = idx / cols, c = idx % cols;
        if (scope == "Current Column" && c != st->findCol) continue;
        if (cellMatches(data[r][c], query, matchCase, entireCell, useRegex)) {
            st->findRow = r; st->findCol = c;
            st->grid->selectCell(r, c);
            st->findPanel->setStatusText("Match found");
            return;
        }
    }
    st->findPanel->setStatusText("No matches");
}

void doReplace(CsvGtkState *st)
{
    if (!st->grid) return;
    std::string current = st->grid->cellValue(st->findRow, st->findCol);
    std::string query = st->findPanel->findText();
    if (cellMatches(current, query, st->findPanel->matchCase(), st->findPanel->matchEntireCell(), st->findPanel->useRegex())) {
        st->grid->setCellValue(st->findRow, st->findCol, st->findPanel->replaceText());
    }
    doFind(st, true);
}

void doReplaceAll(CsvGtkState *st)
{
    if (!st->grid) return;
    std::string query = st->findPanel->findText();
    std::string replacement = st->findPanel->replaceText();
    bool matchCase = st->findPanel->matchCase();
    bool entireCell = st->findPanel->matchEntireCell();
    bool useRegex = st->findPanel->useRegex();
    std::string scope = st->findPanel->currentScope();

    auto data = st->grid->rowData();
    int count = 0;
    for (int r = 0; r < (int)data.size(); ++r) {
        for (int c = 0; c < st->grid->columnCount(); ++c) {
            if (scope == "Current Column" && c != st->findCol) continue;
            if (cellMatches(data[r][c], query, matchCase, entireCell, useRegex)) {
                st->grid->setCellValue(r, c, replacement);
                ++count;
            }
        }
    }
    st->findPanel->setStatusText(std::to_string(count) + " replaced");
}

void destroyState(gpointer data)
{
    delete static_cast<CsvGtkState *>(data);
}

} // namespace

extern "C" {

HWND DCPCALL ListLoad(HWND ParentWin, char *FileToLoad, int ShowFlags)
{
    GtkWidget *parent = GTK_WIDGET(ParentWin);
    auto *st = new CsvGtkState();

    st->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    // gtk_container_add() doesn't register the child the way GtkLayout
    // expects: DC's ResizeWindow later calls gtk_layout_move() on this
    // widget, which asserts the parent is exactly this GtkLayout -- only
    // gtk_layout_put() sets that up.
    gtk_layout_put(GTK_LAYOUT(parent), st->root, 0, 0);

    // Placeholder tree view just so FocusManager has a primaryView to
    // construct with — replaced immediately once loadFile() builds the
    // real grid with the correct column count.
    GtkWidget *placeholder = gtk_tree_view_new();
    st->fm = std::make_unique<GtkFocusManager>(st->root, placeholder);

    st->toolbar = std::make_unique<GtkPluginToolBar>(st->fm.get());

    st->dirtyLabel = gtk_label_new("✓");
    gtk_widget_set_margin_start(st->dirtyLabel, 4);
    gtk_widget_set_margin_end(st->dirtyLabel, 4);
    gtk_box_pack_start(GTK_BOX(st->toolbar->widget()), st->dirtyLabel, FALSE, FALSE, 0);

    st->toolbar->addToolAction("Save", "document-save-symbolic", [st]() { onSaveClicked(st); });
    st->toolbar->addToolAction("Save As...", "document-save-as-symbolic", [st]() { onSaveAsClicked(st); });
    st->undoBtn = st->toolbar->addToolAction("Undo", "edit-undo-symbolic", [st]() {
        st->fm->undo();
        refreshUndoRedoSensitivity(st);
    });
    st->redoBtn = st->toolbar->addToolAction("Redo", "edit-redo-symbolic", [st]() {
        st->fm->redo();
        refreshUndoRedoSensitivity(st);
    });
    st->toolbar->addToolAction("Print", "document-print-symbolic", [st]() { onPrintClicked(st); });
    st->toolbar->addToolAction("Reload", "view-refresh-symbolic", [st]() {
        if (!st->currentFile.empty()) loadFile(st, st->currentFile);
    });
    st->headerToggle = st->toolbar->addToggleAction("Header Row", "view-list-symbolic", true, [st](bool active) {
        st->firstLineAsHeader = active;
        if (!st->currentFile.empty()) loadFile(st, st->currentFile);
    });
    st->findToggle = st->toolbar->addToggleAction("Find/Replace", "edit-find-replace-symbolic", false, [st](bool active) {
        st->findPanel->showPanel(active);
    });
    st->textToggle = st->toolbar->addToggleAction("Show Text", "view-reveal-symbolic", false, [st](bool active) {
        onToggleShowText(st, active);
    });
    st->wrapToggle = st->toolbar->addToggleAction("Word Wrap", "format-text-wrap-symbolic", false, [st](bool active) {
        onToggleWordWrap(st, active);
    });
    st->toolbar->addToolAction("Open Externally", "document-open-symbolic", [st]() { onOpenExternallyClicked(st); });
    gtk_box_pack_start(GTK_BOX(st->root), st->toolbar->widget(), FALSE, FALSE, 0);

    // Real grid gets built by loadFile() below (needs the file's actual
    // column count); pack a temporary empty box as its future slot, plus
    // the (initially hidden) plain-text view used by "Show Text".
    st->gridSlot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(st->gridSlot, TRUE);
    gtk_box_pack_start(GTK_BOX(st->root), st->gridSlot, TRUE, TRUE, 0);
    st->grid = std::make_unique<GtkEditableGridWidget>(1, st->fm.get());
    addRowNumberColumn(st->grid->treeView());
    gtk_box_pack_start(GTK_BOX(st->gridSlot), st->grid->widget(), TRUE, TRUE, 0);

    st->textView = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(st->textView), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(st->textView), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(st->textView), GTK_WRAP_NONE);
    st->textScroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(st->textScroll), st->textView);
    gtk_widget_set_vexpand(st->textScroll, TRUE);
    gtk_box_pack_start(GTK_BOX(st->gridSlot), st->textScroll, TRUE, TRUE, 0);
    gtk_widget_set_no_show_all(st->textScroll, TRUE); // stays hidden until "Show Text" is toggled

    st->findPanel = std::make_unique<GtkScopedFindReplacePanel>(st->fm.get());
    st->findPanel->setScopes({"All Cells", "Current Column"});
    st->findPanel->onFindRequested = [st](bool forward) { doFind(st, forward); };
    st->findPanel->onReplaceRequested = [st]() { doReplace(st); };
    st->findPanel->onReplaceAllRequested = [st]() { doReplaceAll(st); };
    gtk_box_pack_start(GTK_BOX(st->root), st->findPanel->widget(), FALSE, FALSE, 0);

    st->fm->enableUndoShortcuts();
    st->fm->registerShortcut(GDK_KEY_f, GDK_CONTROL_MASK, GtkFocusManager::Always, [st]() {
        bool nowVisible = !st->findPanel->isPanelVisible();
        // Toggling the button (rather than calling showPanel() directly)
        // keeps its pressed-state in sync with the panel's actual
        // visibility, since addToggleAction's callback is what calls
        // showPanel().
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->findToggle), nowVisible);
        return true;
    });
    st->fm->registerShortcut(GDK_KEY_s, GDK_CONTROL_MASK, GtkFocusManager::Always, [st]() {
        onSaveClicked(st);
        return true;
    });
    // Matches Qt6's toolbar/FocusManager shortcuts (csvview/src/plugin.cpp
    // setupToolbar/setupFindReplace) -- these buttons already existed here
    // but had no keybinding at all.
    st->fm->registerShortcut(GDK_KEY_r, GDK_CONTROL_MASK, GtkFocusManager::Always, [st]() {
        bool nowVisible = !st->findPanel->isPanelVisible();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->findToggle), nowVisible);
        return true;
    });
    st->fm->registerShortcut(GDK_KEY_s, static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK),
        GtkFocusManager::Always, [st]() { onSaveAsClicked(st); return true; });
    st->fm->registerShortcut(GDK_KEY_p, GDK_CONTROL_MASK, GtkFocusManager::Always,
        [st]() { onPrintClicked(st); return true; });
    st->fm->registerShortcut(GDK_KEY_F5, static_cast<GdkModifierType>(0), GtkFocusManager::Always, [st]() {
        if (!st->currentFile.empty()) loadFile(st, st->currentFile);
        return true;
    });
    // Ctrl+O (Qt's binding) is unusable here -- see structview_gtk3.cpp's
    // identical comment for why (DC's own Ctrl+O TAction accelerator wins
    // regardless of our key snooper). Ctrl+E doesn't collide with any of
    // DC's default hotkeys.
    st->fm->registerShortcut(GDK_KEY_e, GDK_CONTROL_MASK, GtkFocusManager::Always,
        [st]() { onOpenExternallyClicked(st); return true; });

    g_object_set_data_full(G_OBJECT(st->root), "csv-state", st, destroyState);

    gtk_widget_show_all(st->root);
    gtk_widget_hide(st->textScroll);
    st->findPanel->showPanel(false); // hidden until Ctrl+F / toolbar toggle
    loadFile(st, std::string(FileToLoad));
    refreshUndoRedoSensitivity(st);
    showSeparatorMismatchDialog(st);

    return reinterpret_cast<HWND>(st->root);
}

void DCPCALL ListCloseWindow(HWND ListWin)
{
    GtkWidget *root = GTK_WIDGET(ListWin);
    if (root) gtk_widget_destroy(root);
}

int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter)
{
    GtkWidget *root = GTK_WIDGET(ListWin);
    auto *st = static_cast<CsvGtkState *>(g_object_get_data(G_OBJECT(root), "csv-state"));
    if (!st) return LISTPLUGIN_ERROR;

    if (Command == lc_newparams) {
        loadFile(st, st->currentFile);
        return LISTPLUGIN_OK;
    }
    if (Command == lc_copy) {
        st->grid->copySelection(st->separator);
        return LISTPLUGIN_OK;
    }
    return LISTPLUGIN_ERROR;
}

int DCPCALL ListSearchText(HWND, char *, int)
{
    return LISTPLUGIN_ERROR;
}

void DCPCALL ListGetDetectString(char *DetectString, int maxlen)
{
    snprintf(DetectString, maxlen - 1, "(EXT=\"CSV\" | EXT=\"TSV\") & SIZE<50000000");
}

void DCPCALL ListSetDefaultParams(ListDefaultParamStruct *)
{
}

} // extern "C"
