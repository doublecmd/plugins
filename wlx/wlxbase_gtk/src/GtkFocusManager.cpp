#include "wlxbase_gtk/GtkFocusManager.h"

#include <algorithm>

namespace GtkWlPlugin {

std::vector<GtkFocusManager *> GtkFocusManager::s_instances;

GtkFocusManager::GtkFocusManager(GtkWidget *pluginRoot, GtkWidget *primaryView)
    : m_pluginRoot(pluginRoot)
    , m_primaryView(primaryView)
{
    gtk_widget_add_events(m_pluginRoot, GDK_KEY_PRESS_MASK);
    g_signal_connect(m_pluginRoot, "key-press-event", G_CALLBACK(onKeyPress), this);

    static guint snooperId = 0;
    if (snooperId == 0)
        snooperId = gtk_key_snooper_install(snoopKeyPress, nullptr);
    s_instances.push_back(this);
}

GtkFocusManager::~GtkFocusManager()
{
    s_instances.erase(std::remove(s_instances.begin(), s_instances.end(), this), s_instances.end());
}

gint GtkFocusManager::snoopKeyPress(GtkWidget *, GdkEventKey *event, gpointer)
{
    // The `grab_widget` parameter is ONLY non-NULL while an explicit GTK
    // modal grab (gtk_grab_add()) is active -- nothing here ever
    // establishes one, so it was always NULL and the original version of
    // this function (bailing out via `!grabWidget`) discarded every single
    // key event unconditionally, silently doing nothing. Confirmed live:
    // shortcuts were still 100% intercepted by DC after that "fix" shipped.
    // Real GTK keyboard focus (gtk_window_get_focus() on the toplevel) is
    // the correct thing to scope against instead.
    if (event->type != GDK_KEY_PRESS) return 0;
    for (GtkFocusManager *instance : s_instances) {
        GtkWidget *toplevel = gtk_widget_get_toplevel(instance->m_pluginRoot);
        if (!GTK_IS_WINDOW(toplevel)) continue;
        GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(toplevel));
        if (!focus) continue;
        if (focus != instance->m_pluginRoot && !gtk_widget_is_ancestor(focus, instance->m_pluginRoot))
            continue;
        if (instance->handleKeyPress(event)) return 1; // stop: fully handled, DC never sees it
    }
    return 0; // not ours -- let it fall through to DC's own handling
}

void GtkFocusManager::addInputWidget(GtkWidget *w)
{
    if (std::find(m_inputWidgets.begin(), m_inputWidgets.end(), w) == m_inputWidgets.end())
        m_inputWidgets.push_back(w);
}

void GtkFocusManager::removeInputWidget(GtkWidget *w)
{
    m_inputWidgets.erase(std::remove(m_inputWidgets.begin(), m_inputWidgets.end(), w), m_inputWidgets.end());
}

bool GtkFocusManager::isInputWidget(GtkWidget *w) const
{
    return std::find(m_inputWidgets.begin(), m_inputWidgets.end(), w) != m_inputWidgets.end();
}

bool GtkFocusManager::anyInputFocused() const
{
    for (GtkWidget *w : m_inputWidgets)
        if (gtk_widget_has_focus(w)) return true;
    return false;
}

GtkFocusManager::ShortcutId GtkFocusManager::registerShortcut(
    guint keyval, GdkModifierType mods, ShortcutContext ctx, std::function<bool()> handler)
{
    ShortcutId id = m_nextShortcutId++;
    m_shortcuts.push_back({id, keyval, mods, ctx, std::move(handler)});
    return id;
}

void GtkFocusManager::unregisterShortcut(ShortcutId id)
{
    m_shortcuts.erase(std::remove_if(m_shortcuts.begin(), m_shortcuts.end(),
                                      [id](const RegisteredShortcut &s) { return s.id == id; }),
                       m_shortcuts.end());
}

gboolean GtkFocusManager::onKeyPress(GtkWidget *, GdkEventKey *event, gpointer userData)
{
    auto *self = static_cast<GtkFocusManager *>(userData);
    return self->handleKeyPress(event) ? TRUE : FALSE;
}

bool GtkFocusManager::handleKeyPress(GdkEventKey *event)
{
    // Normalize to the "consumed" modifier set (ignore Lock/NumLock etc.)
    GdkModifierType relevant = static_cast<GdkModifierType>(
        event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK));

    bool inputFocused = anyInputFocused();

    // GDK delivers the SHIFTED keyval when Shift is held -- e.g. Ctrl+Shift+S
    // arrives as keyval 'S' (83), not 's' (115) plus a Shift bit on top of
    // it. Every call site here registers shortcuts with the plain lowercase
    // GDK_KEY_<letter> constant regardless of whether Shift is part of the
    // combo, so a raw keyval comparison silently never matched any
    // Ctrl+Shift+<letter> shortcut at all (confirmed live via a diagnostic
    // log: handleKeyPress ran and evaluated the shortcut list, but s.keyval
    // != event->keyval failed every time for Ctrl+Shift+S and
    // Ctrl+Shift+Z). Comparing the lowercased form of both sides fixes
    // every such registration in one place instead of needing each caller
    // to know to use the uppercase constant for shift combos.
    guint incomingKeyval = gdk_keyval_to_lower(event->keyval);

    for (const auto &s : m_shortcuts) {
        if (s.ctx == WhenNoInput && inputFocused) continue;
        if (gdk_keyval_to_lower(s.keyval) != incomingKeyval) continue;
        if (s.mods != relevant) continue;
        if (s.handler && s.handler())
            return true;
    }
    return false;
}

void GtkFocusManager::pushUndo(UndoCommand cmd)
{
    // Discard any redo history beyond the current point (matches
    // QUndoStack::push semantics).
    if (m_undoIndex < m_undoStack.size())
        m_undoStack.erase(m_undoStack.begin() + m_undoIndex, m_undoStack.end());

    if (cmd.redo) cmd.redo();
    m_undoStack.push_back(std::move(cmd));
    m_undoIndex = m_undoStack.size();
}

void GtkFocusManager::undo()
{
    if (m_undoIndex == 0) return;
    --m_undoIndex;
    if (m_undoStack[m_undoIndex].undo)
        m_undoStack[m_undoIndex].undo();
}

void GtkFocusManager::redo()
{
    if (m_undoIndex >= m_undoStack.size()) return;
    if (m_undoStack[m_undoIndex].redo)
        m_undoStack[m_undoIndex].redo();
    ++m_undoIndex;
}

bool GtkFocusManager::canUndo() const { return m_undoIndex > 0; }
bool GtkFocusManager::canRedo() const { return m_undoIndex < m_undoStack.size(); }

void GtkFocusManager::clearUndoStack()
{
    m_undoStack.clear();
    m_undoIndex = 0;
}

void GtkFocusManager::enableUndoShortcuts()
{
    registerShortcut(GDK_KEY_z, GDK_CONTROL_MASK, Always, [this]() { undo(); return true; });
    registerShortcut(GDK_KEY_z, static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK), Always,
                      [this]() { redo(); return true; });
    registerShortcut(GDK_KEY_y, GDK_CONTROL_MASK, Always, [this]() { redo(); return true; });
}

} // namespace GtkWlPlugin
