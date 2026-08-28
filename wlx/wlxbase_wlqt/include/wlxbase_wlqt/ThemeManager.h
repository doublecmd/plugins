#pragma once

#include <QWidget>
#include <QString>

namespace QtWlPlugin {

/// Shared dark/light theme toggle for WLX plugins.
///
/// Applies a comprehensive Qt stylesheet to the given widget tree.
/// Preference is persisted in QSettings.
class ThemeManager {
public:
    enum Theme { Light, Dark };

    static void applyTheme(QWidget *root, Theme theme);
    static Theme currentTheme();
    static void toggleTheme(QWidget *root);
    static bool isDark();

private:
    static QString darkStylesheet();
    static Theme s_current;
};

} // namespace QtWlPlugin
