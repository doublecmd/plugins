#include "MdbEngine.h"

#include <QFileInfo>
#include <QDebug>

MdbModel::MdbModel(MdbEngine *engine, MdbTableDef *table, QObject *parent)
    : QAbstractTableModel(parent)
    , m_engine(engine)
    , m_table(table)
{
    // Retrieve columns
    mdb_read_columns(m_table);
    for (unsigned int i = 0; i < m_table->num_cols; ++i) {
        MdbColumn *col = (MdbColumn *)g_ptr_array_index(m_table->columns, i);
        m_columnNames.append(QString::fromUtf8(col->name));
        m_columnTypes.append(col->col_type);

        // Allocate bind buffers for catalog/data scanning
        col->bind_ptr = malloc(MDB_BIND_SIZE);
        col->len_ptr = (int *)malloc(sizeof(int));
        memset(col->bind_ptr, 0, MDB_BIND_SIZE);
        *col->len_ptr = 0;
    }
}

MdbModel::~MdbModel()
{
    if (m_table) {
        for (unsigned int i = 0; i < m_table->num_cols; ++i) {
            MdbColumn *col = (MdbColumn *)g_ptr_array_index(m_table->columns, i);
            free(col->bind_ptr);
            free(col->len_ptr);
            col->bind_ptr = nullptr;
            col->len_ptr = nullptr;
        }
        mdb_free_tabledef(m_table);
        m_table = nullptr;
    }
}

int MdbModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_data.size();
}

int MdbModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_columnNames.size();
}

QVariant MdbModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_data.size() || index.column() >= m_columnNames.size())
        return {};

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (isBinaryValue(index.row(), index.column())) {
            int size = m_rawData[index.row()][index.column()].size();
            return QStringLiteral("[Binary Data - %1 bytes]").arg(size);
        }
        return m_data[index.row()][index.column()];
    }
    return {};
}

QVariant MdbModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    if (section < 0 || section >= m_columnNames.size())
        return {};
    return m_columnNames[section];
}

Qt::ItemFlags MdbModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    // MS Access is read-only
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool MdbModel::isBinaryValue(int row, int col) const
{
    if (row < 0 || row >= m_columnTypes.size() || col < 0 || col >= m_columnTypes.size())
        return false;
    int type = m_columnTypes[col];
    return (type == MDB_OLE || type == MDB_BINARY);
}

QByteArray MdbModel::rawValue(int row, int col) const
{
    if (row < 0 || row >= m_rawData.size() || col < 0 || col >= m_columnNames.size())
        return {};
    return m_rawData[row][col];
}

bool MdbModel::select()
{
    beginResetModel();
    m_data.clear();
    m_rawData.clear();

    if (!m_table || !m_engine->handle()) {
        endResetModel();
        return false;
    }

    mdb_rewind_table(m_table);
    while (mdb_fetch_row(m_table)) {
        QVector<QVariant> rowData;
        QVector<QByteArray> rawRowData;
        rowData.reserve(m_table->num_cols);
        rawRowData.reserve(m_table->num_cols);

        for (unsigned int i = 0; i < m_table->num_cols; ++i) {
            MdbColumn *col = (MdbColumn *)g_ptr_array_index(m_table->columns, i);

            if (col->col_type == MDB_OLE) {
                size_t size = 0;
                void *ptr = mdb_ole_read_full(m_engine->handle(), col, &size);
                QByteArray bytes;
                if (ptr) {
                    bytes = QByteArray((const char*)ptr, size);
                    free(ptr);
                }
                rowData.append(QVariant());
                rawRowData.append(bytes);
            } else if (col->col_type == MDB_BINARY) {
                QByteArray bytes;
                if (col->cur_value_len > 0) {
                    bytes = QByteArray((const char*)m_engine->handle()->pg_buf + col->cur_value_start, col->cur_value_len);
                }
                rowData.append(QVariant());
                rawRowData.append(bytes);
            } else {
                QString val = QString::fromUtf8((const char*)col->bind_ptr);
                rowData.append(val);
                rawRowData.append(QByteArray());
            }
        }
        m_data.append(std::move(rowData));
        m_rawData.append(std::move(rawRowData));
    }

    endResetModel();
    return true;
}

MdbEngine::MdbEngine(QObject *parent)
    : DbEngine(parent)
{
}

MdbEngine::~MdbEngine()
{
    close();
}

