// GTK3 UI for dbview: a GtkTreeView schema list (tables/views) on the
// left, a GtkEditableGridWidget-backed grid on the right, a Submit/Revert
// toolbar, and (for SQL engines) a query console -- all on top of the
// Qt-free DbEngineCore/KeyValueEngineCoreBase engines.

#include "core/DbEngineCore.h"
#include "wlxbase_gtk/GtkFocusManager.h"
#include "wlxbase_gtk/GtkEditableGridWidget.h"
#include "wlxbase_gtk/GtkPluginToolBar.h"
#include "wlxbase_gtk/GtkFindReplacePanel.h"

#include <gtk/gtk.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "wlxplugin.h"

#define EXPORT __attribute__((visibility("default")))

using namespace GtkWlPlugin;

namespace {

struct DbViewState {
    GtkWidget *root = nullptr;
    GtkWidget *paned = nullptr;
    GtkWidget *treeView = nullptr;
    GtkTreeStore *treeStore = nullptr;
    GtkWidget *rightBox = nullptr;
    GtkWidget *gridContainer = nullptr;
    GtkWidget *statusLabel = nullptr;
    GtkWidget *sqlEntry = nullptr;

    std::unique_ptr<DbEngineCore> engine;
    std::unique_ptr<GtkFocusManager> focusManager;
    std::unique_ptr<GtkEditableGridWidget> grid;
    std::unique_ptr<GtkPluginToolBar> toolbar;
    std::unique_ptr<GtkFindReplacePanel> findPanel;

    GtkWidget *submitBtn = nullptr;
    GtkWidget *revertBtn = nullptr;
    GtkWidget *findToggle = nullptr;
    bool wordWrap = false;
    bool gridLines = true;

    // Cosmetic-only display toggle for binary cells, mirroring Qt's
    // KeyValueModel::setHexMode -- purely a GTK-side display swap via
    // grid->setCellValue(), no engine state involved (matches how the Qt
    // model's own hex flag never touches the underlying DB either).
    std::set<std::pair<int, int>> hexCells;

