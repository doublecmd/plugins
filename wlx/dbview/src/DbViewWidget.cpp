#include "DbViewWidget.h"
#include "DbEngine.h"
#include "KeyValueModel.h"
#include "DuckDbModel.h"
#include "MdbEngine.h"
#include "SqliteTableModel.h"
#include <QKeyEvent>
#include <QCoreApplication>

#include <wlxbase_wlqt/FocusManager.h>
#include <wlxbase_wlqt/PluginToolBar.h>
#include <wlxbase_wlqt/EditableGridWidget.h>
#include <wlxbase_wlqt/FindReplacePanel.h>
#include <wlxbase_wlqt/FilterableHeaderView.h>
#include <wlxbase_wlqt/PluginStatusBar.h>
#include <wlxbase_wlqt/ThemeManager.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QAbstractItemModel>
#include <QMenu>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QPushButton>
#include <QStyle>
#include <QShortcut>
#include <iostream>

using namespace QtWlPlugin;

namespace {
class DbSortFilterProxyModel : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override {
        if (auto *duckModel = qobject_cast<DuckDbModel*>(sourceModel())) {
            duckModel->sort(column, order);
        } else {
            QSortFilterProxyModel::sort(column, order);
        }
    }
};
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

DbViewWidget::DbViewWidget(QWidget *parent)
    : QWidget(parent)
{
}

DbViewWidget::~DbViewWidget()
{
    if (m_fm) {
        m_fm->setActive(false);
        delete m_fm;
    }
    if (m_schemaTree)
        disconnect(m_schemaTree, nullptr, this, nullptr);
    if (m_filterProxy)
        m_filterProxy->setSourceModel(nullptr);

    if (m_sqlResultsView) {
        QAbstractItemModel *m = m_sqlResultsView->model();
        m_sqlResultsView->setModel(nullptr);
        if (m) {
            delete m;
        }
    }
}

// ---------------------------------------------------------------------------
// File loading
// ---------------------------------------------------------------------------

void DbViewWidget::discardPendingChanges()
{
    if (m_engine && m_engine->supportsSubmitRevert() && !m_engine->isReadOnly())
        m_engine->revertAll();
}

