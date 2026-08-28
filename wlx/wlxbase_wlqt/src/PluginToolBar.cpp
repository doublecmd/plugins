#include <wlxbase_wlqt/PluginToolBar.h>
#include <wlxbase_wlqt/FocusManager.h>

#include <QAction>
#include <QActionEvent>
#include <QTimer>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QIcon>

namespace QtWlPlugin {

PluginToolBar::PluginToolBar(FocusManager *fm, QWidget *parent)
    : QToolBar(parent)
    , m_fm(fm)
{
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet(
        "QToolBar { spacing: 2px; }"
        "QToolButton { padding: 2px 4px; margin: 1px; }"
    );
    enforceNoFocus();
}

static QIcon iconFromText(const QString &text, QWidget *parent)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    
    QFont font = parent ? parent->font() : QFont();
    font.setPixelSize(22);
    painter.setFont(font);
    
    QColor color = Qt::black;
    if (parent) {
        color = parent->palette().color(QPalette::ButtonText);
    }
    painter.setPen(color);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, text);
    painter.end();
    
    return QIcon(pixmap);
}

static QString getFallbackUnicodeIcon(const QString &systemIconName)
{
    if (systemIconName == QStringLiteral("document-save"))
        return QStringLiteral("🖫");
    if (systemIconName == QStringLiteral("document-save-as"))
        return QStringLiteral("🖪");
    if (systemIconName == QStringLiteral("edit-undo"))
        return QStringLiteral("↶");
    if (systemIconName == QStringLiteral("edit-redo"))
        return QStringLiteral("↷");
    if (systemIconName == QStringLiteral("document-print"))
        return QString::fromUtf8("\xf0\x9f\x96\xa8\xef\xb8\x8e"); // 🖨︎
    if (systemIconName == QStringLiteral("view-refresh"))
        return QStringLiteral("⟳");
    if (systemIconName == QStringLiteral("visibility"))
        return QString::fromUtf8("\xf0\x9f\x91\x81\xef\xb8\x8e"); // 👁︎
    if (systemIconName == QStringLiteral("format-text-direction-ltr"))
        return QString::fromUtf8("\xe2\x86\xa9\xef\xb8\x8e"); // ↩︎
    if (systemIconName == QStringLiteral("document-open"))
        return QString::fromUtf8("\xe2\x86\x97\xef\xb8\x8e"); // ↗︎
    if (systemIconName == QStringLiteral("edit-find"))
        return QString::fromUtf8("\xf0\x9f\x94\x8d\xef\xb8\x8e"); // 🔍︎
    return QString();
}

QAction *PluginToolBar::addToolAction(const QString &text,
                                     const QKeySequence &shortcut,
                                     int ctx,
                                     const QString &systemIconName,
                                     const QString &unicodeIcon,
                                     ButtonDisplay display,
                                     IconMode iconMode)
{
    QIcon icon;
    QString resolvedUnicode = unicodeIcon;
    if (resolvedUnicode.isEmpty() && !systemIconName.isEmpty()) {
        resolvedUnicode = getFallbackUnicodeIcon(systemIconName);
    }

    if (iconMode == IconMode::System) {
        if (!systemIconName.isEmpty()) {
            icon = QIcon::fromTheme(systemIconName);
        }
        if (icon.isNull() && !resolvedUnicode.isEmpty()) {
            icon = iconFromText(resolvedUnicode, this);
        }
    } else { // IconMode::Unicode
        if (!resolvedUnicode.isEmpty()) {
            icon = iconFromText(resolvedUnicode, this);
        }
    }

    QAction *action = new QAction(text, this);
    if (!icon.isNull()) {
        action->setIcon(icon);
    }
    action->setProperty("buttonDisplay", static_cast<int>(display));
    addAction(action);

    if (!shortcut.isEmpty()) {
        action->setToolTip(text + " (" + shortcut.toString(QKeySequence::NativeText) + ")");

        if (m_fm) {
            m_fm->registerShortcut(
                shortcut, static_cast<FocusManager::ShortcutContext>(ctx),
                [action]() { action->trigger(); return true; });
        }
    } else {
        action->setToolTip(text);
    }

    // Restore focus to primary view after action trigger
    connect(action, &QAction::triggered, this, [this]() {
        QTimer::singleShot(0, this, [this]() {
            if (m_fm)
                m_fm->restoreViewFocus();
        });
    });

    return action;
}

void PluginToolBar::actionEvent(QActionEvent *event)
{
    QToolBar::actionEvent(event);
    enforceNoFocus();
}

void PluginToolBar::enforceNoFocus()
{
    for (QAction *action : actions()) {
        QWidget *w = widgetForAction(action);
        if (w) {
            w->setFocusPolicy(Qt::NoFocus);
            if (auto *btn = qobject_cast<QToolButton*>(w)) {
                QVariant val = action->property("buttonDisplay");
                if (val.isValid()) {
                    auto display = static_cast<ButtonDisplay>(val.toInt());
                    if (display == ButtonDisplay::IconOnly) {
                        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
                    } else if (display == ButtonDisplay::TextOnly) {
                        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
                    } else {
                        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                    }
                }
            }
        }
    }
}

} // namespace QtWlPlugin
