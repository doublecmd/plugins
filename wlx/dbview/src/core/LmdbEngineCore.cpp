#include "core/KeyValueEngineCore.h"

#include <lmdb.h>
#include <sys/stat.h>

namespace {
bool isDir(const std::string &path) { struct stat st; return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
std::string dirOf(const std::string &path) { auto p = path.find_last_of('/'); return p == std::string::npos ? "." : path.substr(0, p); }
std::string baseOf(const std::string &path) { auto p = path.find_last_of('/'); return p == std::string::npos ? path : path.substr(p + 1); }
}

class LmdbEngineCore : public KeyValueEngineCoreBase {
public:
    ~LmdbEngineCore() override { close(); }

    bool open(const std::string &filepath) override {
        close();
        if (mdb_env_create(&m_env) != 0) return false;
        mdb_env_set_mapsize(m_env, 104857600);

        std::string dbPath;
        unsigned int flags = 0;
        if (isDir(filepath)) dbPath = filepath;
        else if (baseOf(filepath) == "data.mdb") dbPath = dirOf(filepath);
        else { dbPath = filepath; flags |= MDB_NOSUBDIR; }

        int rc = mdb_env_open(m_env, dbPath.c_str(), flags, 0664);
        if (rc != 0) {
            rc = mdb_env_open(m_env, dbPath.c_str(), flags | MDB_RDONLY, 0);
            if (rc != 0) { mdb_env_close(m_env); m_env = nullptr; return false; }
            m_readOnly = true;
        }

        MDB_txn *txn = nullptr;
        if (mdb_txn_begin(m_env, nullptr, m_readOnly ? MDB_RDONLY : 0, &txn) != 0) { close(); return false; }
        if (mdb_dbi_open(txn, nullptr, 0, &m_dbi) != 0) { mdb_txn_abort(txn); close(); return false; }
        mdb_txn_commit(txn);

        totalCount = [this]() -> int {
            if (!m_env) return 0;
            MDB_txn *t = nullptr;
            if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &t) != 0) return 0;
            MDB_cursor *cur = nullptr;
            if (mdb_cursor_open(t, m_dbi, &cur) != 0) { mdb_txn_abort(t); return 0; }
            int count = 0;
            MDB_val k, v;
            while (mdb_cursor_get(cur, &k, &v, MDB_NEXT) == 0) count++;
            mdb_cursor_close(cur); mdb_txn_abort(t);
            return count;
        };

        fetchWindow = [this](int startIndex, int count, std::vector<std::string> &keys, std::vector<std::string> &values) {
            if (!m_env) return;
            MDB_txn *t = nullptr;
            if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &t) != 0) return;
            MDB_cursor *cur = nullptr;
            if (mdb_cursor_open(t, m_dbi, &cur) != 0) { mdb_txn_abort(t); return; }
            MDB_val k, v;
            int rc = mdb_cursor_get(cur, &k, &v, MDB_FIRST);
            for (int i = 0; i < startIndex && rc == 0; i++) rc = mdb_cursor_get(cur, &k, &v, MDB_NEXT);
            for (int i = 0; i < count && rc == 0; i++) {
                keys.emplace_back((const char *)k.mv_data, k.mv_size);
                values.emplace_back((const char *)v.mv_data, v.mv_size);
                rc = mdb_cursor_get(cur, &k, &v, MDB_NEXT);
            }
            mdb_cursor_close(cur); mdb_txn_abort(t);
        };
        putValue = [this](const std::string &key, const std::string &value) -> bool {
            if (m_readOnly) return false;
            MDB_txn *t = nullptr;
            if (mdb_txn_begin(m_env, nullptr, 0, &t) != 0) return false;
            MDB_val k{key.size(), (void *)key.data()}, v{value.size(), (void *)value.data()};
            if (mdb_put(t, m_dbi, &k, &v, 0) != 0) { mdb_txn_abort(t); return false; }
            return mdb_txn_commit(t) == 0;
        };
        return true;
    }

    void close() override {
        if (m_env) { mdb_env_close(m_env); m_env = nullptr; m_dbi = 0; }
        m_keys.clear(); m_values.clear(); m_binary.clear();
    }

    std::string engineName() const override { return "LMDB"; }

private:
    MDB_env *m_env = nullptr;
    MDB_dbi m_dbi = 0;
};

std::unique_ptr<DbEngineCore> createLmdbEngineCore() { return std::make_unique<LmdbEngineCore>(); }
