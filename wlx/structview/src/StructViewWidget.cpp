#include "StructViewWidget.h"

#include <QKeyEvent>
#include <QCoreApplication>
#include <wlxbase_wlqt/FocusManager.h>
#include <wlxbase_wlqt/PluginToolBar.h>
#include <wlxbase_wlqt/EditableGridWidget.h>
#include <wlxbase_wlqt/ScopedFindReplacePanel.h>
#include <wlxbase_wlqt/FilterableHeaderView.h>
#include <wlxbase_wlqt/PluginStatusBar.h>
#include <wlxbase_wlqt/PluginSplitView.h>
#include <wlxbase_wlqt/ThemeManager.h>
#include <wlxbase_wlqt/EncodingUtils.h>
#include <wlxbase_wlqt/SequentialRowProxyModel.h>

#include <QUndoStack>
#include <QUndoCommand>
#include <QStyledItemDelegate>
#include <QPrinter>
#include <QPrintDialog>
#include <QTextDocument>
#include <QDesktopServices>
#include <QIcon>
#include <QStyle>
#include <QUrl>
#include <QFileDialog>
#include <QCborValue>
#include <QJsonDocument>
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonArray>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QTableView>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>

using namespace QtWlPlugin;

namespace {
class StructEditCommand : public QUndoCommand {
public:
    StructEditCommand(QStandardItemModel *model,
                      int rowId,
                      int col,
                      const QVariant &oldVal,
                      const QVariant &newVal)
        : m_model(model), m_rowId(rowId), m_col(col)
        , m_oldVal(oldVal), m_newVal(newVal)
    {
        setText(QStringLiteral("Edit cell"));
    }

    void undo() override {
        applyValue(m_oldVal);
    }

    void redo() override {
        applyValue(m_newVal);
    }

private:
    void applyValue(const QVariant &value) {
        int visualRow = -1;
        for (int r = 0; r < m_model->rowCount(); ++r) {
            if (m_model->data(m_model->index(r, 0), Qt::UserRole + 100).toInt() == m_rowId) {
                visualRow = r;
                break;
            }
        }

        if (visualRow >= 0) {
            QModelIndex idx = m_model->index(visualRow, m_col);
            m_model->setData(idx, value, Qt::EditRole);
        }
    }

    QStandardItemModel *m_model;
    int m_rowId;
    int m_col;
    QVariant m_oldVal;
    QVariant m_newVal;
};

class StructEditDelegate : public QStyledItemDelegate {
public:
    StructEditDelegate(QUndoStack *undoStack, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_undoStack(undoStack) {}

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        QModelIndex srcIndex = index;
        QStandardItemModel *srcModel = qobject_cast<QStandardItemModel*>(model);
        if (auto *proxy = qobject_cast<const QAbstractProxyModel*>(model)) {
            srcIndex = proxy->mapToSource(index);
            srcModel = qobject_cast<QStandardItemModel*>(proxy->sourceModel());
        }

        if (!srcModel) {
            QStyledItemDelegate::setModelData(editor, model, index);
            return;
        }

        QVariant oldValue = srcModel->data(srcIndex, Qt::EditRole);

        // Let default delegate set the model data
        QStyledItemDelegate::setModelData(editor, model, index);

        QVariant newValue = srcModel->data(srcIndex, Qt::EditRole);

        if (oldValue != newValue) {
            int rowId = srcModel->data(srcModel->index(srcIndex.row(), 0), Qt::UserRole + 100).toInt();

            // Revert temporarily so push can apply it cleanly
            srcModel->setData(srcIndex, oldValue, Qt::EditRole);

            m_undoStack->push(new StructEditCommand(srcModel, rowId, srcIndex.column(), oldValue, newValue));
        }
    }

private:
    QUndoStack *m_undoStack;
};
}

StructViewWidget::StructViewWidget(QWidget *parent)
    : QWidget(parent)
    , m_fm(nullptr)
{
    m_undoStack = new QUndoStack(this);
    setupUi();

    // Apply saved theme
    ThemeManager::applyTheme(this, ThemeManager::currentTheme());
}

