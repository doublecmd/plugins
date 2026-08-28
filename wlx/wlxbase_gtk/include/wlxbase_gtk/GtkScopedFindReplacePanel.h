#pragma once

#include "wlxbase_gtk/GtkFindReplacePanel.h"
#include <vector>
#include <string>

namespace GtkWlPlugin {

/// GTK counterpart to QtWlPlugin::ScopedFindReplacePanel — adds a scope
/// GtkComboBoxText ("All Cells", "Current Column", ...) to the base panel.
class GtkScopedFindReplacePanel : public GtkFindReplacePanel {
public:
    explicit GtkScopedFindReplacePanel(GtkFocusManager *fm);

    void setScopes(const std::vector<std::string> &scopes);
    std::string currentScope() const;

private:
    GtkWidget *m_comboScope;
};

} // namespace GtkWlPlugin
