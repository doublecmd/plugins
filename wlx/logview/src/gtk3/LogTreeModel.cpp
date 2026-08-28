#include "LogTreeModel.h"
#include <cstring>

namespace {

void log_tree_model_tree_model_init(GtkTreeModelIface *iface);

} // namespace

G_DEFINE_TYPE_WITH_CODE(LogTreeModel, log_tree_model, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_MODEL, log_tree_model_tree_model_init))

static void log_tree_model_init(LogTreeModel *self)
{
    self->engine = nullptr;
    self->filter = nullptr;
    self->stamp = g_random_int();
    self->lastNotifiedCount = 0;
}

static void log_tree_model_class_init(LogTreeModelClass *)
{
    // No properties/signals of our own; GtkTreeModelIface carries all the
    // behavior (installed via log_tree_model_tree_model_init below).
}

LogTreeModel *log_tree_model_new(LogEngine *engine)
{
    auto *model = LOG_TREE_MODEL(g_object_new(LOG_TYPE_TREE_MODEL, nullptr));
    model->engine = engine;
    return model;
}

int log_tree_model_to_engine_row(LogTreeModel *model, int modelRow)
{
    if (model->filter) {
        if (modelRow < 0 || modelRow >= (int)model->filter->size()) return -1;
        return (*model->filter)[modelRow];
    }
    return modelRow;
}

static int modelRowCount(LogTreeModel *model)
{
    if (model->filter) return (int)model->filter->size();
    return model->engine ? model->engine->lineCount() : 0;
}

// Full structural reset (row count and/or row identities may have
// changed arbitrarily -- a filter swap or a reload), signaled the way
// GtkTreeView actually requires: a real "row-deleted" notification (with
// a real indexed path, not the empty gtk_tree_path_new() this used to
// pass -- that's not a valid "this row was deleted" path and corrupts
// the view's own internal row-count bookkeeping, which is what was
// actually crashing deep inside libgtk-3/libgobject on the *next*
// structural change, once that bookkeeping was already wrong) for every
// row the view currently believes exists, followed by "row-inserted" for
// every row that exists now. Deleting from the end backwards means each
// row-deleted path is still valid at the moment it's emitted (deleting
// row N-1 first doesn't invalidate the index of row N-2, etc.).
static void resetModel(LogTreeModel *model)
{
    model->stamp++;
    for (int r = model->lastNotifiedCount - 1; r >= 0; --r) {
        GtkTreePath *path = gtk_tree_path_new_from_indices(r, -1);
        gtk_tree_model_row_deleted(GTK_TREE_MODEL(model), path);
        gtk_tree_path_free(path);
    }
    int newCount = modelRowCount(model);
    if (newCount > 0)
        log_tree_model_rows_inserted(model, 0, newCount - 1);
    model->lastNotifiedCount = newCount;
}

void log_tree_model_set_filter(LogTreeModel *model, std::vector<int> *filter)
{
    model->filter = filter;
    resetModel(model);
}

void log_tree_model_rows_changed(LogTreeModel *model)
{
    resetModel(model);
}

void log_tree_model_rows_inserted(LogTreeModel *model, int firstRow, int lastRow)
{
    for (int r = firstRow; r <= lastRow; ++r) {
        GtkTreePath *path = gtk_tree_path_new_from_indices(r, -1);
        GtkTreeIter iter;
        iter.stamp = model->stamp;
        iter.user_data = GINT_TO_POINTER(r);
        gtk_tree_model_row_inserted(GTK_TREE_MODEL(model), path, &iter);
        gtk_tree_path_free(path);
    }
    // Centralized here (rather than in each caller) so resetModel()'s
    // row-deleted count -- how many rows the view currently believes
    // exist -- never drifts out of sync, regardless of whether a caller
    // reaches this via resetModel() or the direct tail-follow append in
    // LogViewerWidget_gtk3.cpp.
    if (lastRow + 1 > model->lastNotifiedCount)
        model->lastNotifiedCount = lastRow + 1;
}

// ── GtkTreeModel vtable ─────────────────────────────────────────────

