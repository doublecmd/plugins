#pragma once

#include <QWidget>
#include <QListView>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QDateTimeEdit>
#include <QCheckBox>
#include <QProgressBar>
#include <QLabel>
#include <QSortFilterProxyModel>
#include <QDateTime>
#include <QMenu>
#include <QAction>
#include <QPointer>
#include <QFileDialog>
#include <QFileInfo>

#include "LogModel.h"

// Proxy that filters rows by regex match and/or timestamp range
class LogFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit LogFilterProxy(QObject *parent = nullptr);

    void setRegexFilterActive(bool active);
    void setTimeFilterActive(bool active);
    void setTimeRange(const QDateTime &start, const QDateTime &end);
    void refreshFilter();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    bool m_regexActive = false;
    bool m_timeActive = false;
    QDateTime m_timeStart;
    QDateTime m_timeEnd;
};

class LogViewerWidget : public QWidget {
    Q_OBJECT
public:
    explicit LogViewerWidget(QWidget *parent = nullptr);
    ~LogViewerWidget() override;

    void loadFile(const QString& filePath);
    void triggerSearch(const QString& searchString, int searchParameter);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void executeSearch(bool jumpToNext);
    void onSearchStopClicked();
    void onFollowToggled(bool checked);
    void onFilterModeToggled(bool checked);
    void onSearchFinished(int matchCount);
    void onTimestampsDetected(const QDateTime &first, const QDateTime &last);
    void onTailUpdated();
    void onTimeRangeChanged();
    void copySelectedLines();
    void deleteSelectedLines();
    void clearLogFile();
    void extractSelectedLines();
    void onSettingsClicked();
    std::vector<HighlightRule> loadHighlightRules();
    void saveHighlightRules(const std::vector<HighlightRule>& rules);

private:
    void scrollToSourceRow(int sourceRow);
    void restoreFocusToDC();        // Give focus back to the saved DC widget
    bool isTextInputWidget(QWidget *w) const; // Check if w is a text input (searchEdit, timeStart, timeEnd)

    // UI Elements
    QListView *listView;
    QComboBox *searchComboBox;
    QPushButton *btnFilter;
    QPushButton *btnSearchNext;
    QPushButton *btnSearchStop;
    QPushButton *btnClearLog;
    QPushButton *btnExtract;
    QPushButton *btnSettings;
    QPushButton *btnResetFilter;
    QDateTimeEdit *timeStart;
    QDateTimeEdit *timeEnd;
    QCheckBox *chkFollow;
    QCheckBox *chkFilterMode;
    QProgressBar *progressBar;
    QLabel *statusLabel;


    LogModel *model;
    LogFilterProxy *filterProxy;
    QString currentFile;
    QString m_lastSearchQuery;
    int m_lastMatchRow = -1;
    bool m_timestampsLoading = false;

    // Focus management
    bool m_isActive = false;
    QPointer<QWidget> m_savedFocusWidget;
    QPointer<QWidget> m_activeInput; // currently active text input widget (search/time edits)
};