    std::string filepath;
};

enum { COL_NAME = 0, N_COLS };

void onToggleWordWrap(DbViewState *st, bool active);
void onToggleGridLines(DbViewState *st, bool active);
gboolean onGridButtonPress(GtkWidget *, GdkEventButton *event, gpointer userData);

void updateStatus(DbViewState *st) {
    if (!st->statusLabel || !st->engine) return;
    std::string text = st->engine->engineName() + " - " + st->engine->currentTableName() +
        " (" + std::to_string(st->engine->fetchedRowCount()) + "/" + std::to_string(st->engine->rowCount()) + " rows)";
    if (st->engine->isReadOnly()) text += " [read-only]";
    gtk_label_set_text(GTK_LABEL(st->statusLabel), text.c_str());
}

// Pulls rows [alreadyRead, engine->fetchedRowCount()) out of the engine
// and appends them to the grid -- called once after selectTable()'s first
// chunk, and again each time fetchMoreIfNearBottom() pulls in another
// chunk. Rows already materialized in the engine's own chunk cache are
// converted to grid rows here; the engine itself never holds more than a
// few chunks' worth of rows in memory for a table the UI hasn't scrolled
// through yet (see DbEngineCore::fetchMore()).
void appendEngineRowsToGrid(DbViewState *st, int fromRow) {
    int toRow = st->engine->fetchedRowCount();
    if (toRow <= fromRow) return;
    std::vector<std::vector<std::string>> rows;
    rows.reserve(toRow - fromRow);
    for (int r = fromRow; r < toRow; r++) {
        std::vector<std::string> row;
        row.reserve(st->engine->columnCount());
        for (int c = 0; c < st->engine->columnCount(); c++)
            row.push_back(st->engine->cellText(r, c));
        rows.push_back(std::move(row));
    }
    st->grid->appendRows(rows);
}

void loadGridFromEngine(DbViewState *st) {
    if (st->grid) {
        gtk_container_remove(GTK_CONTAINER(st->gridContainer), st->grid->widget());
        st->grid.reset();
    }
    st->hexCells.clear(); // stale row/col indices from before this reload
    int cols = std::max(1, st->engine->columnCount());
    st->grid = std::make_unique<GtkWlPlugin::GtkEditableGridWidget>(cols, st->focusManager.get());
    g_signal_connect(st->grid->treeView(), "button-press-event", G_CALLBACK(onGridButtonPress), st);
    for (int c = 0; c < st->engine->columnCount(); c++) {
        st->grid->setColumnTitle(c, st->engine->columnName(c));
        // Editability is effectively per-table, not per-cell, for every
        // engine we have (a row either all has a rowid/DB_KEY or none do);
        // row 0 (if any rows are loaded) is representative.
        bool editable = st->engine->fetchedRowCount() > 0 && st->engine->cellEditable(0, c);
        st->grid->setColumnEditable(c, editable);
    }

    appendEngineRowsToGrid(st, 0);

    // Word Wrap / Grid Lines are per-GtkTreeView toggle state on the toolbar,
    // but each table switch tears down and rebuilds the grid's own
    // GtkTreeView -- reapply the persisted toggle state to the new one.
    onToggleWordWrap(st, st->wordWrap);
    onToggleGridLines(st, st->gridLines);

    gtk_box_pack_start(GTK_BOX(st->gridContainer), st->grid->widget(), TRUE, TRUE, 0);
    gtk_widget_show_all(st->grid->widget());

    // Trigger fetchMore() as the user scrolls near the bottom of what's
    // currently loaded, appending only the newly-fetched rows -- mirrors
    // QAbstractItemModel::canFetchMore()/fetchMore() on the Qt side.
    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(st->grid->widget()));
    g_signal_connect(vadj, "value-changed", G_CALLBACK(+[](GtkAdjustment *adj, gpointer data) {
        auto *st2 = (DbViewState *)data;
        if (!st2->engine || !st2->engine->canFetchMore()) return;
        double value = gtk_adjustment_get_value(adj);
        double upper = gtk_adjustment_get_upper(adj);
        double page = gtk_adjustment_get_page_size(adj);
        if (value + page >= upper - page * 0.5) { // within half a page of the bottom
            int before = st2->engine->fetchedRowCount();
            st2->engine->fetchMore();
            appendEngineRowsToGrid(st2, before);
        }
    }), st);
}

void selectTable(DbViewState *st, const std::string &name) {
    if (!st->engine->selectTable(name)) return;
    loadGridFromEngine(st);
    updateStatus(st);
}

void populateTree(DbViewState *st) {
    gtk_tree_store_clear(st->treeStore);
    if (!st->engine) return;

    GtkTreeIter tablesIter;
    gtk_tree_store_append(st->treeStore, &tablesIter, nullptr);
    gtk_tree_store_set(st->treeStore, &tablesIter, COL_NAME, "Tables", -1);
    for (auto &t : st->engine->tableNames()) {
        GtkTreeIter it;
        gtk_tree_store_append(st->treeStore, &it, &tablesIter);
        gtk_tree_store_set(st->treeStore, &it, COL_NAME, t.c_str(), -1);
    }

    auto views = st->engine->viewNames();
    if (!views.empty()) {
        GtkTreeIter viewsIter;
        gtk_tree_store_append(st->treeStore, &viewsIter, nullptr);
        gtk_tree_store_set(st->treeStore, &viewsIter, COL_NAME, "Views", -1);
        for (auto &v : views) {
            GtkTreeIter it;
            gtk_tree_store_append(st->treeStore, &it, &viewsIter);
            gtk_tree_store_set(st->treeStore, &it, COL_NAME, v.c_str(), -1);
        }
    }

    gtk_tree_view_expand_all(GTK_TREE_VIEW(st->treeView));
}

void onTreeSelectionChanged(GtkTreeSelection *sel, gpointer data) {
    auto *st = (DbViewState *)data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;
    if (gtk_tree_model_iter_has_child(model, &iter)) return; // category node ("Tables"/"Views")

    gchar *name = nullptr;
    gtk_tree_model_get(model, &iter, COL_NAME, &name, -1);
    if (name) { selectTable(st, name); g_free(name); }
}

