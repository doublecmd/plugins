#pragma once

#include <QWidget>
#include <QTableView>
#include <QUndoStack>
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QPointer>
#include <QSet>
#include <functional>

namespace QtWlPlugin {

class FocusManager;

/// Defines the memory and undo strategy for EditableGridWidget.
///
/// MemoryDocument: Full QUndoStack tracking for all data mutations.
///   Best for QTableWidget, QStandardItemModel, or any in-memory model.
///   Sorting snapshots entire table state for undo.
///
/// LiveDatabase: Bypasses QUndoStack for data mutations (insert, delete, sort).
///   Best for QSqlTableModel or other transactional models.
///   Sorting delegates to model->sort() (SQL ORDER BY) with no RAM snapshot.
///   Copy, context menus, drag-to-reorder, focus, and shortcuts still work.
enum class GridMode {
    MemoryDocument,
    LiveDatabase
};

/// Custom delegate that wraps text at any character (not just word boundaries).
class WrapAnywhereDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;
    void setWrapAnywhere(bool wrap);
    bool wrapAnywhere() const;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
    void updateEditorGeometry(QWidget *editor,
                              const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    bool m_wrap = false;
};

/// A QTableView wrapper with full undo/redo, keyboard navigation,
/// drag-to-move columns/rows, context menus, and editing support.
///
/// Accepts a pre-instantiated QTableView* and a GridMode via dependency injection.
/// Because QTableWidget is a subclass of QTableView, you can inject either:
///
///   // Item-based (in-memory, full undo):
///   auto *grid = new EditableGridWidget(new QTableWidget(), GridMode::MemoryDocument, fm, this);
///
///   // Database (transactional, no RAM snapshots):
///   auto *sqlView = new QTableView();
///   sqlView->setModel(sqlModel);
///   auto *grid = new EditableGridWidget(sqlView, GridMode::LiveDatabase, fm, this);
///
/// All data access goes through QAbstractItemModel — no QTableWidgetItem
/// dependency in the grid's own logic.
class EditableGridWidget : public QWidget {
    Q_OBJECT
public:
    /// Takes ownership of the view and parents it to this widget.
    explicit EditableGridWidget(QTableView *view, GridMode mode, FocusManager *fm, QWidget *parent = nullptr);

    /// Access the underlying view (may be QTableView or QTableWidget).
    QTableView *view() const;
    GridMode mode() const;
    QUndoStack *undoStack() const;

    // --- Data operations (format-agnostic, model-based) ---
    void copySelection(char separator = '\t');
    QString getSelectionAsText(char separator = '\t');
    void pasteSelection();
    void pasteSelectionAt(int atRow);
    void insertRows(int count, int atRow);
    void deleteSelectedRows();

    void copyColumnSelection(char separator = '\t');
    void pasteColumnSelectionAt(int atCol);
    void insertColumns(int count, int atCol);
    void deleteSelectedColumns();

    // --- Appearance ---
    void setWordWrap(bool wrap);
    bool wordWrap() const;
    void setShowGrid(bool show);

    // --- Context menu integration ---
    /// Set the filter row widget for Filters toggle in context menu.
    void setFilterRow(class FilterRowWidget *filterRow);

    /// Enable the Dark theme toggle in context menu.
    void setThemeToggleEnabled(bool enabled);

    /// Register additional context menu entries.
    /// The callback receives the QMenu and the clicked QModelIndex.
    void setExtraContextMenuCallback(
        std::function<void(QMenu*, const QModelIndex&)> callback);

    // --- State ---
    bool isDirty() const;

signals:
    void dirtyChanged(bool dirty);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void setupView();
    void registerShortcuts();
    void setupDragToMove();
    void showRowContextMenu(const QPoint &pos);
    void showColumnContextMenu(const QPoint &pos);
    void onSortByColumn(int column);
    void onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                       const QList<int> &roles);
    void updateRowNumbers();
    bool isSectionSelected(QHeaderView *header, int logicalIndex) const;

    /// Helper: row/column count via the model
    int rowCount() const;
    int colCount() const;

    FocusManager *m_fm;
    QTableView *m_view;
    GridMode m_mode;
    QUndoStack *m_undoStack;
    WrapAnywhereDelegate *m_wrapDelegate;
    bool m_isProgrammaticChange;

    // Sort state
    int m_lastSortColumn;
    Qt::SortOrder m_lastSortOrder;

    // Drag-to-move state
    QHeaderView *m_dragHeader;
    int m_dragLogicalIndex;
    QList<int> m_dragBeforeOrder;
    QSet<int> m_dragSelectedSections;
    bool m_isDraggingSection;
    QTimer *m_moveDebounceTimer;

    // Context menu integrations
    FilterRowWidget *m_filterRow = nullptr;
    bool m_themeToggleEnabled = false;
    std::function<void(QMenu*, const QModelIndex&)> m_extraMenuCallback;
};

} // namespace QtWlPlugin
