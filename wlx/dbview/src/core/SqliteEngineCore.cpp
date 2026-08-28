#include "core/DbEngineCore.h"

#include <sqlite3.h>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace {
constexpr int kChunkSize = 200;

std::string quoteIdent(const std::string &ident) {
    std::string escaped;
    for (char c : ident) { if (c == '"') escaped += '"'; escaped += c; }
    return "\"" + escaped + "\"";
}
std::string cellToString(sqlite3_stmt *stmt, int col, bool &isNull, bool &isBinary) {
    isNull = false; isBinary = false;
    switch (sqlite3_column_type(stmt, col)) {
        case SQLITE_INTEGER: return std::to_string(sqlite3_column_int64(stmt, col));
        case SQLITE_FLOAT: { std::ostringstream ss; ss.precision(15); ss << sqlite3_column_double(stmt, col); return ss.str(); }
        case SQLITE_TEXT: {
            const unsigned char *t = sqlite3_column_text(stmt, col);
            int len = sqlite3_column_bytes(stmt, col);
            return std::string((const char *)t, len);
        }
        case SQLITE_BLOB: isBinary = true; return "";
        case SQLITE_NULL: default: isNull = true; return "";
    }
}
}

/// Rows are fetched LIMIT/OFFSET-chunked, kChunkSize at a time (matches
/// DuckDbEngineCore's approach), rather than eagerly loading the whole
/// table -- a large table only ever materializes as much as the UI has
/// actually scrolled to.
class SqliteEngineCore : public DbEngineCore {
public:
    ~SqliteEngineCore() override { close(); }

    bool open(const std::string &filepath) override {
        close();
        int flags = m_readOnly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
        int rc = sqlite3_open_v2(filepath.c_str(), &m_db, flags, nullptr);
        if (rc != SQLITE_OK && !m_readOnly) {
            if (m_db) sqlite3_close(m_db);
            m_db = nullptr;
            rc = sqlite3_open_v2(filepath.c_str(), &m_db, SQLITE_OPEN_READONLY, nullptr);
            if (rc == SQLITE_OK) m_readOnly = true;
        }
        if (rc != SQLITE_OK) { if (m_db) sqlite3_close(m_db); m_db = nullptr; return false; }
        if (!m_readOnly) { sqlite3_exec(m_db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr); m_inTransaction = true; }
        return true;
    }

    void close() override {
        if (m_db) {
            if (m_inTransaction) { sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr); m_inTransaction = false; }
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        resetState();
    }

    void resetState() {
        m_currentTable.clear(); m_isQuery = false; m_hasRowId = false;
        m_columns.clear(); m_rows.clear(); m_binary.clear(); m_rowIds.clear();
        m_totalRows = 0; m_baseSql.clear();
    }

    std::vector<std::string> tablesOrViews(const std::string &type) const {
        std::vector<std::string> result;
        if (!m_db) return result;
        static const char *sql = "SELECT name FROM sqlite_master WHERE type = ?1 AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\' ORDER BY name";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
        sqlite3_bind_text(stmt, 1, type.c_str(), (int)type.size(), SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW)
            result.push_back((const char *)sqlite3_column_text(stmt, 0));
        sqlite3_finalize(stmt);
        return result;
    }
    std::vector<std::string> tableNames() const override { return tablesOrViews("table"); }
    std::vector<std::string> viewNames() const override { return tablesOrViews("view"); }

