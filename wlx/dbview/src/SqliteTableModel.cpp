#include "SqliteTableModel.h"

#include <sqlite3.h>

namespace {

/// Quote a SQLite identifier (table/column name), doubling embedded quotes.
QString quoteIdent(const QString &ident)
{
    QString escaped = ident;
    escaped.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}

QVariant columnValue(sqlite3_stmt *stmt, int col)
{
    switch (sqlite3_column_type(stmt, col)) {
    case SQLITE_INTEGER:
        return QVariant(static_cast<qlonglong>(sqlite3_column_int64(stmt, col)));
    case SQLITE_FLOAT:
        return QVariant(sqlite3_column_double(stmt, col));
    case SQLITE_TEXT: {
        const unsigned char *text = sqlite3_column_text(stmt, col);
        int len = sqlite3_column_bytes(stmt, col);
        return QVariant(QString::fromUtf8(reinterpret_cast<const char *>(text), len));
    }
    case SQLITE_BLOB: {
        const void *blob = sqlite3_column_blob(stmt, col);
        int len = sqlite3_column_bytes(stmt, col);
        return QVariant(QByteArray(reinterpret_cast<const char *>(blob), len));
    }
    case SQLITE_NULL:
    default:
        return QVariant();
    }
}

} // namespace

SqliteTableModel::SqliteTableModel(sqlite3 *db, const QString &tableNameOrQuery,
                                   bool isQuery, QObject *parent)
    : QAbstractTableModel(parent)
    , m_db(db)
    , m_isQuery(isQuery)
{
    if (isQuery)
        m_query = tableNameOrQuery;
    else
        m_tableName = tableNameOrQuery;
}

bool SqliteTableModel::select()
{
    beginResetModel();
    m_columnNames.clear();
    m_data.clear();
    m_rowIds.clear();
    m_lastError.clear();
    m_hasRowId = false;

    QString sql;
    if (m_isQuery) {
        sql = m_query;
    } else {
        // Try rowid-tracked SELECT first (works for ordinary rowid tables,
        // which is the vast majority). Falls back to a plain SELECT * for
        // WITHOUT ROWID tables, where editing is then simply unsupported.
        sql = QStringLiteral("SELECT rowid, * FROM %1").arg(quoteIdent(m_tableName));
    }

    sqlite3_stmt *stmt = nullptr;
    QByteArray sqlUtf8 = sql.toUtf8();
    int rc = sqlite3_prepare_v2(m_db, sqlUtf8.constData(), -1, &stmt, nullptr);

    if (rc != SQLITE_OK && !m_isQuery) {
        // Likely a WITHOUT ROWID table — retry without rowid, read-only.
        if (stmt) sqlite3_finalize(stmt);
        sql = QStringLiteral("SELECT * FROM %1").arg(quoteIdent(m_tableName));
        sqlUtf8 = sql.toUtf8();
        rc = sqlite3_prepare_v2(m_db, sqlUtf8.constData(), -1, &stmt, nullptr);
        m_hasRowId = false;
    } else if (rc == SQLITE_OK && !m_isQuery) {
        m_hasRowId = true;
    }

    if (rc != SQLITE_OK) {
        m_lastError = QString::fromUtf8(sqlite3_errmsg(m_db));
        if (stmt) sqlite3_finalize(stmt);
        endResetModel();
        return false;
    }

    int totalCols = sqlite3_column_count(stmt);
    int firstDataCol = (m_hasRowId ? 1 : 0);
    for (int i = firstDataCol; i < totalCols; ++i)
        m_columnNames << QString::fromUtf8(sqlite3_column_name(stmt, i));

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        QVector<QVariant> row;
        row.reserve(totalCols - firstDataCol);
        for (int i = firstDataCol; i < totalCols; ++i)
            row << columnValue(stmt, i);
        m_data << row;
        if (m_hasRowId)
            m_rowIds << sqlite3_column_int64(stmt, 0);
    }

    bool ok = (rc == SQLITE_DONE);
    if (!ok)
        m_lastError = QString::fromUtf8(sqlite3_errmsg(m_db));

    sqlite3_finalize(stmt);
    endResetModel();
    return ok;
}

int SqliteTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_data.size();
}

int SqliteTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_columnNames.size();
}

QVariant SqliteTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_data.size() || index.column() >= m_columnNames.size())
        return {};
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};

    const QVariant &val = m_data[index.row()][index.column()];
    if (role == Qt::DisplayRole) {
        if (val.userType() == QMetaType::QByteArray)
            return QStringLiteral("[Binary Data - %1 bytes]").arg(val.toByteArray().size());
        if (val.isNull())
            return QStringLiteral("NULL");
    }
    return val;
}

QVariant SqliteTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    if (section < 0 || section >= m_columnNames.size())
        return {};
    return m_columnNames[section];
}

Qt::ItemFlags SqliteTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (!m_isQuery && m_hasRowId)
        f |= Qt::ItemIsEditable;
    return f;
}

bool SqliteTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || m_isQuery || !m_hasRowId)
        return false;
    if (index.row() >= m_data.size() || index.column() >= m_columnNames.size())
        return false;

    QString sql = QStringLiteral("UPDATE %1 SET %2 = ? WHERE rowid = ?")
                      .arg(quoteIdent(m_tableName), quoteIdent(m_columnNames[index.column()]));
    QByteArray sqlUtf8 = sql.toUtf8();

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sqlUtf8.constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        m_lastError = QString::fromUtf8(sqlite3_errmsg(m_db));
        return false;
    }

    if (value.isNull()) {
        sqlite3_bind_null(stmt, 1);
    } else {
        switch (value.typeId()) {
        case QMetaType::Int:
        case QMetaType::LongLong:
        case QMetaType::UInt:
        case QMetaType::ULongLong:
            sqlite3_bind_int64(stmt, 1, value.toLongLong());
            break;
        case QMetaType::Double:
            sqlite3_bind_double(stmt, 1, value.toDouble());
            break;
        case QMetaType::QByteArray: {
            QByteArray b = value.toByteArray();
            sqlite3_bind_blob(stmt, 1, b.constData(), b.size(), SQLITE_TRANSIENT);
            break;
        }
        default: {
            QByteArray text = value.toString().toUtf8();
            sqlite3_bind_text(stmt, 1, text.constData(), text.size(), SQLITE_TRANSIENT);
            break;
        }
        }
    }
    sqlite3_bind_int64(stmt, 2, m_rowIds[index.row()]);

    int rc = sqlite3_step(stmt);
    bool ok = (rc == SQLITE_DONE);
    if (!ok)
        m_lastError = QString::fromUtf8(sqlite3_errmsg(m_db));
    sqlite3_finalize(stmt);

    if (ok) {
        m_data[index.row()][index.column()] = value;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    }
    return ok;
}

bool SqliteTableModel::isBinaryValue(int row, int col) const
{
    if (row < 0 || row >= m_data.size() || col < 0 || col >= m_columnNames.size())
        return false;
    return m_data[row][col].userType() == QMetaType::QByteArray;
}

QByteArray SqliteTableModel::rawValue(int row, int col) const
{
    if (row < 0 || row >= m_data.size() || col < 0 || col >= m_columnNames.size())
        return {};
    return m_data[row][col].toByteArray();
}
