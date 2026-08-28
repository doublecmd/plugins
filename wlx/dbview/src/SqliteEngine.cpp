#include "SqliteEngine.h"
#include "SqliteTableModel.h"

#include <sqlite3.h>

namespace {
QString quoteIdent(const QString &ident)
{
    QString escaped = ident;
    escaped.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}
} // namespace

SqliteEngine::SqliteEngine(QObject *parent)
    : DbEngine(parent)
{
}

SqliteEngine::~SqliteEngine()
{
    close();
}

bool SqliteEngine::open(const QString &filepath)
{
    close();

    int flags = m_readOnly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    QByteArray pathUtf8 = filepath.toUtf8();
    int rc = sqlite3_open_v2(pathUtf8.constData(), &m_db, flags, nullptr);

    if (rc != SQLITE_OK && !m_readOnly) {
        // Fall back to read-only (e.g. read-only filesystem / permission denied).
        if (m_db) sqlite3_close(m_db);
        m_db = nullptr;
        rc = sqlite3_open_v2(pathUtf8.constData(), &m_db, SQLITE_OPEN_READONLY, nullptr);
        if (rc == SQLITE_OK)
            m_readOnly = true;
    }

    if (rc != SQLITE_OK) {
        if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
        return false;
    }

    if (!m_readOnly) {
        sqlite3_exec(m_db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
        m_inTransaction = true;
    }

    return true;
}

void SqliteEngine::close()
{
    if (m_currentModel) {
        delete m_currentModel;
        m_currentModel = nullptr;
    }
    m_currentTable.clear();

    if (m_db) {
        if (m_inTransaction) {
            sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
            m_inTransaction = false;
        }
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

QStringList SqliteEngine::tablesOrViews(const QString &type) const
{
    QStringList result;
    if (!m_db) return result;

    static const char *sql =
        "SELECT name FROM sqlite_master WHERE type = ?1 AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\' ORDER BY name";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return result;

    QByteArray typeUtf8 = type.toUtf8();
    sqlite3_bind_text(stmt, 1, typeUtf8.constData(), typeUtf8.size(), SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW)
        result << QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);
    return result;
}

QStringList SqliteEngine::tableNames() const
{
    return tablesOrViews(QStringLiteral("table"));
}

QStringList SqliteEngine::viewNames() const
{
    return tablesOrViews(QStringLiteral("view"));
}

QList<ColumnInfo> SqliteEngine::columnInfos(const QString &tableName) const
{
    QList<ColumnInfo> result;
    if (!m_db) return result;

    QString sql = QStringLiteral("PRAGMA table_info(%1)").arg(quoteIdent(tableName));
    QByteArray sqlUtf8 = sql.toUtf8();
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sqlUtf8.constData(), -1, &stmt, nullptr) != SQLITE_OK)
        return result;

    // PRAGMA table_info columns: cid, name, type, notnull, dflt_value, pk
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ColumnInfo info;
        info.name = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
        const unsigned char *typeText = sqlite3_column_text(stmt, 2);
        info.type = typeText ? QString::fromUtf8(reinterpret_cast<const char *>(typeText)) : QString();
        if (info.type.isEmpty())
            info.type = QStringLiteral("VARIANT");
        info.isPrimaryKey = sqlite3_column_int(stmt, 5) > 0;
        result.append(info);
    }
    sqlite3_finalize(stmt);
    return result;
}

QStringList SqliteEngine::indexes(const QString &tableName) const
{
    QStringList result;
    if (!m_db) return result;

    QString sql = QStringLiteral("PRAGMA index_list(%1)").arg(quoteIdent(tableName));
    QByteArray sqlUtf8 = sql.toUtf8();
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sqlUtf8.constData(), -1, &stmt, nullptr) != SQLITE_OK)
        return result;

    // PRAGMA index_list columns: seq, name, unique, origin, partial
    while (sqlite3_step(stmt) == SQLITE_ROW)
        result.append(QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1))));
    sqlite3_finalize(stmt);
    return result;
}

QAbstractItemModel *SqliteEngine::modelForTable(const QString &tableName)
{
    if (!m_db)
        return nullptr;

    if (m_currentModel) {
        delete m_currentModel;
        m_currentModel = nullptr;
    }

    m_currentTable = tableName;

    auto *model = new SqliteTableModel(m_db, tableName, /*isQuery=*/false, this);
    if (!model->select()) {
        m_lastError = model->lastError();
        delete model;
        return nullptr;
    }

    m_currentModel = model;
    return model;
}

QString SqliteEngine::currentTableName() const
{
    return m_currentTable;
}

bool SqliteEngine::submitAll()
{
    if (!m_db || !m_inTransaction) return false;

    if (sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
        m_lastError = QString::fromUtf8(sqlite3_errmsg(m_db));
        return false;
    }
    sqlite3_exec(m_db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
    return true;
}

bool SqliteEngine::revertAll()
{
    if (!m_db || !m_inTransaction) return false;

    sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

    if (m_currentModel)
        m_currentModel->select();
    return true;
}

QAbstractItemModel *SqliteEngine::executeQuery(const QString &query)
{
    if (!m_db) return nullptr;

    m_lastQueryError = false;
    m_lastError.clear();

    auto *model = new SqliteTableModel(m_db, query, /*isQuery=*/true, this);
    if (!model->select()) {
        m_lastQueryError = true;
        m_lastError = model->lastError();
        delete model;
        return nullptr;
    }

    if (model->columnCount() > 0) {
        // Statement produced rows (SELECT / PRAGMA-with-results / etc.).
        return model;
    }

    // Statement executed successfully but returned no columns (INSERT/UPDATE/
    // DELETE/DDL) — already ran to completion inside SqliteTableModel::select().
    delete model;
    int affected = sqlite3_changes(m_db);
    m_lastError = (affected < 0)
        ? QStringLiteral("Query executed successfully.")
        : QStringLiteral("Query executed successfully, %1 row(s) affected.").arg(affected);
    return nullptr;
}

QString SqliteEngine::lastError() const
{
    if (m_lastQueryError || !m_lastError.isEmpty())
        return m_lastError;
    if (m_currentModel && m_currentModel->hasError())
        return m_currentModel->lastError();
    return m_db ? QString::fromUtf8(sqlite3_errmsg(m_db)) : QString();
}
