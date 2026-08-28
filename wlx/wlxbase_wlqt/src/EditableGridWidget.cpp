#include <wlxbase_wlqt/EditableGridWidget.h>
#include <wlxbase_wlqt/FocusManager.h>
#include <wlxbase_wlqt/FilterRowWidget.h>
#include <wlxbase_wlqt/ThemeManager.h>

#include <QApplication>
#include <QClipboard>
#include <QVBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QTextOption>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QAbstractItemModel>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QAbstractItemDelegate>
#include <QItemSelectionModel>
#include <QRegularExpression>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QPlainTextEdit>
#include <algorithm>

namespace QtWlPlugin {

// ---------------------------------------------------------------------------
// Undo Commands — all operate through QAbstractItemModel, not QTableWidgetItem
// ---------------------------------------------------------------------------

class EditCellCommand : public QUndoCommand {
public:
    EditCellCommand(QAbstractItemModel *model, const QModelIndex &index,
                    const QVariant &oldValue, const QVariant &newValue,
                    QUndoCommand *parent = nullptr)
        : QUndoCommand(parent), m_model(model),
          m_row(index.row()), m_col(index.column()),
          m_oldValue(oldValue), m_newValue(newValue) {
        setText(QString("Edit cell (%1, %2)").arg(m_row).arg(m_col));
    }
    void undo() override {
        QModelIndex idx = m_model->index(m_row, m_col);
        m_model->setData(idx, m_oldValue, Qt::EditRole);
    }
    void redo() override {
        QModelIndex idx = m_model->index(m_row, m_col);
        m_model->setData(idx, m_newValue, Qt::EditRole);
    }
private:
    QAbstractItemModel *m_model;
    int m_row, m_col;
    QVariant m_oldValue, m_newValue;
};

class RowColCommand : public QUndoCommand {
public:
    RowColCommand(QAbstractItemModel *model, int index, int count, bool isRow, bool isInsert,
                  QUndoCommand *parent = nullptr)
        : QUndoCommand(parent), m_model(model), m_index(index), m_count(count),
          m_isRow(isRow), m_isInsert(isInsert) {
        setText(QString("%1 %2 %3(s)").arg(isInsert ? "Insert" : "Delete")
                .arg(count).arg(isRow ? "row" : "col"));
        if (!isInsert) {
            // Snapshot data before deletion for undo restore
            int crossDim = isRow ? model->columnCount() : model->rowCount();
            for (int i = 0; i < count; ++i) {
                QVariantList list;
                for (int j = 0; j < crossDim; ++j) {
                    QModelIndex idx = isRow ? model->index(index + i, j)
                                           : model->index(j, index + i);
                    list << model->data(idx, Qt::EditRole);
                }
                m_data << list;
            }
        }
    }
    void undo() override { if (m_isInsert) applyDelete(); else applyInsert(); }
    void redo() override { if (m_isInsert) applyInsert(); else applyDelete(); }

private:
    void applyInsert() {
        if (m_isRow) m_model->insertRows(m_index, m_count);
        else m_model->insertColumns(m_index, m_count);

        // Restore saved data if we have any (undo of delete)
        if (!m_data.isEmpty()) {
            int crossDim = m_isRow ? m_model->columnCount() : m_model->rowCount();
            for (int i = 0; i < m_count && i < m_data.size(); ++i) {
                const QVariantList &list = m_data[i];
                for (int j = 0; j < crossDim && j < list.size(); ++j) {
                    QModelIndex idx = m_isRow ? m_model->index(m_index + i, j)
                                              : m_model->index(j, m_index + i);
                    m_model->setData(idx, list[j], Qt::EditRole);
                }
            }
        }
    }
    void applyDelete() {
        if (m_isRow) m_model->removeRows(m_index, m_count);
        else m_model->removeColumns(m_index, m_count);
    }
    QAbstractItemModel *m_model;
    int m_index, m_count;
    QList<QVariantList> m_data;
    bool m_isRow, m_isInsert;
};

class DataSnapshotCommand : public QUndoCommand {
public:
    DataSnapshotCommand(QAbstractItemModel *model, const QList<QVariantList> &before,
                        const QList<QVariantList> &after, const QString &text)
        : m_model(model), m_before(before), m_after(after), m_first(true) { setText(text); }
    void undo() override { restore(m_before); }
    void redo() override { if (m_first) { m_first = false; return; } restore(m_after); }
private:
    void restore(const QList<QVariantList> &data) {
        for (int r = 0; r < data.size() && r < m_model->rowCount(); ++r)
            for (int c = 0; c < data[r].size() && c < m_model->columnCount(); ++c) {
                QModelIndex idx = m_model->index(r, c);
                m_model->setData(idx, data[r][c], Qt::EditRole);
            }
    }
    QAbstractItemModel *m_model;
    QList<QVariantList> m_before, m_after;
    bool m_first;
};

class SectionMoveCommand : public QUndoCommand {
public:
    SectionMoveCommand(QHeaderView *header, const QList<int> &beforeOrder,
                       const QList<int> &afterOrder, const QString &text)
        : m_header(header), m_before(beforeOrder), m_after(afterOrder), m_first(true) { setText(text); }
    void undo() override { restore(m_before); }
    void redo() override { if (m_first) { m_first = false; return; } restore(m_after); }
private:
    void restore(const QList<int> &order) {
        for (int target = 0; target < order.size(); ++target) {
            int logical = order[target];
            int currentVisual = m_header->visualIndex(logical);
            if (currentVisual != target)
                m_header->moveSection(currentVisual, target);
        }
    }
    QHeaderView *m_header;
    QList<int> m_before, m_after;
    bool m_first;
};

// ---------------------------------------------------------------------------
// WrapAnywhereDelegate — already model-based (uses QModelIndex)
// ---------------------------------------------------------------------------

void WrapAnywhereDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    QString text = opt.text;
    opt.text.clear();
    QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter);

    if (!text.isEmpty()) {
        painter->save();
        QRect textRect = QApplication::style()->subElementRect(QStyle::SE_ItemViewItemText, &opt);
        painter->setClipRect(textRect);
        painter->setFont(opt.font);
        QTextOption textOption;
        textOption.setWrapMode(m_wrap ? QTextOption::WrapAnywhere : QTextOption::NoWrap);
        textOption.setAlignment(opt.displayAlignment);
        if (opt.state & QStyle::State_Selected)
            painter->setPen(opt.palette.color(QPalette::HighlightedText));
        else
            painter->setPen(opt.palette.color(QPalette::Text));
        painter->drawText(textRect, text, textOption);
        painter->restore();
    }
}

