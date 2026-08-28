#include "core/KeyValueEngineCore.h"

#include <db.h>
#include <cstring>

class BdbEngineCore : public KeyValueEngineCoreBase {
public:
    ~BdbEngineCore() override { close(); }

    bool open(const std::string &filepath) override {
        close();
        if (db_create(&m_db, nullptr, 0) != 0) return false;
        m_db->set_errcall(m_db, [](const DB_ENV *, const char *, const char *) {});

        int ret = m_db->open(m_db, nullptr, filepath.c_str(), nullptr, DB_UNKNOWN, 0, 0664);
        if (ret != 0) {
            ret = m_db->open(m_db, nullptr, filepath.c_str(), nullptr, DB_UNKNOWN, DB_RDONLY, 0);
            if (ret != 0) { m_db->close(m_db, 0); m_db = nullptr; return false; }
            m_readOnly = true;
        }

        DB *db = m_db;
        totalCount = [db]() -> int {
            DBC *cur = nullptr;
            if (db->cursor(db, nullptr, &cur, 0) != 0) return 0;
            int count = 0;
            DBT key, data;
            memset(&key, 0, sizeof(DBT)); memset(&data, 0, sizeof(DBT));
            while (cur->get(cur, &key, &data, DB_NEXT) == 0) count++;
            cur->close(cur);
            return count;
        };
        fetchWindow = [db](int startIndex, int count, std::vector<std::string> &keys, std::vector<std::string> &values) {
            DBC *cur = nullptr;
            if (db->cursor(db, nullptr, &cur, 0) != 0) return;
            DBT key, data;
            memset(&key, 0, sizeof(DBT)); memset(&data, 0, sizeof(DBT));
            int ret = cur->get(cur, &key, &data, DB_FIRST);
            for (int i = 0; i < startIndex && ret == 0; i++) ret = cur->get(cur, &key, &data, DB_NEXT);
            for (int i = 0; i < count && ret == 0; i++) {
                keys.emplace_back((const char *)key.data, key.size);
                values.emplace_back((const char *)data.data, data.size);
                ret = cur->get(cur, &key, &data, DB_NEXT);
            }
            cur->close(cur);
        };
        putValue = [db](const std::string &key, const std::string &value) -> bool {
            DBT k, v;
            memset(&k, 0, sizeof(DBT)); k.data = (void *)key.data(); k.size = (u_int32_t)key.size();
            memset(&v, 0, sizeof(DBT)); v.data = (void *)value.data(); v.size = (u_int32_t)value.size();
            return db->put(db, nullptr, &k, &v, 0) == 0;
        };
        return true;
    }

    void close() override {
        if (m_db) { m_db->close(m_db, 0); m_db = nullptr; }
        m_keys.clear(); m_values.clear(); m_binary.clear();
    }

    std::string engineName() const override { return "Berkeley DB"; }

private:
    DB *m_db = nullptr;
};

std::unique_ptr<DbEngineCore> createBdbEngineCore() { return std::make_unique<BdbEngineCore>(); }