bool DbViewWidget::loadFile(const QString &filepath)
{
    m_loadedFilePath = filepath;
    m_engine = DbEngine::createForFile(filepath);
    if (!m_engine)
        return false;

    QStringList tables = m_engine->tableNames();
    QStringList views = m_engine->viewNames();

    if (tables.isEmpty() && views.isEmpty())
        return false;

    QString firstTable = tables.isEmpty() ? views.first() : tables.first();
    setupUi(firstTable);

    // FIXME (pre-existing, unrelated to this change): ThemeManager has no
    // detectSystemTheme() — swapped to currentTheme() only to unblock
    // building dbview_qt6 at all. Confirm the intended behavior (OS theme
    // detection vs. the app's last-toggled theme) and fix properly.
    ThemeManager::applyTheme(this, ThemeManager::currentTheme());
    return true;
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void DbViewWidget::setupUi(const QString &firstTable)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Primary view
    m_tableView = new QTableView;
    m_tableView->setObjectName(QStringLiteral("mainTableView"));
    m_filterHeader = new FilterableHeaderView(Qt::Horizontal, m_tableView);
    m_filterHeader->setFilterEnabled(true);
    m_filterHeader->setStretchLastSection(true);
    m_tableView->setHorizontalHeader(m_filterHeader);

    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSortingEnabled(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    m_fm = new FocusManager(this, m_tableView, this);
    m_fm->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q), FocusManager::Always, [this]() {
        m_fm->setActive(false);
        if (QWidget *top = m_fm->pluginRoot()->window()) {
            QKeyEvent *ke = new QKeyEvent(QEvent::KeyPress, Qt::Key_Q, Qt::ControlModifier);
            QCoreApplication::postEvent(top, ke);
        }
        return true;
    });

    setupToolbar();
    mainLayout->addWidget(m_toolbar);

    // Read-only constraints on grid
    if (m_engine->isReadOnly()) {
        m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    } else {
        m_tableView->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    }

    // Splitter for top and bottom
    m_mainSplitter = new QSplitter(Qt::Vertical, this);

    // Data Browser (top panel)
    QWidget *dataBrowserWidget = new QWidget;
    QHBoxLayout *dataBrowserLayout = new QHBoxLayout(dataBrowserWidget);
    dataBrowserLayout->setContentsMargins(0, 0, 0, 0);
    dataBrowserLayout->setSpacing(0);

    QSplitter *browserSplitter = new QSplitter(Qt::Horizontal, dataBrowserWidget);

    // Schema navigation tree (left)
    m_schemaTree = new QTreeView;
    m_schemaTree->setHeaderHidden(true);
    m_schemaTree->setMinimumWidth(150);
    m_schemaTree->setMaximumWidth(350);
    populateSchemaTree();

    connect(m_schemaTree, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        QStandardItemModel *model = qobject_cast<QStandardItemModel*>(m_schemaTree->model());
        if (!model) return;
        QStandardItem *item = model->itemFromIndex(index);
        if (!item) return;

        QVariant tVar = item->data(Qt::UserRole);
        if (tVar.isValid()) {
            QString tableName = tVar.toString();
            if (!tableName.isEmpty() && tableName != m_engine->currentTableName()) {
                rebuildGrid(tableName);
                if (m_filterHeader)
                    m_filterHeader->clearFilters();
            }
        }
    });

    browserSplitter->addWidget(m_schemaTree);

    // Grid Container (right)
    QWidget *rightContainer = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    rebuildGrid(firstTable);

    m_grid = new EditableGridWidget(m_tableView, GridMode::LiveDatabase, m_fm, this);

    connect(m_filterHeader, &FilterableHeaderView::filterChanged, this,
            [this](int column, const QString &text) {
        if (m_filterProxy) {
            m_filterProxy->setFilterKeyColumn(column);
            m_filterProxy->setFilterFixedString(text);
            updateStatusBar();
        }
    });

    setupContextMenu();
    rightLayout->addWidget(m_grid, 1);
    browserSplitter->addWidget(rightContainer);

    dataBrowserLayout->addWidget(browserSplitter);
    m_mainSplitter->addWidget(dataBrowserWidget);

    // SQL Console (bottom panel)
    m_sqlConsoleWidget = new QWidget;
    QVBoxLayout *sqlLayout = new QVBoxLayout(m_sqlConsoleWidget);
    sqlLayout->setContentsMargins(0, 4, 0, 0);
    sqlLayout->setSpacing(4);

    m_sqlEditor = new QPlainTextEdit;
    m_sqlEditor->setPlaceholderText(QStringLiteral("Enter custom SQL query (Ctrl+Return to execute)..."));
    m_sqlEditor->setMaximumHeight(100);
    m_sqlEditor->installEventFilter(this);
    m_fm->addInputWidget(m_sqlEditor);

    // SQL Toolbar
    QWidget *sqlToolbarWidget = new QWidget;
    QHBoxLayout *sqlToolbarLayout = new QHBoxLayout(sqlToolbarWidget);
    sqlToolbarLayout->setContentsMargins(4, 0, 4, 0);
    sqlToolbarLayout->setSpacing(6);

    QPushButton *btnExecute = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("Execute"));
    QPushButton *btnClear = new QPushButton(style()->standardIcon(QStyle::SP_DialogDiscardButton), QStringLiteral("Clear"));
    QPushButton *btnExport = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("Export"));

    connect(btnExecute, &QPushButton::clicked, this, &DbViewWidget::onExecuteSqlQuery);
    connect(btnClear, &QPushButton::clicked, this, &DbViewWidget::onClearSqlConsole);
    connect(btnExport, &QPushButton::clicked, this, &DbViewWidget::onExportSqlResults);

    sqlToolbarLayout->addWidget(btnExecute);
    sqlToolbarLayout->addWidget(btnClear);
    sqlToolbarLayout->addWidget(btnExport);
    sqlToolbarLayout->addStretch();

    // Results Stack: Page 0 = table view, Page 1 = message view
    m_sqlResultsStack = new QStackedWidget(m_sqlConsoleWidget);

    m_sqlResultsView = new QTableView(m_sqlResultsStack);
    m_sqlResultsView->setAlternatingRowColors(true);
    m_sqlResultsView->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_sqlResultsView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    m_sqlMessageView = new QTextEdit(m_sqlResultsStack);
    m_sqlMessageView->setReadOnly(true);
    m_fm->addInputWidget(m_sqlResultsView);
    m_fm->addInputWidget(m_sqlMessageView);

    m_sqlResultsStack->addWidget(m_sqlResultsView);
    m_sqlResultsStack->addWidget(m_sqlMessageView);

    sqlLayout->addWidget(m_sqlEditor);
    sqlLayout->addWidget(sqlToolbarWidget);
    sqlLayout->addWidget(m_sqlResultsStack, 1);

    m_mainSplitter->addWidget(m_sqlConsoleWidget);
    mainLayout->addWidget(m_mainSplitter, 1);

    // Show/Hide Bottom SQL Console depending on engine
    // We no longer hide m_schemaTree for single-table engines so tables list is visible.
    if (!m_engine->supportsSqlConsole()) {
        m_sqlConsoleWidget->hide();
    }

    // Find/replace
    setupFindReplace();
    mainLayout->addWidget(m_findPanel);
    m_findPanel->setVisible(false);

    // Status bar
    m_statusBar = new PluginStatusBar(this);
    m_statusBar->setFormatInfo(m_engine->engineName());
    mainLayout->addWidget(m_statusBar);

    updateStatusBar();
}