    std::vector<DbColumnInfo> columnInfos(const std::string &tableName) const override {
        std::vector<DbColumnInfo> result;
        if (!m_db) return result;
        std::string sql = "PRAGMA table_info(" + quoteIdent(tableName) + ")";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return result;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            DbColumnInfo info;
            info.name = (const char *)sqlite3_column_text(stmt, 1);
            const unsigned char *t = sqlite3_column_text(stmt, 2);
            info.type = t ? (const char *)t : "VARIANT";
            if (info.type.empty()) info.type = "VARIANT";
            info.isPrimaryKey = sqlite3_column_int(stmt, 5) > 0;
            result.push_back(info);
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // Fetches one more chunk of rows starting at m_rows.size(), appending
    // to the existing cache.
    int fetchChunk() {
        if (!m_db || m_baseSql.empty()) return 0;
        std::string sql = m_baseSql + " LIMIT " + std::to_string(kChunkSize) + " OFFSET " + std::to_string((int)m_rows.size());
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) { m_lastError = sqlite3_errmsg(m_db); return 0; }

        int totalCols = sqlite3_column_count(stmt);
        int firstDataCol = m_hasRowId ? 1 : 0;
        int fetched = 0;
        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            std::vector<std::string> row; std::vector<bool> binRow;
            for (int i = firstDataCol; i < totalCols; i++) {
                bool isNull, isBin;
                std::string v = cellToString(stmt, i, isNull, isBin);
                row.push_back(isNull ? "NULL" : (isBin ? "[Binary Data]" : v));
                binRow.push_back(isBin);
            }
            m_rows.push_back(std::move(row));
            m_binary.push_back(std::move(binRow));
            if (m_hasRowId) m_rowIds.push_back(sqlite3_column_int64(stmt, 0));
            fetched++;
        }
        if (rc != SQLITE_DONE) m_lastError = sqlite3_errmsg(m_db);
        sqlite3_finalize(stmt);
        return fetched;
    }

    bool prepareSelect(const std::string &baseSql, const std::string &countSql, bool isQuery) {
        resetState();
        m_isQuery = isQuery;
        if (!isQuery) m_hasRowId = true;

        // Column names via a LIMIT 0 probe.
        sqlite3_stmt *stmt = nullptr;
        std::string probe = baseSql + " LIMIT 0";
        int rc = sqlite3_prepare_v2(m_db, probe.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) { m_lastError = sqlite3_errmsg(m_db); if (stmt) sqlite3_finalize(stmt); return false; }
        int totalCols = sqlite3_column_count(stmt);
        int firstDataCol = m_hasRowId ? 1 : 0;
        for (int i = firstDataCol; i < totalCols; i++) m_columns.push_back(sqlite3_column_name(stmt, i));
        sqlite3_finalize(stmt);

        m_totalRows = 0;
        if (!countSql.empty()) {
            sqlite3_stmt *cstmt = nullptr;
            if (sqlite3_prepare_v2(m_db, countSql.c_str(), -1, &cstmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(cstmt) == SQLITE_ROW) m_totalRows = (int)sqlite3_column_int64(cstmt, 0);
                sqlite3_finalize(cstmt);
            }
        }

        m_baseSql = baseSql;
        fetchChunk();
        return true;
    }

    bool selectTable(const std::string &tableName) override {
        if (!m_db) return false;
        m_currentTable = tableName;
        m_hasRowId = true;
        if (prepareSelect("SELECT rowid, * FROM " + quoteIdent(tableName), "SELECT COUNT(*) FROM " + quoteIdent(tableName), false))
            return true;
        // WITHOUT ROWID fallback.
        m_hasRowId = false;
        return prepareSelect("SELECT * FROM " + quoteIdent(tableName), "SELECT COUNT(*) FROM " + quoteIdent(tableName), false);
    }
    bool selectQuery(const std::string &query) override {
        if (!m_db) return false;
        // No cheap COUNT(*) for an arbitrary statement without executing it
        // twice; fetch everything in this path (ad hoc SQL console queries
        // are user-driven and typically bounded, unlike whole-table browsing).
        if (!prepareSelect(query, "", true)) return false;
        while (fetchChunk() > 0) {}
        m_totalRows = (int)m_rows.size();
        return true;
    }

    int rowCount() const override { return m_totalRows; }
    int fetchedRowCount() const override { return (int)m_rows.size(); }
    bool canFetchMore() const override { return (int)m_rows.size() < m_totalRows; }
    int fetchMore() override { return canFetchMore() ? fetchChunk() : 0; }

    int columnCount() const override { return (int)m_columns.size(); }
    std::string columnName(int col) const override { return col >= 0 && col < (int)m_columns.size() ? m_columns[col] : ""; }
    std::string cellText(int row, int col) const override {
        if (row < 0 || row >= (int)m_rows.size() || col < 0 || col >= (int)m_rows[row].size()) return "";
        return m_binary[row][col] ? ("[Binary Data]") : m_rows[row][col];
    }
    bool cellIsBinary(int row, int col) const override {
        return row >= 0 && row < (int)m_binary.size() && col >= 0 && col < (int)m_binary[row].size() && m_binary[row][col];
    }
    // cellToString() (this file's cellToString(), called from fetchChunk())
    // never actually reads a BLOB's bytes -- it just flags isBinary and
    // returns "", so m_rows never holds the real content for a binary cell
    // at all. Confirmed live: "Save Cell to File" always produced an empty
    // file and "Toggle Hex View" always showed nothing, for every engine
    // (none of them override this; the base class default is `return {}`).
    // Re-query the single row/column directly via its rowid (same
    // identifier setCellText() already uses for UPDATE) rather than
    // growing every cached row with a byte blob it almost never needs.
    std::vector<uint8_t> cellRawBytes(int row, int col) const override {
        if (!m_db || !m_hasRowId || row < 0 || row >= (int)m_rowIds.size() ||
            col < 0 || col >= (int)m_columns.size())
            return {};
        std::string sql = "SELECT " + quoteIdent(m_columns[col]) + " FROM " + quoteIdent(m_currentTable) +
                           " WHERE rowid = ?1";
        sqlite3_stmt *stmt = nullptr;
        std::vector<uint8_t> result;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, m_rowIds[row]);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const void *blob = sqlite3_column_blob(stmt, 0);
                int len = sqlite3_column_bytes(stmt, 0);
                if (blob && len > 0) result.assign((const uint8_t *)blob, (const uint8_t *)blob + len);
            }
        }
        sqlite3_finalize(stmt);
        return result;
    }
    bool cellEditable(int, int) const override { return !m_isQuery && m_hasRowId; }

    bool setCellText(int row, int col, const std::string &text) override {
        // Was a bare `return false` with m_lastError left untouched, so a
        // GTK-side caller showing lastError() on failure (e.g. "Load File
        // into Cell") had nothing to show beyond a generic "not editable"
        // guess -- set a real reason for each case.
        if (m_isQuery) { m_lastError = "This is a query result, not a browsed table -- open the table via the schema list to edit cells."; return false; }
        if (!m_hasRowId) { m_lastError = "This table has no usable rowid to address the row for an UPDATE."; return false; }
        if (row < 0 || row >= (int)m_rows.size()) { m_lastError = "Row index out of range."; return false; }
        std::string sql = "UPDATE " + quoteIdent(m_currentTable) + " SET " + quoteIdent(m_columns[col]) + " = ?1 WHERE rowid = ?2";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) { m_lastError = sqlite3_errmsg(m_db); return false; }
        sqlite3_bind_text(stmt, 1, text.c_str(), (int)text.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, m_rowIds[row]);
        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        if (!ok) m_lastError = sqlite3_errmsg(m_db);
        sqlite3_finalize(stmt);
        if (ok) { m_rows[row][col] = text; m_binary[row][col] = false; }
        return ok;
    }

    std::string currentTableName() const override { return m_currentTable; }
    bool supportsMultipleTables() const override { return true; }
    bool supportsSubmitRevert() const override { return true; }
    bool supportsSqlConsole() const override { return true; }
    std::string engineName() const override { return "SQLite"; }

    bool submitAll() override {
        if (!m_db || !m_inTransaction) return false;
        if (sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) { m_lastError = sqlite3_errmsg(m_db); return false; }
        sqlite3_exec(m_db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
        return true;
    }
    bool revertAll() override {
        if (!m_db || !m_inTransaction) return false;
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
        if (!m_currentTable.empty()) selectTable(m_currentTable);
        return true;
    }
    std::string lastError() const override { return !m_lastError.empty() ? m_lastError : (m_db ? sqlite3_errmsg(m_db) : ""); }

private:
    sqlite3 *m_db = nullptr;
    bool m_inTransaction = false;
    bool m_isQuery = false;
    bool m_hasRowId = false;
    int m_totalRows = 0;
    std::string m_baseSql;
    std::string m_currentTable;
    std::vector<std::string> m_columns;
    std::vector<std::vector<std::string>> m_rows;
    std::vector<std::vector<bool>> m_binary;
    std::vector<int64_t> m_rowIds;
    mutable std::string m_lastError;
};

std::unique_ptr<DbEngineCore> createSqliteEngineCore() { return std::make_unique<SqliteEngineCore>(); }