namespace {

GtkTreeModelFlags tm_get_flags(GtkTreeModel *) { return GTK_TREE_MODEL_LIST_ONLY; }

gint tm_get_n_columns(GtkTreeModel *) { return LOG_TREE_MODEL_N_COLUMNS; }

GType tm_get_column_type(GtkTreeModel *, gint) { return G_TYPE_STRING; }

gboolean tm_get_iter(GtkTreeModel *treeModel, GtkTreeIter *iter, GtkTreePath *path)
{
    auto *model = LOG_TREE_MODEL(treeModel);
    if (gtk_tree_path_get_depth(path) != 1) return FALSE;
    gint row = gtk_tree_path_get_indices(path)[0];
    if (row < 0 || row >= modelRowCount(model)) return FALSE;

    iter->stamp = model->stamp;
    iter->user_data = GINT_TO_POINTER(row);
    return TRUE;
}

GtkTreePath *tm_get_path(GtkTreeModel *treeModel, GtkTreeIter *iter)
{
    auto *model = LOG_TREE_MODEL(treeModel);
    if (iter->stamp != model->stamp) return nullptr;
    return gtk_tree_path_new_from_indices(GPOINTER_TO_INT(iter->user_data), -1);
}

void tm_get_value(GtkTreeModel *treeModel, GtkTreeIter *iter, gint column, GValue *value)
{
    auto *model = LOG_TREE_MODEL(treeModel);
    g_value_init(value, G_TYPE_STRING);
    if (iter->stamp != model->stamp || !model->engine) return;

    int modelRow = GPOINTER_TO_INT(iter->user_data);
    int engineRow = log_tree_model_to_engine_row(model, modelRow);
    if (column == LOG_TREE_MODEL_COL_TEXT && engineRow >= 0) {
        std::string text = model->engine->lineText(engineRow);
        g_value_set_string(value, text.c_str());
    }
}

gboolean tm_iter_next(GtkTreeModel *treeModel, GtkTreeIter *iter)
{
    auto *model = LOG_TREE_MODEL(treeModel);
    if (iter->stamp != model->stamp) return FALSE;
    int row = GPOINTER_TO_INT(iter->user_data) + 1;
    if (row >= modelRowCount(model)) return FALSE;
    iter->user_data = GINT_TO_POINTER(row);
    return TRUE;
}

gboolean tm_iter_children(GtkTreeModel *treeModel, GtkTreeIter *iter, GtkTreeIter *parent)
{
    auto *model = LOG_TREE_MODEL(treeModel);
    if (parent) return FALSE;
    if (modelRowCount(model) == 0) return FALSE;
    iter->stamp = model->stamp;
    iter->user_data = GINT_TO_POINTER(0);
    return TRUE;
}

gboolean tm_iter_has_child(GtkTreeModel *, GtkTreeIter *) { return FALSE; }

gint tm_iter_n_children(GtkTreeModel *treeModel, GtkTreeIter *iter)
{
    auto *model = LOG_TREE_MODEL(treeModel);
    if (iter) return 0;
    return modelRowCount(model);
}

gboolean tm_iter_nth_child(GtkTreeModel *treeModel, GtkTreeIter *iter, GtkTreeIter *parent, gint n)
{
    auto *model = LOG_TREE_MODEL(treeModel);
    if (parent) return FALSE;
    if (n < 0 || n >= modelRowCount(model)) return FALSE;
    iter->stamp = model->stamp;
    iter->user_data = GINT_TO_POINTER(n);
    return TRUE;
}

gboolean tm_iter_parent(GtkTreeModel *, GtkTreeIter *, GtkTreeIter *) { return FALSE; }

void log_tree_model_tree_model_init(GtkTreeModelIface *iface)
{
    iface->get_flags = tm_get_flags;
    iface->get_n_columns = tm_get_n_columns;
    iface->get_column_type = tm_get_column_type;
    iface->get_iter = tm_get_iter;
    iface->get_path = tm_get_path;
    iface->get_value = tm_get_value;
    iface->iter_next = tm_iter_next;
    iface->iter_children = tm_iter_children;
    iface->iter_has_child = tm_iter_has_child;
    iface->iter_n_children = tm_iter_n_children;
    iface->iter_nth_child = tm_iter_nth_child;
    iface->iter_parent = tm_iter_parent;
}

} // namespace
