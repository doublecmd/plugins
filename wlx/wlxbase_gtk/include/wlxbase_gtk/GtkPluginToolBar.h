#pragma once

#include <gtk/gtk.h>
#include <functional>
#include <string>

namespace GtkWlPlugin {

class GtkFocusManager;

/// GTK counterpart to QtWlPlugin::PluginToolBar — a GtkBox row of buttons
/// that never take keyboard focus (matching PluginToolBar's `NoFocus`
/// enforcement), so clicking a toolbar button doesn't steal focus away
/// from the primary view.
class GtkPluginToolBar {
public:
    explicit GtkPluginToolBar(GtkFocusManager *fm);

    GtkWidget *widget() const { return m_box; }

    /// Add a labeled/iconed toggle-free action button. Returns the
    /// GtkButton so the caller can further customize it if needed.
    GtkWidget *addToolAction(const std::string &label, const std::string &iconName,
                              std::function<void()> onClicked);

    /// Add a checkable (toggle) action button.
    GtkWidget *addToggleAction(const std::string &label, const std::string &iconName,
                                bool initialState, std::function<void(bool)> onToggled);

    void addSeparator();

private:
    GtkFocusManager *m_fm;
    GtkWidget *m_box;
};

} // namespace GtkWlPlugin
