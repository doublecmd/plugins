#include <wlxbase_wlqt/PluginSplitView.h>

namespace QtWlPlugin {

PluginSplitView::PluginSplitView(QWidget *leftPanel, QWidget *rightContent,
                                 QWidget *parent)
    : QSplitter(Qt::Horizontal, parent)
    , m_left(leftPanel)
    , m_right(rightContent)
{
    addWidget(m_left);
    addWidget(m_right);

    m_left->setMinimumWidth(100);
    m_left->setMaximumWidth(350);

    setStretchFactor(0, 0);  // Left: fixed
    setStretchFactor(1, 1);  // Right: stretches

    setSizes({180, 600});
    setChildrenCollapsible(false);

    setHandleWidth(3);
}

void PluginSplitView::setLeftWidth(int pixels)
{
    setSizes({pixels, width() - pixels});
}

QWidget *PluginSplitView::leftPanel() const { return m_left; }
QWidget *PluginSplitView::rightContent() const { return m_right; }

} // namespace QtWlPlugin
