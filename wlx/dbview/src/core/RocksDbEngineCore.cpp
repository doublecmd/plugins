#ifdef ENABLE_ROCKSDB_LEVELDB
#include "core/KeyValueEngineCore.h"

#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <sys/stat.h>
#include <cstdlib>

namespace {
bool isDir(const std::string &path) { struct stat st; return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
bool fileExists(const std::string &path) { struct stat st; return stat(path.c_str(), &st) == 0; }
std::string dirOf(const std::string &path) { auto p = path.find_last_of('/'); return p == std::string::npos ? "." : path.substr(0, p); }
class NoOpLogger : public rocksdb::Logger {
public:
    using rocksdb::Logger::Logv;
    void Logv(const char *, va_list) override {}
};
}

class RocksDbEngineCore : public KeyValueEngineCoreBase {
public:
    ~RocksDbEngineCore() override { close(); }

    bool open(const std::string &filepath) override {
        close();
        std::string dbPath = isDir(filepath) ? filepath : dirOf(filepath);
        if (!fileExists(dbPath + "/CURRENT")) return false;

        std::string tmpDir = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp") + "/dbview_logs";
        mkdir(tmpDir.c_str(), 0755);

        rocksdb::Options options;
        options.create_if_missing = false;
        options.info_log = std::make_shared<NoOpLogger>();
        options.db_log_dir = tmpDir; options.wal_dir = tmpDir;

        rocksdb::DB *db = nullptr;
        rocksdb::Status status = rocksdb::DB::Open(options, dbPath, &db);
        if (status.ok() && db) { m_db = db; m_readOnly = false; }
        else {
            db = nullptr;
            status = rocksdb::DB::OpenForReadOnly(options, dbPath, &db);
            if (!status.ok() || !db) return false;
            m_db = db; m_readOnly = true;
        }

        rocksdb::DB *dbp = m_db;
        totalCount = [dbp]() -> int {
            int count = 0;
            rocksdb::Iterator *it = dbp->NewIterator(rocksdb::ReadOptions());
            for (it->SeekToFirst(); it->Valid(); it->Next()) count++;
            delete it;
            return count;
        };
        fetchWindow = [dbp](int startIndex, int count, std::vector<std::string> &keys, std::vector<std::string> &values) {
            rocksdb::Iterator *it = dbp->NewIterator(rocksdb::ReadOptions());
            it->SeekToFirst();
            for (int i = 0; i < startIndex && it->Valid(); ++i) it->Next();
            for (int i = 0; i < count && it->Valid(); ++i) {
                keys.push_back(it->key().ToString());
                values.push_back(it->value().ToString());
                it->Next();
            }
            delete it;
        };
        putValue = [dbp](const std::string &key, const std::string &value) -> bool {
            return dbp->Put(rocksdb::WriteOptions(), key, value).ok();
        };
        return true;
    }
    void close() override { delete m_db; m_db = nullptr; m_keys.clear(); m_values.clear(); m_binary.clear(); }
    std::string engineName() const override { return "RocksDB"; }

private:
    rocksdb::DB *m_db = nullptr;
};

std::unique_ptr<DbEngineCore> createRocksDbEngineCore() { return std::make_unique<RocksDbEngineCore>(); }
#endif
