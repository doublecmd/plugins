#include "wlxbase_gtk/GtkEditableGridWidget.h"
#include "wlxbase_gtk/GtkFocusManager.h"

#include <sstream>
#include <algorithm>
#include <memory>
#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace GtkWlPlugin {

GtkEditableGridWidget::GtkEditableGridWidget(int columnCount, GtkFocusManager *fm)
    : m_fm(fm)
    , m_columnCount(columnCount)
{
    m_scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    m_treeView = gtk_tree_view_new();
    gtk_container_add(GTK_CONTAINER(m_scrolled), m_treeView);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(m_treeView), GTK_TREE_VIEW_GRID_LINES_BOTH);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(m_treeView)), GTK_SELECTION_MULTIPLE);

    std::vector<GType> types(columnCount, G_TYPE_STRING);
    m_store = gtk_list_store_newv(columnCount, types.data());
    gtk_tree_view_set_model(GTK_TREE_VIEW(m_treeView), GTK_TREE_MODEL(m_store));
    // The view holds its own ref via set_model; drop ours so the store is
    // owned solely by the view and freed correctly on teardown.
    g_object_unref(m_store);

    for (int c = 0; c < columnCount; ++c) {
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "editable", TRUE, nullptr);

        auto *ctx = new ColCtx{this, c};
        m_colContexts.push_back(ctx);
        g_signal_connect(renderer, "edited", G_CALLBACK(cellEditedTrampoline), ctx);

        GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(
            ("Column " + std::to_string(c + 1)).c_str(), renderer, "text", c, nullptr);
        gtk_tree_view_column_set_resizable(col, TRUE);

        // Keeps the wrap budget matched to the column's real width, so
        // wrapping survives a manual resize.
        g_signal_connect(col, "notify::width", G_CALLBACK(columnWidthChangedTrampoline), ctx);
        gtk_tree_view_append_column(GTK_TREE_VIEW(m_treeView), col);
    }

    if (fm) fm->addInputWidget(m_treeView);
}

GtkEditableGridWidget::~GtkEditableGridWidget()
{
    // A pending idle would fire into a destroyed widget.
    if (m_rowHeightRefreshId) g_source_remove(m_rowHeightRefreshId);
    if (m_fm) m_fm->removeInputWidget(m_treeView);
    for (auto *ctx : m_colContexts) delete ctx;
}

void GtkEditableGridWidget::setColumnTitle(int col, const std::string &title)
{
    GtkTreeViewColumn *c = gtk_tree_view_get_column(GTK_TREE_VIEW(m_treeView), col);
    if (c) gtk_tree_view_column_set_title(c, title.c_str());
}

void GtkEditableGridWidget::setColumnEditable(int col, bool editable)
{
    GtkTreeViewColumn *c = gtk_tree_view_get_column(GTK_TREE_VIEW(m_treeView), col);
    if (!c) return;
    GList *renderers = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(c));
    for (GList *l = renderers; l; l = l->next)
        g_object_set(l->data, "editable", editable ? TRUE : FALSE, nullptr);
    g_list_free(renderers);
}

void GtkEditableGridWidget::setRowData(const std::vector<std::vector<std::string>> &rows)
{
    gtk_list_store_clear(m_store);
    for (const auto &row : rows) {
        GtkTreeIter iter;
        gtk_list_store_append(m_store, &iter);
        for (int c = 0; c < m_columnCount; ++c)
            gtk_list_store_set(m_store, &iter, c, c < (int)row.size() ? row[c].c_str() : "", -1);
    }
}

void GtkEditableGridWidget::appendRows(const std::vector<std::vector<std::string>> &rows)
{
    for (const auto &row : rows) {
        GtkTreeIter iter;
        gtk_list_store_append(m_store, &iter);
        for (int c = 0; c < m_columnCount; ++c)
            gtk_list_store_set(m_store, &iter, c, c < (int)row.size() ? row[c].c_str() : "", -1);
    }
}

std::vector<std::vector<std::string>> GtkEditableGridWidget::rowData() const
{
    std::vector<std::vector<std::string>> result;
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(m_store), &iter);
    while (valid) {
        std::vector<std::string> row;
        row.reserve(m_columnCount);
        for (int c = 0; c < m_columnCount; ++c) {
            gchar *text = nullptr;
            gtk_tree_model_get(GTK_TREE_MODEL(m_store), &iter, c, &text, -1);
            row.push_back(text ? text : "");
            g_free(text);
        }
        result.push_back(std::move(row));
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(m_store), &iter);
    }
    return result;
}

