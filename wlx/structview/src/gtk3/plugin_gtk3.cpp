// GTK3 UI for structview: a GtkTreeStore document tree on the left, and a
// Grid/Text GtkNotebook on the right, built on wlxbase_gtk's
// GtkFocusManager/GtkEditableGridWidget/GtkPluginToolBar/
// GtkFindReplacePanel, mirroring StructViewWidget's Qt6 toolbar (Save,
// Save As, Undo, Redo, Print, Reload, Show Text, Word Wrap, Open
// Externally, Find) over the same Qt-free structview_core
// (DocumentNode/TextFormatEngine + the 6 format engines).

#include "core/DocumentModel.h"
#include "wlxbase_gtk/GtkFocusManager.h"
#include "wlxbase_gtk/GtkEditableGridWidget.h"
#include "wlxbase_gtk/GtkPluginToolBar.h"
#include "wlxbase_gtk/GtkFindReplacePanel.h"

#include <gtk/gtk.h>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

#include "wlxplugin.h"

#define EXPORT __attribute__((visibility("default")))

using namespace GtkWlPlugin;

namespace {

struct StructViewState {
    GtkWidget *root = nullptr;
    GtkWidget *paned = nullptr;
    GtkWidget *treeView = nullptr;
    GtkTreeStore *treeStore = nullptr;
    GtkWidget *notebook = nullptr;
    GtkWidget *gridContainer = nullptr; // holds the current GtkEditableGridWidget's widget()
    GtkWidget *textView = nullptr;

    std::unique_ptr<TextFormatEngine> engine;
    std::unique_ptr<GtkFocusManager> focusManager;
    std::unique_ptr<GtkEditableGridWidget> grid;
    std::unique_ptr<GtkPluginToolBar> toolbar;
    std::unique_ptr<GtkFindReplacePanel> findPanel;

    GtkWidget *dirtyLabel = nullptr;
    GtkWidget *undoBtn = nullptr;
    GtkWidget *redoBtn = nullptr;
    GtkWidget *findToggle = nullptr;
    GtkWidget *formatLabel = nullptr;
    GtkWidget *rowCountLabel = nullptr;

    std::string filepath;
    DocumentNode *currentNode = nullptr;
    bool dirty = false;
    bool wordWrap = false;

    // GtkTreeStore column 0 = display name, column 1 = DocumentNode* (as gpointer)
};

enum { COL_NAME = 0, COL_NODE = 1, N_COLS };

void updateDirtyIndicator(StructViewState *st) {
    if (st->dirtyLabel) gtk_label_set_text(GTK_LABEL(st->dirtyLabel), st->dirty ? "●" : "✓");
    if (st->undoBtn) gtk_widget_set_sensitive(st->undoBtn, st->focusManager->canUndo());
    if (st->redoBtn) gtk_widget_set_sensitive(st->redoBtn, st->focusManager->canRedo());
}

// Mirrors PluginStatusBar's format-name + row-count fields (Qt6). There's
// no GTK equivalent of PluginStatusBar in wlxbase_gtk yet, so this is a
// simple two-label bar local to structview rather than a new shared widget.
void updateStatusBar(StructViewState *st) {
    if (st->formatLabel && st->engine)
        gtk_label_set_text(GTK_LABEL(st->formatLabel), st->engine->formatName().c_str());
    if (st->rowCountLabel) {
        int rows = st->grid ? st->grid->rowCount() : 0;
        int cols = st->grid ? st->grid->columnCount() : 0;
        gtk_label_set_text(GTK_LABEL(st->rowCountLabel),
            (std::to_string(rows) + " rows, " + std::to_string(cols) + " cols").c_str());
    }
}

void updateTextTab(StructViewState *st) {
    if (!st->engine || !st->textView) return;
    // Sync current grid edits back into the tree before regenerating the
    // Text tab, matching the Qt side's syncGridToNode()+updateTextTab().
    if (st->grid && st->currentNode) st->currentNode->rows = st->grid->rowData();
    std::string text = st->engine->serialize();
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(st->textView));
    gtk_text_buffer_set_text(buf, text.c_str(), -1);
}

void showNode(StructViewState *st, DocumentNode *node);

