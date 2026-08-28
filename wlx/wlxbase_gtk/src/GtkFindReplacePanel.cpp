#include "wlxbase_gtk/GtkFindReplacePanel.h"
#include "wlxbase_gtk/GtkFocusManager.h"

namespace GtkWlPlugin {

GtkFindReplacePanel::GtkFindReplacePanel(GtkFocusManager *fm)
    : m_fm(fm)
{
    m_root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(m_root), 4);

    GtkWidget *findRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    m_txtFind = gtk_search_entry_new();
    gtk_widget_set_hexpand(m_txtFind, TRUE);
    gtk_box_pack_start(GTK_BOX(findRow), gtk_label_new("Find:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(findRow), m_txtFind, TRUE, TRUE, 0);

    GtkWidget *btnPrev = gtk_button_new_with_label("Previous");
    GtkWidget *btnNext = gtk_button_new_with_label("Next");
    gtk_box_pack_start(GTK_BOX(findRow), btnPrev, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(findRow), btnNext, FALSE, FALSE, 0);

    m_btnClose = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_box_pack_start(GTK_BOX(findRow), m_btnClose, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_root), findRow, FALSE, FALSE, 0);

    GtkWidget *replaceRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    m_lblReplace = gtk_label_new("Replace:");
    m_txtReplace = gtk_entry_new();
    gtk_widget_set_hexpand(m_txtReplace, TRUE);
    m_btnReplace = gtk_button_new_with_label("Replace");
    m_btnReplaceAll = gtk_button_new_with_label("Replace All");
    gtk_box_pack_start(GTK_BOX(replaceRow), m_lblReplace, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(replaceRow), m_txtReplace, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(replaceRow), m_btnReplace, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(replaceRow), m_btnReplaceAll, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_root), replaceRow, FALSE, FALSE, 0);

    m_optionsRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    m_chkMatchCase = gtk_check_button_new_with_label("Match case");
    m_chkMatchEntire = gtk_check_button_new_with_label("Entire cell");
    m_chkRegex = gtk_check_button_new_with_label("Regex");
    gtk_box_pack_start(GTK_BOX(m_optionsRow), m_chkMatchCase, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_optionsRow), m_chkMatchEntire, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_optionsRow), m_chkRegex, FALSE, FALSE, 0);
    m_lblStatus = gtk_label_new("");
    gtk_box_pack_end(GTK_BOX(m_optionsRow), m_lblStatus, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m_root), m_optionsRow, FALSE, FALSE, 0);

    if (m_fm) {
        m_fm->addInputWidget(m_txtFind);
        m_fm->addInputWidget(m_txtReplace);
    }

    g_signal_connect_swapped(btnNext, "clicked", G_CALLBACK(+[](gpointer data) {
        auto *self = static_cast<GtkFindReplacePanel *>(data);
        if (self->onFindRequested) self->onFindRequested(true);
    }), this);
    g_signal_connect_swapped(btnPrev, "clicked", G_CALLBACK(+[](gpointer data) {
        auto *self = static_cast<GtkFindReplacePanel *>(data);
        if (self->onFindRequested) self->onFindRequested(false);
    }), this);
    g_signal_connect_swapped(m_txtFind, "activate", G_CALLBACK(+[](gpointer data) {
        auto *self = static_cast<GtkFindReplacePanel *>(data);
        if (self->onFindRequested) self->onFindRequested(true);
    }), this);
    g_signal_connect_swapped(m_btnReplace, "clicked", G_CALLBACK(+[](gpointer data) {
        auto *self = static_cast<GtkFindReplacePanel *>(data);
        if (self->onReplaceRequested) self->onReplaceRequested();
    }), this);
    g_signal_connect_swapped(m_btnReplaceAll, "clicked", G_CALLBACK(+[](gpointer data) {
        auto *self = static_cast<GtkFindReplacePanel *>(data);
        if (self->onReplaceAllRequested) self->onReplaceAllRequested();
    }), this);
    g_signal_connect_swapped(m_btnClose, "clicked", G_CALLBACK(+[](gpointer data) {
        auto *self = static_cast<GtkFindReplacePanel *>(data);
        self->showPanel(false);
        if (self->onPanelClosed) self->onPanelClosed();
    }), this);

    gtk_widget_set_no_show_all(m_root, TRUE); // stays hidden until showPanel(true)
}

GtkFindReplacePanel::~GtkFindReplacePanel()
{
    if (m_fm) {
        m_fm->removeInputWidget(m_txtFind);
        m_fm->removeInputWidget(m_txtReplace);
    }
}

void GtkFindReplacePanel::setReplaceEnabled(bool enabled)
{
    gtk_widget_set_visible(m_lblReplace, enabled);
    gtk_widget_set_visible(m_txtReplace, enabled);
    gtk_widget_set_visible(m_btnReplace, enabled);
    gtk_widget_set_visible(m_btnReplaceAll, enabled);
}

std::string GtkFindReplacePanel::findText() const { return gtk_entry_get_text(GTK_ENTRY(m_txtFind)); }
std::string GtkFindReplacePanel::replaceText() const { return gtk_entry_get_text(GTK_ENTRY(m_txtReplace)); }
bool GtkFindReplacePanel::matchCase() const { return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m_chkMatchCase)); }
bool GtkFindReplacePanel::matchEntireCell() const { return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m_chkMatchEntire)); }
bool GtkFindReplacePanel::useRegex() const { return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m_chkRegex)); }

void GtkFindReplacePanel::setStatusText(const std::string &text)
{
    gtk_label_set_text(GTK_LABEL(m_lblStatus), text.c_str());
}

void GtkFindReplacePanel::showPanel(bool show)
{
    if (show) {
        // m_root has no_show_all set (so an ancestor's show_all() at
        // ListLoad time doesn't prematurely reveal the panel) -- but that
        // flag makes gtk_widget_show_all() skip m_root's ENTIRE subtree
        // when called directly on m_root too, not just when recursing
        // into it as someone else's descendant. The previous
        // gtk_widget_set_visible(m_root, show) + gtk_widget_show_all(m_root)
        // pair left m_root itself visible but every child (find/replace
        // rows, options row) still hidden. Clear the flag on first reveal
        // -- hiding afterwards always goes through plain gtk_widget_hide()
        // below, never relies on no_show_all again, so it's safe to drop
        // permanently here.
        gtk_widget_set_no_show_all(m_root, FALSE);
        gtk_widget_show_all(m_root);
        gtk_widget_grab_focus(m_txtFind);
    } else {
        gtk_widget_hide(m_root);
    }
}

bool GtkFindReplacePanel::isPanelVisible() const { return gtk_widget_get_visible(m_root); }

} // namespace GtkWlPlugin
