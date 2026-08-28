#pragma once

#include "core/DbEngineCore.h"
#include <functional>

/// Shared base for the key/value engines (LMDB, Berkeley DB, optionally
/// RocksDB/LevelDB): a single "<keys>" pseudo-table, 2 columns (Key,
/// Value), fetched kChunkSize entries at a time via a cursor-based
/// fetchWindow callback (mirrors the Qt side's KeyValueModel::IteratorOps,
/// just sequential/append-only instead of a recentering random-access
/// window -- the GTK grid only ever scrolls forward through what's been
/// appended, so a simpler forward cursor is sufficient and avoids
/// re-walking the cursor from the start on every window move).
class KeyValueEngineCoreBase : public DbEngineCore {
public:
    static constexpr int kChunkSize = 200;

    std::vector<std::string> tableNames() const override { return {"<keys>"}; }
    std::string currentTableName() const override { return "<keys>"; }
    bool supportsMultipleTables() const override { return false; }
    bool supportsSubmitRevert() const override { return true; }
    std::string lastError() const override { return m_lastError; }

    bool selectTable(const std::string &) override {
        m_keys.clear(); m_values.clear(); m_binary.clear();
        m_totalRows = totalCount ? totalCount() : 0;
        fetchMore();
        return true;
    }

    int rowCount() const override { return m_totalRows; }
    int fetchedRowCount() const override { return (int)m_keys.size(); }
    bool canFetchMore() const override { return (int)m_keys.size() < m_totalRows; }
    int fetchMore() override {
        if (!canFetchMore() || !fetchWindow) return 0;
        std::vector<std::string> keys, values;
        fetchWindow((int)m_keys.size(), kChunkSize, keys, values);
        for (size_t i = 0; i < keys.size(); i++) {
            m_keys.push_back(keys[i]);
            m_values.push_back(values[i]);
            m_binary.push_back(!isValidUtf8(values[i]));
        }
        return (int)keys.size();
    }

    int columnCount() const override { return 2; }
    std::string columnName(int col) const override { return col == 0 ? "Key" : "Value"; }

    std::string cellText(int row, int col) const override {
        if (row < 0 || row >= (int)m_keys.size()) return "";
        if (col == 0) return isValidUtf8(m_keys[row]) ? m_keys[row] : toHex(m_keys[row]);
        return m_binary[row] ? ("[Binary Data - " + std::to_string(m_values[row].size()) + " bytes]") : m_values[row];
    }
    bool cellIsBinary(int row, int col) const override { return col == 1 && row >= 0 && row < (int)m_binary.size() && m_binary[row]; }
    // Unlike the SQL engines (SqliteEngineCore/DuckDbEngineCore), m_values
    // already holds the real bytes for every row -- fetchWindow() never
    // discarded them, cellText() just substitutes a placeholder for
    // display. So this is a straight return, no re-query needed.
    std::vector<uint8_t> cellRawBytes(int row, int col) const override {
        if (col != 1 || row < 0 || row >= (int)m_values.size()) return {};
        return std::vector<uint8_t>(m_values[row].begin(), m_values[row].end());
    }
    bool cellEditable(int, int col) const override { return col == 1 && !m_readOnly; }

    bool setCellText(int row, int col, const std::string &text) override {
        if (col != 1 || row < 0 || row >= (int)m_keys.size() || !putValue) return false;
        if (!putValue(m_keys[row], text)) return false;
        m_values[row] = text;
        m_binary[row] = !isValidUtf8(text);
        return true;
    }

    bool submitAll() override { return true; } // writes are immediate (see setCellText)
    bool revertAll() override { return selectTable(""); }

    /// Appends up to `count` (key, value) pairs starting at the `startIndex`-th
    /// entry (0-based, in the engine's natural cursor order) to keys/values.
    std::function<void(int startIndex, int count, std::vector<std::string> &keys, std::vector<std::string> &values)> fetchWindow;
    std::function<bool(const std::string &key, const std::string &value)> putValue;
    std::function<int()> totalCount;

protected:
    static bool isValidUtf8(const std::string &s) {
        size_t i = 0;
        while (i < s.size()) {
            unsigned char c = s[i];
            int n;
            if ((c & 0x80) == 0) n = 0;
            else if ((c & 0xE0) == 0xC0) n = 1;
            else if ((c & 0xF0) == 0xE0) n = 2;
            else if ((c & 0xF8) == 0xF0) n = 3;
            else return false;
            if (i + n >= s.size() + 1 && n > 0 && i + n > s.size()) return false;
            for (int k = 1; k <= n; k++) {
                if (i + k >= s.size() || (((unsigned char)s[i + k]) & 0xC0) != 0x80) return false;
            }
            i += n + 1;
        }
        return true;
    }
    static std::string toHex(const std::string &s) {
        static const char *hex = "0123456789abcdef";
        std::string out;
        for (size_t i = 0; i < s.size(); i++) {
            if (i) out += ' ';
            unsigned char c = s[i];
            out += hex[c >> 4]; out += hex[c & 0xF];
        }
        return out;
    }

    std::vector<std::string> m_keys, m_values;
    std::vector<bool> m_binary;
    int m_totalRows = 0;
    std::string m_lastError;
};