QSize WrapAnywhereDelegate::sizeHint(const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const {
    if (!m_wrap) return QStyledItemDelegate::sizeHint(option, index);
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    QRect textRect = QApplication::style()->subElementRect(QStyle::SE_ItemViewItemText, &opt);
    int width = textRect.width();
    if (width <= 0) width = opt.rect.width();
    QTextDocument doc;
    doc.setDefaultFont(opt.font);
    QTextOption textOption;
    textOption.setWrapMode(QTextOption::WrapAnywhere);
    doc.setDefaultTextOption(textOption);
    doc.setTextWidth(width);
    doc.setPlainText(opt.text);
    return QSize(width, qMax((int)doc.size().height(), opt.fontMetrics.height()));
}

void WrapAnywhereDelegate::setWrapAnywhere(bool wrap) { m_wrap = wrap; }
bool WrapAnywhereDelegate::wrapAnywhere() const { return m_wrap; }

QWidget *WrapAnywhereDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                                            const QModelIndex &index) const {
    auto *editor = new QPlainTextEdit(parent);
    editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    return editor;
}

void WrapAnywhereDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const {
    QString value = index.model()->data(index, Qt::EditRole).toString();
    auto *textEdit = qobject_cast<QPlainTextEdit*>(editor);
    if (textEdit)
        textEdit->setPlainText(value);
    else
        QStyledItemDelegate::setEditorData(editor, index);
}

void WrapAnywhereDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                        const QModelIndex &index) const {
    auto *textEdit = qobject_cast<QPlainTextEdit*>(editor);
    if (textEdit)
        model->setData(index, textEdit->toPlainText(), Qt::EditRole);
    else
        QStyledItemDelegate::setModelData(editor, model, index);
}

void WrapAnywhereDelegate::updateEditorGeometry(QWidget *editor,
                                                const QStyleOptionViewItem &option,
                                                const QModelIndex &index) const {
    editor->setGeometry(option.rect);
}

bool WrapAnywhereDelegate::eventFilter(QObject *obj, QEvent *event) {
    // Qt installs the delegate as event filter on the editor widget.
    // We intercept plain Enter to commit+close, but let Ctrl+Enter fall through
    // to QPlainTextEdit::keyPressEvent which inserts a newline naturally.
    // (QAbstractItemDelegate::eventFilter already skips commit for QPlainTextEdit,
    // so the event reaches keyPressEvent regardless — but we must not consume it.)
    auto *editor = qobject_cast<QPlainTextEdit*>(obj);
    if (editor && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent*>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
                && !(ke->modifiers() & Qt::ControlModifier)) {
            // Plain Enter: commit and close
            emit commitData(editor);
            emit closeEditor(editor, QAbstractItemDelegate::SubmitModelCache);
            return true;
        }
        // Ctrl+Enter: fall through — QPlainTextEdit::keyPressEvent inserts a newline.
    }
    return QStyledItemDelegate::eventFilter(obj, event);
}

// ---------------------------------------------------------------------------
// EditableGridWidget
// ---------------------------------------------------------------------------

EditableGridWidget::EditableGridWidget(QTableView *view, GridMode mode, FocusManager *fm, QWidget *parent)
    : QWidget(parent)
    , m_fm(fm)
    , m_view(view)
    , m_mode(mode)
    , m_isProgrammaticChange(false)
    , m_lastSortColumn(-1)
    , m_lastSortOrder(Qt::AscendingOrder)
    , m_dragHeader(nullptr)
    , m_dragLogicalIndex(-1)
    , m_isDraggingSection(false)
{
    setFocusPolicy(Qt::NoFocus);

    // Take ownership
    m_view->setParent(this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    setupView();

    m_undoStack = new QUndoStack(this);
    fm->setUndoStack(m_undoStack);

    connect(m_undoStack, &QUndoStack::cleanChanged, this, [this](bool clean) {
        emit dirtyChanged(!clean);
    });
    connect(m_undoStack, &QUndoStack::indexChanged, this, [this]() { updateRowNumbers(); });

    // MemoryDocument mode: track data changes for undo integration.
    // LiveDatabase mode: the model handles its own transactions;
    // intercepting dataChanged would force the entire DB into RAM.
    if (m_mode == GridMode::MemoryDocument) {
        if (m_view->model()) {
            connect(m_view->model(), &QAbstractItemModel::dataChanged,
                    this, &EditableGridWidget::onDataChanged);
        }

        // Stash old value when entering a cell editor
        connect(fm, &FocusManager::inputWidgetEntered, this, [this](QWidget *) {
            QModelIndex current = m_view->currentIndex();
            if (!current.isValid() || !m_view->model()) return;

            QAbstractItemModel *model = m_view->model();
            QModelIndex sourceIndex = current;
            QAbstractItemModel *sourceModel = model;
            if (auto *proxy = qobject_cast<QSortFilterProxyModel*>(model)) {
                sourceModel = proxy->sourceModel();
                sourceIndex = proxy->mapToSource(current);
            }

            QVariant existing = sourceModel->data(sourceIndex, Qt::UserRole);
            if (!existing.isValid()) {
                m_isProgrammaticChange = true;
                sourceModel->setData(sourceIndex,
                    sourceModel->data(sourceIndex, Qt::EditRole), Qt::UserRole);
                m_isProgrammaticChange = false;
            }
        });
    }

    registerShortcuts();
    setupDragToMove();
}

void EditableGridWidget::setupView()
{
    m_view->setFocusPolicy(Qt::ClickFocus);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);

    m_view->horizontalHeader()->setSectionsClickable(true);
    m_view->horizontalHeader()->setHighlightSections(true);
    m_view->horizontalHeader()->setSectionsMovable(false);
    m_view->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);

    m_view->verticalHeader()->setSectionsClickable(true);
    m_view->verticalHeader()->setHighlightSections(true);
    m_view->verticalHeader()->setSectionsMovable(false);
    m_view->verticalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Interactive);

    m_view->setSortingEnabled(false);

    m_wrapDelegate = new WrapAnywhereDelegate(m_view);
    m_view->setItemDelegate(m_wrapDelegate);

    m_view->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_view, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) { showRowContextMenu(pos); });
    connect(m_view->verticalHeader(), &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) { showRowContextMenu(pos); });
    connect(m_view->horizontalHeader(), &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) { showColumnContextMenu(pos); });
    connect(m_view->horizontalHeader(), &QHeaderView::sectionClicked, this,
            &EditableGridWidget::onSortByColumn);

    // Install event filter on header viewports for drag-to-move
    m_view->horizontalHeader()->viewport()->installEventFilter(this);
    m_view->verticalHeader()->viewport()->installEventFilter(this);
    // Install on view itself for in-editor key handling
    m_view->installEventFilter(this);
}