void onSubmitClicked(DbViewState *st) {
    // submitAll() may reopen the cursor internally (Firebird: statements
    // must be freed before COMMIT), which resets the engine's own fetched-
    // rows cache -- reload the grid from scratch to match, same as revert.
    if (st->engine->submitAll()) { loadGridFromEngine(st); updateStatus(st); }
}
void onRevertClicked(DbViewState *st) {
    if (st->engine->revertAll()) { loadGridFromEngine(st); updateStatus(st); }
}

void onToggleWordWrap(DbViewState *st, bool active) {
    st->wordWrap = active;
    if (!st->grid) return;
    // Was an ad-hoc copy here with a hardcoded 300px wrap width and no
    // row-height invalidation, so text wrapped but the rows stayed one line
    // tall and hid everything past the first line. GtkEditableGridWidget now
    // owns this (sized to the real column width, hyphen-free, and it
    // re-autosizes rows), so defer to it rather than keeping a second,
    // subtly-different implementation.
    st->grid->setWordWrap(active);
}

void onToggleGridLines(DbViewState *st, bool active) {
    st->gridLines = active;
    if (st->grid) st->grid->setShowGrid(active);
}

void onExportTableData(DbViewState *st) {
    if (!st->grid) return;
    GtkWidget *toplevel = gtk_widget_get_toplevel(st->root);
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Export Table Data",
        GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Export", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),
        (st->engine->currentTableName() + ".csv").c_str());
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        std::ofstream out(path);
        if (out) {
            int ncols = st->grid->columnCount();
            for (int c = 0; c < ncols; c++) {
                if (c) out << ',';
                out << '"' << st->engine->columnName(c) << '"';
            }
            out << '\n';
            for (auto &row : st->grid->rowData()) {
                for (int c = 0; c < (int)row.size(); c++) {
                    if (c) out << ',';
                    std::string cell = row[c];
                    std::string escaped;
                    for (char ch : cell) { if (ch == '"') escaped += '"'; escaped += ch; }
                    out << '"' << escaped << '"';
                }
                out << '\n';
            }
        }
        g_free(path);
    }
    gtk_widget_destroy(dialog);
}

bool dbCellMatches(const std::string &text, const std::string &query, bool matchCase) {
    if (query.empty()) return false;
    if (matchCase) return text.find(query) != std::string::npos;
    std::string t = text, q = query;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return std::tolower(c); });
    return t.find(q) != std::string::npos;
}

void doFind(DbViewState *st, bool forward) {
    if (!st->grid) return;
    std::string query = st->findPanel->findText();
    if (query.empty()) return;
    bool matchCase = st->findPanel->matchCase();

    auto rows = st->grid->rowData();
    int nrows = (int)rows.size(), ncols = st->grid->columnCount();
    if (nrows == 0 || ncols == 0) return;
    int total = nrows * ncols;
    for (int step = 1; step <= total; ++step) {
        int idx = forward ? step - 1 : total - step;
        int r = idx / ncols, c = idx % ncols;
        if (r >= (int)rows.size() || c >= (int)rows[r].size()) continue;
        if (dbCellMatches(rows[r][c], query, matchCase)) {
            st->grid->selectCell(r, c);
            st->findPanel->setStatusText("Match found");
            return;
        }
    }
    st->findPanel->setStatusText("No matches");
}
void onRunSqlClicked(GtkButton *, gpointer data) {
    auto *st = (DbViewState *)data;
    if (!st->sqlEntry) return;
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(st->sqlEntry));
    if (!text || !*text) return;
    if (st->engine->selectQuery(text)) {
        loadGridFromEngine(st);
        gtk_label_set_text(GTK_LABEL(st->statusLabel), (st->engine->engineName() + " - query result").c_str());
    } else {
        gtk_label_set_text(GTK_LABEL(st->statusLabel), ("Query error: " + st->engine->lastError()).c_str());
    }
}

std::string copySelectionAsText(DbViewState *st) {
    if (!st->grid) return {};
    st->grid->copySelection('\t');
    GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gchar *text = gtk_clipboard_wait_for_text(cb);
    std::string result = text ? text : "";
    if (text) g_free(text);
    return result;
}

