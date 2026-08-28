#pragma once

#include "DbEngine.h"

extern "C" {
#include <mdbtools.h>
}

#include <QAbstractTableModel>
#include <QVector>

class MdbEngine;

class MdbModel : public QAbstractTableModel {
    Q_OBJECT
public:
    MdbModel(MdbEngine *engine, MdbTableDef *table, QObject *parent = nullptr);
    ~MdbModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // BLOB helpers
    bool isBinaryValue(int row, int col) const;
    QByteArray rawValue(int row, int col) const;

    bool select();

private:
    MdbEngine *m_engine;
    MdbTableDef *m_table;
    QStringList m_columnNames;
    QList<int> m_columnTypes;
    QVector<QVector<QVariant>> m_data;
    QVector<QVector<QByteArray>> m_rawData; // to preserve raw BLOB bytes
};

class MdbEngine : public DbEngine {
    Q_OBJECT
public:
    explicit MdbEngine(QObject *parent = nullptr);
    ~MdbEngine() override;

    bool open(const QString &filepath) override;
    void close() override;

    QStringList tableNames() const override;
    QList<ColumnInfo> columnInfos(const QString &tableName) const override;
    QStringList indexes(const QString &tableName) const override;

    QAbstractItemModel *modelForTable(const QString &tableName) override;
    QString currentTableName() const override;

    bool supportsMultipleTables() const override { return true; }
    bool supportsSubmitRevert() const override { return false; }
    QString engineName() const override { return QStringLiteral("MS Access"); }

    MdbHandle *handle() const { return m_mdb; }

private:
    MdbHandle *m_mdb = nullptr;
    QString m_currentTable;
    MdbModel *m_currentModel = nullptr;
};