int EditableGridWidget::rowCount() const
{
    return m_view->model() ? m_view->model()->rowCount() : 0;
}

int EditableGridWidget::colCount() const
{
    return m_view->model() ? m_view->model()->columnCount() : 0;
}

void EditableGridWidget::registerShortcuts()
{
    // Ctrl+C → copy
    m_fm->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_C), FocusManager::WhenNoInput,
        [this]() { copySelection('\t'); return true; });

    // Ctrl+V → paste
    m_fm->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_V), FocusManager::WhenNoInput,
        [this]() { pasteSelection(); return true; });

    // Delete → delete selected rows
    m_fm->registerShortcut(QKeySequence(Qt::Key_Delete), FocusManager::WhenNoInput,
        [this]() { deleteSelectedRows(); return true; });

    // Enter/Return → edit current cell
    m_fm->registerShortcut(QKeySequence(Qt::Key_Return), FocusManager::WhenNoInput,
        [this]() {
            QModelIndex current = m_view->currentIndex();
            if (current.isValid()) { m_view->edit(current); return true; }
            return false;
        });
    m_fm->registerShortcut(QKeySequence(Qt::Key_Enter), FocusManager::WhenNoInput,
        [this]() {
            QModelIndex current = m_view->currentIndex();
            if (current.isValid()) { m_view->edit(current); return true; }
            return false;
        });

    // Arrow keys with right-wrap
    auto arrowHandler = [this](int key) -> bool {
        QModelIndex current = m_view->currentIndex();
        if (!current.isValid()) return false;
        int visualCol = m_view->horizontalHeader()->visualIndex(current.column());
        int visualRow = m_view->verticalHeader()->visualIndex(current.row());
        int numRows = rowCount();
        int numCols = colCount();
        if (key == Qt::Key_Up) visualRow--;
        if (key == Qt::Key_Down) visualRow++;
        if (key == Qt::Key_Left) {
            visualCol--;
            if (visualCol < 0 && visualRow > 0) {
                visualCol = numCols - 1;
                visualRow--;
            }
        }
        if (key == Qt::Key_Right) {
            visualCol++;
            if (visualCol >= numCols && visualRow < numRows - 1) {
                visualCol = 0;
                visualRow++;
            }
        }
        visualRow = qBound(0, visualRow, numRows - 1);
        visualCol = qBound(0, visualCol, numCols - 1);
        int r = m_view->verticalHeader()->logicalIndex(visualRow);
        int c = m_view->horizontalHeader()->logicalIndex(visualCol);
        m_view->setCurrentIndex(m_view->model()->index(r, c));
        return true;
    };

    m_fm->registerShortcut(QKeySequence(Qt::Key_Up), FocusManager::WhenNoInput,
        [=]() { return arrowHandler(Qt::Key_Up); });
    m_fm->registerShortcut(QKeySequence(Qt::Key_Down), FocusManager::WhenNoInput,
        [=]() { return arrowHandler(Qt::Key_Down); });
    m_fm->registerShortcut(QKeySequence(Qt::Key_Left), FocusManager::WhenNoInput,
        [=]() { return arrowHandler(Qt::Key_Left); });
    m_fm->registerShortcut(QKeySequence(Qt::Key_Right), FocusManager::WhenNoInput,
        [=]() { return arrowHandler(Qt::Key_Right); });
}

void EditableGridWidget::setupDragToMove()
{
    m_moveDebounceTimer = new QTimer(this);
    m_moveDebounceTimer->setSingleShot(true);
    m_moveDebounceTimer->setInterval(0);

    connect(m_moveDebounceTimer, &QTimer::timeout, this, [this]() {
        if (!m_isDraggingSection || !m_dragHeader) return;

        int newVisual = m_dragHeader->visualIndex(m_dragLogicalIndex);
        bool anyMoved = (newVisual != m_dragBeforeOrder.indexOf(m_dragLogicalIndex));

        if (anyMoved) {
            bool isHorizontal = (m_dragHeader == m_view->horizontalHeader());
            QList<int> currentOrder;
            for (int v = 0; v < m_dragHeader->count(); ++v)
                currentOrder.append(m_dragHeader->logicalIndex(v));

            QList<int> nonSelected;
            for (int li : currentOrder)
                if (!m_dragSelectedSections.contains(li)) nonSelected.append(li);

            QList<int> selectedInOrder;
            for (int li : m_dragBeforeOrder)
                if (m_dragSelectedSections.contains(li)) selectedInOrder.append(li);

            int insertIdx = qBound(0, newVisual, nonSelected.size());
            QList<int> targetOrder;
            for (int i = 0; i < insertIdx; ++i) targetOrder.append(nonSelected[i]);
            for (int li : selectedInOrder) targetOrder.append(li);
            for (int i = insertIdx; i < nonSelected.size(); ++i) targetOrder.append(nonSelected[i]);

            for (int v = 0; v < targetOrder.size(); ++v) {
                int logical = targetOrder[v];
                int curVisual = m_dragHeader->visualIndex(logical);
                if (curVisual != v) m_dragHeader->moveSection(curVisual, v);
            }

            QList<int> afterOrder;
            for (int v = 0; v < m_dragHeader->count(); ++v)
                afterOrder.append(m_dragHeader->logicalIndex(v));
            m_undoStack->push(new SectionMoveCommand(m_dragHeader, m_dragBeforeOrder, afterOrder,
                isHorizontal ? "Move columns" : "Move rows"));
            updateRowNumbers();
        }

        m_isDraggingSection = false;
        m_dragHeader->setSectionsMovable(false);
        m_dragHeader = nullptr;
    });

    auto connectMoveDebounce = [this](QHeaderView *header) {
        connect(header, &QHeaderView::sectionMoved, this, [this](int, int, int) {
            if (m_isDraggingSection) m_moveDebounceTimer->start();
        });
    };
    connectMoveDebounce(m_view->horizontalHeader());
    connectMoveDebounce(m_view->verticalHeader());
}