void DbViewWidget::populateSchemaTree()
{
    auto *model = new QStandardItemModel(m_schemaTree);

    QStandardItem *tablesRoot = new QStandardItem(style()->standardIcon(QStyle::SP_DirIcon), QStringLiteral("Tables"));
    QStandardItem *viewsRoot = new QStandardItem(style()->standardIcon(QStyle::SP_DirIcon), QStringLiteral("Views"));

    QStringList tables = m_engine->tableNames();
    QStringList views = m_engine->viewNames();

    for (const auto &t : tables) {
        QStandardItem *tableItem = new QStandardItem(style()->standardIcon(QStyle::SP_FileIcon), t);
        tableItem->setData(t, Qt::UserRole);

        // Columns
        QStandardItem *colsRoot = new QStandardItem(QStringLiteral("Columns"));
        QList<ColumnInfo> cols = m_engine->columnInfos(t);
        for (const auto &c : cols) {
            QString label = c.name + QStringLiteral(" : ") + c.type;
            if (c.isPrimaryKey) label += QStringLiteral(" [PK]");
            if (c.isForeignKey) label += QStringLiteral(" [FK]");
            QStandardItem *colItem = new QStandardItem(style()->standardIcon(QStyle::SP_FileDialogInfoView), label);
            colsRoot->appendRow(colItem);
        }
        tableItem->appendRow(colsRoot);

        // Indexes
        QStringList idxs = m_engine->indexes(t);
        if (!idxs.isEmpty()) {
            QStandardItem *idxsRoot = new QStandardItem(QStringLiteral("Indexes"));
            for (const auto &idx : idxs) {
                QStandardItem *idxItem = new QStandardItem(style()->standardIcon(QStyle::SP_ComputerIcon), idx);
                idxsRoot->appendRow(idxItem);
            }
            tableItem->appendRow(idxsRoot);
        }

        tablesRoot->appendRow(tableItem);
    }

    for (const auto &v : views) {
        QStandardItem *viewItem = new QStandardItem(style()->standardIcon(QStyle::SP_FileIcon), v);
        viewItem->setData(v, Qt::UserRole);

        QStandardItem *colsRoot = new QStandardItem(QStringLiteral("Columns"));
        QList<ColumnInfo> cols = m_engine->columnInfos(v);
        for (const auto &c : cols) {
            QString label = c.name + QStringLiteral(" : ") + c.type;
            QStandardItem *colItem = new QStandardItem(style()->standardIcon(QStyle::SP_FileDialogInfoView), label);
            colsRoot->appendRow(colItem);
        }
        viewItem->appendRow(colsRoot);
        viewsRoot->appendRow(viewItem);
    }

    if (tablesRoot->rowCount() > 0)
        model->appendRow(tablesRoot);
    if (viewsRoot->rowCount() > 0)
        model->appendRow(viewsRoot);

    m_schemaTree->setModel(model);
    m_schemaTree->expandToDepth(0);
}