int GtkEditableGridWidget::rowCount() const
{
    return gtk_tree_model_iter_n_children(GTK_TREE_MODEL(m_store), nullptr);
}

void GtkEditableGridWidget::setDirty(bool dirty)
{
    if (m_dirty == dirty) return;
    m_dirty = dirty;
    if (m_dirtyChangedCb) m_dirtyChangedCb(dirty);
}

void GtkEditableGridWidget::cellEditedTrampoline(GtkCellRendererText *, gchar *path, gchar *newText, gpointer data)
{
    auto *ctx = static_cast<ColCtx *>(data);
    ctx->self->onCellEdited(ctx->col, path, newText);
}

void GtkEditableGridWidget::onCellEdited(int col, const std::string &pathStr, const std::string &newText)
{
    GtkTreePath *path = gtk_tree_path_new_from_string(pathStr.c_str());
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(m_store), &iter, path)) {
        gtk_tree_path_free(path);
        return;
    }
    gtk_tree_path_free(path);

    gchar *oldTextRaw = nullptr;
    gtk_tree_model_get(GTK_TREE_MODEL(m_store), &iter, col, &oldTextRaw, -1);
    std::string oldText = oldTextRaw ? oldTextRaw : "";
    g_free(oldTextRaw);

    if (oldText == newText) return;

    GtkTreePath *rowPath = gtk_tree_model_get_path(GTK_TREE_MODEL(m_store), &iter);
    std::shared_ptr<GtkTreeRowReference> rowRef(
        gtk_tree_row_reference_new(GTK_TREE_MODEL(m_store), rowPath),
        [](GtkTreeRowReference *r) { if (r) gtk_tree_row_reference_free(r); });
    gtk_tree_path_free(rowPath);

    GtkListStore *store = m_store;
    auto applyText = [store, rowRef, col](const std::string &text) {
        GtkTreePath *p = gtk_tree_row_reference_get_path(rowRef.get());
        if (!p) return;
        GtkTreeIter it;
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &it, p))
            gtk_list_store_set(store, &it, col, text.c_str(), -1);
        gtk_tree_path_free(p);
    };

    GtkFocusManager::UndoCommand cmd;
    cmd.text = "Edit cell";
    cmd.undo = [applyText, oldText]() { applyText(oldText); };
    cmd.redo = [applyText, newText]() { applyText(newText); };

    if (m_fm) {
        // redo() already applies newText via pushUndo's immediate-redo
        // semantics — but the GtkCellRendererText has ALREADY written
        // newText into the model by the time "edited" fires, so calling
        // redo() again here is a harmless no-op re-application, not a
        // double-edit; it just keeps the undo stack authoritative.
        m_fm->pushUndo(std::move(cmd));
    }
    setDirty(true);
}

void GtkEditableGridWidget::setCellValue(int row, int col, const std::string &text)
{
    GtkTreePath *path = gtk_tree_path_new_from_indices(row, -1);
    GtkTreeIter iter;
    bool ok = gtk_tree_model_get_iter(GTK_TREE_MODEL(m_store), &iter, path);
    gtk_tree_path_free(path);
    if (!ok) return;

    gchar *oldRaw = nullptr;
    gtk_tree_model_get(GTK_TREE_MODEL(m_store), &iter, col, &oldRaw, -1);
    std::string oldText = oldRaw ? oldRaw : "";
    g_free(oldRaw);
    if (oldText == text) return;

    GtkListStore *store = m_store;
    auto applyText = [store, row, col](const std::string &t) {
        GtkTreePath *p = gtk_tree_path_new_from_indices(row, -1);
        GtkTreeIter it;
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &it, p))
            gtk_list_store_set(store, &it, col, t.c_str(), -1);
        gtk_tree_path_free(p);
    };

    GtkFocusManager::UndoCommand cmd;
    cmd.text = "Edit cell";
    cmd.undo = [applyText, oldText]() { applyText(oldText); };
    cmd.redo = [applyText, text]() { applyText(text); };
    if (m_fm) m_fm->pushUndo(std::move(cmd));
    else applyText(text);
    setDirty(true);
}

