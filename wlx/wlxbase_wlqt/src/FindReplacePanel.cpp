#include <wlxbase_wlqt/FindReplacePanel.h>
#include <wlxbase_wlqt/FocusManager.h>

#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace QtWlPlugin {

FindReplacePanel::FindReplacePanel(FocusManager *fm, QWidget *parent)
    : QWidget(parent)
    , m_fm(fm)
{
    setObjectName("FindReplacePanel");
    setVisible(false);
    setStyleSheet(
        "QWidget#FindReplacePanel { background-color: palette(window); border-top: 1px solid palette(mid); }"
        "QPushButton { border: 1px solid palette(mid); border-radius: 3px; padding: 2px 8px; background-color: palette(button); }"
        "QPushButton:hover { background-color: palette(light); }"
        "QPushButton:pressed { background-color: palette(midlight); }"
        "QPushButton#CloseButton { border: none; background: transparent; }"
        "QPushButton#CloseButton:hover { background-color: palette(light); }"
    );

    auto *panelLayout = new QVBoxLayout(this);
    panelLayout->setContentsMargins(6, 6, 6, 6);
    panelLayout->setSpacing(6);

    // --- Row 1: Find + Replace inputs ---
    auto *row1 = new QHBoxLayout();
    row1->setSpacing(6);

    auto *lblFind = new QLabel("Find:", this);
    m_txtFind = new QLineEdit(this);
    m_txtFind->setPlaceholderText("Search query...");

    m_lblReplace = new QLabel("Replace:", this);
    m_txtReplace = new QLineEdit(this);
    m_txtReplace->setPlaceholderText("Replacement text...");

    row1->addWidget(lblFind);
    row1->addWidget(m_txtFind, 1);
    row1->addWidget(m_lblReplace);
    row1->addWidget(m_txtReplace, 1);

    // --- Row 2: Options + actions ---
    m_optionsRow = new QHBoxLayout();
    m_optionsRow->setSpacing(6);

    m_chkMatchCase = new QCheckBox("Match Case", this);
    m_chkMatchEntire = new QCheckBox("Match Entire Cell", this);
    m_chkRegex = new QCheckBox("Regular Expression", this);

    m_chkMatchCase->setFocusPolicy(Qt::NoFocus);
    m_chkMatchEntire->setFocusPolicy(Qt::NoFocus);
    m_chkRegex->setFocusPolicy(Qt::NoFocus);

    auto *btnFindPrev = new QPushButton("Find Previous", this);
    auto *btnFindNext = new QPushButton("Find Next", this);
    m_btnReplace = new QPushButton("Replace", this);
    m_btnReplaceAll = new QPushButton("Replace All", this);

    btnFindPrev->setFocusPolicy(Qt::NoFocus);
    btnFindNext->setFocusPolicy(Qt::NoFocus);
    m_btnReplace->setFocusPolicy(Qt::NoFocus);
    m_btnReplaceAll->setFocusPolicy(Qt::NoFocus);

    m_lblStatus = new QLabel(this);
    m_lblStatus->setStyleSheet("color: palette(link); font-weight: bold;");

    auto *btnClose = new QPushButton("\u2715", this);
    btnClose->setObjectName("CloseButton");
    btnClose->setFixedWidth(30);
    btnClose->setFlat(true);
    btnClose->setFocusPolicy(Qt::NoFocus);

    m_optionsRow->addWidget(m_chkMatchCase);
    m_optionsRow->addWidget(m_chkMatchEntire);
    m_optionsRow->addWidget(m_chkRegex);
    m_optionsRow->addWidget(btnFindPrev);
    m_optionsRow->addWidget(btnFindNext);
    m_optionsRow->addWidget(m_btnReplace);
    m_optionsRow->addWidget(m_btnReplaceAll);
    m_optionsRow->addWidget(m_lblStatus, 1);
    m_optionsRow->addWidget(btnClose);

    panelLayout->addLayout(row1);
    panelLayout->addLayout(m_optionsRow);

    // --- Connections ---
    connect(btnFindNext, &QPushButton::clicked, this, [this]() { emit findRequested(true); });
    connect(btnFindPrev, &QPushButton::clicked, this, [this]() { emit findRequested(false); });
    connect(m_btnReplace, &QPushButton::clicked, this, &FindReplacePanel::replaceRequested);
    connect(m_btnReplaceAll, &QPushButton::clicked, this, &FindReplacePanel::replaceAllRequested);
    connect(btnClose, &QPushButton::clicked, this, [this]() { showPanel(false); });
    connect(m_txtFind, &QLineEdit::returnPressed, this, [this]() { emit findRequested(true); });
    connect(m_txtReplace, &QLineEdit::returnPressed, this, &FindReplacePanel::replaceRequested);

    // Register find/replace inputs as input widgets with FocusManager
    fm->addInputWidget(m_txtFind);
    fm->addInputWidget(m_txtReplace);

    // Register shortcuts (Always context — should work even when editing)
    fm->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), FocusManager::Always,
        [this]() { showPanel(!isPanelVisible()); return true; });
    fm->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), FocusManager::Always,
        [this]() { showPanel(!isPanelVisible()); return true; });
}

void FindReplacePanel::setReplaceEnabled(bool enabled)
{
    m_lblReplace->setVisible(enabled);
    m_txtReplace->setVisible(enabled);
    m_btnReplace->setVisible(enabled);
    m_btnReplaceAll->setVisible(enabled);
}

QString FindReplacePanel::findText() const { return m_txtFind->text(); }
QString FindReplacePanel::replaceText() const { return m_txtReplace->text(); }
bool FindReplacePanel::matchCase() const { return m_chkMatchCase->isChecked(); }
bool FindReplacePanel::matchEntireCell() const { return m_chkMatchEntire->isChecked(); }
bool FindReplacePanel::useRegex() const { return m_chkRegex->isChecked(); }

void FindReplacePanel::setStatusText(const QString &text) { m_lblStatus->setText(text); }

void FindReplacePanel::showPanel(bool show)
{
    setVisible(show);
    if (show) {
        m_fm->setFocusProxy(m_txtFind);
        m_txtFind->setFocus(Qt::OtherFocusReason);
        m_txtFind->selectAll();
        m_lblStatus->clear();
    } else {
        m_fm->resetFocusProxy();
        m_lblStatus->clear();
        m_fm->restoreViewFocus();
        emit panelClosed();
    }
}

bool FindReplacePanel::isPanelVisible() const { return isVisible(); }

FocusManager *FindReplacePanel::focusManager() const { return m_fm; }
QLabel *FindReplacePanel::statusLabel() const { return m_lblStatus; }
QHBoxLayout *FindReplacePanel::optionsRow() const { return m_optionsRow; }

} // namespace QtWlPlugin
