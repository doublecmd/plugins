#include <wlxbase_wlqt/ThemeManager.h>

#include <QSettings>

namespace QtWlPlugin {

ThemeManager::Theme ThemeManager::s_current = ThemeManager::Light;

void ThemeManager::applyTheme(QWidget *root, Theme theme)
{
    s_current = theme;

    if (theme == Light) {
        root->setStyleSheet(QString());
    } else {
        root->setStyleSheet(darkStylesheet());
    }

    // Persist
    QSettings settings(QStringLiteral("QtWlPlugin"), QStringLiteral("Preferences"));
    settings.setValue(QStringLiteral("theme"), theme == Dark ? QStringLiteral("dark") : QStringLiteral("light"));
}

ThemeManager::Theme ThemeManager::currentTheme()
{
    QSettings settings(QStringLiteral("QtWlPlugin"), QStringLiteral("Preferences"));
    QString val = settings.value(QStringLiteral("theme"), QStringLiteral("light")).toString();
    s_current = (val == QStringLiteral("dark")) ? Dark : Light;
    return s_current;
}

void ThemeManager::toggleTheme(QWidget *root)
{
    applyTheme(root, s_current == Light ? Dark : Light);
}

bool ThemeManager::isDark()
{
    return s_current == Dark;
}

QString ThemeManager::darkStylesheet()
{
    return QStringLiteral(R"(
        QWidget {
            background-color: #1e1e1e;
            color: #d4d4d4;
            selection-background-color: #264f78;
            selection-color: #ffffff;
        }

        QTableView {
            background-color: #1e1e1e;
            alternate-background-color: #252526;
            gridline-color: #3c3c3c;
            border: 1px solid #3c3c3c;
        }

        QHeaderView::section {
            background-color: #333333;
            color: #d4d4d4;
            border: 1px solid #3c3c3c;
            padding: 3px 5px;
            font-weight: bold;
        }

        QHeaderView::section:hover {
            background-color: #404040;
        }

        QTreeView {
            background-color: #252526;
            alternate-background-color: #2d2d2d;
            border: 1px solid #3c3c3c;
        }

        QTreeView::item:selected {
            background-color: #264f78;
        }

        QTreeView::item:hover {
            background-color: #2a2d2e;
        }

        QListWidget {
            background-color: #252526;
            border: 1px solid #3c3c3c;
        }

        QListWidget::item:selected {
            background-color: #264f78;
        }

        QListWidget::item:hover {
            background-color: #2a2d2e;
        }

        QToolBar {
            background-color: #333333;
            border-bottom: 1px solid #3c3c3c;
            spacing: 2px;
        }

        QToolBar QLabel {
            color: #d4d4d4;
        }

        QToolButton {
            background-color: transparent;
            color: #d4d4d4;
            border: 1px solid transparent;
            padding: 3px 6px;
            border-radius: 3px;
        }

        QToolButton:hover {
            background-color: #404040;
            border-color: #505050;
        }

        QToolButton:checked {
            background-color: #264f78;
            border-color: #3a7bd5;
        }

        QLineEdit {
            background-color: #3c3c3c;
            color: #d4d4d4;
            border: 1px solid #555555;
            padding: 2px 4px;
            border-radius: 2px;
        }

        QLineEdit:focus {
            border-color: #007acc;
        }

        QComboBox {
            background-color: #3c3c3c;
            color: #d4d4d4;
            border: 1px solid #555555;
            padding: 2px 4px;
            border-radius: 2px;
        }

        QComboBox::drop-down {
            border-left: 1px solid #555555;
        }

        QComboBox QAbstractItemView {
            background-color: #252526;
            color: #d4d4d4;
            selection-background-color: #264f78;
        }

        QMenu {
            background-color: #252526;
            color: #d4d4d4;
            border: 1px solid #3c3c3c;
        }

        QMenu::item:selected {
            background-color: #264f78;
        }

        QMenu::separator {
            height: 1px;
            background: #3c3c3c;
            margin: 4px 8px;
        }

        QTabWidget::pane {
            border: 1px solid #3c3c3c;
            background-color: #1e1e1e;
        }

        QTabBar::tab {
            background-color: #2d2d2d;
            color: #969696;
            border: 1px solid #3c3c3c;
            padding: 4px 12px;
            margin-right: 1px;
        }

        QTabBar::tab:selected {
            background-color: #1e1e1e;
            color: #d4d4d4;
            border-bottom-color: #1e1e1e;
        }

        QTabBar::tab:hover {
            background-color: #353535;
        }

        QPlainTextEdit {
            background-color: #1e1e1e;
            color: #d4d4d4;
            border: 1px solid #3c3c3c;
            font-family: "Cascadia Code", "Fira Code", "Source Code Pro", monospace;
        }

        QSplitter::handle {
            background-color: #3c3c3c;
        }

        QSplitter::handle:hover {
            background-color: #007acc;
        }

        QScrollBar:vertical {
            background-color: #1e1e1e;
            width: 12px;
            border: none;
        }

        QScrollBar::handle:vertical {
            background-color: #424242;
            min-height: 20px;
            border-radius: 3px;
            margin: 2px;
        }

        QScrollBar::handle:vertical:hover {
            background-color: #555555;
        }

        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }

        QScrollBar:horizontal {
            background-color: #1e1e1e;
            height: 12px;
            border: none;
        }

        QScrollBar::handle:horizontal {
            background-color: #424242;
            min-width: 20px;
            border-radius: 3px;
            margin: 2px;
        }

        QScrollBar::handle:horizontal:hover {
            background-color: #555555;
        }

        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0px;
        }

        /* Status bar */
        QFrame[frameShape="5"] {
            color: #555555;
        }
    )");
}

} // namespace QtWlPlugin
