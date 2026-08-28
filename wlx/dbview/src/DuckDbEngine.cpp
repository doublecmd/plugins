#include "DuckDbEngine.h"
#include "DuckDbModel.h"

#include "duckdb.hpp"

#include <QFileInfo>
#include <QDebug>

DuckDbEngine::DuckDbEngine(QObject *parent)
    : DbEngine(parent)
{
}

DuckDbEngine::~DuckDbEngine()
{
    close();
}

bool DuckDbEngine::open(const QString &filepath)
{
    close();

    try {
        duckdb::DBConfig config;
        config.SetOptionByName("threads", duckdb::Value::INTEGER(1));

        bool isParquet = filepath.endsWith(QStringLiteral(".parquet"), Qt::CaseInsensitive) ||
                         filepath.endsWith(QStringLiteral(".pq"), Qt::CaseInsensitive);

        if (isParquet) {
            m_duckdb = std::make_unique<duckdb::DuckDB>("", &config);
            m_conn = std::make_unique<duckdb::Connection>(*m_duckdb);

            QFileInfo fi(filepath);
            QString baseName = fi.baseName();
            QString cleanName = "";
            for (QChar c : baseName) {
                if (c.isLetterOrNumber() || c == '_') {
                    cleanName.append(c);
                }
            }
            if (cleanName.isEmpty()) {
                cleanName = QStringLiteral("parquet_data");
            }

            QString escapedPath = filepath;
            escapedPath.replace(QStringLiteral("'"), QStringLiteral("''"));

            QString query = QStringLiteral("CREATE VIEW \"%1\" AS SELECT * FROM read_parquet('%2')")
                                .arg(cleanName)
                                .arg(escapedPath);

            auto res = m_conn->Query(query.toStdString());
            if (!res || res->HasError()) {
                m_lastError = QString::fromStdString(res ? res->GetError() : "Unknown error creating parquet view");
                return false;
            }
            m_readOnly = true;
            m_inTransaction = false;
        } else {
            // Try read-write first
            try {
                m_duckdb = std::make_unique<duckdb::DuckDB>(filepath.toStdString(), &config);
                m_conn = std::make_unique<duckdb::Connection>(*m_duckdb);
                m_conn->Query("BEGIN TRANSACTION");
                m_inTransaction = true;
                m_readOnly = false;
            } catch (...) {
                // Fallback to read-only
                config.SetOptionByName("access_mode", duckdb::Value("READ_ONLY"));
                m_duckdb = std::make_unique<duckdb::DuckDB>(filepath.toStdString(), &config);
                m_conn = std::make_unique<duckdb::Connection>(*m_duckdb);
                m_inTransaction = false;
                m_readOnly = true;
            }
        }

        return true;
    } catch (const std::exception &e) {
        m_lastError = QString::fromStdString(e.what());
        return false;
    }
}

void DuckDbEngine::close()
{
    delete m_currentModel;
    m_currentModel = nullptr;

    if (m_inTransaction && m_conn) {
        try { m_conn->Query("ROLLBACK"); } catch (...) {}
        m_inTransaction = false;
    }

    m_conn.reset();
    m_duckdb.reset();
    m_currentTable.clear();
}

QStringList DuckDbEngine::tableNames() const
{
    QStringList result;
    if (!m_conn) return result;

    try {
        auto qr = m_conn->Query(
            "SELECT table_name FROM information_schema.tables "
            "WHERE table_schema = 'main' AND table_type = 'BASE TABLE' "
            "ORDER BY table_name");
        if (qr && !qr->HasError()) {
            for (auto &row : *qr) {
                result.append(QString::fromStdString(row.GetValue<std::string>(0)));
            }
        }
    } catch (...) {}
    return result;
}

QStringList DuckDbEngine::viewNames() const
{
    QStringList result;
    if (!m_conn) return result;

    try {
        auto qr = m_conn->Query(
            "SELECT table_name FROM information_schema.tables "
            "WHERE table_schema = 'main' AND table_type = 'VIEW' "
            "ORDER BY table_name");
        if (qr && !qr->HasError()) {
            for (auto &row : *qr) {
                result.append(QString::fromStdString(row.GetValue<std::string>(0)));
            }
        }
    } catch (...) {}
    return result;
}

