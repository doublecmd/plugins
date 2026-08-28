#include <wlxbase_wlqt/PluginStatusBar.h>

#include <QHBoxLayout>
#include <QFrame>
#include <QFont>

namespace QtWlPlugin {

PluginStatusBar::PluginStatusBar(QWidget *parent)
    : QWidget(parent)
    , m_layout(new QHBoxLayout(this))
{
    m_layout->setContentsMargins(4, 2, 4, 2);
    m_layout->setSpacing(0);
    setFixedHeight(22);
    setStyleSheet(QStringLiteral(
        "PluginStatusBar { border-top: 1px solid #c0c0c0; }"
    ).replace(QStringLiteral("PluginStatusBar"),
              QStringLiteral("QtWlPlugin--PluginStatusBar")));

    QFont smallFont = font();
    smallFont.setPointSize(9);

    m_encodingLabel = new QLabel(this);
    m_encodingLabel->setFont(smallFont);

    m_formatLabel = new QLabel(this);
    m_formatLabel->setFont(smallFont);

    m_rowLabel = new QLabel(this);
    m_rowLabel->setFont(smallFont);

    rebuild();
}

QFrame *PluginStatusBar::createSeparator()
{
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    sep->setFixedWidth(2);
    return sep;
}

void PluginStatusBar::rebuild()
{
    // Remove all items from layout (without deleting the labels themselves)
    while (m_layout->count() > 0) {
        QLayoutItem *item = m_layout->takeAt(0);
        // Delete separators and spacers, keep our persistent labels
        if (item->widget()
            && item->widget() != m_encodingLabel
            && item->widget() != m_formatLabel
            && item->widget() != m_rowLabel
            && !m_extras.values().contains(qobject_cast<QLabel*>(item->widget()))) {
            delete item->widget();
        }
        delete item;
    }

    // Add extras first (sorted by key)
    QStringList keys = m_extras.keys();
    keys.sort();
    for (const auto &key : keys) {
        if (m_layout->count() > 0)
            m_layout->addWidget(createSeparator());
        m_layout->addWidget(m_extras[key]);
    }

    // Encoding
    if (!m_encodingLabel->text().isEmpty()) {
        if (m_layout->count() > 0)
            m_layout->addWidget(createSeparator());
        m_layout->addWidget(m_encodingLabel);
    }

    // Format
    if (!m_formatLabel->text().isEmpty()) {
        if (m_layout->count() > 0)
            m_layout->addWidget(createSeparator());
        m_layout->addWidget(m_formatLabel);
    }

    // Spacer → row count on right
    m_layout->addStretch(1);

    if (!m_rowLabel->text().isEmpty()) {
        m_layout->addWidget(createSeparator());
        m_layout->addWidget(m_rowLabel);
    }
}

void PluginStatusBar::setEncoding(const QString &encoding)
{
    m_encodingLabel->setText(QStringLiteral(" %1 ").arg(encoding));
    rebuild();
}

void PluginStatusBar::setFormatInfo(const QString &info)
{
    m_formatLabel->setText(QStringLiteral(" %1 ").arg(info));
    rebuild();
}

void PluginStatusBar::setRowCount(int filtered, int total)
{
    if (filtered == total)
        m_rowLabel->setText(QStringLiteral(" Rows: %1 ").arg(total));
    else
        m_rowLabel->setText(QStringLiteral(" Rows: %1/%2 ").arg(filtered).arg(total));
    rebuild();
}

void PluginStatusBar::setExtraInfo(const QString &key, const QString &value)
{
    QLabel *label = m_extras.value(key, nullptr);
    if (!label) {
        QFont smallFont = font();
        smallFont.setPointSize(9);
        label = new QLabel(this);
        label->setFont(smallFont);
        m_extras[key] = label;
    }
    label->setText(QStringLiteral(" %1: %2 ").arg(key, value));
    rebuild();
}

void PluginStatusBar::removeExtraInfo(const QString &key)
{
    if (m_extras.contains(key)) {
        delete m_extras.take(key);
        rebuild();
    }
}

} // namespace QtWlPlugin
