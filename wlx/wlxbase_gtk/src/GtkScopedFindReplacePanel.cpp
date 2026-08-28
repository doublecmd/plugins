#include "wlxbase_gtk/GtkScopedFindReplacePanel.h"

namespace GtkWlPlugin {

GtkScopedFindReplacePanel::GtkScopedFindReplacePanel(GtkFocusManager *fm)
    : GtkFindReplacePanel(fm)
{
    m_comboScope = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(optionsRow()), m_comboScope, FALSE, FALSE, 0);
    gtk_widget_show(m_comboScope);
}

void GtkScopedFindReplacePanel::setScopes(const std::vector<std::string> &scopes)
{
    GtkComboBoxText *combo = GTK_COMBO_BOX_TEXT(m_comboScope);
    // Clear existing entries.
    gtk_combo_box_text_remove_all(combo);
    for (const auto &s : scopes)
        gtk_combo_box_text_append_text(combo, s.c_str());
    if (!scopes.empty())
        gtk_combo_box_set_active(GTK_COMBO_BOX(m_comboScope), 0);
}

std::string GtkScopedFindReplacePanel::currentScope() const
{
    gchar *text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(m_comboScope));
    std::string result = text ? text : "";
    g_free(text);
    return result;
}

} // namespace GtkWlPlugin