StructViewWidget::~StructViewWidget()
{
    // Disconnect all signals BEFORE child widgets start being destroyed.
    // Without this, Qt's arbitrary child destruction order can trigger
    // callbacks (e.g. selectionChanged, dataChanged) on half-destroyed objects,
    // causing crashes when the user navigates away with unsaved edits.
    if (m_treeView && m_treeView->selectionModel())
        disconnect(m_treeView->selectionModel(), nullptr, this, nullptr);
    if (m_gridModel)
        disconnect(m_gridModel, nullptr, this, nullptr);
    if (m_filterProxy)
        disconnect(m_filterProxy, nullptr, this, nullptr);
    if (m_fm)
        m_fm->setActive(false);
}

void StructViewWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- Create the primary view and FocusManager FIRST ---
    // (setupToolbar needs m_fm for shortcut registration)
    m_gridView = new QTableView;
    m_gridModel = new QStandardItemModel(this);
    m_filterProxy = new SequentialRowProxyModel(this);
    m_filterProxy->setSourceModel(m_gridModel);
    m_filterProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    // Install filterable header (must be before setModel)
    m_filterHeader = new FilterableHeaderView(Qt::Horizontal, m_gridView);
    m_filterHeader->setFilterEnabled(true);
    m_filterHeader->setStretchLastSection(true);
    m_gridView->setHorizontalHeader(m_filterHeader);

    m_gridView->setModel(m_filterProxy);
    m_gridView->setSortingEnabled(true);
    m_gridView->setAlternatingRowColors(true);
    m_gridView->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_gridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_gridView->setItemDelegate(new StructEditDelegate(m_undoStack, this));

    connect(m_filterHeader, &FilterableHeaderView::filterChanged, this,
            [this](int column, const QString &text) {
        if (m_filterProxy) {
            m_filterProxy->setFilterKeyColumn(column);
            m_filterProxy->setFilterFixedString(text);
            updateStatusBar();
        }
    });

    m_fm = new FocusManager(this, m_gridView, this);
    m_fm->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q), FocusManager::Always, [this]() {
        m_fm->setActive(false);
        if (QWidget *top = m_fm->pluginRoot()->window()) {
            QKeyEvent *ke = new QKeyEvent(QEvent::KeyPress, Qt::Key_Q, Qt::ControlModifier);
            QCoreApplication::postEvent(top, ke);
        }
        return true;
    });
    m_grid = new EditableGridWidget(m_gridView, GridMode::MemoryDocument, m_fm, this);

    // --- Toolbar ---
    setupToolbar();
    mainLayout->addWidget(m_toolbar);

    // --- Left panel: Tree view ---
    m_treeView = new QTreeView;
    m_treeModel = new QStandardItemModel(this);
    m_treeView->setModel(m_treeModel);
    m_treeView->setHeaderHidden(true);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setMinimumWidth(120);

    // --- Right panel: Tabs ---
    m_tabWidget = new QTabWidget;

    // Grid tab
    auto *gridContainer = new QWidget;
    auto *gridLayout = new QVBoxLayout(gridContainer);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(0);

    m_grid->setThemeToggleEnabled(true);

    gridLayout->addWidget(m_grid);

    m_tabWidget->addTab(gridContainer, QStringLiteral("Grid"));

    // Text tab (read-only)
    m_textView = new QPlainTextEdit;
    m_textView->setReadOnly(true);
    m_textView->setFont(QFont(QStringLiteral("monospace"), 10));
    m_textView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_tabWidget->addTab(m_textView, QStringLiteral("Text"));

    // --- Split view ---
    m_splitView = new PluginSplitView(m_treeView, m_tabWidget, this);
    mainLayout->addWidget(m_splitView, 1);

    // --- Find/replace ---
    setupFindReplace();
    mainLayout->addWidget(m_findReplace);

    // --- Status bar ---
    m_statusBar = new PluginStatusBar(this);
    mainLayout->addWidget(m_statusBar);

    // --- Connect tree selection ---
    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &StructViewWidget::onTreeNodeSelected);

    // --- Connect tab widget changes ---
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1) {
            updateTextTab();
        }
        if (m_actShowText) {
            QSignalBlocker blocker(m_actShowText);
            m_actShowText->setChecked(index == 1);
        }
    });
}

