#pragma once

#include <QAbstractTableModel>
#include <QVector>
#include <QByteArray>
#include <QSet>
#include <QMap>
#include <functional>

/// QAbstractTableModel for Key-Value stores (LevelDB, RocksDB, LMDB, BDB).
///
/// Uses a sliding window cache of ~1000 entries centered on the viewport.
/// Binary values that fail UTF-8 validation are displayed as
/// "[Binary Data - X bytes]" with support for hex view toggle
/// and save/load binary values as files.
///
/// Edits are buffered in memory until explicitly committed via submitAll().
/// revertAll() discards the buffer and refreshes the display.
///
/// This model works with abstract iterator callbacks so the same model
/// serves LevelDB, RocksDB, LMDB, and Berkeley DB.
class KeyValueModel : public QAbstractTableModel {
    Q_OBJECT
public:
    struct Entry {
        QByteArray key;
        QByteArray value;
    };

    /// Iterator abstraction: the engine provides callbacks for iteration.
    struct IteratorOps {
        /// Fetch entries for the window [startIndex, startIndex + count).
        std::function<QVector<Entry>(int startIndex, int count)> fetchWindow;

        /// Write a value for the given key. Returns true on success.
        std::function<bool(const QByteArray &key, const QByteArray &value)> putValue;

        /// Delete a key. Returns true on success.
        std::function<bool(const QByteArray &key)> deleteKey;
    };

    explicit KeyValueModel(int totalRows, IteratorOps ops, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    /// Check if a value at a given row is binary (non-UTF-8).
    bool isBinaryValue(int row) const;

    /// Get raw binary value for a row (for save-to-file).
    QByteArray rawValue(int row) const;

    /// Get raw key for a row.
    QByteArray rawKey(int row) const;

    /// Set hex display mode for a row.
    void setHexMode(int row, bool enabled);

    /// Replace a value from file contents (buffered, not committed).
    bool loadValueFromFile(int row, const QByteArray &fileData);

    /// Flush all pending edits to the database. Returns true if all writes succeed.
    bool submitAll();

    /// Discard all pending edits and refresh the display.
    void revertAll();

    /// Whether there are pending uncommitted edits.
    bool hasPendingChanges() const { return !m_pendingEdits.isEmpty(); }

private:
    void ensureCached(int row) const;
    static bool isValidUtf8(const QByteArray &data);
    static QString formatBinaryPlaceholder(int size);
    static QString toHexString(const QByteArray &data);

    int m_totalRows;
    IteratorOps m_ops;

    // Sliding window cache
    static constexpr int kWindowSize = 1000;
    mutable QVector<Entry> m_cache;
    mutable int m_cacheStartRow = -1;

    // Per-row hex display toggle
    mutable QSet<int> m_hexRows;

    // Buffered edits: row index -> new value (uncommitted)
    QMap<int, QByteArray> m_pendingEdits;
};
