#pragma once

#ifdef ENABLE_ROCKSDB_LEVELDB

#include "DbEngine.h"
#include "KeyValueModel.h"
#include <memory>

namespace rocksdb { class DB; }

/// LevelDB engine: opens LevelDB directories as Key-Value stores using RocksDB API.
class LevelDbEngine : public DbEngine {
    Q_OBJECT
public:
    explicit LevelDbEngine(QObject *parent = nullptr);
    ~LevelDbEngine() override;

    bool open(const QString &filepath) override;
    void close() override;

    QStringList tableNames() const override;
    QAbstractItemModel *modelForTable(const QString &tableName) override;
    QString currentTableName() const override;

    bool supportsMultipleTables() const override { return false; }
    bool supportsSubmitRevert() const override { return true; }
    bool submitAll() override;
    bool revertAll() override;
    QString engineName() const override { return QStringLiteral("LevelDB"); }

private:
    int countKeys() const;

    rocksdb::DB *m_db = nullptr;
    int m_keyCount = 0;
    KeyValueModel *m_model = nullptr;
};

#endif // ENABLE_ROCKSDB_LEVELDB