void DbViewWidget::setupToolbar()
{
    m_toolbar = new PluginToolBar(m_fm, this);

    // Commit / Revert
    if (m_engine->supportsSubmitRevert()) {
        m_actSubmit = m_toolbar->addToolAction(
            QStringLiteral("Commit"),
            QKeySequence(Qt::CTRL | Qt::Key_S),
            FocusManager::Always);
        connect(m_actSubmit, &QAction::triggered, this, &DbViewWidget::onSubmitChanges);

        m_actRevert = m_toolbar->addToolAction(
            QStringLiteral("Revert"),
            QKeySequence(Qt::CTRL | Qt::Key_Z),
            FocusManager::WhenNoInput);
        connect(m_actRevert, &QAction::triggered, this, &DbViewWidget::onRevertChanges);

        // Register Ctrl+Shift+Z as secondary redo/revert shortcut
        QShortcut *redoShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), this);
        connect(redoShortcut, &QShortcut::activated, this, &DbViewWidget::onSubmitChanges);

        if (m_engine->isReadOnly()) {
            m_actSubmit->setEnabled(false);
            m_actRevert->setEnabled(false);
            redoShortcut->setEnabled(false);
        }
    }

    // Word wrap
    auto *actWrap = m_toolbar->addToolAction(QStringLiteral("Word Wrap"), QKeySequence(), 0);
    actWrap->setCheckable(true);
    connect(actWrap, &QAction::toggled, this, [this](bool on) {
        if (m_grid) m_grid->setWordWrap(on);
    });

    // Grid lines
    auto *actGrid = m_toolbar->addToolAction(QStringLiteral("Grid Lines"), QKeySequence(), 0);
    actGrid->setCheckable(true);
    actGrid->setChecked(true);
    connect(actGrid, &QAction::toggled, this, [this](bool on) {
        if (m_grid) m_grid->setShowGrid(on);
    });

    // Find
    auto *actFind = m_toolbar->addToolAction(
        QStringLiteral("Find"),
        QKeySequence(Qt::CTRL | Qt::Key_F),
        FocusManager::Always);
    connect(actFind, &QAction::triggered, this, [this]() {
        if (m_findPanel)
            m_findPanel->showPanel(!m_findPanel->isPanelVisible());
    });

    // Export table data
    auto *actExport = m_toolbar->addToolAction(
        QStringLiteral("Export"),
        QKeySequence(), 0);
    connect(actExport, &QAction::triggered, this, &DbViewWidget::onExportTableData);
}

void DbViewWidget::setupFindReplace()
{
    m_findPanel = new FindReplacePanel(m_fm, this);
    m_findPanel->setReplaceEnabled(false);

    connect(m_findPanel, &FindReplacePanel::findRequested,
            this, &DbViewWidget::onFind);
}