// ── Cell context menu (right-click on a grid cell) ────────────────────
// Matches Qt6's DbViewWidget::setupContextMenu item-for-item: Toggle Hex
// View (binary cells only), Save Cell to File (BLOB Export), Load File
// into Cell (BLOB Import, hidden when the engine is read-only). GTK
// previously had NO grid context menu at all.

std::string bytesToHex(const std::vector<uint8_t> &data) {
    static const char *hexDigits = "0123456789ABCDEF";
    std::string out;
    out.reserve(data.size() * 3);
    for (size_t i = 0; i < data.size(); ++i) {
        if (i) out += ' ';
        out += hexDigits[(data[i] >> 4) & 0xF];
        out += hexDigits[data[i] & 0xF];
    }
    return out;
}

void onToggleHexView(GtkMenuItem *, gpointer userDataRaw) {
    auto *ctx = static_cast<std::pair<DbViewState *, std::pair<int, int>> *>(userDataRaw);
    DbViewState *st = ctx->first;
    int row = ctx->second.first, col = ctx->second.second;
    auto key = std::make_pair(row, col);
    bool nowHex = st->hexCells.find(key) == st->hexCells.end();
    if (nowHex) {
        st->hexCells.insert(key);
        st->grid->setCellValue(row, col, bytesToHex(st->engine->cellRawBytes(row, col)));
    } else {
        st->hexCells.erase(key);
        st->grid->setCellValue(row, col, st->engine->cellText(row, col));
    }
}

void onSaveCellToFile(GtkMenuItem *, gpointer userDataRaw) {
    auto *ctx = static_cast<std::pair<DbViewState *, std::pair<int, int>> *>(userDataRaw);
    DbViewState *st = ctx->first;
    int row = ctx->second.first, col = ctx->second.second;

    GtkWidget *toplevel = gtk_widget_get_toplevel(st->root);
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Save BLOB Value",
        GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        auto bytes = st->engine->cellRawBytes(row, col);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (out) out.write(reinterpret_cast<const char *>(bytes.data()), (std::streamsize)bytes.size());
        g_free(path);
    }
    gtk_widget_destroy(dialog);
}

void onLoadFileIntoCell(GtkMenuItem *, gpointer userDataRaw) {
    auto *ctx = static_cast<std::pair<DbViewState *, std::pair<int, int>> *>(userDataRaw);
    DbViewState *st = ctx->first;
    int row = ctx->second.first, col = ctx->second.second;

    GtkWidget *toplevel = gtk_widget_get_toplevel(st->root);
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Load BLOB Value",
        GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            // DbEngineCore::setCellText() takes std::string, not raw bytes --
            // there is no setCellRawBytes() in the shared Qt-free core (only
            // cellRawBytes() for reading). A std::string can hold arbitrary
            // bytes including embedded NULs; this only round-trips correctly
            // for engine backends whose setCellText() writes the string's
            // full length rather than treating it as a NUL-terminated
            // C-string internally.
            //
            // setCellText() failing (e.g. cellEditable() is false for this
            // column/row, or the underlying UPDATE errors) used to fail
            // completely silently -- reported live as "after selecting the
            // file, it did nothing", with no way to tell whether the import
            // was attempted at all. Surface it.
            bool ok = st->engine->setCellText(row, col, ss.str());
            if (!ok) {
                GtkWidget *err = gtk_message_dialog_new(GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr,
                    GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                    "Failed to load file into cell.\n\n%s",
                    st->engine->lastError().empty() ? "This cell may not be editable." : st->engine->lastError().c_str());
                gtk_dialog_run(GTK_DIALOG(err));
                gtk_widget_destroy(err);
            }
            loadGridFromEngine(st);
        } else {
            GtkWidget *err = gtk_message_dialog_new(GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr,
                GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Could not open file:\n%s", path);
            gtk_dialog_run(GTK_DIALOG(err));
            gtk_widget_destroy(err);
        }
        g_free(path);
    }
    gtk_widget_destroy(dialog);
}

