#pragma once

#include <QToolBar>
#include <QKeySequence>

namespace QtWlPlugin {

class FocusManager;

enum class ButtonDisplay {
    IconOnly,
    TextOnly,
    Both
};

enum class IconMode {
    System,
    Unicode
};

/// A QToolBar subclass that automatically integrates with FocusManager.
///
/// All action widgets are set to Qt::NoFocus, and focus is restored to the
/// primary view after any action trigger. Provides convenience for adding
/// actions with automatic shortcut registration through FocusManager.
class PluginToolBar : public QToolBar {
    Q_OBJECT
public:
    explicit PluginToolBar(FocusManager *fm, QWidget *parent = nullptr);

    /// Add an action and optionally register a shortcut through FocusManager.
    QAction *addToolAction(const QString &text,
                           const QKeySequence &shortcut = {},
                           int ctx = 0 /* FocusManager::WhenNoInput */,
                           const QString &systemIconName = {},
                           const QString &unicodeIcon = {},
                           ButtonDisplay display = ButtonDisplay::Both,
                           IconMode iconMode = IconMode::System);

protected:
    void actionEvent(QActionEvent *event) override;

private:
    void enforceNoFocus();
    FocusManager *m_fm;
};

} // namespace QtWlPlugin