void DbViewWidget::setupContextMenu()
{
    m_grid->setExtraContextMenuCallback(
        [this](QMenu *menu, const QModelIndex &idx) {
        if (!idx.isValid()) return;

        QAbstractItemModel *model = m_tableView->model();
        auto *kvModel = qobject_cast<KeyValueModel*>(model);
        QModelIndex srcIdx = idx;
        if (auto *proxy = qobject_cast<QSortFilterProxyModel*>(model)) {
            kvModel = qobject_cast<KeyValueModel*>(proxy->sourceModel());
            srcIdx = proxy->mapToSource(idx);
        }

        // Hex toggle for KeyValueModel
        if (kvModel && srcIdx.column() == 1 && kvModel->isBinaryValue(srcIdx.row())) {
            int row = srcIdx.row();
            QAction *hexAction = menu->addAction(QStringLiteral("Toggle Hex View"));
            connect(hexAction, &QAction::triggered, this, [kvModel, row]() {
                bool currentlyHex = kvModel->data(kvModel->index(row, 1), Qt::DisplayRole)
                                         .toString().contains(QStringLiteral(" "));
                kvModel->setHexMode(row, !currentlyHex);
            });
        }

        // Save Cell to File (BLOB Export)
        QAction *saveAction = menu->addAction(QStringLiteral("Save Cell to File (BLOB Export)..."));
        connect(saveAction, &QAction::triggered, this, [this, idx]() {
            QByteArray data = getCellRawValue(idx);
            QString path = QFileDialog::getSaveFileName(nullptr, QStringLiteral("Save BLOB Value"));
            if (path.isEmpty()) return;
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(data);
                f.close();
            }
        });

        // Load File into Cell (BLOB Import) - only if NOT read-only
        if (!m_engine->isReadOnly()) {
            QAction *loadAction = menu->addAction(QStringLiteral("Load File into Cell (BLOB Import)..."));
            connect(loadAction, &QAction::triggered, this, [this, idx]() {
                QString path = QFileDialog::getOpenFileName(nullptr, QStringLiteral("Load BLOB Value"));
                if (path.isEmpty()) return;
                QFile f(path);
                if (f.open(QIODevice::ReadOnly)) {
                    QByteArray fileData = f.readAll();
                    f.close();
                    setCellRawValue(idx, fileData);
                }
            });
        }
    });
}

// ---------------------------------------------------------------------------
// BLOB helpers
// ---------------------------------------------------------------------------

bool DbViewWidget::isCellBinary(const QModelIndex &idx) const
{
    if (!idx.isValid()) return false;
    QAbstractItemModel *model = m_tableView->model();
    QModelIndex srcIdx = idx;
    if (auto *proxy = qobject_cast<QSortFilterProxyModel*>(model)) {
        model = proxy->sourceModel();
        srcIdx = proxy->mapToSource(idx);
    }

    if (auto *kvModel = qobject_cast<KeyValueModel*>(model)) {
        return srcIdx.column() == 1 && kvModel->isBinaryValue(srcIdx.row());
    }
    if (auto *duckModel = qobject_cast<DuckDbModel*>(model)) {
        return duckModel->isBinaryValue(srcIdx.row(), srcIdx.column());
    }
    if (auto *mdbModel = qobject_cast<MdbModel*>(model)) {
        return mdbModel->isBinaryValue(srcIdx.row(), srcIdx.column());
    }
    if (auto *sqliteModel = qobject_cast<SqliteTableModel*>(model)) {
        return sqliteModel->isBinaryValue(srcIdx.row(), srcIdx.column());
    }
    return false;
}