void rebuildGrid(StructViewState *st, DocumentNode *node) {
    // Tear down the old grid widget (if any) and build a fresh one sized
    // for this node's column count -- GtkEditableGridWidget's column count
    // is fixed at construction, and different tree nodes have different
    // shapes, so we recreate it per-selection (mirrors the Qt side
    // resetting the QStandardItemModel's column count per node).
    if (st->grid) {
        gtk_container_remove(GTK_CONTAINER(st->gridContainer), st->grid->widget());
        st->grid.reset();
    }

    int cols = std::max(1, (int)node->columnNames.size());
    st->grid = std::make_unique<GtkEditableGridWidget>(cols, st->focusManager.get());
    for (size_t c = 0; c < node->columnNames.size(); c++)
        st->grid->setColumnTitle((int)c, node->columnNames[c]);
    st->grid->setRowData(node->rows);
    st->grid->setDirtyChangedCallback([st](bool d) {
        if (d) st->dirty = true;
        updateDirtyIndicator(st);
        updateStatusBar(st); // row count may have changed (row insert/delete)
    });

    gtk_box_pack_start(GTK_BOX(st->gridContainer), st->grid->widget(), TRUE, TRUE, 0);
    gtk_widget_show_all(st->grid->widget());
    updateStatusBar(st);
}

void showNode(StructViewState *st, DocumentNode *node) {
    st->currentNode = node;
    rebuildGrid(st, node);
    // Text tab always mirrors the whole document's raw text (matches the
    // Qt side's read-only Text tab), not just this node.
}

void populateTreeNode(GtkTreeStore *store, GtkTreeIter *parentIter, DocumentNode *node) {
    GtkTreeIter iter;
    gtk_tree_store_append(store, &iter, parentIter);
    gtk_tree_store_set(store, &iter, COL_NAME, node->name.c_str(), COL_NODE, (gpointer)node, -1);
    for (auto *child : node->children)
        populateTreeNode(store, &iter, child);
}

void populateTree(StructViewState *st) {
    gtk_tree_store_clear(st->treeStore);
    if (!st->engine || !st->engine->rootNode()) return;
    populateTreeNode(st->treeStore, nullptr, st->engine->rootNode());

    GtkTreeIter first;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(st->treeStore), &first)) {
        GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(st->treeStore), &first);
        gtk_tree_view_expand_row(GTK_TREE_VIEW(st->treeView), path, FALSE);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(st->treeView), path, nullptr, FALSE);
        gtk_tree_path_free(path);
        showNode(st, st->engine->rootNode());
    }
}

void onTreeSelectionChanged(GtkTreeSelection *sel, gpointer data) {
    auto *st = (StructViewState *)data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;
    gpointer nodePtr = nullptr;
    gtk_tree_model_get(model, &iter, COL_NODE, &nodePtr, -1);
    if (nodePtr) showNode(st, (DocumentNode *)nodePtr);
}

bool doSave(StructViewState *st, const std::string &path) {
    if (!st->engine || path.empty()) return false;
    // Flush the currently-visible grid's edits back into the tree before
    // serializing (matches the Qt side's syncGridToNode()).
    if (st->grid && st->currentNode) st->currentNode->rows = st->grid->rowData();
    std::string data = st->engine->serialize();
    if (data.empty()) return false;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << data;
    st->dirty = false;
    updateDirtyIndicator(st);
    return true;
}

void reloadFile(StructViewState *st) {
    std::ifstream f(st->filepath, std::ios::binary);
    if (!f) return;
    std::ostringstream ss; ss << f.rdbuf();
    if (!st->engine->parse(ss.str())) return;
    st->dirty = false;
    populateTree(st);
    updateDirtyIndicator(st);
    st->focusManager->clearUndoStack();
}