gboolean onGridButtonPress(GtkWidget *, GdkEventButton *event, gpointer userData) {
    if (event->button != GDK_BUTTON_SECONDARY) return FALSE;
    auto *st = static_cast<DbViewState *>(userData);
    if (!st->grid || !st->engine) return FALSE;

    GtkTreePath *path = nullptr;
    GtkTreeViewColumn *tvColumn = nullptr;
    if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(st->grid->treeView()),
            (gint)event->x, (gint)event->y, &path, &tvColumn, nullptr, nullptr))
        return FALSE;
    int row = gtk_tree_path_get_indices(path)[0];
    gtk_tree_path_free(path);

    GList *columns = gtk_tree_view_get_columns(GTK_TREE_VIEW(st->grid->treeView()));
    int col = (int)g_list_index(columns, tvColumn);
    g_list_free(columns);
    if (row < 0 || col < 0 || row >= (int)st->grid->rowData().size() || col >= st->grid->columnCount())
        return FALSE;

    GtkWidget *menu = gtk_menu_new();
    // Leaked deliberately, same lifetime pattern as csvview's per-item
    // context: freed by the GClosure notify below once the menu item (and
    // therefore this callback data) is destroyed.
    auto addItem = [&](const std::string &label, GCallback cb) {
        GtkWidget *item = gtk_menu_item_new_with_label(label.c_str());
        auto *ctx = new std::pair<DbViewState *, std::pair<int, int>>(st, std::make_pair(row, col));
        g_signal_connect_data(item, "activate", cb, ctx,
            +[](gpointer d, GClosure *) { delete static_cast<std::pair<DbViewState *, std::pair<int, int>> *>(d); },
            (GConnectFlags)0);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        return item;
    };

    if (st->engine->cellIsBinary(row, col)) {
        bool isHex = st->hexCells.count({row, col}) != 0;
        addItem(isHex ? "Show Plain Text" : "Toggle Hex View", G_CALLBACK(onToggleHexView));
    }
    addItem("Save Cell to File (BLOB Export)...", G_CALLBACK(onSaveCellToFile));
    if (!st->engine->isReadOnly()) {
        addItem("Load File into Cell (BLOB Import)...", G_CALLBACK(onLoadFileIntoCell));
    }

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
    return TRUE;
}

void destroyState(GtkWidget *, gpointer data) { delete (DbViewState *)data; }

} // namespace

