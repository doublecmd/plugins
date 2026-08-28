#include <wlxbase_wlqt/SequentialRowProxyModel.h>

namespace QtWlPlugin {

SequentialRowProxyModel::SequentialRowProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

SequentialRowProxyModel::~SequentialRowProxyModel()
{
}

QVariant SequentialRowProxyModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Vertical && role == Qt::DisplayRole) {
        return section + 1;
    }
    return QSortFilterProxyModel::headerData(section, orientation, role);
}

} // namespace QtWlPlugin