QByteArray DbViewWidget::getCellRawValue(const QModelIndex &idx) const
{
    if (!idx.isValid()) return {};
    QAbstractItemModel *model = m_tableView->model();
    QModelIndex srcIdx = idx;
    if (auto *proxy = qobject_cast<QSortFilterProxyModel*>(model)) {
        model = proxy->sourceModel();
        srcIdx = proxy->mapToSource(idx);
    }

    if (auto *kvModel = qobject_cast<KeyValueModel*>(model)) {
        return kvModel->rawValue(srcIdx.row());
    }
    if (auto *duckModel = qobject_cast<DuckDbModel*>(model)) {
        return duckModel->rawValue(srcIdx.row(), srcIdx.column());
    }
    if (auto *mdbModel = qobject_cast<MdbModel*>(model)) {
        return mdbModel->rawValue(srcIdx.row(), srcIdx.column());
    }
    if (auto *sqliteModel = qobject_cast<SqliteTableModel*>(model)) {
        return sqliteModel->rawValue(srcIdx.row(), srcIdx.column());
    }
    return {};
}

bool DbViewWidget::setCellRawValue(const QModelIndex &idx, const QByteArray &bytes)
{
    if (!idx.isValid() || m_engine->isReadOnly()) return false;
    QAbstractItemModel *model = m_tableView->model();
    QModelIndex srcIdx = idx;
    if (auto *proxy = qobject_cast<QSortFilterProxyModel*>(model)) {
        model = proxy->sourceModel();
        srcIdx = proxy->mapToSource(idx);
    }

    if (auto *kvModel = qobject_cast<KeyValueModel*>(model)) {
        return kvModel->loadValueFromFile(srcIdx.row(), bytes);
    }
    if (auto *duckModel = qobject_cast<DuckDbModel*>(model)) {
        return duckModel->setData(srcIdx, bytes, Qt::EditRole);
    }
    if (auto *sqliteModel = qobject_cast<SqliteTableModel*>(model)) {
        return sqliteModel->setData(srcIdx, bytes, Qt::EditRole);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Table switching
// ---------------------------------------------------------------------------

void DbViewWidget::rebuildGrid(const QString &tableName)
{
    m_tableView->setModel(nullptr);
    if (m_filterProxy)
        m_filterProxy->setSourceModel(nullptr);

    QAbstractItemModel *model = m_engine->modelForTable(tableName);
    if (!model) return;

    if (!m_filterProxy) {
        m_filterProxy = new DbSortFilterProxyModel(this);
        m_filterProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    }
    m_filterProxy->setSourceModel(model);
    m_tableView->setModel(m_filterProxy);

    updateStatusBar();
}

void DbViewWidget::updateStatusBar()
{
    if (!m_statusBar) return;

    int total = 0;
    int filtered = 0;
    if (m_tableView && m_tableView->model()) {
        filtered = m_tableView->model()->rowCount();
        if (m_filterProxy && m_filterProxy->sourceModel()) {
            total = m_filterProxy->sourceModel()->rowCount();
        } else {
            total = filtered;
        }
    }

    m_statusBar->setRowCount(filtered, total);

    if (m_engine)
        m_statusBar->setEncoding(m_engine->currentTableName());
}

// ---------------------------------------------------------------------------
// Data operations
// ---------------------------------------------------------------------------

void DbViewWidget::onSubmitChanges()
{
    if (!m_engine || !m_engine->supportsSubmitRevert() || m_engine->isReadOnly()) return;

    if (!m_engine->submitAll()) {
        QMessageBox::warning(nullptr, QStringLiteral("Commit Error"),
            QStringLiteral("Failed to commit changes:\n%1").arg(m_engine->lastError()));
        m_engine->revertAll();
    }
    updateStatusBar();
}

void DbViewWidget::onRevertChanges()
{
    if (!m_engine || !m_engine->supportsSubmitRevert() || m_engine->isReadOnly()) return;

    m_engine->revertAll();
    updateStatusBar();
}

// ---------------------------------------------------------------------------
// SQL Console Slots
// ---------------------------------------------------------------------------

void DbViewWidget::onExecuteSqlQuery()
{
    QString query = m_sqlEditor->toPlainText().trimmed();
    if (query.isEmpty()) return;

    if (m_engine->isReadOnly()) {
        if (!query.startsWith(QStringLiteral("SELECT"), Qt::CaseInsensitive) &&
            !query.startsWith(QStringLiteral("PRAGMA"), Qt::CaseInsensitive) &&
            !query.startsWith(QStringLiteral("SHOW"), Qt::CaseInsensitive) &&
            !query.startsWith(QStringLiteral("DESCRIBE"), Qt::CaseInsensitive)) {
            m_sqlMessageView->setHtml(QStringLiteral("<font color='orange'><b>Read-Only Constraint:</b></font><br/>"
                "This database is opened in read-only mode. Only SELECT/read queries are allowed."));
            m_sqlResultsStack->setCurrentWidget(m_sqlMessageView);
            return;
        }
    }

    QAbstractItemModel *model = m_engine->executeQuery(query);
    if (!model) {
        QAbstractItemModel *oldModel = m_sqlResultsView->model();
        m_sqlResultsView->setModel(nullptr);
        if (oldModel) {
            delete oldModel;
        }

        if (m_engine->lastQueryError()) {
            m_sqlMessageView->setHtml(QStringLiteral("<font color='red'><b>SQL Query Error:</b></font><br/>%1")
                .arg(m_engine->lastError().toHtmlEscaped()));
        } else {
            m_sqlMessageView->setHtml(QStringLiteral("<font color='green'><b>Query Executed Successfully:</b></font><br/>%1")
                .arg(m_engine->lastError().isEmpty() ? QStringLiteral("Query executed successfully.") : m_engine->lastError().toHtmlEscaped()));
        }
        m_sqlResultsStack->setCurrentWidget(m_sqlMessageView);
        return;
    }

    QAbstractItemModel *oldModel = m_sqlResultsView->model();
    m_sqlResultsView->setModel(model);
    if (oldModel) {
        delete oldModel;
    }
    m_sqlResultsStack->setCurrentWidget(m_sqlResultsView);
}

void DbViewWidget::onClearSqlConsole()
{
    m_sqlEditor->clear();
    QAbstractItemModel *oldModel = m_sqlResultsView->model();
    m_sqlResultsView->setModel(nullptr);
    if (oldModel) {
        delete oldModel;
    }
    m_sqlMessageView->clear();
    m_sqlResultsStack->setCurrentWidget(m_sqlResultsView);
}

void DbViewWidget::onExportSqlResults()
{
    QAbstractItemModel *model = m_sqlResultsView->model();
    if (!model || model->rowCount() == 0) {
        QMessageBox::information(nullptr, QStringLiteral("Export Results"), QStringLiteral("No results to export."));
        return;
    }

    QString filter = QStringLiteral("CSV Files (*.csv);;TSV Files (*.tsv)");
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(nullptr, QStringLiteral("Export Results"), QString(), filter, &selectedFilter);
    if (path.isEmpty()) return;

    QChar sep = selectedFilter.contains(QStringLiteral("TSV")) ? QLatin1Char('\t') : QLatin1Char(',');

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(nullptr, QStringLiteral("Export Error"), QStringLiteral("Failed to open file for writing."));
        return;
    }

    QTextStream out(&f);

    // Headers
    int cols = model->columnCount();
    for (int col = 0; col < cols; ++col) {
        QString header = model->headerData(col, Qt::Horizontal).toString();
        header.replace(QStringLiteral("\""), QStringLiteral("\"\""));
        if (col > 0) out << sep;
        out << QStringLiteral("\"%1\"").arg(header);
    }
    out << QStringLiteral("\n");

    // Rows
    int rows = model->rowCount();
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            QString val = model->data(model->index(row, col), Qt::DisplayRole).toString();
            val.replace(QStringLiteral("\""), QStringLiteral("\"\""));
            if (col > 0) out << sep;
            out << QStringLiteral("\"%1\"").arg(val);
        }
        out << QStringLiteral("\n");
    }

    f.close();
}