void onOpenExternally(StructViewState *st) {
    if (st->filepath.empty()) return;
    GtkWidget *toplevel = gtk_widget_get_toplevel(st->root);
    auto showError = [&](const std::string &msg) {
        GtkWidget *dlg = gtk_message_dialog_new(GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr,
            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Could not open file externally.\n\n%s", msg.c_str());
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
    };

    // g_app_info_launch_default_for_uri() reports success as soon as it
    // has spawned SOMETHING -- see csvview_gtk3.cpp's identical fix for
    // why (a stale/uninstalled default-app association's exec() failure
    // happens in the spawned process, not synchronously here). Resolving
    // the GAppInfo ourselves and checking its executable is actually on
    // PATH catches that case explicitly instead of silently doing nothing.
    gboolean uncertain = FALSE;
    gchar *contentType = g_content_type_guess(st->filepath.c_str(), nullptr, 0, &uncertain);
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
    GList *files = g_list_append(nullptr, g_file_new_for_path(st->filepath.c_str()));
    bool ok = g_app_info_launch(appInfo, files, nullptr, &error);
    g_list_free_full(files, g_object_unref);
    g_object_unref(appInfo);
    if (!ok) {
        showError(error ? error->message : "Unknown error.");
        if (error) g_error_free(error);
    }
}

void showSaveAsDialog(StructViewState *st) {
    GtkWidget *toplevel = gtk_widget_get_toplevel(st->root);
    GtkWidget *dlg = gtk_file_chooser_dialog_new("Save As",
        GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    if (!st->filepath.empty())
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dlg), st->filepath.c_str());
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (doSave(st, filename)) st->filepath = filename;
        g_free(filename);
    }
    gtk_widget_destroy(dlg);
}

// --- Print: same monospace-paginated-text approach as csvview's Print ---

struct PrintCtx { std::vector<std::string> lines; int linesPerPage = 1; };

void onPrintBeginPrint(GtkPrintOperation *op, GtkPrintContext *ctx, gpointer userData) {
    auto *pc = static_cast<PrintCtx *>(userData);
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

void onPrintDrawPage(GtkPrintOperation *, GtkPrintContext *ctx, gint pageNr, gpointer userData) {
    auto *pc = static_cast<PrintCtx *>(userData);
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

std::vector<std::string> splitLines(const std::string &s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t pos = s.find('\n', start);
        if (pos == std::string::npos) { if (start < s.size()) out.push_back(s.substr(start)); break; }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

void onPrint(StructViewState *st) {
    if (st->grid && st->currentNode) st->currentNode->rows = st->grid->rowData();
    if (!st->engine) return;
    auto pc = std::make_unique<PrintCtx>();
    pc->lines = splitLines(st->engine->serialize());

    GtkPrintOperation *op = gtk_print_operation_new();
    g_signal_connect(op, "begin-print", G_CALLBACK(onPrintBeginPrint), pc.get());
    g_signal_connect(op, "draw-page", G_CALLBACK(onPrintDrawPage), pc.get());
    GtkWidget *toplevel = gtk_widget_get_toplevel(st->root);
    gtk_print_operation_run(op, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
        GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr, nullptr);
    g_object_unref(op);
}

// --- Find (single-direction, grid cells) ---

bool cellMatches(const std::string &text, const std::string &query, bool matchCase) {
    if (query.empty()) return false;
    if (matchCase) return text.find(query) != std::string::npos;
    std::string t = text, q = query;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return std::tolower(c); });
    return t.find(q) != std::string::npos;
}

void doFind(StructViewState *st, bool forward) {
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
        if (cellMatches(rows[r][c], query, matchCase)) {
            st->grid->selectCell(r, c);
            st->findPanel->setStatusText("Match found");
            return;
        }
    }
    st->findPanel->setStatusText("No matches");
}