void StructViewWidget::setupToolbar()
{
    m_toolbar = new PluginToolBar(m_fm, this);

    // Save (Ctrl+S)
    auto *actSave = m_toolbar->addToolAction(
        QStringLiteral("Save"),
        QKeySequence::Save,
        FocusManager::Always,
        QStringLiteral("document-save"));
    connect(actSave, &QAction::triggered, this, &StructViewWidget::onSave);

    // Save As (Ctrl+Shift+S)
    auto *actSaveAs = m_toolbar->addToolAction(
        QStringLiteral("Save As"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S),
        FocusManager::Always,
        QStringLiteral("document-save-as"));
    connect(actSaveAs, &QAction::triggered, this, &StructViewWidget::onSaveAs);

    // Undo (Ctrl+Z)
    auto *actUndo = m_toolbar->addToolAction(
        QStringLiteral("Undo"),
        QKeySequence::Undo,
        FocusManager::Always,
        QStringLiteral("edit-undo"));
    connect(actUndo, &QAction::triggered, m_undoStack, &QUndoStack::undo);

    // Redo (Ctrl+Y, Ctrl+Shift+Z)
    auto *actRedo = m_toolbar->addToolAction(
        QStringLiteral("Redo"),
        QKeySequence::Redo,
        FocusManager::Always,
        QStringLiteral("edit-redo"));
    actRedo->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    connect(actRedo, &QAction::triggered, m_undoStack, &QUndoStack::redo);

    // Print (Ctrl+P)
    auto *actPrint = m_toolbar->addToolAction(
        QStringLiteral("Print"),
        QKeySequence::Print,
        FocusManager::Always,
        QStringLiteral("document-print"));
    connect(actPrint, &QAction::triggered, this, &StructViewWidget::onPrint);

    // Reload (F5)
    auto *actReload = m_toolbar->addToolAction(
        QStringLiteral("Reload"),
        QKeySequence(Qt::Key_F5),
        FocusManager::Always,
        QStringLiteral("view-refresh"));
    connect(actReload, &QAction::triggered, this, &StructViewWidget::onReload);

    // Show Text
    m_actShowText = m_toolbar->addToolAction(
        QStringLiteral("Show Text"),
        QKeySequence(),
        FocusManager::Always,
        QStringLiteral("visibility"));
    m_actShowText->setCheckable(true);
    connect(m_actShowText, &QAction::toggled, this, [this](bool on) {
        m_tabWidget->setCurrentIndex(on ? 1 : 0);
    });

    // Word Wrap / Line Wrap
    auto *actWrap = m_toolbar->addToolAction(
        QStringLiteral("Word Wrap"),
        QKeySequence(),
        FocusManager::Always,
        QStringLiteral("format-text-direction-ltr"));
    actWrap->setCheckable(true);
    connect(actWrap, &QAction::toggled, this, [this](bool on) {
        m_grid->setWordWrap(on);
        if (m_textView) {
            m_textView->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        }
    });

    // Open Externally (Ctrl+O)
    auto *actOpen = m_toolbar->addToolAction(
        QStringLiteral("Open Externally"),
        QKeySequence(Qt::CTRL | Qt::Key_O),
        FocusManager::Always,
        QStringLiteral("document-open"));
    connect(actOpen, &QAction::triggered, this, &StructViewWidget::onOpenExternally);
}

