#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

/// Toolkit-neutral database browsing/editing core, sitting on top of the
/// same raw C/C++ database APIs the existing Qt engines already talk to
/// directly (vendored sqlite3, duckdb, lmdb, Berkeley DB, Firebird's
/// classic API, libmdb, and optionally RocksDB/LevelDB). No Qt, no GTK --
/// the UI layer (src/gtk3/) just calls into this for table lists, column
/// metadata, and row data.
///
/// Rows are fetched in chunks (fetchMore()/canFetchMore()), same principle
/// as the Qt side's QAbstractTableModel::fetchMore()/canFetchMore() (which
/// DuckDbModel already used, and KeyValueModel approximated with a
/// recentering window) -- selectTable()/selectQuery() only prepares the
/// query and loads the first chunk; rowCount() reports the *total* row
/// count (known upfront via COUNT(*) or an equivalent), while
/// fetchedRowCount() reports how many rows are actually materialized in
/// memory right now. The UI is expected to call fetchMore() as the user
/// scrolls close to the bottom of what's currently loaded, appending only
/// the newly-fetched rows to its own widget rather than re-populating
/// everything.

struct DbColumnInfo {
    std::string name;
    std::string type;
    bool isPrimaryKey = false;
    bool isForeignKey = false;
};

class DbEngineCore {
public:
    virtual ~DbEngineCore() = default;

    virtual bool open(const std::string &filepath) = 0;
    virtual void close() = 0;

    virtual std::vector<std::string> tableNames() const = 0;
    virtual std::vector<std::string> viewNames() const { return {}; }
    virtual std::vector<DbColumnInfo> columnInfos(const std::string &tableName) const { return {}; }
    virtual std::vector<std::string> indexes(const std::string &tableName) const { return {}; }

    /// Prepares a table's (or keyspace's) row set and loads the first
    /// chunk. Returns false on error (see lastError()).
    virtual bool selectTable(const std::string &tableName) = 0;
    /// Runs a custom query (SQL engines only) and loads its first chunk.
    virtual bool selectQuery(const std::string &query) { return false; }

    /// Total row count (known upfront), independent of how many rows are
    /// currently fetched into memory.
    virtual int rowCount() const = 0;
    /// How many rows are currently materialized (i.e. safe to index via
    /// cellText() et al.) -- always <= rowCount().
    virtual int fetchedRowCount() const = 0;
    virtual bool canFetchMore() const = 0;
    /// Loads the next chunk. Returns how many additional rows became
    /// available (0 if none, e.g. already fully fetched or on error).
    virtual int fetchMore() = 0;

    virtual int columnCount() const = 0;
    virtual std::string columnName(int col) const = 0;

    /// Display text for a cell -- "NULL" for SQL NULL, "[Binary Data - N
    /// bytes]" for BLOB/binary cells (matches the Qt side's placeholder).
    /// Only valid for row < fetchedRowCount().
    virtual std::string cellText(int row, int col) const = 0;
    virtual bool cellIsBinary(int row, int col) const { return false; }
    virtual std::vector<uint8_t> cellRawBytes(int row, int col) const { return {}; }

    /// Whether a cell is editable at all (false for query results, KV
    /// keys, and read-only engines like MS Access).
    virtual bool cellEditable(int row, int col) const { return false; }
    /// Writes a new value immediately (mirrors the Qt side's per-cell
    /// commit-inside-open-transaction pattern). Returns false on error.
    virtual bool setCellText(int row, int col, const std::string &text) { return false; }

    virtual std::string currentTableName() const = 0;
    virtual bool supportsMultipleTables() const = 0;
    virtual bool supportsSubmitRevert() const = 0;
    virtual bool supportsSqlConsole() const { return false; }
    virtual std::string engineName() const = 0;

    virtual bool submitAll() { return false; }
    virtual bool revertAll() { return false; }

    virtual std::string lastError() const { return {}; }

    bool isReadOnly() const { return m_readOnly; }

    /// Factory: probes the file/directory and returns the right engine
    /// (same detection order as the Qt side's DbEngine::createForFile).
    static std::unique_ptr<DbEngineCore> createForFile(const std::string &filepath);

protected:
    bool m_readOnly = false;
};
