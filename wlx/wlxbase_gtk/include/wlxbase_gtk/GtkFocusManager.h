#pragma once

#include <gtk/gtk.h>
#include <functional>
#include <vector>
#include <string>
#include <memory>

namespace GtkWlPlugin {

/// GTK counterpart to QtWlPlugin::FocusManager. Manages a shortcut
/// registry (keyval+modifiers -> handler, gated by whether an input
/// widget currently has focus) and an undo/redo command stack, mirroring
/// the Qt version's API shape closely enough that porting a plugin's
/// shortcut-registration code is close to mechanical.
///
/// Key handling: GTK3 delivers key-press-event to whichever widget has
/// focus, then lets it bubble up through parent containers if
/// unhandled — so shortcuts are caught by connecting "key-press-event" on
/// pluginRoot itself (the same principle as Qt's qApp-level eventFilter,
/// just using GTK's native propagation instead of a global filter).
class GtkFocusManager {
public:
    enum ShortcutContext { WhenNoInput, Always };
    using ShortcutId = int;

    GtkFocusManager(GtkWidget *pluginRoot, GtkWidget *primaryView);
    ~GtkFocusManager();

    GtkFocusManager(const GtkFocusManager &) = delete;
    GtkFocusManager &operator=(const GtkFocusManager &) = delete;

    // --- Input widget tracking ---
    void addInputWidget(GtkWidget *w);
    void removeInputWidget(GtkWidget *w);
    bool isInputWidget(GtkWidget *w) const;
    bool anyInputFocused() const;

    // --- Shortcut registration ---
    ShortcutId registerShortcut(guint keyval, GdkModifierType mods, ShortcutContext ctx,
                                 std::function<bool()> handler);
    void unregisterShortcut(ShortcutId id);

    // --- Undo/redo ---
    struct UndoCommand {
        std::string text;
        std::function<void()> undo;
        std::function<void()> redo;
    };
    /// Pushes the command and immediately calls its redo() (matching
    /// QUndoStack::push semantics: pushing == doing).
    void pushUndo(UndoCommand cmd);
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    void clearUndoStack();
    /// Auto-registers Ctrl+Z (undo), Ctrl+Shift+Z / Ctrl+Y (redo) — call
    /// once after construction if the consumer wants undo shortcuts.
    void enableUndoShortcuts();

    GtkWidget *pluginRoot() const { return m_pluginRoot; }
    GtkWidget *primaryView() const { return m_primaryView; }

private:
    static gboolean onKeyPress(GtkWidget *widget, GdkEventKey *event, gpointer userData);
    bool handleKeyPress(GdkEventKey *event);

    // Live-tested against the real dcgtk process: DC (a Lazarus/LCL app)
    // intercepts most of our registered shortcuts before this class's
    // "key-press-event" handler on pluginRoot ever runs -- Ctrl+S, Ctrl+P,
    // F5, Ctrl+O, Ctrl+Shift+S, Ctrl+Shift+Z all got swallowed by DC's own
    // global keyboard handling, while Ctrl+Z (which DC apparently has no
    // competing global binding for) worked correctly via the normal
    // "key-press-event" bubbling path. That means DC's own key handling
    // runs at a point that pre-empts ordinary GTK widget-level dispatch
    // for any key it cares about, most plausibly a
    // gtk_key_snooper_install() callback (Lazarus/LCL is a known user of
    // this GTK3 API for its own global accelerator table) -- key snoopers
    // are checked before GTK ever delivers the event to a focus widget at
    // all, so no amount of signal-handler placement on our side of the
    // widget tree can out-race it.
    //
    // Fix: install our own key snooper too. GTK's snooper list is a
    // GSList built via prepend, so the most-recently-installed snooper is
    // checked first -- ours, installed when a plugin panel is first
    // constructed (necessarily after DC's own snooper, installed at
    // DC startup), runs BEFORE DC's, giving us first refusal on any key
    // while our own plugin panel is the one that's focused. The original
    // "key-press-event" connection is left in place as a harmless
    // fallback for whatever this doesn't need to handle.
    static gint snoopKeyPress(GtkWidget *grabWidget, GdkEventKey *event, gpointer);
    static std::vector<GtkFocusManager *> s_instances;

    GtkWidget *m_pluginRoot;
    GtkWidget *m_primaryView;
    std::vector<GtkWidget *> m_inputWidgets;

    struct RegisteredShortcut {
        ShortcutId id;
        guint keyval;
        GdkModifierType mods;
        ShortcutContext ctx;
        std::function<bool()> handler;
    };
    std::vector<RegisteredShortcut> m_shortcuts;
    ShortcutId m_nextShortcutId = 1;

    std::vector<UndoCommand> m_undoStack;
    size_t m_undoIndex = 0; // number of commands currently "done"; redo stack is [m_undoIndex, size())
};

} // namespace GtkWlPlugin
