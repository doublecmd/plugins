#include "DbEngine.h"

#include "SqliteEngine.h"
#include "DuckDbEngine.h"
#include "MdbEngine.h"
#include "BdbEngine.h"
#include "LmdbEngine.h"

#ifdef ENABLE_ROCKSDB_LEVELDB
#include "LevelDbEngine.h"
#include "RocksDbEngine.h"
#endif

#include <QFileInfo>
#include <QFile>

std::unique_ptr<DbEngine> DbEngine::createForFile(const QString &filepath)
{
    QFileInfo info(filepath);
    QString ext = info.suffix().toLower();

    // --- SQL engines ---

    // SQLite
    if (ext == QStringLiteral("sqlite") || ext == QStringLiteral("sqlite3")
        || ext == QStringLiteral("db") || ext == QStringLiteral("db3")) {
        auto engine = std::make_unique<SqliteEngine>();
        if (engine->open(filepath))
            return engine;
        // Fallback: might be a DuckDB file or Berkeley DB with .db extension
    }

    // DuckDB & Parquet (via DuckDB proxy)
    if (ext == QStringLiteral("duckdb") || ext == QStringLiteral("parquet") || ext == QStringLiteral("pq")) {
        auto engine = std::make_unique<DuckDbEngine>();
        if (engine->open(filepath))
            return engine;
    }

    // --- KV engines ---

    // LMDB: data.mdb or file ending in .lmdb
    if (ext == QStringLiteral("lmdb") || info.fileName() == QStringLiteral("data.mdb")) {
        auto engine = std::make_unique<LmdbEngine>();
        if (engine->open(filepath))
            return engine;
    }

    // Berkeley DB: .bdb or fallback for .db
    if (ext == QStringLiteral("bdb")) {
        auto engine = std::make_unique<BdbEngine>();
        if (engine->open(filepath))
            return engine;
    }

#ifdef ENABLE_ROCKSDB_LEVELDB
    // LevelDB: files inside a LevelDB directory
    if (ext == QStringLiteral("ldb") || ext == QStringLiteral("sst")
        || ext == QStringLiteral("log")) {
        // Check if parent dir looks like a LevelDB directory
        QString parentDir = info.absolutePath();
        if (QFile::exists(parentDir + QStringLiteral("/CURRENT"))) {
            auto engine = std::make_unique<LevelDbEngine>();
            if (engine->open(filepath))
                return engine;
        }
    }

    // RocksDB: same file extensions as LevelDB, try if LevelDB failed
    if (ext == QStringLiteral("sst") || ext == QStringLiteral("log")) {
        QString parentDir = info.absolutePath();
        if (QFile::exists(parentDir + QStringLiteral("/CURRENT"))) {
            auto engine = std::make_unique<RocksDbEngine>();
            if (engine->open(filepath))
                return engine;
        }
    }
#endif

    // MS Access: .mdb, .accdb (after LMDB check since data.mdb is LMDB)
    if (ext == QStringLiteral("mdb") || ext == QStringLiteral("accdb")) {
        auto engine = std::make_unique<MdbEngine>();
        if (engine->open(filepath))
            return engine;
    }

    // --- Fallback: try SQLite/Berkeley DB for any unknown/shared extension ---
    {
        auto engine = std::make_unique<SqliteEngine>();
        if (engine->open(filepath)) {
            // Check if it actually has tables
            if (!engine->tableNames().isEmpty() || !engine->viewNames().isEmpty())
                return engine;
        }
    }
    {
        auto engine = std::make_unique<BdbEngine>();
        if (engine->open(filepath)) {
            return engine;
        }
    }

    return nullptr;
}
