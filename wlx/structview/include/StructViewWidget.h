#pragma once

#include <QWidget>
#include <QTreeView>
#include <QTableView>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <memory>

#include "TextFormatEngine.h"

namespace QtWlPlugin {
class FocusManager;
class PluginToolBar;
class EditableGridWidget;
class ScopedFindReplacePanel;
class FilterableHeaderView;
class PluginStatusBar;
class PluginSplitView;
class SequentialRowProxyModel;
}
class QUndoStack;

/// Main plugin widget for structured text file viewing/editing.
///
/// Layout:
///   ┌────────────────────────────────────────────┐
///   │  PluginToolBar                             │
///   ├────────────┬───────────────────────────────┤
///   │ QTreeView  │ QTabWidget (Grid | Text)      │
///   │ (document  │  ┌ FilterableHeaderView       │
///   │  tree)     │  ├ QTableView (grid)          │
///   │            │  │                            │
///   ├────────────┴──┴────────────────────────────┤
///   │  PluginStatusBar                           │
///   └────────────────────────────────────────────┘
class StructViewWidget : public QWidget {
    Q_OBJECT
public:
    explicit StructViewWidget(QWidget *parent = nullptr);
    ~StructViewWidget() override;

    bool loadFile(const QString &filepath);
    bool saveFile();
    bool saveFileAs(const QString &path);

    // WLX bridge accessors
    QtWlPlugin::FocusManager *focusManager() const;
    QtWlPlugin::EditableGridWidget *grid() const;
    QString getSelectionAsText(char sep = '\t');

private slots:
    void onSave();
    void onSaveAs();
    void onReload();
    void onPrint();
    void onOpenExternally();
    void onFind(bool forward);
    void onTreeNodeSelected(const QModelIndex &current, const QModelIndex &previous);

private:
    void setupUi();
    void setupToolbar();
    void setupFindReplace();
    void populateTree();
    void populateTreeNode(QStandardItem *parentItem, DocumentNode *node);
    void showNodeData(DocumentNode *node);
    void updateStatusBar();
    void updateTextTab();
    void syncGridToNode();

    QString m_filepath;
    std::unique_ptr<TextFormatEngine> m_engine;
    QtWlPlugin::FocusManager *m_fm;
    QtWlPlugin::PluginToolBar *m_toolbar;
    QtWlPlugin::ScopedFindReplacePanel *m_findReplace;
    QtWlPlugin::PluginStatusBar *m_statusBar;
    QAction *m_actShowText = nullptr;

    // Left panel: document tree
    QtWlPlugin::PluginSplitView *m_splitView;
    QTreeView *m_treeView;
    QStandardItemModel *m_treeModel;

    // Right panel: Grid + Text tabs
    QTabWidget *m_tabWidget;
    QtWlPlugin::EditableGridWidget *m_grid;
    QTableView *m_gridView;
    QStandardItemModel *m_gridModel;
    QtWlPlugin::SequentialRowProxyModel *m_filterProxy;
    QtWlPlugin::FilterableHeaderView *m_filterHeader;
    QPlainTextEdit *m_textView;

    DocumentNode *m_currentNode = nullptr;
    QUndoStack *m_undoStack = nullptr;
};
