#include <wlxbase_wlqt/ScopedFindReplacePanel.h>
#include <wlxbase_wlqt/FocusManager.h>

#include <QComboBox>
#include <QLabel>
#include <QHBoxLayout>

namespace QtWlPlugin {

ScopedFindReplacePanel::ScopedFindReplacePanel(FocusManager *fm, QWidget *parent)
    : FindReplacePanel(fm, parent)
{
    auto *lblScope = new QLabel("Scope:", this);
    m_comboScope = new QComboBox(this);
    m_comboScope->setFocusPolicy(Qt::NoFocus);

    // Insert at the beginning of the options row (before checkboxes)
    QHBoxLayout *row = optionsRow();
    row->insertWidget(0, lblScope);
    row->insertWidget(1, m_comboScope);
}

void ScopedFindReplacePanel::setScopes(const QStringList &scopes)
{
    m_comboScope->clear();
    m_comboScope->addItems(scopes);
}

QString ScopedFindReplacePanel::currentScope() const
{
    return m_comboScope->currentText();
}

} // namespace QtWlPlugin
