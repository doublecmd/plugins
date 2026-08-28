#include "LogModel.h"
#include <QMetaObject>

namespace {

QDateTime toQDateTime(const LogTimestamp &ts)
{
    if (!ts.valid) return {};
    return QDateTime(QDate(ts.year, ts.month, ts.day), QTime(ts.hour, ts.minute, ts.second));
}

QColor toQColor(const LogColor &c)
{
    if (!c.valid) return {};
    return QColor(c.r, c.g, c.b);
}

LogColor toLogColor(const QColor &c)
{
    if (!c.isValid()) return {};
    return LogColor{c.red(), c.green(), c.blue(), true};
}

EngineHighlightRule toEngineRule(const HighlightRule &r)
{
    EngineHighlightRule er;
    er.pattern = r.pattern.toStdString();
    er.foregroundColor = toLogColor(r.foregroundColor);
    er.backgroundColor = toLogColor(r.backgroundColor);
    er.compiledRegex = r.compiledRegex;
    return er;
}

} // namespace

LogModel::LogModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_engine(std::make_unique<LogEngine>())
{
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &LogModel::onFileChanged);
}

LogModel::~LogModel()
{
    m_engine->stopSearch();
}

int LogModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_engine->lineCount();
}

int LogModel::lineCount() const { return rowCount(); }

QString LogModel::lineText(int row) const
{
    return QString::fromUtf8(m_engine->lineText(row).c_str());
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return {};
    const int row = index.row();
    if (row < 0 || row >= lineCount()) return {};

    if (role == Qt::DisplayRole)
        return lineText(row);

    if (role == Qt::BackgroundRole || role == Qt::ForegroundRole) {
        LogColor bg, fg;
        m_engine->colorsForRow(row, bg, fg);
        if (role == Qt::BackgroundRole && bg.valid) return toQColor(bg);
        if (role == Qt::ForegroundRole && fg.valid) return toQColor(fg);
    }

    return {};
}

void LogModel::setHighlightRules(const std::vector<HighlightRule> &rules)
{
    m_qtRules = rules;
    std::vector<EngineHighlightRule> engineRules;
    engineRules.reserve(rules.size());
    for (const auto &r : rules) engineRules.push_back(toEngineRule(r));
    m_engine->setHighlightRules(std::move(engineRules));

    if (lineCount() > 0)
        emit dataChanged(index(0), index(lineCount() - 1), {Qt::BackgroundRole, Qt::ForegroundRole});
}

void LogModel::loadFile(const QString &filePath)
{
    beginResetModel();
    m_engine->stopSearch();

    m_filePath = filePath;
    m_engine->loadFile(filePath.toStdString());

    endResetModel();

    LogTimestamp first = m_engine->firstTimestamp();
    LogTimestamp last = m_engine->lastTimestamp();
    if (first.valid || last.valid)
        emit timestampsDetected(toQDateTime(first), toQDateTime(last));

    m_watcher->addPath(m_filePath);
}

void LogModel::clearFile()
{
    if (m_filePath.isEmpty()) return;
    m_engine->clearFile();
    loadFile(m_filePath);
}

void LogModel::deleteRows(const std::vector<int> &sourceRows)
{
    if (sourceRows.empty() || m_filePath.isEmpty()) return;
    beginResetModel();
    m_engine->deleteRows(sourceRows);
    endResetModel();
}

void LogModel::startSearch(const QString &query)
{
    m_engine->startSearch(query.toStdString(), [this](int matches) {
        // LogEngine invokes this from its own search jthread — marshal
        // back onto the Qt UI thread, matching the original's
        // QMetaObject::invokeMethod(..., Qt::QueuedConnection) usage.
        QMetaObject::invokeMethod(this, [this, matches]() {
            if (lineCount() > 0)
                emit dataChanged(index(0), index(lineCount() - 1), {Qt::BackgroundRole});
            emit searchFinished(matches);
        }, Qt::QueuedConnection);
    });
}

void LogModel::stopSearch() { m_engine->stopSearch(); }
bool LogModel::isMatch(int row) const { return m_engine->isMatch(row); }
int LogModel::matchCount() const { return m_engine->matchCount(); }
int LogModel::nextMatch(int fromRow) const { return m_engine->nextMatch(fromRow); }

QDateTime LogModel::firstTimestamp() const { return toQDateTime(m_engine->firstTimestamp()); }
QDateTime LogModel::lastTimestamp() const { return toQDateTime(m_engine->lastTimestamp()); }

QDateTime LogModel::parseTimestampFromLine(const QString &line)
{
    return toQDateTime(LogEngine::parseTimestampFromLine(line.toStdString()));
}

QDateTime LogModel::getInterpolatedTimestamp(int row) const
{
    return toQDateTime(m_engine->getInterpolatedTimestamp(row));
}

void LogModel::setFollowEnabled(bool enabled) { m_engine->setFollowEnabled(enabled); }

void LogModel::onFileChanged(const QString &path)
{
    if (path != m_filePath || !m_engine->followEnabled()) return;

    auto result = m_engine->refreshTail();
    if (result.grew) {
        beginInsertRows(QModelIndex(), result.oldLineCount, result.newLineCount - 1);
        endInsertRows();
        emit tailUpdated();
    }

    // QFileSystemWatcher may remove the path after a change; re-add it
    if (!m_watcher->files().contains(m_filePath))
        m_watcher->addPath(m_filePath);
}