void setupToolbar(StructViewState *st) {
    st->toolbar = std::make_unique<GtkPluginToolBar>(st->focusManager.get());

    st->dirtyLabel = gtk_label_new("");
    gtk_widget_set_margin_start(st->dirtyLabel, 4);
    gtk_widget_set_margin_end(st->dirtyLabel, 4);
    gtk_box_pack_start(GTK_BOX(st->toolbar->widget()), st->dirtyLabel, FALSE, FALSE, 0);

    st->toolbar->addToolAction("Save", "document-save-symbolic", [st]() { doSave(st, st->filepath); });
    st->toolbar->addToolAction("Save As...", "document-save-as-symbolic", [st]() { showSaveAsDialog(st); });
    st->undoBtn = st->toolbar->addToolAction("Undo", "edit-undo-symbolic", [st]() {
        st->focusManager->undo();
        updateDirtyIndicator(st);
    });
    st->redoBtn = st->toolbar->addToolAction("Redo", "edit-redo-symbolic", [st]() {
        st->focusManager->redo();
        updateDirtyIndicator(st);
    });
    st->toolbar->addToolAction("Print", "document-print-symbolic", [st]() { onPrint(st); });
    st->toolbar->addToolAction("Reload", "view-refresh-symbolic", [st]() { reloadFile(st); });
    st->toolbar->addToggleAction("Show Text", "view-reveal-symbolic", false, [st](bool active) {
        if (active) updateTextTab(st);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(st->notebook), active ? 1 : 0);
    });
    st->toolbar->addToggleAction("Word Wrap", "format-text-wrap-symbolic", false, [st](bool active) {
        st->wordWrap = active;
        // The grid is the primary view -- wrapping only the optional Show Text
        // panel (which is hidden by default) is why this button read as doing
        // nothing at all.
        if (st->grid) st->grid->setWordWrap(active);
        if (st->textView) gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(st->textView), active ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
    });
    st->toolbar->addToolAction("Open Externally", "document-open-symbolic", [st]() { onOpenExternally(st); });
    st->findToggle = st->toolbar->addToggleAction("Find", "edit-find-symbolic", false, [st](bool active) {
        st->findPanel->showPanel(active);
    });
}

std::string copySelectionAsText(StructViewState *st) {
    if (!st->grid) return {};
    // GtkEditableGridWidget::copySelection() writes to the GTK clipboard
    // directly; read it back so ListSendCommand(lc_copy) can hand DC the
    // same text without duplicating the tab-join logic.
    st->grid->copySelection('\t');
    GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gchar *text = gtk_clipboard_wait_for_text(cb);
    std::string result = text ? text : "";
    if (text) g_free(text);
    return result;
}

void destroyState(GtkWidget *, gpointer data) { delete (StructViewState *)data; }

} // namespace

