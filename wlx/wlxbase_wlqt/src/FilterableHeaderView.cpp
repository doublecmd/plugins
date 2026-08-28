#include <wlxbase_wlqt/FilterableHeaderView.h>

#include <QLineEdit>
#include <QAbstractItemModel>
#include <QPainter>
#include <QStyleOptionHeader>

namespace QtWlPlugin {

FilterableHeaderView::FilterableHeaderView(Qt::Orientation orientation, QWidget *parent)
    : QHeaderView(orientation, parent)
{
    // Re-position filter inputs whenever sections change
    connect(this, &QHeaderView::sectionResized, this, &FilterableHeaderView::adjustInputPositions);
    connect(this, &QHeaderView::sectionMoved, this, &FilterableHeaderView::adjustInputPositions);
    connect(this, &QHeaderView::geometriesChanged, this, &FilterableHeaderView::adjustInputPositions);
}

void FilterableHeaderView::setFilterEnabled(bool enabled)
{
    if (m_filterEnabled == enabled)
        return;
    m_filterEnabled = enabled;

    if (m_filterEnabled) {
        rebuildInputs();
    } else {
        for (auto *input : m_inputs)
            delete input;
        m_inputs.clear();
    }

    // Force header to recalculate its size
    updateGeometries();
    viewport()->update();
}

bool FilterableHeaderView::isFilterEnabled() const
{
    return m_filterEnabled;
}

void FilterableHeaderView::clearFilters()
{
    for (auto *input : m_inputs)
        input->clear();
}

QString FilterableHeaderView::filterText(int column) const
{
    if (column >= 0 && column < m_inputs.size())
        return m_inputs[column]->text();
    return {};
}

void FilterableHeaderView::setHeaderVisible(bool visible)
{
    if (m_headerVisible == visible)
        return;
    m_headerVisible = visible;
    updateGeometries();
    viewport()->update();
}

bool FilterableHeaderView::isHeaderVisible() const
{
    return m_headerVisible;
}

QSize FilterableHeaderView::sizeHint() const
{
    int h = 0;
    if (m_headerVisible) {
        h += QHeaderView::sizeHint().height();
    }
    if (m_filterEnabled) {
        h += m_filterRowHeight;
    }
    return QSize(QHeaderView::sizeHint().width(), h);
}

void FilterableHeaderView::updateGeometries()
{
    int bottomMargin = 0;
    if (m_filterEnabled && m_headerVisible) {
        bottomMargin = m_filterRowHeight;
    }
    setViewportMargins(0, 0, 0, bottomMargin);

    QHeaderView::updateGeometries();

    if (m_filterEnabled)
        adjustInputPositions();
}

void FilterableHeaderView::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    if (!m_headerVisible) {
        painter->save();
        QStyleOptionHeader opt;
        initStyleOption(&opt);
        opt.section = logicalIndex;
        opt.rect = rect;
        opt.text.clear();
        opt.icon = QIcon();
        style()->drawControl(QStyle::CE_HeaderSection, &opt, painter, this);
        painter->restore();
        return;
    }
    QHeaderView::paintSection(painter, rect, logicalIndex);
}

void FilterableHeaderView::adjustInputPositions()
{
    if (!m_filterEnabled || !model())
        return;

    int cols = model()->columnCount();

    // Rebuild if column count changed
    if (m_inputs.size() != cols)
        rebuildInputs();

    int labelHeight = m_headerVisible ? QHeaderView::sizeHint().height() : 0;

    for (int i = 0; i < m_inputs.size() && i < cols; ++i) {
        int logicalIdx = logicalIndex(i);
        if (logicalIdx < 0 || logicalIdx >= m_inputs.size())
            continue;

        int xPos = sectionViewportPosition(logicalIdx);
        int w = sectionSize(logicalIdx);

        m_inputs[logicalIdx]->setGeometry(xPos, labelHeight, w, m_filterRowHeight);
        m_inputs[logicalIdx]->setVisible(!isSectionHidden(logicalIdx));
    }
}

void FilterableHeaderView::rebuildInputs()
{
    for (auto *input : m_inputs)
        delete input;
    m_inputs.clear();

    if (!model() || !m_filterEnabled)
        return;

    int cols = model()->columnCount();
    m_inputs.reserve(cols);

    for (int c = 0; c < cols; ++c) {
        auto *input = new QLineEdit(this);
        input->setPlaceholderText(QStringLiteral("Filter..."));
        input->setFrame(true);
        input->setClearButtonEnabled(true);

        // Use a small font to fit the compact row height
        QFont f = input->font();
        f.setPointSize(f.pointSize() - 1);
        input->setFont(f);

        int col = c;
        connect(input, &QLineEdit::textChanged, this, [this, col](const QString &text) {
            emit filterChanged(col, text);
        });

        m_inputs.append(input);
    }

    adjustInputPositions();
}

} // namespace QtWlPlugin