void StructViewWidget::setupFindReplace()
{
    m_findReplace = new ScopedFindReplacePanel(m_fm, this);
    auto *actFind = m_toolbar->addToolAction(
        QStringLiteral("Find"),
        QKeySequence::Find,
        0,
        QStringLiteral("edit-find"));
    connect(actFind, &QAction::triggered, this, [this]() {
        m_findReplace->showPanel(!m_findReplace->isPanelVisible());
    });
    connect(m_findReplace, &ScopedFindReplacePanel::findRequested, this,
            &StructViewWidget::onFind);
}

bool StructViewWidget::loadFile(const QString &filepath)
{
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QByteArray data = file.readAll();
    file.close();

    m_engine = TextFormatEngine::createForFile(filepath);
    if (!m_engine) return false;

    if (!m_engine->parse(data))
        return false;

    m_filepath = filepath;

    // Populate tree
    populateTree();

    // Set text tab
    m_textView->setPlainText(m_engine->rawText());

    // Status bar
    QFileInfo fi(filepath);
    m_statusBar->setFormatInfo(m_engine->formatName());
    m_statusBar->setEncoding(EncodingUtils::detectEncoding(data));

    // Select root node
    if (m_treeModel->rowCount() > 0) {
        m_treeView->setCurrentIndex(m_treeModel->index(0, 0));
        m_treeView->expandAll();
    }

    return true;
}

void StructViewWidget::populateTree()
{
    m_treeModel->clear();
    DocumentNode *root = m_engine->rootNode();
    if (!root) return;

    auto *rootItem = new QStandardItem(root->name);
    rootItem->setData(QVariant::fromValue(reinterpret_cast<quintptr>(root)),
                      Qt::UserRole);
    m_treeModel->appendRow(rootItem);

    populateTreeNode(rootItem, root);
}

void StructViewWidget::populateTreeNode(QStandardItem *parentItem, DocumentNode *node)
{
    for (auto *child : node->children) {
        auto *item = new QStandardItem(child->name);
        item->setData(QVariant::fromValue(reinterpret_cast<quintptr>(child)),
                      Qt::UserRole);
        item->setEditable(false);

        // Icon: folder for containers, document for leaves
        if (child->isContainer()) {
            item->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
        } else {
            item->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
        }

        parentItem->appendRow(item);
        populateTreeNode(item, child);
    }
}

void StructViewWidget::onTreeNodeSelected(const QModelIndex &current,
                                           const QModelIndex & /*previous*/)
{
    if (!current.isValid()) return;

    auto ptr = current.data(Qt::UserRole).value<quintptr>();
    auto *node = reinterpret_cast<DocumentNode*>(ptr);
    if (!node) return;

    showNodeData(node);
}

void StructViewWidget::showNodeData(DocumentNode *node)
{
    m_currentNode = node;

    m_gridModel->clear();
    if (m_undoStack)
        m_undoStack->clear();

    if (node->columnNames.isEmpty() && node->rows.isEmpty()) {
        // No grid data — show message
        m_gridModel->setHorizontalHeaderLabels({QStringLiteral("Info")});
        auto *item = new QStandardItem(
            QStringLiteral("Select a child node to view data."));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_gridModel->appendRow(item);
    } else {
        // Set column headers
        m_gridModel->setHorizontalHeaderLabels(node->columnNames);

        // Populate rows
        int rowId = 0;
        for (const auto &row : node->rows) {
            QList<QStandardItem*> items;
            for (const auto &val : row) {
                auto *item = new QStandardItem(val.toString());
                if (!node->editable)
                    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                items.append(item);
            }
            if (!items.isEmpty()) {
                items.first()->setData(rowId++, Qt::UserRole + 100);
            }
            m_gridModel->appendRow(items);
        }
    }

    m_filterHeader->clearFilters();

    updateStatusBar();

    // Hide header if virtual (e.g. Key/Value, Name/Value, or Value only)
    bool isVirtual = false;
    if (node->columnNames.isEmpty()) {
        isVirtual = true;
    } else if (node->columnNames.size() == 1 && node->columnNames[0] == QStringLiteral("Value")) {
        isVirtual = true;
    } else if (node->columnNames.size() == 2 &&
               (node->columnNames[0] == QStringLiteral("Key") || node->columnNames[0] == QStringLiteral("Name")) &&
               node->columnNames[1] == QStringLiteral("Value")) {
        isVirtual = true;
    }

    m_gridView->horizontalHeader()->show();
    m_filterHeader->setHeaderVisible(!isVirtual);

    // Resize columns
    m_gridView->horizontalHeader()->setStretchLastSection(true);
    m_gridView->resizeColumnsToContents();
}

