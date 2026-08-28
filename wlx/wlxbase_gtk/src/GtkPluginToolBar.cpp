#include "wlxbase_gtk/GtkPluginToolBar.h"
#include "wlxbase_gtk/GtkFocusManager.h"

namespace GtkWlPlugin {

namespace {

// Ports PluginToolBar's (Qt6) getFallbackUnicodeIcon() table, adjusted for
// the "-symbolic" suffix convention GTK icon names use here -- shown when
// the active icon theme doesn't have the requested icon at all (common on
// minimal/non-GNOME themes), so a toolbar button never silently ends up
// with no icon and no visible label.
const char *fallbackUnicodeIcon(const std::string &iconName)
{
    if (iconName == "document-save-symbolic") return "\xF0\x9F\x96\xAB";         // 🖫
    if (iconName == "document-save-as-symbolic") return "\xF0\x9F\x96\xAA";      // 🖪
    if (iconName == "edit-undo-symbolic") return "\xE2\x86\xB6";                 // ↶
    if (iconName == "edit-redo-symbolic") return "\xE2\x86\xB7";                 // ↷
    if (iconName == "document-print-symbolic") return "\xF0\x9F\x96\xA8\xEF\xB8\x8E"; // 🖨︎
    if (iconName == "view-refresh-symbolic") return "\xE2\x9F\xB3";              // ⟳
    if (iconName == "view-reveal-symbolic") return "\xF0\x9F\x91\x81\xEF\xB8\x8E"; // 👁︎
    if (iconName == "format-text-wrap-symbolic") return "\xE2\x86\xA9\xEF\xB8\x8E"; // ↩︎
    if (iconName == "document-open-symbolic") return "\xE2\x86\x97\xEF\xB8\x8E";  // ↗︎
    if (iconName == "edit-find-symbolic") return "\xF0\x9F\x94\x8D\xEF\xB8\x8E";  // 🔍︎
    if (iconName == "edit-find-replace-symbolic") return "\xF0\x9F\x94\x8D\xEF\xB8\x8E"; // 🔍︎
    if (iconName == "view-list-symbolic") return "\xE2\x98\xB0";                 // ☰
    if (iconName == "view-grid-symbolic") return "\xE2\x96\xA6";                 // ▦
    if (iconName == "document-send-symbolic") return "\xE2\x86\x91";             // ↑
    return nullptr;
}

// Builds a horizontal icon+label box, matching Qt's default
// ToolButtonTextBesideIcon display: an icon from the current theme when
// available, falling back to a Unicode glyph (as a label, sized up to read
// as an icon) when the theme lacks it, followed by the button's text label.
GtkWidget *buildIconTextBox(const std::string &label, const std::string &iconName)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    if (!iconName.empty()) {
        if (gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), iconName.c_str())) {
            gtk_box_pack_start(GTK_BOX(box),
                gtk_image_new_from_icon_name(iconName.c_str(), GTK_ICON_SIZE_SMALL_TOOLBAR),
                FALSE, FALSE, 0);
        } else if (const char *glyph = fallbackUnicodeIcon(iconName)) {
            GtkWidget *glyphLabel = gtk_label_new(nullptr);
            std::string markup = "<span size='large'>" + std::string(glyph) + "</span>";
            gtk_label_set_markup(GTK_LABEL(glyphLabel), markup.c_str());
            gtk_box_pack_start(GTK_BOX(box), glyphLabel, FALSE, FALSE, 0);
        }
    }
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(label.c_str()), FALSE, FALSE, 0);
    return box;
}

} // namespace

GtkPluginToolBar::GtkPluginToolBar(GtkFocusManager *fm)
    : m_fm(fm)
{
    m_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(m_box), 2);
}

GtkWidget *GtkPluginToolBar::addToolAction(const std::string &label, const std::string &iconName,
                                            std::function<void()> onClicked)
{
    GtkWidget *btn = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(btn), buildIconTextBox(label, iconName));
    gtk_widget_set_tooltip_text(btn, label.c_str());
    gtk_widget_set_can_focus(btn, FALSE);
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);

    auto *cb = new std::function<void()>(std::move(onClicked));
    g_signal_connect_data(btn, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
        (*static_cast<std::function<void()> *>(data))();
    }), cb, +[](gpointer data, GClosure *) { delete static_cast<std::function<void()> *>(data); }, G_CONNECT_AFTER);

    gtk_box_pack_start(GTK_BOX(m_box), btn, FALSE, FALSE, 0);
    gtk_widget_show_all(btn);
    return btn;
}

GtkWidget *GtkPluginToolBar::addToggleAction(const std::string &label, const std::string &iconName,
                                              bool initialState, std::function<void(bool)> onToggled)
{
    GtkWidget *btn = gtk_toggle_button_new();
    gtk_container_add(GTK_CONTAINER(btn), buildIconTextBox(label, iconName));
    gtk_widget_set_tooltip_text(btn, label.c_str());
    gtk_widget_set_can_focus(btn, FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), initialState);

    auto *cb = new std::function<void(bool)>(std::move(onToggled));
    g_signal_connect_data(btn, "toggled", G_CALLBACK(+[](GtkToggleButton *tb, gpointer data) {
        (*static_cast<std::function<void(bool)> *>(data))(gtk_toggle_button_get_active(tb));
    }), cb, +[](gpointer data, GClosure *) { delete static_cast<std::function<void(bool)> *>(data); }, G_CONNECT_AFTER);

    gtk_box_pack_start(GTK_BOX(m_box), btn, FALSE, FALSE, 0);
    gtk_widget_show_all(btn);
    return btn;
}

void GtkPluginToolBar::addSeparator()
{
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(m_box), sep, FALSE, FALSE, 4);
    gtk_widget_show(sep);
}

} // namespace GtkWlPlugin