bool EditableGridWidget::eventFilter(QObject *obj, QEvent *event)
{
    QHeaderView *hHeader = m_view->horizontalHeader();
    QHeaderView *vHeader = m_view->verticalHeader();

    // --- Header viewport: drag-to-move ---
    if (event->type() == QEvent::MouseButtonPress) {
        if (obj == hHeader->viewport() || obj == vHeader->viewport()) {
            QHeaderView *header = (obj == hHeader->viewport()) ? hHeader : vHeader;
            auto *me = static_cast<QMouseEvent *>(event);
            int logicalIndex = header->logicalIndexAt(me->pos());
            if (logicalIndex >= 0 && isSectionSelected(header, logicalIndex)) {
                header->setSectionsMovable(true);
                m_isDraggingSection = true;
                m_dragHeader = header;
                m_dragLogicalIndex = logicalIndex;
                m_dragBeforeOrder.clear();
                for (int v = 0; v < header->count(); ++v)
                    m_dragBeforeOrder.append(header->logicalIndex(v));

                bool isHorizontal = (header == hHeader);
                m_dragSelectedSections.clear();
                QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
                for (const QModelIndex &idx : sel) {
                    int li = isHorizontal ? idx.column() : idx.row();
                    m_dragSelectedSections.insert(li);
                }

                QItemSelection savedSel = m_view->selectionModel()->selection();
                QTimer::singleShot(0, this, [this, savedSel]() {
                    if (m_isDraggingSection)
                        m_view->selectionModel()->select(savedSel, QItemSelectionModel::ClearAndSelect);
                });
            } else {
                header->setSectionsMovable(false);
            }
        }
    }

    // --- In-editor key handling ---
    if (event->type() == QEvent::KeyPress && obj == m_view && m_fm->activeInput()) {
        auto *ke = static_cast<QKeyEvent *>(event);
        QAbstractItemModel *model = m_view->model();
        if (!model) return QWidget::eventFilter(obj, event);

        if (ke->key() == Qt::Key_Escape) {
            QModelIndex current = m_view->currentIndex();
            if (current.isValid()) {
                QVariant oldData = model->data(current, Qt::UserRole);
                if (oldData.isValid()) {
                    m_isProgrammaticChange = true;
                    model->setData(current, oldData, Qt::EditRole);
                    model->setData(current, QVariant(), Qt::UserRole);
                    m_isProgrammaticChange = false;
                }
            }
            m_view->closePersistentEditor(m_view->currentIndex());
            return true;
        }

        if (ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down) {
            QModelIndex current = m_view->currentIndex();
            int r = current.row(), c = current.column();
            if (ke->key() == Qt::Key_Up) r--;
            if (ke->key() == Qt::Key_Down) r++;
            r = qBound(0, r, rowCount() - 1);

            // Cancel current edit without saving
            if (current.isValid()) {
                QVariant oldData = model->data(current, Qt::UserRole);
                if (oldData.isValid()) {
                    m_isProgrammaticChange = true;
                    model->setData(current, oldData, Qt::EditRole);
                    model->setData(current, QVariant(), Qt::UserRole);
                    m_isProgrammaticChange = false;
                }
            }
            m_view->closePersistentEditor(m_view->currentIndex());
            QModelIndex target = model->index(r, c);
            m_view->setCurrentIndex(target);
            m_view->edit(target);
            return true;
        }

        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            // QPlainTextEdit editors handle Enter in WrapAnywhereDelegate::eventFilter
            // (Qt routes editor key events to the delegate, not to m_view).

            QModelIndex current = m_view->currentIndex();
            int r = current.row(), c = current.column();

            // Commit via delegate
            QAbstractItemDelegate *delegate = m_view->itemDelegateForIndex(current);
            QWidget *editor = m_fm->activeInput();
            if (delegate && editor)
                delegate->setModelData(editor, model, current);

            // Navigate right (wrap to next row)
            int visualCol = m_view->horizontalHeader()->visualIndex(c);
            int visualRow = m_view->verticalHeader()->visualIndex(r);
            visualCol++;
            if (visualCol >= colCount()) {
                visualCol = 0;
                visualRow++;
            }
            if (visualRow < rowCount()) {
                int nr = m_view->verticalHeader()->logicalIndex(visualRow);
                int nc = m_view->horizontalHeader()->logicalIndex(visualCol);
                QTimer::singleShot(0, this, [this, nr, nc]() {
                    if (m_view->model())
                        m_view->setCurrentIndex(m_view->model()->index(nr, nc));
                });
            }
            return true;
        }
    }

    return QWidget::eventFilter(obj, event);
}

QTableView *EditableGridWidget::view() const { return m_view; }
GridMode EditableGridWidget::mode() const { return m_mode; }
QUndoStack *EditableGridWidget::undoStack() const { return m_undoStack; }
bool EditableGridWidget::isDirty() const { return !m_undoStack->isClean(); }

