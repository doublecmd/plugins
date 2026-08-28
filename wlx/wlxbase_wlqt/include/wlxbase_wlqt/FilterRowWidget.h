#pragma once

#include <QWidget>
#include <QVector>

class QLineEdit;
class QHBoxLayout;
class QTableView;

namespace QtWlPlugin {

/// A row of QLineEdit widgets for per-column filtering of a QTableView.
///
/// Sits between the column headers and the data. Each column gets its own
/// filter input. Widths sync with column widths automatically.
///
/// Usage:
///   auto *filter = new FilterRowWidget(tableView, this);
///   connect(filter, &FilterRowWidget::filterChanged, ...);
class FilterRowWidget : public QWidget {
    Q_OBJECT
public:
    explicit FilterRowWidget(QTableView *view, QWidget *parent = nullptr);

    void setFilterVisible(bool visible);
    bool isFilterVisible() const;
    void clearFilters();

    /// Rebuild filter inputs when the model/columns change.
    void syncToModel();

signals:
    void filterChanged(int column, const QString &text);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void rebuildInputs();
    void syncWidths();

    QTableView *m_view;
    QHBoxLayout *m_layout;
    QVector<QLineEdit*> m_inputs;
    bool m_visible = true;
};

} // namespace QtWlPlugin
