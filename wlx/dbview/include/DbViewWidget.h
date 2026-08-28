#pragma once

#include <QWidget>
#include <QTreeView>
#include <QTableView>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextEdit>
#include <memory>

class QAction;
class QStandardItem;

namespace QtWlPlugin {
class FocusManager;
class PluginToolBar;
class EditableGridWidget;
class FindReplacePanel;
class FilterableHeaderView;
class PluginStatusBar;
class PluginSplitView;
}

class DbEngine;
class KeyValueModel;
class QSortFilterProxyModel;

/// Main plugin widget for database file viewing/editing.
class DbViewWidget : public QWidget {
    Q_OBJECT
public:
    explicit DbViewWidget(QWidget *parent = nullptr);
    ~DbViewWidget() override;

    bool loadFile(const QString &filepath);
    QString currentFilePath() const { return m_loadedFilePath; }

    /// Discard any uncommitted edits (works for all engine types).
    void discardPendingChanges();

    // WLX bridge accessors
    QtWlPlugin::FocusManager *focusManager() const;
    QtWlPlugin::EditableGridWidget *grid() const;
    QString getSelectionAsText(char sep = '\t');

private slots:
    void onSubmitChanges();
    void onRevertChanges();
    void onFind(bool forward);
    void onExecuteSqlQuery();
    void onClearSqlConsole();
    void onExportSqlResults();
    void onExportTableData();

private:
    void setupUi(const QString &firstTable);
    void setupToolbar();
    void setupFindReplace();
    void rebuildGrid(const QString &tableName);
    void updateStatusBar();
    void populateSchemaTree();
    void setupContextMenu();

    // BLOB helpers
    bool isCellBinary(const QModelIndex &idx) const;
    QByteArray getCellRawValue(const QModelIndex &idx) const;
    bool setCellRawValue(const QModelIndex &idx, const QByteArray &bytes);

    bool eventFilter(QObject *obj, QEvent *event) override;

    std::unique_ptr<DbEngine> m_engine;
    QtWlPlugin::FocusManager *m_fm = nullptr;
    QtWlPlugin::PluginToolBar *m_toolbar = nullptr;
    QtWlPlugin::EditableGridWidget *m_grid = nullptr;
    QtWlPlugin::FindReplacePanel *m_findPanel = nullptr;
    QtWlPlugin::FilterableHeaderView *m_filterHeader = nullptr;
    QtWlPlugin::PluginStatusBar *m_statusBar = nullptr;

    QTableView *m_tableView = nullptr;
    QTreeView *m_schemaTree = nullptr;
    QAction *m_actSubmit = nullptr;
    QAction *m_actRevert = nullptr;
    QSortFilterProxyModel *m_filterProxy = nullptr;

    // SQL Console
    QWidget *m_sqlConsoleWidget = nullptr;
    QPlainTextEdit *m_sqlEditor = nullptr;
    QStackedWidget *m_sqlResultsStack = nullptr;
    QTableView *m_sqlResultsView = nullptr;
    QTextEdit *m_sqlMessageView = nullptr;
    QSplitter *m_mainSplitter = nullptr;
    QString m_loadedFilePath;
};