void EditableGridWidget::setWordWrap(bool wrap)
{
    m_wrapDelegate->setWrapAnywhere(wrap);
    m_view->setWordWrap(wrap);
    if (wrap) {
        m_view->resizeRowsToContents();
    } else {
        m_view->verticalHeader()->setDefaultSectionSize(m_view->fontMetrics().height() + 8);
        m_view->resizeRowsToContents();
    }
}

bool EditableGridWidget::wordWrap() const { return m_wrapDelegate->wrapAnywhere(); }
void EditableGridWidget::setShowGrid(bool show) { m_view->setShowGrid(show); }

void EditableGridWidget::setFilterRow(FilterRowWidget *filterRow) { m_filterRow = filterRow; }
void EditableGridWidget::setThemeToggleEnabled(bool enabled) { m_themeToggleEnabled = enabled; }

void EditableGridWidget::setExtraContextMenuCallback(
    std::function<void(QMenu*, const QModelIndex&)> callback)
{
    m_extraMenuCallback = std::move(callback);
}

void EditableGridWidget::onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                                        const QList<int> &roles)
{
    Q_UNUSED(bottomRight);
    Q_UNUSED(roles);
    if (m_isProgrammaticChange || !topLeft.isValid()) return;

    QAbstractItemModel *model = m_view->model();
    if (!model) return;

    QModelIndex sourceIndex = topLeft;
    QAbstractItemModel *sourceModel = model;
    if (auto *proxy = qobject_cast<QSortFilterProxyModel*>(model)) {
        sourceModel = proxy->sourceModel();
        sourceIndex = proxy->mapToSource(topLeft);
    }

    QVariant oldData = sourceModel->data(sourceIndex, Qt::UserRole);
    if (!oldData.isValid()) return;

    QVariant newValue = sourceModel->data(sourceIndex, Qt::EditRole);
    QString oldText = oldData.toString();
    QString newText = newValue.toString();

    m_isProgrammaticChange = true;
    sourceModel->setData(sourceIndex, QVariant(), Qt::UserRole); // clear stash
    m_isProgrammaticChange = false;

    if (oldText != newText) {
        m_isProgrammaticChange = true;
        sourceModel->setData(sourceIndex, oldData, Qt::EditRole); // revert so undo command applies it
        m_isProgrammaticChange = false;
        m_undoStack->push(new EditCellCommand(sourceModel, sourceIndex, oldData, newValue));
    }
}

