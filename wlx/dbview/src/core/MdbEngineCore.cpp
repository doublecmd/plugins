#include "core/DbEngineCore.h"

#include <mdbtools.h>
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace { constexpr int kChunkSize = 200; }

/// MS Access engine. Always read-only, same as the Qt side. libmdb's row
/// cursor (mdb_fetch_row) is forward-only, so the table cursor is kept
/// open across fetchMore() calls instead of re-rewinding+skipping each
/// time (which would make later chunks progressively more expensive).
class MdbEngineCore : public DbEngineCore {
public:
    MdbEngineCore() { m_readOnly = true; }
    ~MdbEngineCore() override { close(); }

    bool open(const std::string &filepath) override {
        close();
        m_mdb = mdb_open(filepath.c_str(), MDB_NOFLAGS);
        return m_mdb != nullptr;
    }

    void close() override {
        closeCursor();
        if (m_mdb) { mdb_close(m_mdb); m_mdb = nullptr; }
        m_currentTable.clear(); m_columns.clear(); m_rows.clear(); m_binary.clear(); m_rawBytes.clear();
        m_totalRows = 0;
    }

    void closeCursor() {
        if (m_table) {
            for (unsigned int i = 0; i < m_table->num_cols; i++) {
                auto *col = (MdbColumn *)g_ptr_array_index(m_table->columns, i);
                free(col->bind_ptr); free(col->len_ptr);
                col->bind_ptr = nullptr; col->len_ptr = nullptr;
            }
            mdb_free_tabledef(m_table);
            m_table = nullptr;
        }
    }

    std::vector<std::string> tableNames() const override {
        std::vector<std::string> result;
        if (!m_mdb) return result;
        mdb_read_catalog(m_mdb, MDB_TABLE);
        for (unsigned int i = 0; i < m_mdb->num_catalog; i++) {
            auto *entry = (MdbCatalogEntry *)g_ptr_array_index(m_mdb->catalog, i);
            if (entry->object_type == MDB_TABLE && mdb_is_user_table(entry))
                result.push_back(entry->object_name);
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    std::vector<DbColumnInfo> columnInfos(const std::string &tableName) const override {
        std::vector<DbColumnInfo> result;
        if (!m_mdb) return result;
        auto *entry = mdb_get_catalogentry_by_name(m_mdb, tableName.c_str());
        if (!entry) return result;
        auto *table = mdb_read_table(entry);
        if (!table) return result;
        mdb_read_columns(table);
        for (unsigned int i = 0; i < table->num_cols; i++) {
            auto *col = (MdbColumn *)g_ptr_array_index(table->columns, i);
            DbColumnInfo info;
            info.name = col->name;
            info.type = mdb_get_colbacktype_string(col);
            if (col->col_type == MDB_TEXT) info.type += "(" + std::to_string(col->col_size) + ")";
            result.push_back(info);
        }
        mdb_free_tabledef(table);
        return result;
    }

    bool selectTable(const std::string &tableName) override {
        closeCursor();
        m_currentTable = tableName;
        m_columns.clear(); m_rows.clear(); m_binary.clear(); m_rawBytes.clear(); m_totalRows = 0;
        if (!m_mdb) return false;

        auto *entry = mdb_get_catalogentry_by_name(m_mdb, tableName.c_str());
        if (!entry) return false;
        m_table = mdb_read_table(entry);
        if (!m_table) return false;

        mdb_read_columns(m_table);
        m_totalRows = (int)m_table->num_rows;
        for (unsigned int i = 0; i < m_table->num_cols; i++) {
            auto *col = (MdbColumn *)g_ptr_array_index(m_table->columns, i);
            m_columns.push_back(col->name);
            col->bind_ptr = malloc(MDB_BIND_SIZE);
            col->len_ptr = (int *)malloc(sizeof(int));
            memset(col->bind_ptr, 0, MDB_BIND_SIZE);
            *col->len_ptr = 0;
        }
        mdb_rewind_table(m_table);

        fetchMore();
        return true;
    }

    int fetchMore() override {
        if (!m_table) return 0;
        int fetched = 0;
        while (fetched < kChunkSize && mdb_fetch_row(m_table)) {
            std::vector<std::string> row; std::vector<bool> bin;
            std::vector<std::vector<uint8_t>> raw(m_table->num_cols);
            for (unsigned int i = 0; i < m_table->num_cols; i++) {
                auto *col = (MdbColumn *)g_ptr_array_index(m_table->columns, i);
                if (col->col_type == MDB_OLE || col->col_type == MDB_BINARY) {
                    row.push_back("[Binary Data]"); bin.push_back(true);
                    // fetchMore() never captured the actual bytes -- only
                    // the "[Binary Data]" placeholder -- so "Save Cell to
                    // File"/"Toggle Hex View" always had nothing to show
                    // for an MDB binary column. len_ptr is populated by
                    // mdb_fetch_row() with the real byte count for this row.
                    if (col->bind_ptr && col->len_ptr && *col->len_ptr > 0) {
                        auto *bytes = static_cast<uint8_t *>(col->bind_ptr);
                        raw[i].assign(bytes, bytes + *col->len_ptr);
                    }
                } else {
                    row.push_back((const char *)col->bind_ptr); bin.push_back(false);
                }
            }
            m_rows.push_back(std::move(row));
            m_binary.push_back(std::move(bin));
            m_rawBytes.push_back(std::move(raw));
            fetched++;
        }
        return fetched;
    }
    bool canFetchMore() const override { return (int)m_rows.size() < m_totalRows; }

    int rowCount() const override { return m_totalRows; }
    int fetchedRowCount() const override { return (int)m_rows.size(); }
    int columnCount() const override { return (int)m_columns.size(); }
    std::string columnName(int col) const override { return col >= 0 && col < (int)m_columns.size() ? m_columns[col] : ""; }
    std::string cellText(int row, int col) const override {
        return (row >= 0 && row < (int)m_rows.size() && col >= 0 && col < (int)m_rows[row].size()) ? m_rows[row][col] : "";
    }
    bool cellIsBinary(int row, int col) const override {
        return row >= 0 && row < (int)m_binary.size() && col >= 0 && col < (int)m_binary[row].size() && m_binary[row][col];
    }
    std::vector<uint8_t> cellRawBytes(int row, int col) const override {
        if (row < 0 || row >= (int)m_rawBytes.size() || col < 0 || col >= (int)m_rawBytes[row].size())
            return {};
        return m_rawBytes[row][col];
    }

    std::string currentTableName() const override { return m_currentTable; }
    bool supportsMultipleTables() const override { return true; }
    bool supportsSubmitRevert() const override { return false; }
    std::string engineName() const override { return "MS Access"; }

private:
    MdbHandle *m_mdb = nullptr;
    MdbTableDef *m_table = nullptr;
    int m_totalRows = 0;
    std::string m_currentTable;
    std::vector<std::string> m_columns;
    std::vector<std::vector<std::string>> m_rows;
    std::vector<std::vector<bool>> m_binary;
    std::vector<std::vector<std::vector<uint8_t>>> m_rawBytes;
};

std::unique_ptr<DbEngineCore> createMdbEngineCore() { return std::make_unique<MdbEngineCore>(); }