extern "C" {

EXPORT HWND DCPCALL ListLoad(HWND ParentWin, char *FileToLoad, int ShowFlags) {
    std::string filepath = FileToLoad;

    auto engine = TextFormatEngine::createForFile(filepath);
    if (!engine) return nullptr;

    std::ifstream f(filepath, std::ios::binary);
    if (!f) return nullptr;
    std::ostringstream ss; ss << f.rdbuf();
    if (!engine->parse(ss.str())) return nullptr;

    auto *st = new StructViewState();
    st->filepath = filepath;
    st->engine = std::move(engine);

    st->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    // gtk_layout_put() (not gtk_container_add()) is required here: DC's
    // ResizeWindow later calls gtk_layout_move() on this widget, which
    // asserts the widget's parent is exactly this GtkLayout.
    gtk_layout_put(GTK_LAYOUT(ParentWin), st->root, 0, 0);

    // Placeholder tree view just so FocusManager has a primaryView to
    // construct with -- the real one is created and assigned below (same
    // "placeholder then swap" shape csvview/dbview use for their grids).
    GtkWidget *placeholder = gtk_tree_view_new();
    st->focusManager = std::make_unique<GtkFocusManager>(st->root, placeholder);

    setupToolbar(st);
    gtk_box_pack_start(GTK_BOX(st->root), st->toolbar->widget(), FALSE, FALSE, 0);

    // Tree | Grid+Text
    st->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(st->paned, TRUE);
    gtk_box_pack_start(GTK_BOX(st->root), st->paned, TRUE, TRUE, 0);

    st->treeStore = gtk_tree_store_new(N_COLS, G_TYPE_STRING, G_TYPE_POINTER);
    st->treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(st->treeStore));
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Document", renderer, "text", COL_NAME, nullptr);
    gtk_tree_view_append_column(GTK_TREE_VIEW(st->treeView), col);
    GtkWidget *treeScroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(treeScroll), st->treeView);
    gtk_widget_set_size_request(treeScroll, 220, -1);
    gtk_paned_pack1(GTK_PANED(st->paned), treeScroll, FALSE, TRUE);

    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(st->treeView)), "changed",
                      G_CALLBACK(onTreeSelectionChanged), st);

    st->notebook = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(st->notebook), FALSE); // driven by the "Show Text" toggle instead
    gtk_paned_pack2(GTK_PANED(st->paned), st->notebook, TRUE, TRUE);

    st->gridContainer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(st->notebook), st->gridContainer, gtk_label_new("Grid"));

    st->textView = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(st->textView), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(st->textView), TRUE);
    GtkWidget *textScroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(textScroll), st->textView);
    gtk_notebook_append_page(GTK_NOTEBOOK(st->notebook), textScroll, gtk_label_new("Text"));

    st->findPanel = std::make_unique<GtkFindReplacePanel>(st->focusManager.get());
    st->findPanel->setReplaceEnabled(false); // read-only find here, matches structview's Qt-side onFind (no replace wired)
    st->findPanel->onFindRequested = [st](bool forward) { doFind(st, forward); };
    gtk_box_pack_start(GTK_BOX(st->root), st->findPanel->widget(), FALSE, FALSE, 0);

    // Status bar: format name + row/column count, mirroring PluginStatusBar (Qt6).
    GtkWidget *statusBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(statusBar), 2);
    st->formatLabel = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(statusBar), st->formatLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(statusBar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
    st->rowCountLabel = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(statusBar), st->rowCountLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(st->root), statusBar, FALSE, FALSE, 0);

    st->focusManager->enableUndoShortcuts();
    st->focusManager->registerShortcut(GDK_KEY_s, GDK_CONTROL_MASK, GtkFocusManager::Always,
        [st]() { doSave(st, st->filepath); return true; });
    st->focusManager->registerShortcut(GDK_KEY_f, GDK_CONTROL_MASK, GtkFocusManager::Always, [st]() {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->findToggle), !st->findPanel->isPanelVisible());
        return true;
    });
    // Matches Qt6's toolbar shortcuts (StructViewWidget::setupToolbar) --
    // these buttons already existed here but had no keybinding at all.
    st->focusManager->registerShortcut(GDK_KEY_s, static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK),
        GtkFocusManager::Always, [st]() { showSaveAsDialog(st); return true; });
    st->focusManager->registerShortcut(GDK_KEY_p, GDK_CONTROL_MASK, GtkFocusManager::Always,
        [st]() { onPrint(st); return true; });
    st->focusManager->registerShortcut(GDK_KEY_F5, static_cast<GdkModifierType>(0), GtkFocusManager::Always,
        [st]() { reloadFile(st); return true; });
    // Ctrl+O (Qt's binding) is unusable here: confirmed live that DC has
    // its own Ctrl+O ("Toggle Fullscreen Console", a TAction with a real
    // menu accelerator, uglobs.pas) that fires regardless of what our own
    // key snooper does -- unlike a plain hotkey-table-only binding (e.g.
    // Ctrl+F), a TAction's accelerator appears to go through a GTK/LCL
    // code path our snooper can't preempt. Ctrl+E doesn't collide with any
    // of DC's default hotkeys.
    st->focusManager->registerShortcut(GDK_KEY_e, GDK_CONTROL_MASK, GtkFocusManager::Always,
        [st]() { onOpenExternally(st); return true; });

    g_signal_connect(st->root, "destroy", G_CALLBACK(destroyState), st);
    g_object_set_data(G_OBJECT(st->root), "__structview_state_ptr", st);

    populateTree(st);
    updateDirtyIndicator(st);

    gtk_widget_show_all(st->root);
    st->findPanel->showPanel(false);
    return (HWND)st->root;
}

EXPORT void DCPCALL ListCloseWindow(HWND ListWin) {
    GtkWidget *w = (GtkWidget *)ListWin;
    if (w) gtk_widget_destroy(w);
}

EXPORT void DCPCALL ListGetDetectString(char *DetectString, int maxlen) {
    snprintf(DetectString, maxlen - 1,
        "EXT=\"JSON\" | EXT=\"XML\" | EXT=\"INI\" | EXT=\"CBOR\" | "
        "EXT=\"YAML\" | EXT=\"YML\" | EXT=\"TOML\" | "
        "EXT=\"DESKTOP\" | EXT=\"INF\"");
}

EXPORT int DCPCALL ListSearchText(HWND ListWin, char *SearchString, int SearchParameter) {
    GtkWidget *w = (GtkWidget *)ListWin;
    if (!w) return LISTPLUGIN_ERROR;
    auto *st = (StructViewState *)g_object_get_data(G_OBJECT(w), "__structview_state_ptr");
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
    auto *st = (StructViewState *)g_object_get_data(G_OBJECT(w), "__structview_state_ptr");
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