void EditableGridWidget::onSortByColumn(int column)
{
    QAbstractItemModel *model = m_view->model();
    if (!model) return;

    if (column != m_lastSortColumn) {
        m_lastSortColumn = column;
        m_lastSortOrder = Qt::AscendingOrder;
        m_view->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
        return;
    }

    // --- LiveDatabase mode ---
    // Do not snapshot memory. Just issue the ORDER BY command via model->sort().
    // QSqlTableModel translates this to an SQL query and re-fetches lazily.
    if (m_mode == GridMode::LiveDatabase) {
        QAbstractItemModel *targetModel = model;
        if (auto *proxy = qobject_cast<QSortFilterProxyModel*>(model)) {
            if (proxy->sourceModel())
                targetModel = proxy->sourceModel();
        }
        targetModel->sort(column, m_lastSortOrder);
        m_view->horizontalHeader()->setSortIndicatorShown(true);
        m_view->horizontalHeader()->setSortIndicator(column, m_lastSortOrder);
        m_lastSortOrder = (m_lastSortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder;
        return;
    }

    // --- MemoryDocument mode ---
    // Safely snapshot in-memory data for undo/redo.
    int rows = rowCount(), cols = colCount();

    QList<QVariantList> beforeData;
    for (int r = 0; r < rows; ++r) {
        QVariantList row;
        for (int c = 0; c < cols; ++c)
            row << model->data(model->index(r, c), Qt::EditRole);
        beforeData << row;
    }

    model->sort(column, m_lastSortOrder);

    QList<QVariantList> afterData;
    for (int r = 0; r < rows; ++r) {
        QVariantList row;
        for (int c = 0; c < cols; ++c)
            row << model->data(model->index(r, c), Qt::EditRole);
        afterData << row;
    }

    m_undoStack->push(new DataSnapshotCommand(model, beforeData, afterData, "Sort"));
    m_view->horizontalHeader()->setSortIndicatorShown(true);
    m_view->horizontalHeader()->setSortIndicator(column, m_lastSortOrder);
    m_lastSortOrder = (m_lastSortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder;
}

void EditableGridWidget::updateRowNumbers()
{
    QAbstractItemModel *model = m_view->model();
    if (!model) return;
    QHeaderView *vh = m_view->verticalHeader();
    for (int v = 0; v < rowCount(); ++v) {
        int logical = vh->logicalIndex(v);
        model->setHeaderData(logical, Qt::Vertical, QString::number(v + 1));
    }
}

bool EditableGridWidget::isSectionSelected(QHeaderView *header, int logicalIndex) const
{
    QItemSelectionModel *sel = m_view->selectionModel();
    QAbstractItemModel *model = m_view->model();
    if (!sel || !model) return false;
    bool isHorizontal = (header == m_view->horizontalHeader());
    if (isHorizontal) {
        for (int r = 0; r < rowCount(); ++r)
            if (!sel->isSelected(model->index(r, logicalIndex))) return false;
        return rowCount() > 0;
    } else {
        for (int c = 0; c < colCount(); ++c)
            if (!sel->isSelected(model->index(logicalIndex, c))) return false;
        return colCount() > 0;
    }
}

// ---------------------------------------------------------------------------
// Copy / Paste / Insert / Delete — all via QAbstractItemModel
// ---------------------------------------------------------------------------

void EditableGridWidget::copySelection(char separator)
{
    QString text = getSelectionAsText(separator);
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

QString EditableGridWidget::getSelectionAsText(char separator)
{
    QAbstractItemModel *model = m_view->model();
    if (!model || !m_view->selectionModel()) return {};
    QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) return {};

    int minVRow = rowCount(), maxVRow = -1;
    int minVCol = colCount(), maxVCol = -1;
    for (const auto &index : sel) {
        int vr = m_view->verticalHeader()->visualIndex(index.row());
        int vc = m_view->horizontalHeader()->visualIndex(index.column());
        if (vr < minVRow) minVRow = vr; if (vr > maxVRow) maxVRow = vr;
        if (vc < minVCol) minVCol = vc; if (vc > maxVCol) maxVCol = vc;
    }

    QString outText;
    for (int vr = minVRow; vr <= maxVRow; ++vr) {
        int r = m_view->verticalHeader()->logicalIndex(vr);
        QStringList rowItems;
        for (int vc = minVCol; vc <= maxVCol; ++vc) {
            int c = m_view->horizontalHeader()->logicalIndex(vc);
            QModelIndex idx = model->index(r, c);
            QString cellText;
            if (m_view->selectionModel()->isSelected(idx))
                cellText = model->data(idx, Qt::DisplayRole).toString();
            rowItems << cellText;
        }
        outText += rowItems.join(separator) + "\n";
    }
    return outText;
}

void EditableGridWidget::pasteSelection()
{
    QAbstractItemModel *model = m_view->model();
    if (!model) return;

    if (m_mode == GridMode::LiveDatabase) {
        // LiveDatabase: insert at end without undo wrapping
        pasteSelectionAt(rowCount());
        return;
    }

    // MemoryDocument: insert at selection with undo macro for reorder
    int targetVisualRow = rowCount();
    QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
    if (!sel.isEmpty()) {
        int minVRow = rowCount();
        for (const auto &index : sel) {
            int vr = m_view->verticalHeader()->visualIndex(index.row());
            if (vr < minVRow) minVRow = vr;
        }
        targetVisualRow = minVRow;
    }

    int endRow = rowCount();
    bool needsMove = (targetVisualRow < endRow);

    QList<int> beforeOrder;
    QHeaderView *vh = m_view->verticalHeader();
    if (needsMove) {
        for (int v = 0; v < vh->count(); ++v)
            beforeOrder.append(vh->logicalIndex(v));
    }

    if (needsMove) m_undoStack->beginMacro("Paste rows");
    pasteSelectionAt(endRow);

    int rowsInserted = rowCount() - endRow;
    if (rowsInserted > 0 && needsMove) {
        QList<int> midOrder;
        for (int v = 0; v < vh->count(); ++v) midOrder.append(vh->logicalIndex(v));

        for (int i = 0; i < rowsInserted; ++i) {
            int logicalRow = endRow + i;
            int curVisual = vh->visualIndex(logicalRow);
            vh->moveSection(curVisual, targetVisualRow + i);
        }

        QList<int> afterOrder;
        for (int v = 0; v < vh->count(); ++v) afterOrder.append(vh->logicalIndex(v));
        m_undoStack->push(new SectionMoveCommand(vh, midOrder, afterOrder, "Move pasted rows"));
        updateRowNumbers();
    }
    if (needsMove) m_undoStack->endMacro();
}

void EditableGridWidget::pasteSelectionAt(int atRow)
{
    QAbstractItemModel *model = m_view->model();
    if (!model) return;
    int targetCols = colCount();
    if (targetCols <= 0) return;

    QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) return;

    QStringList lines = text.split(QRegularExpression("\r?\n"));
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    if (lines.isEmpty()) return;

    // Detect separator: try tab first, then comma
    char sep = '\t';
    QStringList testList = lines.first().split(QLatin1Char('\t'));
    if (testList.size() != targetCols) {
        QStringList commaTest = lines.first().split(QLatin1Char(','));
        if (commaTest.size() == targetCols) sep = ',';
        else if (testList.size() != targetCols) return;
    }

    int rowsToInsert = lines.size();

    QAbstractItemModel *targetModel = model;
    int targetRow = atRow;
    if (auto *proxy = qobject_cast<QSortFilterProxyModel*>(model)) {
        targetModel = proxy->sourceModel();
        if (atRow < proxy->rowCount()) {
            QModelIndex proxyIdx = proxy->index(atRow, 0);
            targetRow = proxy->mapToSource(proxyIdx).row();
        } else {
            targetRow = targetModel->rowCount();
        }
    }

    if (m_mode == GridMode::LiveDatabase) {
        // Direct model insertion, no undo command wrapper
        targetModel->insertRows(targetRow, rowsToInsert);
    } else {
        m_isProgrammaticChange = true;
        m_undoStack->push(new RowColCommand(targetModel, targetRow, rowsToInsert, true, true));
        m_isProgrammaticChange = false;
    }

    for (int i = 0; i < rowsToInsert; ++i) {
        QStringList list = lines.at(i).split(QLatin1Char(sep));
        for (int vc = 0; vc < targetCols; ++vc) {
            int c = m_view->horizontalHeader()->logicalIndex(vc);
            QString cellText = vc < list.size() ? list.at(vc).trimmed() : "";
            m_isProgrammaticChange = true;
            targetModel->setData(targetModel->index(targetRow + i, c), cellText, Qt::EditRole);
            m_isProgrammaticChange = false;
        }
    }
}

void EditableGridWidget::insertRows(int count, int atRow)
{
    QAbstractItemModel *model = m_view->model();
    if (!model || colCount() <= 0 || count <= 0) return;

    QAbstractItemModel *targetModel = model;
    int targetRow = atRow;
    if (auto *proxy = qobject_cast<QSortFilterProxyModel*>(model)) {
        targetModel = proxy->sourceModel();
        if (atRow < proxy->rowCount()) {
            QModelIndex proxyIdx = proxy->index(atRow, 0);
            targetRow = proxy->mapToSource(proxyIdx).row();
        } else {
            targetRow = targetModel->rowCount();
        }
    }

    if (m_mode == GridMode::LiveDatabase) {
        targetModel->insertRows(targetRow, count);
        return;
    }
    m_undoStack->push(new RowColCommand(targetModel, targetRow, count, true, true));
}

void EditableGridWidget::deleteSelectedRows()
{
    QAbstractItemModel *model = m_view->model();
    if (!model || !m_view->selectionModel()) return;
    QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) return;

    QAbstractItemModel *targetModel = model;
    QSortFilterProxyModel *proxy = qobject_cast<QSortFilterProxyModel*>(model);
    if (proxy) {
        targetModel = proxy->sourceModel();
    }

    QSet<int> rowsSet;
    for (const auto &index : sel) {
        if (proxy) {
            rowsSet.insert(proxy->mapToSource(index).row());
        } else {
            rowsSet.insert(index.row());
        }
    }
    QList<int> rowsToDelete = rowsSet.values();
    std::sort(rowsToDelete.begin(), rowsToDelete.end(), std::greater<int>());

    if (m_mode == GridMode::LiveDatabase) {
        for (int r : rowsToDelete)
            targetModel->removeRow(r);
        return;
    }

    m_undoStack->beginMacro("Delete rows");
    for (int r : rowsToDelete)
        m_undoStack->push(new RowColCommand(targetModel, r, 1, true, false));
    m_undoStack->endMacro();
}

