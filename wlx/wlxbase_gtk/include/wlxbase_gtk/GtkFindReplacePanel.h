#pragma once

#include <gtk/gtk.h>
#include <functional>
#include <string>

namespace GtkWlPlugin {

class GtkFocusManager;

/// GTK counterpart to QtWlPlugin::FindReplacePanel — a GtkBox shell with
/// find/replace entries, match-case/entire-cell/regex toggles, and
/// action buttons. Same "shell only, consumer does the matching" design
/// as the Qt version: connect to the callbacks below.
class GtkFindReplacePanel {
public:
    explicit GtkFindReplacePanel(GtkFocusManager *fm);
    virtual ~GtkFindReplacePanel();

    GtkWidget *widget() const { return m_root; }

    void setReplaceEnabled(bool enabled);

    std::string findText() const;
    std::string replaceText() const;
    bool matchCase() const;
    bool matchEntireCell() const;
    bool useRegex() const;

    void setStatusText(const std::string &text);

    void showPanel(bool show);
    bool isPanelVisible() const;

    // --- Callbacks (GTK equivalent of the Qt version's signals) ---
    std::function<void(bool forward)> onFindRequested;
    std::function<void()> onReplaceRequested;
    std::function<void()> onReplaceAllRequested;
    std::function<void()> onPanelClosed;

protected:
    /// Subclasses (GtkScopedFindReplacePanel) can pack extra widgets here.
    GtkWidget *optionsRow() const { return m_optionsRow; }
    GtkFocusManager *focusManager() const { return m_fm; }

private:
    GtkFocusManager *m_fm;
    GtkWidget *m_root;
    GtkWidget *m_txtFind;
    GtkWidget *m_txtReplace;
    GtkWidget *m_lblReplace;
    GtkWidget *m_btnReplace;
    GtkWidget *m_btnReplaceAll;
    GtkWidget *m_chkMatchCase;
    GtkWidget *m_chkMatchEntire;
    GtkWidget *m_chkRegex;
    GtkWidget *m_lblStatus;
    GtkWidget *m_optionsRow;
    GtkWidget *m_btnClose;
};

} // namespace GtkWlPlugin
