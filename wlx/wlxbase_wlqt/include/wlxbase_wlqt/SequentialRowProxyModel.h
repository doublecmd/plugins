#pragma once

#include <QSortFilterProxyModel>

namespace QtWlPlugin {

class SequentialRowProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit SequentialRowProxyModel(QObject *parent = nullptr);
    ~SequentialRowProxyModel() override;

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
};

} // namespace QtWlPlugin
