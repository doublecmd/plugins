#include "LmdbEngine.h"
#include "KeyValueModel.h"

#include <QFileInfo>
#include <QDebug>

LmdbEngine::LmdbEngine(QObject *parent)
    : DbEngine(parent)
{
}

LmdbEngine::~LmdbEngine()
{
    close();
}

bool LmdbEngine::open(const QString &filepath)
{
    close();

    int rc = mdb_env_create(&m_env);
    if (rc != 0) return false;

    // Set large map size
    mdb_env_set_mapsize(m_env, 104857600); // 100MB

    QFileInfo info(filepath);
    QString dbPath;
    unsigned int flags = 0;
    if (info.isDir()) {
        dbPath = filepath;
    } else {
        if (info.fileName() == QStringLiteral("data.mdb")) {
            dbPath = info.absolutePath();
        } else {
            dbPath = filepath;
            flags |= MDB_NOSUBDIR;
        }
    }

    // Try read-write
    rc = mdb_env_open(m_env, dbPath.toLocal8Bit().constData(), flags, 0664);
    if (rc != 0) {
        // Fallback to read-only
        rc = mdb_env_open(m_env, dbPath.toLocal8Bit().constData(), flags | MDB_RDONLY, 0);
        if (rc != 0) {
            mdb_env_close(m_env);
            m_env = nullptr;
            return false;
        }
        m_readOnly = true;
    } else {
        m_readOnly = false;
    }

    // Open transaction to open DBI database
    MDB_txn *txn = nullptr;
    rc = mdb_txn_begin(m_env, nullptr, m_readOnly ? MDB_RDONLY : 0, &txn);
    if (rc != 0) {
        close();
        return false;
    }

    rc = mdb_dbi_open(txn, nullptr, 0, &m_dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        close();
        return false;
    }

    mdb_txn_commit(txn);

    m_keyCount = countKeys();
    return true;
}

void LmdbEngine::close()
{
    delete m_model;
    m_model = nullptr;

    if (m_env) {
        // DBI is closed automatically when environment is closed
        mdb_env_close(m_env);
        m_env = nullptr;
        m_dbi = 0;
    }
    m_keyCount = 0;
}

int LmdbEngine::countKeys() const
{
    if (!m_env) return 0;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != 0) return 0;

    MDB_cursor *cursor = nullptr;
    rc = mdb_cursor_open(txn, m_dbi, &cursor);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return 0;
    }

    int count = 0;
    MDB_val key, data;
    while (mdb_cursor_get(cursor, &key, &data, MDB_NEXT) == 0) {
        count++;
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    return count;
}

QStringList LmdbEngine::tableNames() const
{
    return { QStringLiteral("<keys>") };
}

QString LmdbEngine::currentTableName() const
{
    return QStringLiteral("<keys>");
}

QAbstractItemModel *LmdbEngine::modelForTable(const QString &tableName)
{
    Q_UNUSED(tableName);
    if (!m_env) return nullptr;

    delete m_model;
    m_model = nullptr;

    KeyValueModel::IteratorOps ops;

    ops.fetchWindow = [this](int startIndex, int count) -> QVector<KeyValueModel::Entry> {
        QVector<KeyValueModel::Entry> entries;
        if (!m_env) return entries;
        entries.reserve(count);

        MDB_txn *txn = nullptr;
        int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
        if (rc != 0) return entries;

        MDB_cursor *cursor = nullptr;
        rc = mdb_cursor_open(txn, m_dbi, &cursor);
        if (rc != 0) {
            mdb_txn_abort(txn);
            return entries;
        }

        MDB_val key, data;
        rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
        if (rc == 0) {
            for (int i = 0; i < startIndex; ++i) {
                rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
                if (rc != 0) break;
            }
            if (rc == 0) {
                for (int i = 0; i < count; ++i) {
                    KeyValueModel::Entry e;
                    e.key = QByteArray(static_cast<const char*>(key.mv_data), static_cast<int>(key.mv_size));
                    e.value = QByteArray(static_cast<const char*>(data.mv_data), static_cast<int>(data.mv_size));
                    entries.append(std::move(e));
                    rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
                    if (rc != 0) break;
                }
            }
        }

        mdb_cursor_close(cursor);
        mdb_txn_abort(txn);
        return entries;
    };

    ops.putValue = [this](const QByteArray &key, const QByteArray &value) -> bool {
        if (!m_env || m_readOnly) return false;

        MDB_txn *txn = nullptr;
        int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
        if (rc != 0) return false;

        MDB_val k, v;
        k.mv_data = (void*)key.constData();
        k.mv_size = key.size();
        v.mv_data = (void*)value.constData();
        v.mv_size = value.size();

        rc = mdb_put(txn, m_dbi, &k, &v, 0);
        if (rc != 0) {
            mdb_txn_abort(txn);
            return false;
        }

        rc = mdb_txn_commit(txn);
        return rc == 0;
    };

    ops.deleteKey = [this](const QByteArray &key) -> bool {
        if (!m_env || m_readOnly) return false;

        MDB_txn *txn = nullptr;
        int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
        if (rc != 0) return false;

        MDB_val k;
        k.mv_data = (void*)key.constData();
        k.mv_size = key.size();

        rc = mdb_del(txn, m_dbi, &k, nullptr);
        if (rc != 0) {
            mdb_txn_abort(txn);
            return false;
        }

        rc = mdb_txn_commit(txn);
        if (rc == 0) {
            m_keyCount--;
            return true;
        }
        return false;
    };

    m_model = new KeyValueModel(m_keyCount, std::move(ops), this);
    return m_model;
}

bool LmdbEngine::submitAll()
{
    return m_model ? m_model->submitAll() : false;
}

bool LmdbEngine::revertAll()
{
    if (m_model) m_model->revertAll();
    return true;
}
