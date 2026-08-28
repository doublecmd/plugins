#pragma once

#include <QHeaderView>
#include <QVector>

class QLineEdit;

namespace QtWlPlugin {

/// QHeaderView with an optional embedded filter row below the column labels.
///
/// When filtering is enabled, QLineEdit inputs appear directly under each
/// column header, pixel-aligned with the sections (no spacer hacks needed).
///
/// Usage:
///   auto *header = new FilterableHeaderView(Qt::Horizontal, tableView);
///   header->setFilterEnabled(true);
///   tableView->setHorizontalHeader(header);
///   connect(header, &FilterableHeaderView::filterChanged, ...);
class FilterableHeaderView : public QHeaderView {
    Q_OBJECT
public:
    explicit FilterableHeaderView(Qt::Orientation orientation, QWidget *parent = nullptr);

    /// Enable or disable the filter row.
    void setFilterEnabled(bool enabled);
    bool isFilterEnabled() const;

    /// Enable or disable the header labels (if false, the header collapses to only show the filters).
    void setHeaderVisible(bool visible);
    bool isHeaderVisible() const;

    /// Clear all filter inputs.
    void clearFilters();

    /// Get the current filter text for a column.
    QString filterText(int column) const;

    QSize sizeHint() const override;

signals:
    /// Emitted when the user types in a column's filter input.
    void filterChanged(int column, const QString &text);

protected:
    void updateGeometries() override;
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;

private slots:
    void adjustInputPositions();

private:
    void rebuildInputs();

    bool m_filterEnabled = false;
    bool m_headerVisible = true;
    int m_filterRowHeight = 24;
    QVector<QLineEdit*> m_inputs;
};

} // namespace QtWlPlugin
