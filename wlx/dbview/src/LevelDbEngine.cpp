#ifdef ENABLE_ROCKSDB_LEVELDB

#include "LevelDbEngine.h"
#include "KeyValueModel.h"

#include <rocksdb/db.h>
#include <rocksdb/iterator.h>

#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QStandardPaths>

namespace {
class RocksDbNoOpLogger : public rocksdb::Logger {
public:
    using rocksdb::Logger::Logv;
    void Logv(const char* format, va_list ap) override {
        Q_UNUSED(format);
        Q_UNUSED(ap);
    }
};
}

LevelDbEngine::LevelDbEngine(QObject *parent)
    : DbEngine(parent)
{
}

LevelDbEngine::~LevelDbEngine()
{
    close();
}

bool LevelDbEngine::open(const QString &filepath)
{
    close();

    QFileInfo info(filepath);
    QString dbPath = info.isDir() ? filepath : info.absolutePath();

    if (!QFile::exists(dbPath + QStringLiteral("/CURRENT")))
        return false;

    // Redirect all engine file writes away from the DB directory so DC's
    // file-change detection is not triggered (which causes focus loss/reload).
    QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                     + QStringLiteral("/dbview_logs");
    QDir().mkpath(tmpDir);

    rocksdb::Options options;
    options.create_if_missing = false;
    options.info_log = std::make_shared<RocksDbNoOpLogger>();
    options.db_log_dir = tmpDir.toStdString();
    options.wal_dir = tmpDir.toStdString();
    // Disable background threads — RocksDB compaction/flush threads conflict
    // with Qt's GUI threading model (Breeze theme crash on native LevelDB DBs).
    options.max_background_jobs = 0;
    options.disable_auto_compactions = true;

    rocksdb::DB *db = nullptr;

    // Try writable first — enables editing
    rocksdb::Status status = rocksdb::DB::Open(options, dbPath.toStdString(), &db);
    if (status.ok() && db) {
        m_db = db;
        m_readOnly = false;
        m_keyCount = countKeys();
        return true;
    }

    // Fallback: read-only (DB locked by another process)
    db = nullptr;
    status = rocksdb::DB::OpenForReadOnly(options, dbPath.toStdString(), &db);
    if (!status.ok() || !db)
        return false;

    m_db = db;
    m_readOnly = true;
    m_keyCount = countKeys();
    return true;
}

void LevelDbEngine::close()
{
    delete m_model;
    m_model = nullptr;

    delete m_db;
    m_db = nullptr;

    m_keyCount = 0;
}

int LevelDbEngine::countKeys() const
{
    if (!m_db) return 0;

    int count = 0;
    rocksdb::Iterator *it = m_db->NewIterator(rocksdb::ReadOptions());
    for (it->SeekToFirst(); it->Valid(); it->Next())
        ++count;
    delete it;
    return count;
}

QStringList LevelDbEngine::tableNames() const
{
    return { QStringLiteral("<keys>") };
}

QString LevelDbEngine::currentTableName() const
{
    return QStringLiteral("<keys>");
}

QAbstractItemModel *LevelDbEngine::modelForTable(const QString &tableName)
{
    Q_UNUSED(tableName);
    if (!m_db) return nullptr;

    delete m_model;
    m_model = nullptr;

    rocksdb::DB *db = m_db;

    KeyValueModel::IteratorOps ops;

    ops.fetchWindow = [db](int startIndex, int count) -> QVector<KeyValueModel::Entry> {
        QVector<KeyValueModel::Entry> entries;
        entries.reserve(count);

        rocksdb::Iterator *it = db->NewIterator(rocksdb::ReadOptions());
        it->SeekToFirst();

        for (int i = 0; i < startIndex && it->Valid(); ++i)
            it->Next();

        for (int i = 0; i < count && it->Valid(); ++i) {
            KeyValueModel::Entry e;
            e.key = QByteArray(it->key().data(), static_cast<int>(it->key().size()));
            e.value = QByteArray(it->value().data(), static_cast<int>(it->value().size()));
            entries.append(std::move(e));
            it->Next();
        }

        delete it;
        return entries;
    };

    ops.putValue = [db](const QByteArray &key, const QByteArray &value) -> bool {
        rocksdb::Slice k(key.constData(), key.size());
        rocksdb::Slice v(value.constData(), value.size());
        rocksdb::Status s = db->Put(rocksdb::WriteOptions(), k, v);
        return s.ok();
    };

    ops.deleteKey = [db](const QByteArray &key) -> bool {
        rocksdb::Slice k(key.constData(), key.size());
        rocksdb::Status s = db->Delete(rocksdb::WriteOptions(), k);
        return s.ok();
    };

    m_model = new KeyValueModel(m_keyCount, std::move(ops), this);
    return m_model;
}

bool LevelDbEngine::submitAll()
{
    return m_model ? m_model->submitAll() : false;
}

bool LevelDbEngine::revertAll()
{
    if (m_model) m_model->revertAll();
    return true;
}

#endif // ENABLE_ROCKSDB_LEVELDB
