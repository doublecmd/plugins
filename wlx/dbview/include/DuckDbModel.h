#pragma once

#include <QAbstractTableModel>
#include <QVariant>
#include <QVector>
#include <QStringList>

namespace duckdb {
class Connection;
}

/// Custom QAbstractTableModel for DuckDB query results.
///
/// Loads data in chunks using LIMIT/OFFSET for lazy loading.
/// Supports editing via UPDATE statements.
class DuckDbModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit DuckDbModel(duckdb::Connection *conn, const QString &tableNameOrQuery,
                        QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    void fetchMore(const QModelIndex &parent) override;
    bool canFetchMore(const QModelIndex &parent) const override;

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    bool select();

    // BLOB helpers
    bool isBinaryValue(int row, int col) const;
    QByteArray rawValue(int row, int col) const;

private:
    void loadChunk(int offset, int limit);

    duckdb::Connection *m_conn;
    QString m_tableName;
    QString m_query;
    bool m_isQuery = false;
    QStringList m_columnNames;
    QVector<QVector<QVariant>> m_data;
    int m_totalRows = 0;
    bool m_allFetched = false;
    int m_sortColumn = -1;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    bool m_hasRowId = false;
    QVector<int64_t> m_rowIds;
    static constexpr int kChunkSize = 1000;
};
