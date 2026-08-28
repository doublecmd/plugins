#include "KeyValueModel.h"

#include <QStringDecoder>

KeyValueModel::KeyValueModel(int totalRows, IteratorOps ops, QObject *parent)
    : QAbstractTableModel(parent)
    , m_totalRows(totalRows)
    , m_ops(std::move(ops))
{
}

int KeyValueModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_totalRows;
}

int KeyValueModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 2; // Key, Value
}

QVariant KeyValueModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    return section == 0 ? QStringLiteral("Key") : QStringLiteral("Value");
}

Qt::ItemFlags KeyValueModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    // Keys are read-only, values are editable
    if (index.column() == 0)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

void KeyValueModel::ensureCached(int row) const
{
    if (m_cacheStartRow >= 0 && row >= m_cacheStartRow
        && row < m_cacheStartRow + m_cache.size())
        return;

    // Center the window on the requested row
    int start = qMax(0, row - kWindowSize / 2);
    int count = qMin(kWindowSize, m_totalRows - start);

    m_cache = m_ops.fetchWindow(start, count);
    m_cacheStartRow = start;
}

QVariant KeyValueModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_totalRows)
        return {};

    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};

    // For the value column, check for pending (uncommitted) edits first
    if (index.column() == 1 && m_pendingEdits.contains(index.row())) {
        const QByteArray &raw = m_pendingEdits[index.row()];
        if (!isValidUtf8(raw)) {
            if (m_hexRows.contains(index.row()))
                return toHexString(raw);
            return formatBinaryPlaceholder(raw.size());
        }
        return QString::fromUtf8(raw);
    }

    ensureCached(index.row());

    int localIdx = index.row() - m_cacheStartRow;
    if (localIdx < 0 || localIdx >= m_cache.size())
        return QStringLiteral("[Out of range]");

    const Entry &entry = m_cache[localIdx];
    const QByteArray &raw = (index.column() == 0) ? entry.key : entry.value;

    // Binary value handling (column 1 only)
    if (index.column() == 1 && !isValidUtf8(raw)) {
        if (m_hexRows.contains(index.row()))
            return toHexString(raw);
        return formatBinaryPlaceholder(raw.size());
    }

    // Keys and valid UTF-8 values
    if (isValidUtf8(raw))
        return QString::fromUtf8(raw);

    // Binary key (unusual but possible)
    return toHexString(raw);
}

bool KeyValueModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || index.column() != 1 || !index.isValid())
        return false;

    QByteArray newValue = value.toString().toUtf8();

    // Buffer the edit — don't write to the DB yet
    m_pendingEdits[index.row()] = newValue;
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

bool KeyValueModel::submitAll()
{
    if (!m_ops.putValue)
        return false;

    bool allOk = true;
    for (auto it = m_pendingEdits.constBegin(); it != m_pendingEdits.constEnd(); ++it) {
        int row = it.key();
        const QByteArray &newValue = it.value();

        ensureCached(row);
        int localIdx = row - m_cacheStartRow;
        if (localIdx < 0 || localIdx >= m_cache.size()) {
            allOk = false;
            continue;
        }

        QByteArray key = m_cache[localIdx].key;
        if (m_ops.putValue(key, newValue)) {
            // Update cache to reflect the committed value
            m_cache[localIdx].value = newValue;
        } else {
            allOk = false;
        }
    }

    m_pendingEdits.clear();
    return allOk;
}

void KeyValueModel::revertAll()
{
    if (m_pendingEdits.isEmpty())
        return;

    // Collect affected rows before clearing
    QList<int> rows = m_pendingEdits.keys();
    m_pendingEdits.clear();

    // Invalidate cache so next data() re-fetches from DB
    m_cacheStartRow = -1;
    m_cache.clear();

    // Notify the view that these rows changed back to DB values
    for (int row : rows) {
        QModelIndex idx = index(row, 1);
        emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
    }
}

bool KeyValueModel::isBinaryValue(int row) const
{
    if (m_pendingEdits.contains(row))
        return !isValidUtf8(m_pendingEdits[row]);
    ensureCached(row);
    int localIdx = row - m_cacheStartRow;
    if (localIdx < 0 || localIdx >= m_cache.size())
        return false;
    return !isValidUtf8(m_cache[localIdx].value);
}

QByteArray KeyValueModel::rawValue(int row) const
{
    if (m_pendingEdits.contains(row))
        return m_pendingEdits[row];
    ensureCached(row);
    int localIdx = row - m_cacheStartRow;
    if (localIdx < 0 || localIdx >= m_cache.size())
        return {};
    return m_cache[localIdx].value;
}

QByteArray KeyValueModel::rawKey(int row) const
{
    ensureCached(row);
    int localIdx = row - m_cacheStartRow;
    if (localIdx < 0 || localIdx >= m_cache.size())
        return {};
    return m_cache[localIdx].key;
}

void KeyValueModel::setHexMode(int row, bool enabled)
{
    if (enabled)
        m_hexRows.insert(row);
    else
        m_hexRows.remove(row);

    QModelIndex idx = index(row, 1);
    emit dataChanged(idx, idx, {Qt::DisplayRole});
}

bool KeyValueModel::loadValueFromFile(int row, const QByteArray &fileData)
{
    // Buffer the file data as a pending edit
    m_pendingEdits[row] = fileData;
    QModelIndex idx = index(row, 1);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

bool KeyValueModel::isValidUtf8(const QByteArray &data)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString result = decoder(data);
    Q_UNUSED(result);
    return !decoder.hasError();
}

QString KeyValueModel::formatBinaryPlaceholder(int size)
{
    return QStringLiteral("[Binary Data - %1 bytes]").arg(size);
}

QString KeyValueModel::toHexString(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' '));
}