void StructViewWidget::updateStatusBar()
{
    int total = m_gridModel->rowCount();
    int filtered = m_filterProxy->rowCount();
    m_statusBar->setRowCount(filtered, total);
}

void StructViewWidget::syncGridToNode()
{
    if (m_currentNode && !m_currentNode->columnNames.isEmpty()) {
        m_currentNode->rows.clear();
        for (int r = 0; r < m_gridModel->rowCount(); ++r) {
            QVector<QVariant> row;
            for (int c = 0; c < m_gridModel->columnCount(); ++c) {
                auto *item = m_gridModel->item(r, c);
                row.append(item ? item->data(Qt::DisplayRole) : QVariant());
            }
            m_currentNode->rows.append(std::move(row));
        }
    }
}

bool StructViewWidget::saveFile()
{
    return saveFileAs(m_filepath);
}

bool StructViewWidget::saveFileAs(const QString &path)
{
    if (!m_engine) return false;

    // Sync current grid data back to the node
    syncGridToNode();

    QByteArray output = m_engine->serialize();
    if (output.isEmpty()) return false;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    file.write(output);
    file.close();

    // Update m_textView so it matches the saved content
    if (m_engine->formatName() == QStringLiteral("CBOR")) {
        QCborParserError err;
        QCborValue cbor = QCborValue::fromCbor(output, &err);
        if (err.error == QCborError::NoError) {
            QJsonValue jsonVal = cbor.toJsonValue();
            QJsonDocument doc;
            if (jsonVal.isObject())
                doc = QJsonDocument(jsonVal.toObject());
            else if (jsonVal.isArray())
                doc = QJsonDocument(jsonVal.toArray());
            m_textView->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
        }
    } else {
        m_textView->setPlainText(QString::fromUtf8(output));
    }

    return true;
}

void StructViewWidget::onSave()
{
    if (!saveFile()) {
        QMessageBox::warning(this, QStringLiteral("Save Error"),
                             QStringLiteral("Could not save file."));
    }
}

void StructViewWidget::onSaveAs()
{
    QString filter;
    if (m_engine) {
        if (m_engine->formatName() == QStringLiteral("JSON")) {
            filter = QStringLiteral("JSON Files (*.json);;All Files (*)");
        } else if (m_engine->formatName() == QStringLiteral("XML")) {
            filter = QStringLiteral("XML Files (*.xml);;All Files (*)");
        } else if (m_engine->formatName() == QStringLiteral("YAML")) {
            filter = QStringLiteral("YAML Files (*.yaml *.yml);;All Files (*)");
        } else if (m_engine->formatName() == QStringLiteral("TOML")) {
            filter = QStringLiteral("TOML Files (*.toml);;All Files (*)");
        } else if (m_engine->formatName() == QStringLiteral("INI")) {
            filter = QStringLiteral("INI Files (*.ini);;All Files (*)");
        } else if (m_engine->formatName() == QStringLiteral("CBOR")) {
            filter = QStringLiteral("CBOR Files (*.cbor);;All Files (*)");
        }
    }
    if (filter.isEmpty()) {
        filter = QStringLiteral("All Files (*)");
    }
    QString filePath = QFileDialog::getSaveFileName(this, QStringLiteral("Save As"), m_filepath, filter);
    if (!filePath.isEmpty()) {
        if (saveFileAs(filePath)) {
            m_filepath = filePath;
        } else {
            QMessageBox::warning(this, QStringLiteral("Save Error"),
                                 QStringLiteral("Could not save file."));
        }
    }
}