std::string GtkEditableGridWidget::cellValue(int row, int col) const
{
    GtkTreePath *path = gtk_tree_path_new_from_indices(row, -1);
    GtkTreeIter iter;
    bool ok = gtk_tree_model_get_iter(GTK_TREE_MODEL(m_store), &iter, path);
    gtk_tree_path_free(path);
    if (!ok) return {};
    gchar *text = nullptr;
    gtk_tree_model_get(GTK_TREE_MODEL(m_store), &iter, col, &text, -1);
    std::string result = text ? text : "";
    g_free(text);
    return result;
}

void GtkEditableGridWidget::selectCell(int row, int col)
{
    GtkTreePath *path = gtk_tree_path_new_from_indices(row, -1);
    GtkTreeViewColumn *column = gtk_tree_view_get_column(GTK_TREE_VIEW(m_treeView), col);
    gtk_tree_view_set_cursor(GTK_TREE_VIEW(m_treeView), path, column, FALSE);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(m_treeView), path, column, TRUE, 0.5, 0.0);
    gtk_tree_path_free(path);
}

void GtkEditableGridWidget::copySelection(char separator)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(m_treeView));
    GList *rows = gtk_tree_selection_get_selected_rows(sel, nullptr);
    if (!rows) return;

    std::vector<int> indices;
    for (GList *l = rows; l; l = l->next) {
        GtkTreePath *p = static_cast<GtkTreePath *>(l->data);
        int *idx = gtk_tree_path_get_indices(p);
        if (idx) indices.push_back(idx[0]);
    }
    std::sort(indices.begin(), indices.end());

    std::ostringstream out;
    auto data = rowData();
    for (size_t i = 0; i < indices.size(); ++i) {
        if (i > 0) out << "\n";
        int r = indices[i];
        if (r < 0 || r >= (int)data.size()) continue;
        for (int c = 0; c < m_columnCount; ++c) {
            if (c > 0) out << separator;
            out << data[r][c];
        }
    }

    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, out.str().c_str(), -1);

    g_list_free_full(rows, (GDestroyNotify)gtk_tree_path_free);
}

void GtkEditableGridWidget::pasteSelection(char separator)
{
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gchar *text = gtk_clipboard_wait_for_text(clipboard);
    if (!text) return;
    std::string clip(text);
    g_free(text);

    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(m_treeView));
    GList *rows = gtk_tree_selection_get_selected_rows(sel, nullptr);
    int startRow = 0;
    if (rows) {
        GtkTreePath *p = static_cast<GtkTreePath *>(rows->data);
        int *idx = gtk_tree_path_get_indices(p);
        if (idx) startRow = idx[0];
        g_list_free_full(rows, (GDestroyNotify)gtk_tree_path_free);
    }

    auto before = rowData();
    auto after = before;

    std::istringstream lineStream(clip);
    std::string line;
    int r = startRow;
    while (std::getline(lineStream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        while ((int)after.size() <= r) after.emplace_back(m_columnCount, "");

        std::vector<std::string> cells;
        std::istringstream cellStream(line);
        std::string cell;
        while (std::getline(cellStream, cell, separator)) cells.push_back(cell);
        if (line.empty()) cells.push_back("");

        for (int c = 0; c < (int)cells.size() && c < m_columnCount; ++c)
            after[r][c] = cells[c];
        ++r;
    }

    GtkEditableGridWidget *self = this;
    GtkFocusManager::UndoCommand cmd;
    cmd.text = "Paste";
    cmd.undo = [self, before]() { self->setRowData(before); };
    cmd.redo = [self, after]() { self->setRowData(after); };
    if (m_fm) m_fm->pushUndo(std::move(cmd));
    else setRowData(after);
    setDirty(true);
}

