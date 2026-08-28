#pragma once

#include <QAbstractTableModel>
#include <QVariant>
#include <QVector>
#include <QStringList>

struct sqlite3;

/// Custom QAbstractTableModel for SQLite tables/query results, backed
/// directly by the sqlite3 C API (see SqliteEngine — this replaces the
/// previous QSqlTableModel-based approach, which routed through Qt's
/// QSqlDatabase/QSQLITE driver plugin instead of talking to SQLite
/// directly).
///
/// Mirrors DuckDbModel's shape/semantics deliberately, since DbViewWidget
/// already special-cases DuckDbModel/MdbModel for BLOB cell handling —
/// SqliteTableModel is dispatched the same way (see DbViewWidget.cpp).
///
/// Editing writes immediately via a parameterized `UPDATE ... WHERE
/// rowid = ?` inside the engine's already-open transaction; the engine's
/// submitAll()/revertAll() commit or roll back that transaction (same
/// pattern as DuckDbEngine/DuckDbModel).
class SqliteTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    /// tableNameOrQuery: a bare table name (editable, rowid-tracked) when
    /// isQuery is false, or an arbitrary SQL statement (read-only) when true.
    explicit SqliteTableModel(sqlite3 *db, const QString &tableNameOrQuery,
                               bool isQuery, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    /// (Re)run the query/table SELECT and refresh all rows.
    bool select();

    bool hasError() const { return !m_lastError.isEmpty(); }
    QString lastError() const { return m_lastError; }

    // BLOB helpers, mirroring DuckDbModel's interface so DbViewWidget can
    // dispatch to this model the same way it already does for DuckDbModel.
    bool isBinaryValue(int row, int col) const;
    QByteArray rawValue(int row, int col) const;

private:
    sqlite3 *m_db;
    QString m_tableName;
    QString m_query;
    bool m_isQuery;
    bool m_hasRowId = false;

    QStringList m_columnNames;
    QVector<QVector<QVariant>> m_data;
    QVector<qint64> m_rowIds;
    QString m_lastError;
};