QList<ColumnInfo> DuckDbEngine::columnInfos(const QString &tableName) const
{
    QList<ColumnInfo> result;
    if (!m_conn) return result;

    try {
        std::string q = "SELECT column_name, data_type FROM information_schema.columns "
                        "WHERE table_name = '" + tableName.toStdString() + "' "
                        "AND table_schema = 'main' ORDER BY ordinal_position";
        auto qr = m_conn->Query(q);
        if (qr && !qr->HasError()) {
            for (auto &row : *qr) {
                ColumnInfo info;
                info.name = QString::fromStdString(row.GetValue<std::string>(0));
                info.type = QString::fromStdString(row.GetValue<std::string>(1));
                result.append(info);
            }
        }

        std::string pi = "PRAGMA table_info('" + tableName.toStdString() + "')";
        auto pir = m_conn->Query(pi);
        if (pir && !pir->HasError()) {
            for (auto &row : *pir) {
                QString colName = QString::fromStdString(row.GetValue<std::string>(1));
                int pk = row.GetValue<int32_t>(5);
                if (pk > 0) {
                    for (auto &info : result) {
                        if (info.name == colName) {
                            info.isPrimaryKey = true;
                            break;
                        }
                    }
                }
            }
        }
    } catch (...) {}

    return result;
}

QStringList DuckDbEngine::indexes(const QString &tableName) const
{
    QStringList result;
    if (!m_conn) return result;

    try {
        std::string q = "SELECT index_name FROM duckdb_indexes WHERE table_name = '" + tableName.toStdString() + "'";
        auto qr = m_conn->Query(q);
        if (qr && !qr->HasError()) {
            for (auto &row : *qr) {
                result.append(QString::fromStdString(row.GetValue<std::string>(0)));
            }
        }
    } catch (...) {}
    return result;
}

QAbstractItemModel *DuckDbEngine::modelForTable(const QString &tableName)
{
    if (!m_conn) return nullptr;

    delete m_currentModel;
    m_currentModel = nullptr;
    m_currentTable = tableName;

    auto *model = new DuckDbModel(m_conn.get(), tableName, this);
    if (!model->select()) {
        delete model;
        return nullptr;
    }

    m_currentModel = model;
    return model;
}

QString DuckDbEngine::currentTableName() const
{
    return m_currentTable;
}

bool DuckDbEngine::submitAll()
{
    if (!m_conn || !m_inTransaction || m_readOnly) return false;

    try {
        auto qr = m_conn->Query("COMMIT");
        if (qr && qr->HasError()) {
            m_lastError = QString::fromStdString(qr->GetError());
            return false;
        }
        m_conn->Query("BEGIN TRANSACTION");
        m_inTransaction = true;
        return true;
    } catch (const std::exception &e) {
        m_lastError = QString::fromStdString(e.what());
        return false;
    }
}

bool DuckDbEngine::revertAll()
{
    if (!m_conn || !m_inTransaction || m_readOnly) return false;

    try {
        m_conn->Query("ROLLBACK");
        m_conn->Query("BEGIN TRANSACTION");
        m_inTransaction = true;

        if (!m_currentTable.isEmpty()) {
            auto *model = qobject_cast<DuckDbModel*>(m_currentModel);
            if (model) model->select();
        }
        return true;
    } catch (const std::exception &e) {
        m_lastError = QString::fromStdString(e.what());
        return false;
    }
}

QAbstractItemModel *DuckDbEngine::executeQuery(const QString &query)
{
    if (!m_conn) return nullptr;

    m_lastQueryError = false;
    m_lastError.clear();

    try {
        auto result = m_conn->Query(query.toStdString());
        if (!result) {
            m_lastQueryError = true;
            m_lastError = QStringLiteral("Unknown error executing query.");
            return nullptr;
        }
        if (result->HasError()) {
            m_lastQueryError = true;
            m_lastError = QString::fromStdString(result->GetError());
            return nullptr;
        }

        int colCount = result->ColumnCount();
        if (colCount == 0) {
            m_lastQueryError = false;
            m_lastError = QStringLiteral("Query executed successfully.");
            return nullptr;
        }

        if (colCount == 1 && result->ColumnName(0) == "Count") {
            int64_t count = 0;
            for (auto &row : *result) {
                count = row.GetValue<int64_t>(0);
            }
            m_lastQueryError = false;
            m_lastError = QStringLiteral("Query executed successfully, %1 row(s) affected.").arg(count);
            return nullptr;
        }

        auto *model = new DuckDbModel(m_conn.get(), query, this);
        if (!model->select()) {
            delete model;
            return nullptr;
        }
        return model;
    } catch (const std::exception &e) {
        m_lastQueryError = true;
        m_lastError = QString::fromStdString(e.what());
        return nullptr;
    }
}

QString DuckDbEngine::lastError() const
{
    return m_lastError;
}