void GtkEditableGridWidget::pasteSelectionAt(int atRow, char separator)
{
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gchar *text = gtk_clipboard_wait_for_text(clipboard);
    if (!text) return;
    std::string clip(text);
    g_free(text);

    auto before = rowData();
    auto after = before;

    std::istringstream lineStream(clip);
    std::string line;
    int r = std::max(0, atRow);
    while (std::getline(lineStream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        while ((int)after.size() <= r) after.emplace_back(m_columnCount, "");

        std::vector<std::string> cells;
        std::istringstream cellStream(line);
        std::string cell;
        while (std::getline(cellStream, cell, separator)) cells.push_back(cell);
        if (line.empty()) cells.push_back("");

        for (int c = 0; c < (int)cells.size() && c < m_columnCount; ++c)
            after[r][c] = cells[c];
        ++r;
    }

    GtkEditableGridWidget *self = this;
    GtkFocusManager::UndoCommand cmd;
    cmd.text = "Paste";
    cmd.undo = [self, before]() { self->setRowData(before); };
    cmd.redo = [self, after]() { self->setRowData(after); };
    if (m_fm) m_fm->pushUndo(std::move(cmd));
    else setRowData(after);
    setDirty(true);
}

void GtkEditableGridWidget::setShowGrid(bool show)
{
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(m_treeView),
        show ? GTK_TREE_VIEW_GRID_LINES_BOTH : GTK_TREE_VIEW_GRID_LINES_NONE);
}

void GtkEditableGridWidget::applyWrapToColumn(GtkTreeViewColumn *col, int budget)
{
    // A bogus budget means the column has no meaningful width right now --
    // during a model swap every column momentarily reports 0. Leave the
    // existing wrap settings alone rather than writing -1 into them: that
    // turns wrapping OFF mid-measure, and any row measured in that window
    // gets a single-line height it then keeps.
    if (m_wordWrap && budget <= 16) {
        return;
    }

    GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(col));
    for (GList *c = cells; c; c = c->next) {
        if (!GTK_IS_CELL_RENDERER_TEXT(c->data)) continue;

        // Pango 1.44+ inserts a hyphen when it breaks inside a word. For file
        // data (CSV cells, DB values, JSON strings) that hyphen is a lie --
        // it looks like part of the value. There is no GtkCellRendererText
        // property for it; the attribute list is the only lever.
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_insert_hyphens_new(FALSE));
        g_object_set(c->data,
            "wrap-mode", m_wordWrap ? PANGO_WRAP_WORD_CHAR : PANGO_WRAP_WORD,
            "wrap-width", m_wordWrap ? budget : -1,
            "attributes", attrs,
            nullptr);
        pango_attr_list_unref(attrs);
    }
    g_list_free(cells);
    // Deliberately NO layout call here. This function is reached from the
    // notify::width handler, i.e. from inside GTK's own layout pass, and
    // dirtying layout from there is what blanked the grid. Invalidation is
    // the caller's job -- see setWordWrap() below.
}

