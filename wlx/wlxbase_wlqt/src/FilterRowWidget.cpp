#include <wlxbase_wlqt/FilterRowWidget.h>

#include <QLineEdit>
#include <QHBoxLayout>
#include <QTableView>
#include <QHeaderView>
#include <QAbstractItemModel>
#include <QEvent>

namespace QtWlPlugin {

FilterRowWidget::FilterRowWidget(QTableView *view, QWidget *parent)
    : QWidget(parent)
    , m_view(view)
    , m_layout(new QHBoxLayout(this))
{
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    setFixedHeight(24);

    // Track header geometry changes to sync widths
    if (m_view->horizontalHeader())
        m_view->horizontalHeader()->installEventFilter(this);

    rebuildInputs();
}

void FilterRowWidget::rebuildInputs()
{
    // Clear existing
    for (auto *input : m_inputs)
        delete input;
    m_inputs.clear();

    QAbstractItemModel *model = m_view->model();
    if (!model) return;

    int cols = model->columnCount();
    for (int c = 0; c < cols; ++c) {
        auto *input = new QLineEdit(this);
        input->setPlaceholderText(QStringLiteral("Filter..."));
        input->setMaximumHeight(22);
        input->setFrame(true);
        input->setClearButtonEnabled(true);

        int col = c;
        connect(input, &QLineEdit::textChanged, this, [this, col](const QString &text) {
            emit filterChanged(col, text);
        });

        m_layout->addWidget(input);
        m_inputs.append(input);
    }

    syncWidths();
}

void FilterRowWidget::syncWidths()
{
    QHeaderView *header = m_view->horizontalHeader();
    if (!header) return;

    for (int c = 0; c < m_inputs.size(); ++c) {
        int w = header->sectionSize(c);
        m_inputs[c]->setFixedWidth(w);
    }
}

void FilterRowWidget::setFilterVisible(bool visible)
{
    m_visible = visible;
    setVisible(visible);
}

bool FilterRowWidget::isFilterVisible() const
{
    return m_visible;
}

void FilterRowWidget::clearFilters()
{
    for (auto *input : m_inputs)
        input->clear();
}

void FilterRowWidget::syncToModel()
{
    rebuildInputs();
}

bool FilterRowWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_view->horizontalHeader()) {
        if (event->type() == QEvent::Resize
            || event->type() == QEvent::LayoutRequest) {
            syncWidths();
        }
    }
    return QWidget::eventFilter(obj, event);
}

} // namespace QtWlPlugin