void StructViewWidget::onReload()
{
    if (!m_filepath.isEmpty()) {
        if (loadFile(m_filepath)) {
            m_undoStack->clear();
        } else {
            QMessageBox::warning(this, QStringLiteral("Reload Error"),
                                 QStringLiteral("Could not reload file."));
        }
    }
}

void StructViewWidget::onPrint()
{
    if (!m_engine) return;

    // Sync and serialize so raw text is up to date
    syncGridToNode();
    QByteArray output = m_engine->serialize();

    QString printText;
    if (m_engine->formatName() == QStringLiteral("CBOR")) {
        QCborParserError err;
        QCborValue cbor = QCborValue::fromCbor(output, &err);
        if (err.error == QCborError::NoError) {
            QJsonValue jsonVal = cbor.toJsonValue();
            QJsonDocument doc;
            if (jsonVal.isObject())
                doc = QJsonDocument(jsonVal.toObject());
            else if (jsonVal.isArray())
                doc = QJsonDocument(jsonVal.toArray());
            printText = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
        } else {
            printText = QStringLiteral("<Failed to parse CBOR to JSON>");
        }
    } else {
        printText = QString::fromUtf8(output);
    }

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(QStringLiteral("Print Document"));
    
    if (dialog.exec() == QDialog::Accepted) {
        QTextDocument doc;
        doc.setPlainText(printText);
        doc.print(&printer);
    }
}

void StructViewWidget::onOpenExternally()
{
    if (!m_filepath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_filepath));
    }
}

void StructViewWidget::updateTextTab()
{
    if (!m_engine) return;
    
    // Sync current grid data
    syncGridToNode();
    
    QByteArray output = m_engine->serialize();
    if (m_engine->formatName() == QStringLiteral("CBOR")) {
        // CBOR should show indented JSON
        QCborParserError err;
        QCborValue cbor = QCborValue::fromCbor(output, &err);
        if (err.error == QCborError::NoError) {
            QJsonValue jsonVal = cbor.toJsonValue();
            QJsonDocument doc;
            if (jsonVal.isObject())
                doc = QJsonDocument(jsonVal.toObject());
            else if (jsonVal.isArray())
                doc = QJsonDocument(jsonVal.toArray());
            m_textView->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
        } else {
            m_textView->setPlainText(QStringLiteral("<Failed to parse CBOR to JSON>"));
        }
    } else {
        m_textView->setPlainText(QString::fromUtf8(output));
    }
}

void StructViewWidget::onFind(bool forward)
{
    // Delegate to grid search
    if (!m_gridView->model()) return;

    QString searchText = m_findReplace->findText();
    if (searchText.isEmpty()) return;

    QModelIndex start = m_gridView->currentIndex();
    if (!start.isValid())
        start = m_gridView->model()->index(0, 0);

    QAbstractItemModel *model = m_gridView->model();
    int rows = model->rowCount();
    int cols = model->columnCount();
    int startRow = start.row();
    int startCol = start.column();

    // Search from current position
    for (int i = 0; i < rows * cols; ++i) {
        int offset = forward ? (i + 1) : (rows * cols - i - 1);
        int r = (startRow + offset / cols) % rows;
        int c = (startCol + offset % cols) % cols;
        QModelIndex idx = model->index(r, c);
        QString text = model->data(idx, Qt::DisplayRole).toString();
        if (text.contains(searchText, Qt::CaseInsensitive)) {
            m_gridView->setCurrentIndex(idx);
            m_gridView->scrollTo(idx);
            return;
        }
    }
}

FocusManager *StructViewWidget::focusManager() const { return m_fm; }
EditableGridWidget *StructViewWidget::grid() const { return m_grid; }

QString StructViewWidget::getSelectionAsText(char sep)
{
    return m_grid->getSelectionAsText(sep);
}
