#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QDateTime>
#include <QString>
#include <QColor>
#include <vector>
#include <memory>

#include "LogEngine.h"

/// Qt-typed highlight rule — kept distinct from LogEngine's own (neutral)
/// HighlightRule because LogViewerWidget.cpp's color-rules editor and its
/// QSettings load/save construct this directly with QString/QColor
/// fields. LogModel::setHighlightRules() converts to LogEngine's neutral
/// struct internally; this Qt-facing shape is unchanged from before.
struct HighlightRule {
    QString pattern;
    QColor foregroundColor;
    QColor backgroundColor;
    std::shared_ptr<re2::RE2> compiledRegex;
};

/// Thin QAbstractListModel adapter over LogEngine (src/core/) — all the
/// actual mmap/RE2/threading logic now lives there, toolkit-neutral. This
/// class only translates between LogEngine's plain-C++ types
/// (LogTimestamp/LogColor/std::string) and Qt's (QDateTime/QColor/QString)
/// and forwards Qt signals from LogEngine's callbacks. LogViewerWidget's
/// usage of this class is unchanged — same public API as before.
class LogModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit LogModel(QObject *parent = nullptr);
    ~LogModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void loadFile(const QString& filePath);
    void clearFile();
    void deleteRows(const std::vector<int>& sourceRows);

    // Search — returns match count
    void startSearch(const QString& query);
    void stopSearch();
    bool isMatch(int row) const;
    int  matchCount() const;
    int  nextMatch(int fromRow) const;

    // Line access
    QString lineText(int row) const;
    int lineCount() const;

    // Timestamps detected from first/last lines
    QDateTime firstTimestamp() const;
    QDateTime lastTimestamp()  const;

    // Follow / tail
    void setFollowEnabled(bool enabled);

    // Highlighting rules
    void setHighlightRules(const std::vector<HighlightRule>& rules);
    std::vector<HighlightRule> highlightRules() const { return m_qtRules; }

    // Timestamp parsing for external use (filter proxy)
    static QDateTime parseTimestampFromLine(const QString &line);
    QDateTime getInterpolatedTimestamp(int row) const;

    signals:
    void searchFinished(int matchCount);
    void timestampsDetected(const QDateTime &first, const QDateTime &last);
    void tailUpdated();

    private slots:
    void onFileChanged(const QString &path);

    private:
    std::unique_ptr<LogEngine> m_engine;
    QString m_filePath;
    QFileSystemWatcher *m_watcher = nullptr;
    std::vector<HighlightRule> m_qtRules; // kept for highlightRules()'s Qt-typed return
};