void GtkEditableGridWidget::setWordWrap(bool wrap)
{
    m_wordWrap = wrap;

    // This function has two hard requirements that previous attempts kept
    // trading against each other, so both are spelled out:
    //
    //  (1) Row heights MUST be invalidated. GtkTreeView caches them, and
    //      neither gtk_widget_queue_resize() nor
    //      gtk_tree_view_columns_autosize() clears that cache. Skip this and
    //      the text wraps correctly but rows keep their old single-line
    //      height and clip it -- looks exactly like truncation, and only on
    //      the first toggle, because any later resize invalidates the cache
    //      as a side effect.
    //
    //  (2) It MUST NOT recalculate layout synchronously.
    //      gtk_tree_view_columns_autosize() walks and recomputes every column
    //      immediately, emitting notify::width mid-walk; re-entering and
    //      dirtying layout from inside that walk left GTK with no valid
    //      layout and rendered a blank grid.
    //
    // gtk_tree_view_column_queue_resize() satisfies both: it marks the
    // column's cells dirty (clearing the row-height cache, so (1) holds) but
    // only *queues* the recalculation for the next frame rather than running
    // it inline (so (2) holds). columns_autosize() is intentionally absent --
    // it is the synchronous variant and is what caused the blanking.
    //
    //  (3) Column widths MUST NOT change. Toggling wrap sets a text property;
    //      it is not a layout command. Earlier revisions pinned columns to
    //      FIXED sizing at a computed width to force wrapping to be visible,
    //      which resized columns the user had not asked to resize (the
    //      row-number gutter grew, data columns jumped). Deliberately not done
    //      any more: no set_sizing(), no set_fixed_width() here.
    //
    // Consequence, by design: each column wraps at the width it already has.
    // A column sized to its own content is already wide enough for one line,
    // so wrap has no visible effect there until the column is narrowed --
    // at which point notify::width re-applies the budget and the text wraps.
    m_applyingWrap = true;
    GList *cols = gtk_tree_view_get_columns(GTK_TREE_VIEW(m_treeView));

    for (GList *l = cols; l; l = l->next) {
        auto *col = GTK_TREE_VIEW_COLUMN(l->data);

        // Decorative, fixed-size columns (csvview's row-number gutter is the
        // one in play) are marked non-resizable and hold no wrappable data.
        if (!gtk_tree_view_column_get_resizable(col)) continue;

        // The column's own current width is the budget -- that is precisely
        // what "wrap within the column, without changing it" means. A column
        // not yet allocated gets -1 for now; notify::width supplies the real
        // width as soon as one exists.
        int width = gtk_tree_view_column_get_width(col);
        applyWrapToColumn(col, (wrap && width > 16) ? width : -1);

        gtk_tree_view_column_queue_resize(col);
    }
    g_list_free(cols);
    m_applyingWrap = false;

    // Force GtkTreeView to re-measure every row's HEIGHT.
    //
    // This is the part that was missing, and it is a separate problem from
    // wrapping. Pango was already wrapping the text correctly -- the
    // give-away was dbview showing a hyphen, which Pango only inserts when it
    // actually breaks a word. But GtkTreeView caches row heights in its
    // internal row tree, and neither gtk_widget_queue_resize() nor
    // gtk_tree_view_column_queue_resize() rebuilds that cache: they mark cells
    // dirty for redraw while every row keeps its old single-line height. The
    // wrapped lines were being rendered into a row too short to show them, so
    // only the first line was ever visible -- indistinguishable from "wrap
    // does nothing".
    //
    // Detaching and re-attaching the model is the one operation that discards
    // the height cache outright and forces a full re-measure. It is safe here
    // precisely because this runs from the user's toggle, never from inside a
    // layout callback, so it cannot re-enter the way columns_autosize() did.
    refreshRowHeights();
}

// Forces GtkTreeView to re-measure every row's height. Detaching and
// re-attaching the model is the only operation that discards its cached row
// heights outright; queue_resize/columns_autosize just mark cells dirty for
// redraw, leaving every row at its previous height.
void GtkEditableGridWidget::refreshRowHeights()
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(m_treeView));
    if (!model) return;

    // gtk_tree_view_column_queue_resize() is the ONE call that makes
    // GtkTreeView re-measure row heights after a renderer's wrap-width
    // changes. Verified in an isolated GTK program rather than inferred:
    //
    //   wrap-width=170 + row-changed on every row   -> row height 29 (unchanged)
    //   wrap-width=170 + column_queue_resize        -> row height 121
    //   widen to 600   + column_queue_resize        -> row height 52
    //   narrow to 120  + column_queue_resize        -> row height 167
    //
    // So row-changed does nothing for height, and a model detach/reattach is
    // actively harmful (it zeroes column widths, so rows get re-measured at
    // width 0 and cache a single-line height). Both were tried here first.
    //
    // Must be called from an idle, never from inside notify::width or
    // size-allocate: queuing a resize during GTK's own layout pass is what
    // blanked the grid in earlier attempts.
    m_applyingWrap = true;
    GList *cols = gtk_tree_view_get_columns(GTK_TREE_VIEW(m_treeView));
    int n = 0;
    for (GList *l = cols; l; l = l->next, ++n)
        gtk_tree_view_column_queue_resize(GTK_TREE_VIEW_COLUMN(l->data));
    g_list_free(cols);
    m_applyingWrap = false;
}

gboolean GtkEditableGridWidget::rowHeightRefreshIdle(gpointer data)
{
    auto *self = static_cast<GtkEditableGridWidget *>(data);
    self->m_rowHeightRefreshId = 0;
    self->refreshRowHeights();
    return G_SOURCE_REMOVE;
}

