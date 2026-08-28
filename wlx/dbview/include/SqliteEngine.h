#pragma once

#include "DbEngine.h"

struct sqlite3;
class SqliteTableModel;

/// SQLite engine: talks to SQLite directly via the vendored sqlite3 C API
/// (see src/libsqlite3/), not through Qt's QSqlDatabase/QSQLITE driver
/// plugin as before. This removes a runtime dependency on the Qt6 SQL
/// module and its driver plugins entirely for SQLite files — the plugin
/// only needs to load, not Qt6Sql + libqsqlite.
///
/// Keeps a single open transaction the whole time a database is open
/// (mirrors DuckDbEngine's approach): edits made through SqliteTableModel
/// write immediately inside that transaction, submitAll() commits it and
/// opens a new one, revertAll() rolls it back, opens a new one, and
/// reloads the current model.
class SqliteEngine : public DbEngine {
    Q_OBJECT
public:
    explicit SqliteEngine(QObject *parent = nullptr);
    ~SqliteEngine() override;

    bool open(const QString &filepath) override;
    void close() override;

    QStringList tableNames() const override;
    QStringList viewNames() const override;
    QList<ColumnInfo> columnInfos(const QString &tableName) const override;
    QStringList indexes(const QString &tableName) const override;

    QAbstractItemModel *modelForTable(const QString &tableName) override;
    QString currentTableName() const override;

    bool supportsMultipleTables() const override { return true; }
    bool supportsSubmitRevert() const override { return true; }
    bool supportsSqlConsole() const override { return true; }
    bool lastQueryError() const override { return m_lastQueryError; }
    QString engineName() const override { return QStringLiteral("SQLite"); }

    bool submitAll() override;
    bool revertAll() override;
    QAbstractItemModel *executeQuery(const QString &query) override;
    QString lastError() const override;

private:
    QStringList tablesOrViews(const QString &type) const;

    sqlite3 *m_db = nullptr;
    bool m_inTransaction = false;
    QString m_currentTable;
    SqliteTableModel *m_currentModel = nullptr;
    bool m_lastQueryError = false;
    QString m_lastError;
};