extern "C" {

EXPORT HWND DCPCALL ListLoad(HWND ParentWin, char *FileToLoad, int ShowFlags) {
    std::string filepath = FileToLoad;
    auto engine = DbEngineCore::createForFile(filepath);
    if (!engine) return nullptr;

    auto tables = engine->tableNames();
    if (tables.empty()) return nullptr;
    if (!engine->selectTable(tables[0])) return nullptr;

    auto *st = new DbViewState();
    st->filepath = filepath;
    st->engine = std::move(engine);

    st->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    // gtk_layout_put() (not gtk_container_add()) is required here: DC's
    // ResizeWindow later calls gtk_layout_move() on this widget, which
    // asserts the widget's parent is exactly this GtkLayout.
    gtk_layout_put(GTK_LAYOUT(ParentWin), st->root, 0, 0);

    GtkWidget *toolbarRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    st->statusLabel = gtk_label_new("");
    gtk_box_pack_end(GTK_BOX(toolbarRow), st->statusLabel, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(st->root), toolbarRow, FALSE, FALSE, 2);

    st->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(st->root), st->paned, TRUE, TRUE, 0);

    st->treeStore = gtk_tree_store_new(N_COLS, G_TYPE_STRING);
    st->treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(st->treeStore));
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Schema", renderer, "text", COL_NAME, nullptr);
    gtk_tree_view_append_column(GTK_TREE_VIEW(st->treeView), col);
    GtkWidget *treeScroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(treeScroll), st->treeView);
    gtk_widget_set_size_request(treeScroll, 200, -1);
    gtk_paned_pack1(GTK_PANED(st->paned), treeScroll, FALSE, TRUE);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(st->treeView)), "changed",
                      G_CALLBACK(onTreeSelectionChanged), st);

    st->rightBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_paned_pack2(GTK_PANED(st->paned), st->rightBox, TRUE, TRUE);

    if (st->engine->supportsSqlConsole()) {
        GtkWidget *sqlBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        st->sqlEntry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(st->sqlEntry), "SQL query...");
        gtk_box_pack_start(GTK_BOX(sqlBar), st->sqlEntry, TRUE, TRUE, 2);
        GtkWidget *runBtn = gtk_button_new_with_label("Run");
        g_signal_connect(runBtn, "clicked", G_CALLBACK(onRunSqlClicked), st);
        gtk_box_pack_start(GTK_BOX(sqlBar), runBtn, FALSE, FALSE, 2);
        gtk_box_pack_start(GTK_BOX(st->rightBox), sqlBar, FALSE, FALSE, 2);
    }

    st->gridContainer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(st->rightBox), st->gridContainer, TRUE, TRUE, 0);

    st->focusManager = std::make_unique<GtkFocusManager>(st->root, st->treeView);

    st->toolbar = std::make_unique<GtkPluginToolBar>(st->focusManager.get());
    if (st->engine->supportsSubmitRevert()) {
        st->submitBtn = st->toolbar->addToolAction("Commit", "document-save-symbolic",
            [st]() { onSubmitClicked(st); });
        st->revertBtn = st->toolbar->addToolAction("Revert", "edit-undo-symbolic",
            [st]() { onRevertClicked(st); });
        if (st->engine->isReadOnly()) {
            gtk_widget_set_sensitive(st->submitBtn, FALSE);
            gtk_widget_set_sensitive(st->revertBtn, FALSE);
        }
        st->toolbar->addSeparator();
    }
    st->toolbar->addToggleAction("Word Wrap", "format-text-wrap-symbolic", false,
        [st](bool active) { onToggleWordWrap(st, active); });
    st->toolbar->addToggleAction("Grid Lines", "view-grid-symbolic", true,
        [st](bool active) { onToggleGridLines(st, active); });
    st->findToggle = st->toolbar->addToggleAction("Find", "edit-find-symbolic", false,
        [st](bool active) { st->findPanel->showPanel(active); });
    st->toolbar->addToolAction("Export", "document-send-symbolic",
        [st]() { onExportTableData(st); });
    gtk_box_pack_start(GTK_BOX(toolbarRow), st->toolbar->widget(), TRUE, TRUE, 0);
    gtk_box_reorder_child(GTK_BOX(toolbarRow), st->toolbar->widget(), 0);

    st->findPanel = std::make_unique<GtkFindReplacePanel>(st->focusManager.get());
    st->findPanel->setReplaceEnabled(false);
    st->findPanel->onFindRequested = [st](bool forward) { doFind(st, forward); };
    gtk_box_pack_start(GTK_BOX(st->root), st->findPanel->widget(), FALSE, FALSE, 0);
    gtk_box_reorder_child(GTK_BOX(st->root), st->findPanel->widget(), 1);

    // Matches Qt6's toolbar/FocusManager shortcuts (DbViewWidget's
    // constructor/setupToolbar) -- dbview_gtk3 constructed a
    // GtkFocusManager but never registered a single shortcut on it, unlike
    // every other GTK plugin here.
    if (st->engine->supportsSubmitRevert() && !st->engine->isReadOnly()) {
        st->focusManager->registerShortcut(GDK_KEY_s, GDK_CONTROL_MASK, GtkFocusManager::Always,
            [st]() { onSubmitClicked(st); return true; });
        // Real, granular per-edit undo/redo -- NOT the whole-transaction
        // "Revert" toolbar button. GtkEditableGridWidget already pushes an
        // UndoCommand onto this SAME GtkFocusManager on every cell edit
        // (see its onCellEdited()/pushUndo() calls), so this was already
        // fully wired and working the moment the grid was constructed with
        // st->focusManager -- Ctrl+Z/Ctrl+Shift+Z/Ctrl+Y just needed to be
        // registered to reach it, exactly matching "undo/redo should work
        // until it's committed" (committing clears the grid's own dirty/
        // undo state via loadGridFromEngine()'s fresh GtkEditableGridWidget).
        // Previously Ctrl+Z was bound to onRevertClicked() (roll back the
        // WHOLE pending transaction) instead, which isn't the same thing;
        // that action remains available via the Revert toolbar button.
        st->focusManager->enableUndoShortcuts();
    }
    st->focusManager->registerShortcut(GDK_KEY_f, GDK_CONTROL_MASK, GtkFocusManager::Always, [st]() {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->findToggle), !st->findPanel->isPanelVisible());
        return true;
    });

    loadGridFromEngine(st);

    g_signal_connect(st->root, "destroy", G_CALLBACK(destroyState), st);
    g_object_set_data(G_OBJECT(st->root), "__dbview_state_ptr", st);

    populateTree(st);
    updateStatus(st);

    gtk_widget_show_all(st->root);
    return (HWND)st->root;
}