void EditableGridWidget::copyColumnSelection(char separator)
{
    QAbstractItemModel *model = m_view->model();
    if (!model || !m_view->selectionModel()) return;
    QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) return;

    int minCol = colCount(), maxCol = -1;
    for (const auto &index : sel) {
        if (index.column() < minCol) minCol = index.column();
        if (index.column() > maxCol) maxCol = index.column();
    }

    QString outText;
    for (int r = 0; r < rowCount(); ++r) {
        QStringList rowItems;
        for (int c = minCol; c <= maxCol; ++c)
            rowItems << model->data(model->index(r, c), Qt::DisplayRole).toString();
        outText += rowItems.join(separator) + "\n";
    }
    QApplication::clipboard()->setText(outText);
}

void EditableGridWidget::pasteColumnSelectionAt(int atCol)
{
    QAbstractItemModel *model = m_view->model();
    if (!model) return;

    QAbstractItemModel *targetModel = model;
    if (auto *proxy = qobject_cast<QSortFilterProxyModel*>(model))
        targetModel = proxy->sourceModel();

    QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) return;

    QStringList lines = text.split(QRegularExpression("\r?\n"));
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    if (lines.isEmpty()) return;

    if (lines.size() != rowCount()) {
        QMessageBox::warning(this, "Paste Error",
            QString("Clipboard contains %1 rows, but table has %2.")
            .arg(lines.size()).arg(rowCount()));
        return;
    }

    char sep = '\t';
    int colsToInsert = lines.first().split(QLatin1Char('\t')).size();
    if (colsToInsert <= 1) {
        sep = ',';
        colsToInsert = lines.first().split(QLatin1Char(',')).size();
    }

    if (m_mode == GridMode::LiveDatabase) {
        // Direct model insertion, no undo command wrapper
        targetModel->insertColumns(atCol, colsToInsert);
    } else {
        m_isProgrammaticChange = true;
        m_undoStack->push(new RowColCommand(targetModel, atCol, colsToInsert, false, true));
        m_isProgrammaticChange = false;
    }

    QSortFilterProxyModel *proxy = qobject_cast<QSortFilterProxyModel*>(model);
    for (int r = 0; r < rowCount(); ++r) {
        int targetRow = r;
        if (proxy) {
            QModelIndex proxyIdx = proxy->index(r, 0);
            targetRow = proxy->mapToSource(proxyIdx).row();
        }
        QStringList list = lines.at(r).split(QLatin1Char(sep));
        for (int c = 0; c < colsToInsert; ++c) {
            QString cellText = c < list.size() ? list.at(c).trimmed() : "";
            m_isProgrammaticChange = true;
            targetModel->setData(targetModel->index(targetRow, atCol + c), cellText, Qt::EditRole);
            m_isProgrammaticChange = false;
        }
    }
}

void EditableGridWidget::insertColumns(int count, int atCol)
{
    QAbstractItemModel *model = m_view->model();
    if (!model || rowCount() <= 0 || count <= 0) return;

    QAbstractItemModel *targetModel = model;
    if (auto *proxy = qobject_cast<QSortFilterProxyModel*>(model))
        targetModel = proxy->sourceModel();

    if (m_mode == GridMode::LiveDatabase) {
        targetModel->insertColumns(atCol, count);
        return;
    }
    m_undoStack->push(new RowColCommand(targetModel, atCol, count, false, true));
}

void EditableGridWidget::deleteSelectedColumns()
{
    QAbstractItemModel *model = m_view->model();
    if (!model || !m_view->selectionModel()) return;
    QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) return;

    QAbstractItemModel *targetModel = model;
    if (auto *proxy = qobject_cast<QSortFilterProxyModel*>(model))
        targetModel = proxy->sourceModel();

    QSet<int> colsSet;
    for (const auto &index : sel) colsSet.insert(index.column());
    QList<int> colsToDelete = colsSet.values();
    std::sort(colsToDelete.begin(), colsToDelete.end(), std::greater<int>());

    if (m_mode == GridMode::LiveDatabase) {
        for (int c : colsToDelete)
            targetModel->removeColumn(c);
        return;
    }

    m_undoStack->beginMacro("Delete cols");
    for (int c : colsToDelete)
        m_undoStack->push(new RowColCommand(targetModel, c, 1, false, false));
    m_undoStack->endMacro();
}

// ---------------------------------------------------------------------------
// Context Menus
// ---------------------------------------------------------------------------