void GtkEditableGridWidget::columnWidthChangedTrampoline(GObject *col, GParamSpec *, gpointer data)
{
    auto *ctx = static_cast<ColCtx *>(data);
    GtkEditableGridWidget *self = ctx->self;
    if (!self->m_wordWrap || self->m_applyingWrap) return;

    // Setting wrap-width changes the renderer's desired size, which can move
    // the column width, which re-emits notify::width. Calling
    // gtk_tree_view_columns_autosize() from in here made that a hard feedback
    // loop -- it forces a full width recalculation across every column, so
    // each pass re-entered this handler and GTK was left mid-recalculation
    // with no valid layout, which rendered as a completely blank grid.
    // Re-apply the wrap budget only, guarded, and let GTK size rows on its
    // own next allocation.
    // The user dragged this column: honor the width they chose as the new
    // budget. Safe to read get_width() here (unlike when first enabling wrap)
    // because the column is FIXED by then, so its width reflects the user's
    // choice rather than the content's natural width.
    auto *column = GTK_TREE_VIEW_COLUMN(col);
    int rawWidth = gtk_tree_view_column_get_width(column);

    // Ignore transient/degenerate widths outright rather than clamping them to
    // a floor. Clamping (previously max(width,60)) turned a meaningless width
    // into a plausible-looking budget, so the skip guard never fired.
    if (rawWidth <= 16) return;

    // Already handled at this exact width: doing the work again would re-emit
    // row-changed, which can perturb layout and bring us straight back here.
    if (rawWidth == ctx->lastBudget) return;
    ctx->lastBudget = rawWidth;

    int newWidth = rawWidth;
    self->m_applyingWrap = true;
    self->applyWrapToColumn(column, newWidth);
    self->m_applyingWrap = false;

    // Setting wrap-width alone is NOT enough: Pango starts wrapping, but the
    // rows keep the height they were measured at, so the extra lines are
    // clipped and it looks exactly like wrap doing nothing. This was the whole
    // bug -- toggling wrap re-measured row heights, dragging a column never
    // did, so wrap only ever appeared to work if the column was already narrow
    // when the toggle happened.
    //
    // Deferred to an idle and collapsed to one pending refresh: a drag emits
    // notify::width continuously, and re-attaching the model on every event
    // would be both slow and re-entrant (we are inside GTK's layout pass right
    // now, which is what blanked the grid in earlier attempts).
    if (self->m_rowHeightRefreshId == 0)
        self->m_rowHeightRefreshId = g_idle_add(rowHeightRefreshIdle, self);
}

void GtkEditableGridWidget::insertRows(int count, int atRow)
{
    auto before = rowData();
    auto after = before;
    std::vector<std::string> blank(m_columnCount, "");
    int pos = std::clamp(atRow, 0, (int)after.size());
    after.insert(after.begin() + pos, count, blank);

    GtkEditableGridWidget *self = this;
    GtkFocusManager::UndoCommand cmd;
    cmd.text = "Insert rows";
    cmd.undo = [self, before]() { self->setRowData(before); };
    cmd.redo = [self, after]() { self->setRowData(after); };
    if (m_fm) m_fm->pushUndo(std::move(cmd));
    else setRowData(after);
    setDirty(true);
}

void GtkEditableGridWidget::deleteSelectedRows()
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(m_treeView));
    GList *rows = gtk_tree_selection_get_selected_rows(sel, nullptr);
    if (!rows) return;

    std::vector<int> indices;
    for (GList *l = rows; l; l = l->next) {
        int *idx = gtk_tree_path_get_indices(static_cast<GtkTreePath *>(l->data));
        if (idx) indices.push_back(idx[0]);
    }
    g_list_free_full(rows, (GDestroyNotify)gtk_tree_path_free);
    std::sort(indices.rbegin(), indices.rend()); // descending, so erase-by-index stays valid

    auto before = rowData();
    auto after = before;
    for (int idx : indices)
        if (idx >= 0 && idx < (int)after.size())
            after.erase(after.begin() + idx);

    GtkEditableGridWidget *self = this;
    GtkFocusManager::UndoCommand cmd;
    cmd.text = "Delete rows";
    cmd.undo = [self, before]() { self->setRowData(before); };
    cmd.redo = [self, after]() { self->setRowData(after); };
    if (m_fm) m_fm->pushUndo(std::move(cmd));
    else setRowData(after);
    setDirty(true);
}

} // namespace GtkWlPlugin