EXPORT void DCPCALL ListCloseWindow(HWND ListWin) {
    GtkWidget *w = (GtkWidget *)ListWin;
    if (w) gtk_widget_destroy(w);
}

EXPORT void DCPCALL ListGetDetectString(char *DetectString, int maxlen) {
    snprintf(DetectString, maxlen - 1,
        "EXT=\"SQLITE\" | EXT=\"SQLITE3\" | EXT=\"DB\" | EXT=\"DB3\" | "
        "EXT=\"DUCKDB\" | EXT=\"PARQUET\" | EXT=\"PQ\" | EXT=\"FDB\" | "
        "EXT=\"LMDB\" | EXT=\"BDB\" | EXT=\"MDB\" | EXT=\"ACCDB\"");
}

EXPORT int DCPCALL ListSearchText(HWND ListWin, char *SearchString, int SearchParameter) {
    GtkWidget *w = (GtkWidget *)ListWin;
    if (!w) return LISTPLUGIN_ERROR;
    auto *st = (DbViewState *)g_object_get_data(G_OBJECT(w), "__dbview_state_ptr");
    if (!st || !st->grid) return LISTPLUGIN_ERROR;

    std::string needle = SearchString ? SearchString : "";
    bool matchCase = SearchParameter & lcs_matchcase;
    bool backward = SearchParameter & lcs_backwards;

    auto rows = st->grid->rowData();
    int nrows = (int)rows.size();
    int ncols = st->grid->columnCount();
    if (nrows == 0 || ncols == 0) return LISTPLUGIN_ERROR;

    auto toLower = [](std::string s) { for (auto &c : s) c = (char)tolower((unsigned char)c); return s; };
    std::string needleCmp = matchCase ? needle : toLower(needle);

    int total = nrows * ncols;
    for (int i = 0; i < total; ++i) {
        int idx = backward ? (total - 1 - i) : i;
        int r = idx / ncols, c = idx % ncols;
        if (r >= (int)rows.size() || c >= (int)rows[r].size()) continue;
        std::string cell = matchCase ? rows[r][c] : toLower(rows[r][c]);
        if (cell.find(needleCmp) != std::string::npos) {
            st->grid->selectCell(r, c);
            return LISTPLUGIN_OK;
        }
    }
    return LISTPLUGIN_ERROR;
}

EXPORT int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter) {
    GtkWidget *w = (GtkWidget *)ListWin;
    if (!w) return LISTPLUGIN_ERROR;
    auto *st = (DbViewState *)g_object_get_data(G_OBJECT(w), "__dbview_state_ptr");
    if (!st) return LISTPLUGIN_ERROR;

    switch (Command) {
        case lc_copy: {
            std::string text = copySelectionAsText(st);
            return text.empty() ? LISTPLUGIN_ERROR : LISTPLUGIN_OK;
        }
        case lc_focus:
            if (Parameter != 0) gtk_widget_grab_focus(st->treeView);
            return LISTPLUGIN_OK;
        case lc_newparams:
            return LISTPLUGIN_OK;
        default:
            return LISTPLUGIN_ERROR;
    }
}

} // extern "C"