void DbViewWidget::onExportTableData()
{
    QAbstractItemModel *model = m_tableView ? m_tableView->model() : nullptr;
    if (!model || model->rowCount() == 0) {
        QMessageBox::information(nullptr, QStringLiteral("Export Table"),
                                 QStringLiteral("No data to export."));
        return;
    }

    QString tableName = m_engine ? m_engine->currentTableName() : QStringLiteral("table");
    QString filter = QStringLiteral("CSV Files (*.csv);;TSV Files (*.tsv)");
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("Export Table"), tableName, filter, &selectedFilter);
    if (path.isEmpty()) return;

    QChar sep = selectedFilter.contains(QStringLiteral("TSV")) ? QLatin1Char('\t') : QLatin1Char(',');

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(nullptr, QStringLiteral("Export Error"),
                             QStringLiteral("Failed to open file for writing."));
        return;
    }

    QTextStream out(&f);

    // Headers
    int cols = model->columnCount();
    for (int col = 0; col < cols; ++col) {
        QString header = model->headerData(col, Qt::Horizontal).toString();
        header.replace(QStringLiteral("\""), QStringLiteral("\"\""));
        if (col > 0) out << sep;
        out << QStringLiteral("\"%1\"").arg(header);
    }
    out << QStringLiteral("\n");

    // Rows
    int rows = model->rowCount();
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            QString val = model->data(model->index(row, col), Qt::DisplayRole).toString();
            val.replace(QStringLiteral("\""), QStringLiteral("\"\""));
            if (col > 0) out << sep;
            out << QStringLiteral("\"%1\"").arg(val);
        }
        out << QStringLiteral("\n");
    }

    f.close();
}