void EditableGridWidget::showRowContextMenu(const QPoint &pos)
{
    QAbstractItemModel *model = m_view->model();
    if (!model) return;

    QMenu menu(this);

    // --- Copy cell / Copy row ---
    QModelIndex clickedIdx = m_view->indexAt(pos);
    QAction *actCopyCell = nullptr;
    QAction *actCopyRow = nullptr;

    if (clickedIdx.isValid()) {
        actCopyCell = menu.addAction(QStringLiteral("Copy cell"));
        actCopyRow = menu.addAction(QStringLiteral("Copy row"));
        menu.addSeparator();
    }

    // --- Selection-based actions ---
    QAction *actCopyTSV = nullptr, *actCopyCSV = nullptr, *actDelete = nullptr;
    QAction *actInsertAbove = nullptr, *actInsertBelow = nullptr;
    QAction *actPasteAbove = nullptr, *actPasteBelow = nullptr;

    int minRow = rowCount(), maxRow = -1, numRows = 0;
    QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
    if (!sel.isEmpty()) {
        QSet<int> rows;
        for (const auto &index : sel) {
            rows.insert(index.row());
            if (index.row() < minRow) minRow = index.row();
            if (index.row() > maxRow) maxRow = index.row();
        }
        numRows = rows.size();
        actCopyTSV = menu.addAction("Copy Selection as TSV");
        actCopyCSV = menu.addAction("Copy Selection as CSV");
        menu.addSeparator();
        actDelete = menu.addAction("Delete Selected Rows");
    } else {
        int clickedRow = m_view->rowAt(pos.y());
        if (clickedRow >= 0) { minRow = maxRow = clickedRow; numRows = 1; }
    }

    if (numRows > 0) {
        menu.addSeparator();
        QString rowStr = (numRows == 1) ? "1 row" : QString("%1 rows").arg(numRows);
        actInsertAbove = menu.addAction(QString("Insert %1 above").arg(rowStr));
        actInsertBelow = menu.addAction(QString("Insert %1 below").arg(rowStr));
        if (!QApplication::clipboard()->text().isEmpty()) {
            menu.addSeparator();
            actPasteAbove = menu.addAction("Insert from Clipboard above");
            actPasteBelow = menu.addAction("Insert from Clipboard below");
        }
    }

    // --- Extra callback (KV binary ops, etc.) ---
    if (m_extraMenuCallback && clickedIdx.isValid()) {
        menu.addSeparator();
        m_extraMenuCallback(&menu, clickedIdx);
    }

    // --- Filters and Dark theme toggles ---
    menu.addSeparator();

    QAction *actFilters = nullptr;
    if (m_filterRow) {
        actFilters = menu.addAction(QStringLiteral("Filters"));
        actFilters->setCheckable(true);
        actFilters->setChecked(m_filterRow->isFilterVisible());
    }

    QAction *actDarkTheme = nullptr;
    if (m_themeToggleEnabled) {
        actDarkTheme = menu.addAction(QStringLiteral("Dark theme"));
        actDarkTheme->setCheckable(true);
        actDarkTheme->setChecked(ThemeManager::isDark());
    }

    QAction *res = menu.exec(m_view->viewport()->mapToGlobal(pos));
    QTimer::singleShot(0, this, [this]() { m_fm->restoreViewFocus(); });
    if (!res) return;

    // Handle results
    if (res == actCopyCell && clickedIdx.isValid()) {
        QString text = model->data(clickedIdx, Qt::DisplayRole).toString();
        QApplication::clipboard()->setText(text);
    } else if (res == actCopyRow && clickedIdx.isValid()) {
        int row = clickedIdx.row();
        QStringList cells;
        for (int c = 0; c < model->columnCount(); ++c)
            cells << model->data(model->index(row, c), Qt::DisplayRole).toString();
        QApplication::clipboard()->setText(cells.join(QChar('\t')));
    } else if (res == actCopyTSV) copySelection('\t');
    else if (res == actCopyCSV) copySelection(',');
    else if (res == actDelete) deleteSelectedRows();
    else if (res == actInsertAbove) insertRows(numRows, minRow);
    else if (res == actInsertBelow) insertRows(numRows, maxRow + 1);
    else if (res == actPasteAbove) pasteSelectionAt(minRow);
    else if (res == actPasteBelow) pasteSelectionAt(maxRow + 1);
    else if (res == actFilters && m_filterRow) {
        m_filterRow->setFilterVisible(!m_filterRow->isFilterVisible());
    } else if (res == actDarkTheme) {
        ThemeManager::toggleTheme(window());
    }
}

void EditableGridWidget::showColumnContextMenu(const QPoint &pos)
{
    QAbstractItemModel *model = m_view->model();
    if (!model) return;

    QMenu menu(this);
    QAction *actCopy = nullptr, *actDelete = nullptr;
    QAction *actInsertLeft = nullptr, *actInsertRight = nullptr;
    QAction *actPasteLeft = nullptr, *actPasteRight = nullptr;

    int minCol = colCount(), maxCol = -1, numCols = 0;
    QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
    if (!sel.isEmpty()) {
        QSet<int> cols;
        for (const auto &index : sel) {
            cols.insert(index.column());
            if (index.column() < minCol) minCol = index.column();
            if (index.column() > maxCol) maxCol = index.column();
        }
        numCols = cols.size();
        actCopy = menu.addAction("Copy Columns");
        menu.addSeparator();
        actDelete = menu.addAction("Delete Selected Columns");
    } else {
        int clickedCol = m_view->columnAt(pos.x());
        if (clickedCol >= 0) { minCol = maxCol = clickedCol; numCols = 1; }
    }

    if (numCols > 0) {
        menu.addSeparator();
        QString colStr = (numCols == 1) ? "1 col" : QString("%1 cols").arg(numCols);
        actInsertLeft = menu.addAction(QString("Insert %1 left").arg(colStr));
        actInsertRight = menu.addAction(QString("Insert %1 right").arg(colStr));
        if (!QApplication::clipboard()->text().isEmpty()) {
            menu.addSeparator();
            actPasteLeft = menu.addAction("Insert from Clipboard left");
            actPasteRight = menu.addAction("Insert from Clipboard right");
        }
    }

    QAction *res = menu.exec(m_view->horizontalHeader()->viewport()->mapToGlobal(pos));
    QTimer::singleShot(0, this, [this]() { m_fm->restoreViewFocus(); });
    if (!res) return;

    if (res == actCopy) copyColumnSelection('\t');
    else if (res == actDelete) deleteSelectedColumns();
    else if (res == actInsertLeft) insertColumns(numCols, minCol);
    else if (res == actInsertRight) insertColumns(numCols, maxCol + 1);
    else if (res == actPasteLeft) pasteColumnSelectionAt(minCol);
    else if (res == actPasteRight) pasteColumnSelectionAt(maxCol + 1);
}

} // namespace QtWlPlugin
