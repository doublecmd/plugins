#include "BdbEngine.h"
#include "KeyValueModel.h"

#include <QFileInfo>
#include <QDebug>

BdbEngine::BdbEngine(QObject *parent)
    : DbEngine(parent)
{
}

BdbEngine::~BdbEngine()
{
    close();
}

bool BdbEngine::open(const QString &filepath)
{
    close();

    int ret = db_create(&m_db, nullptr, 0);
    if (ret != 0) {
        return false;
    }

    // Suppress BDB error messages to stderr — this engine is often used as a
    // probe/fallback in DbEngine::createForFile, and format-mismatch errors
    // (e.g. "DB0004 fop_read_meta: unexpected file type") are confusing noise.
    m_db->set_errcall(m_db, [](const DB_ENV*, const char*, const char*) {});

    // Try read-write open (0 flag)
    ret = m_db->open(m_db, nullptr, filepath.toLocal8Bit().constData(), nullptr, DB_UNKNOWN, 0, 0664);
    if (ret != 0) {
        // Fallback to read-only
        ret = m_db->open(m_db, nullptr, filepath.toLocal8Bit().constData(), nullptr, DB_UNKNOWN, DB_RDONLY, 0);
        if (ret != 0) {
            m_db->close(m_db, 0);
            m_db = nullptr;
            return false;
        }
        m_readOnly = true;
    } else {
        m_readOnly = false;
    }

    m_keyCount = countKeys();
    return true;
}

void BdbEngine::close()
{
    delete m_model;
    m_model = nullptr;

    if (m_db) {
        m_db->close(m_db, 0);
        m_db = nullptr;
    }
    m_keyCount = 0;
}

int BdbEngine::countKeys() const
{
    if (!m_db) return 0;

    DBC *dbcp = nullptr;
    int ret = m_db->cursor(m_db, nullptr, &dbcp, 0);
    if (ret != 0) return 0;

    int count = 0;
    DBT key, data;
    memset(&key, 0, sizeof(DBT));
    memset(&data, 0, sizeof(DBT));

    while (dbcp->get(dbcp, &key, &data, DB_NEXT) == 0) {
        count++;
    }

    dbcp->close(dbcp);
    return count;
}

QStringList BdbEngine::tableNames() const
{
    return { QStringLiteral("<keys>") };
}

QString BdbEngine::currentTableName() const
{
    return QStringLiteral("<keys>");
}

QAbstractItemModel *BdbEngine::modelForTable(const QString &tableName)
{
    Q_UNUSED(tableName);
    if (!m_db) return nullptr;

    delete m_model;
    m_model = nullptr;

    DB *db = m_db;

    KeyValueModel::IteratorOps ops;

    ops.fetchWindow = [db](int startIndex, int count) -> QVector<KeyValueModel::Entry> {
        QVector<KeyValueModel::Entry> entries;
        entries.reserve(count);

        DBC *dbcp = nullptr;
        int ret = db->cursor(db, nullptr, &dbcp, 0);
        if (ret != 0) return entries;

        DBT key, data;
        memset(&key, 0, sizeof(DBT));
        memset(&data, 0, sizeof(DBT));

        // Skip to startIndex
        ret = dbcp->get(dbcp, &key, &data, DB_FIRST);
        if (ret == 0) {
            for (int i = 0; i < startIndex; ++i) {
                ret = dbcp->get(dbcp, &key, &data, DB_NEXT);
                if (ret != 0) break;
            }
            // Collect entries
            if (ret == 0) {
                for (int i = 0; i < count; ++i) {
                    KeyValueModel::Entry e;
                    e.key = QByteArray(static_cast<const char*>(key.data), static_cast<int>(key.size));
                    e.value = QByteArray(static_cast<const char*>(data.data), static_cast<int>(data.size));
                    entries.append(std::move(e));
                    ret = dbcp->get(dbcp, &key, &data, DB_NEXT);
                    if (ret != 0) break;
                }
            }
        }

        dbcp->close(dbcp);
        return entries;
    };

    ops.putValue = [db](const QByteArray &key, const QByteArray &value) -> bool {
        DBT k, v;
        memset(&k, 0, sizeof(DBT));
        k.data = (void*)key.constData();
        k.size = key.size();

        memset(&v, 0, sizeof(DBT));
        v.data = (void*)value.constData();
        v.size = value.size();

        int ret = db->put(db, nullptr, &k, &v, 0);
        return ret == 0;
    };

    ops.deleteKey = [db](const QByteArray &key) -> bool {
        DBT k;
        memset(&k, 0, sizeof(DBT));
        k.data = (void*)key.constData();
        k.size = key.size();

        int ret = db->del(db, nullptr, &k, 0);
        return ret == 0;
    };

    m_model = new KeyValueModel(m_keyCount, std::move(ops), this);
    return m_model;
}

bool BdbEngine::submitAll()
{
    return m_model ? m_model->submitAll() : false;
}

bool BdbEngine::revertAll()
{
    if (m_model) m_model->revertAll();
    return true;
}
