#include "core/DbEngineCore.h"

#include <sys/stat.h>
#include <algorithm>

std::unique_ptr<DbEngineCore> createSqliteEngineCore();
std::unique_ptr<DbEngineCore> createDuckDbEngineCore();
std::unique_ptr<DbEngineCore> createLmdbEngineCore();
std::unique_ptr<DbEngineCore> createBdbEngineCore();
std::unique_ptr<DbEngineCore> createMdbEngineCore();
#ifdef ENABLE_ROCKSDB_LEVELDB
std::unique_ptr<DbEngineCore> createLevelDbEngineCore();
std::unique_ptr<DbEngineCore> createRocksDbEngineCore();
#endif

namespace {
bool fileExists(const std::string &path) { struct stat st; return stat(path.c_str(), &st) == 0; }
std::string extensionOf(const std::string &path) {
    auto slash = path.find_last_of('/');
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });
    return ext;
}
std::string baseName(const std::string &path) {
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
std::string dirOf(const std::string &path) {
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}
}

std::unique_ptr<DbEngineCore> DbEngineCore::createForFile(const std::string &filepath) {
    std::string ext = extensionOf(filepath);

    if (ext == "sqlite" || ext == "sqlite3" || ext == "db" || ext == "db3") {
        auto engine = createSqliteEngineCore();
        if (engine->open(filepath)) return engine;
    }

    if (ext == "duckdb" || ext == "parquet" || ext == "pq") {
        auto engine = createDuckDbEngineCore();
        if (engine->open(filepath)) return engine;
    }

    if (ext == "lmdb" || baseName(filepath) == "data.mdb") {
        auto engine = createLmdbEngineCore();
        if (engine->open(filepath)) return engine;
    }

    if (ext == "bdb") {
        auto engine = createBdbEngineCore();
        if (engine->open(filepath)) return engine;
    }

#ifdef ENABLE_ROCKSDB_LEVELDB
    if (ext == "ldb" || ext == "sst" || ext == "log") {
        if (fileExists(dirOf(filepath) + "/CURRENT")) {
            auto engine = createLevelDbEngineCore();
            if (engine->open(filepath)) return engine;
        }
    }
    if (ext == "sst" || ext == "log") {
        if (fileExists(dirOf(filepath) + "/CURRENT")) {
            auto engine = createRocksDbEngineCore();
            if (engine->open(filepath)) return engine;
        }
    }
#endif

    if (ext == "mdb" || ext == "accdb") {
        auto engine = createMdbEngineCore();
        if (engine->open(filepath)) return engine;
    }

    {
        auto engine = createSqliteEngineCore();
        if (engine->open(filepath)) {
            if (!engine->tableNames().empty() || !engine->viewNames().empty()) return engine;
        }
    }
    {
        auto engine = createBdbEngineCore();
        if (engine->open(filepath)) return engine;
    }

    return nullptr;
}
