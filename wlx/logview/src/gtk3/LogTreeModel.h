#pragma once

#include <gtk/gtk.h>
#include "LogEngine.h"
#include <vector>
#include <cstdint>

/// Minimal custom GtkTreeModel backed directly by LogEngine — NOT a
/// GtkListStore. A log file can be huge (that's the entire reason
/// LogEngine mmaps it and indexes line offsets instead of loading
/// everything into memory), so materializing every row into a
/// GtkListStore would defeat the point; this exposes LogEngine's rows
/// virtually, the same role QAbstractListModel played on the Qt side.
///
/// Single column (COL_TEXT, G_TYPE_STRING) — foreground/background
/// colors are applied via a cell-data-func in the GTK layer (reading
/// LogEngine::colorsForRow() directly) rather than as model columns, to
/// keep the model itself simple.
///
/// Supports an optional row filter (for the time-range / "filter mode"
/// feature): when active, only the indices in the filter list are
/// exposed, renumbered as consecutive GtkTreeIter positions 0..N-1.
///
/// Implemented as a classic manual GObject type (GTypeInfo + a
/// GtkTreeModelIface vtable) rather than via G_DEFINE_TYPE, since this
/// header is consumed from C++ and a plain function-based registration
/// keeps that unambiguous.

#define LOG_TYPE_TREE_MODEL (log_tree_model_get_type())
#define LOG_TREE_MODEL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), LOG_TYPE_TREE_MODEL, LogTreeModel))
#define LOG_IS_TREE_MODEL(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), LOG_TYPE_TREE_MODEL))

enum { LOG_TREE_MODEL_COL_TEXT = 0, LOG_TREE_MODEL_N_COLUMNS };

struct LogTreeModel {
    GObject parent_instance;
    LogEngine *engine = nullptr; // not owned
    std::vector<int> *filter = nullptr; // not owned; nullptr = unfiltered
    int stamp = 0; // bumped on every structural change to invalidate old iterators
    int lastNotifiedCount = 0; // row count observers (GtkTreeView) currently believe -- see the .cpp for why this matters
};

struct LogTreeModelClass {
    GObjectClass parent_class;
};

GType log_tree_model_get_type();
LogTreeModel *log_tree_model_new(LogEngine *engine);

/// Call after LogEngine's row count changes (load, tail growth, delete)
/// to notify the GtkTreeView. `oldCount`/`newCount` are in terms of the
/// model's CURRENT (possibly filtered) row space.
void log_tree_model_rows_changed(LogTreeModel *model);
void log_tree_model_rows_inserted(LogTreeModel *model, int firstRow, int lastRow);

/// Install/clear a row filter (vector of LogEngine row indices to show,
/// in order). Pass nullptr to clear (show everything).
void log_tree_model_set_filter(LogTreeModel *model, std::vector<int> *filter);

/// Translate a model row (possibly filtered) to the underlying LogEngine
/// row index.
int log_tree_model_to_engine_row(LogTreeModel *model, int modelRow);