bool MdbEngine::open(const QString &filepath)
{
    close();

    // MS Access is always read-only
    m_mdb = mdb_open(filepath.toLocal8Bit().constData(), MDB_NOFLAGS);
    if (!m_mdb) {
        return false;
    }

    m_readOnly = true;
    return true;
}

void MdbEngine::close()
{
    delete m_currentModel;
    m_currentModel = nullptr;

    if (m_mdb) {
        mdb_close(m_mdb);
        m_mdb = nullptr;
    }
    m_currentTable.clear();
}

QStringList MdbEngine::tableNames() const
{
    QStringList result;
    if (!m_mdb) return result;

    mdb_read_catalog(m_mdb, MDB_TABLE);
    for (unsigned int i = 0; i < m_mdb->num_catalog; ++i) {
        MdbCatalogEntry *entry = (MdbCatalogEntry *)g_ptr_array_index(m_mdb->catalog, i);
        if (entry->object_type == MDB_TABLE) {
            if (mdb_is_user_table(entry)) {
                result.append(QString::fromUtf8(entry->object_name));
            }
        }
    }
    result.sort();
    return result;
}

QList<ColumnInfo> MdbEngine::columnInfos(const QString &tableName) const
{
    QList<ColumnInfo> result;
    if (!m_mdb) return result;

    MdbCatalogEntry *entry = mdb_get_catalogentry_by_name(m_mdb, tableName.toLocal8Bit().constData());
    if (!entry) return result;

    MdbTableDef *table = mdb_read_table(entry);
    if (!table) return result;

    mdb_read_columns(table);
    for (unsigned int i = 0; i < table->num_cols; ++i) {
        MdbColumn *col = (MdbColumn *)g_ptr_array_index(table->columns, i);
        ColumnInfo info;
        info.name = QString::fromUtf8(col->name);
        info.type = QString::fromLatin1(mdb_get_colbacktype_string(col));
        if (col->col_type == MDB_TEXT) {
            info.type += QStringLiteral("(%1)").arg(col->col_size);
        }
        // libmdb does not easily expose primary keys unless we read relationships/indices.
        // We can check indexes below.
        result.append(info);
    }

    // Try to tag primary key columns if there's an index called "PrimaryKey"
    GPtrArray *indices = mdb_read_indices(table);
    if (indices) {
        for (unsigned int i = 0; i < indices->len; ++i) {
            MdbIndex *idx = (MdbIndex *)g_ptr_array_index(indices, i);
            if (strcmp(idx->name, "PrimaryKey") == 0) {
                for (int k = 0; k < idx->num_keys; ++k) {
                    int colIdx = idx->key_col_num[k] - 1;
                    if (colIdx >= 0 && colIdx < result.size()) {
                        result[colIdx].isPrimaryKey = true;
                    }
                }
            }
        }
        mdb_free_indices(indices);
    }

    mdb_free_tabledef(table);
    return result;
}

QStringList MdbEngine::indexes(const QString &tableName) const
{
    QStringList result;
    if (!m_mdb) return result;

    MdbCatalogEntry *entry = mdb_get_catalogentry_by_name(m_mdb, tableName.toLocal8Bit().constData());
    if (!entry) return result;

    MdbTableDef *table = mdb_read_table(entry);
    if (!table) return result;

    mdb_read_columns(table);

    GPtrArray *indices = mdb_read_indices(table);
    if (indices) {
        for (unsigned int i = 0; i < indices->len; ++i) {
            MdbIndex *idx = (MdbIndex *)g_ptr_array_index(indices, i);
            result.append(QString::fromUtf8(idx->name));
        }
        mdb_free_indices(indices);
    }

    mdb_free_tabledef(table);
    return result;
}

QAbstractItemModel *MdbEngine::modelForTable(const QString &tableName)
{
    if (!m_mdb) return nullptr;

    delete m_currentModel;
    m_currentModel = nullptr;
    m_currentTable = tableName;

    MdbCatalogEntry *entry = mdb_get_catalogentry_by_name(m_mdb, tableName.toLocal8Bit().constData());
    if (!entry) return nullptr;

    MdbTableDef *table = mdb_read_table(entry);
    if (!table) return nullptr;

    auto *model = new MdbModel(this, table, this);
    if (!model->select()) {
        delete model;
        return nullptr;
    }

    m_currentModel = model;
    return model;
}

QString MdbEngine::currentTableName() const
{
    return m_currentTable;
}