// ---------------------------------------------------------------------------
// Find
// ---------------------------------------------------------------------------

void DbViewWidget::onFind(bool forward)
{
    if (!m_findPanel || !m_tableView || !m_tableView->model())
        return;

    QString query = m_findPanel->findText();
    if (query.isEmpty()) return;

    bool caseSensitive = m_findPanel->matchCase();
    QAbstractItemModel *model = m_tableView->model();
    QModelIndex current = m_tableView->currentIndex();

    int startRow = current.isValid() ? current.row() : 0;
    int startCol = current.isValid() ? current.column() : 0;
    int rows = model->rowCount();
    int cols = model->columnCount();
    int total = rows * cols;

    if (forward) {
        startCol++;
        if (startCol >= cols) { startCol = 0; startRow++; }
    } else {
        startCol--;
        if (startCol < 0) { startCol = cols - 1; startRow--; }
    }

    for (int i = 0; i < total; ++i) {
        int offset = forward ? i : -i;
        int linearStart = startRow * cols + startCol;
        int linearIdx = (linearStart + offset + total) % total;
        int r = linearIdx / cols;
        int c = linearIdx % cols;

        QModelIndex idx = model->index(r, c);
        QString cellText = model->data(idx, Qt::DisplayRole).toString();

        bool match = caseSensitive
            ? cellText.contains(query, Qt::CaseSensitive)
            : cellText.contains(query, Qt::CaseInsensitive);

        if (match) {
            m_tableView->setCurrentIndex(idx);
            m_tableView->scrollTo(idx);
            m_findPanel->setStatusText(QStringLiteral("Found at row %1, col %2").arg(r + 1).arg(c + 1));
            return;
        }
    }

    m_findPanel->setStatusText(QStringLiteral("\"%1\" not found").arg(query));
}

// ---------------------------------------------------------------------------
// WLX bridge accessors
// ---------------------------------------------------------------------------

FocusManager *DbViewWidget::focusManager() const { return m_fm; }
EditableGridWidget *DbViewWidget::grid() const { return m_grid; }

QString DbViewWidget::getSelectionAsText(char sep)
{
    return m_grid ? m_grid->getSelectionAsText(sep) : QString();
}

bool DbViewWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_sqlEditor && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) &&
            (keyEvent->modifiers() & Qt::ControlModifier)) {
            onExecuteSqlQuery();
            return true; // Consume event
        }
    }
    return QWidget::eventFilter(obj, event);
}
