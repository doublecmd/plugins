#pragma once

#include <QObject>
#include <QWidget>
#include <QPointer>
#include <QKeySequence>
#include <QSet>
#include <QVector>
#include <functional>

class QUndoStack;

namespace QtWlPlugin {

/// Core focus management framework for Qt WLX plugins embedded in a host application.
///
/// Manages the entire focus lifecycle: activation/deactivation on click,
/// focus bounce prevention, input widget tracking, and a shortcut registry
/// that replaces the hardcoded eventFilter if-chains found in existing plugins.
class FocusManager : public QObject {
    Q_OBJECT
public:
    explicit FocusManager(QWidget *pluginRoot, QWidget *primaryView, QObject *parent = nullptr);
    ~FocusManager() override;

    // --- Activation ---
    bool isActive() const;
    void setActive(bool active);

    // --- Input widget tracking ---
    void addInputWidget(QWidget *w);
    void removeInputWidget(QWidget *w);
    bool isInputWidget(QWidget *w) const;
    QWidget *activeInput() const;

    // --- Focus proxy management ---
    void setFocusProxy(QWidget *proxy);
    void resetFocusProxy();

    // --- Shortcut registration ---
    enum ShortcutContext { WhenNoInput, Always };
    using ShortcutId = int;
    ShortcutId registerShortcut(const QKeySequence &keys, ShortcutContext ctx,
                                std::function<bool()> handler);
    void unregisterShortcut(ShortcutId id);

    // --- Optional undo/redo ---
    /// When set, auto-registers Ctrl+Z (undo), Ctrl+Shift+Z (redo), Ctrl+Y (redo).
    /// The consumer retains full access to the stack for custom manipulation.
    void setUndoStack(QUndoStack *stack);
    QUndoStack *undoStack() const;

    // --- Saved focus (for restoring to host app) ---
    void saveFocusWidget(QWidget *w);

    // --- Access ---
    QWidget *pluginRoot() const;
    QWidget *primaryView() const;

    // --- Focus restoration ---
    void restoreViewFocus();
    static void expectReloadFocus();

signals:
    void activated();
    void deactivated();
    void inputWidgetEntered(QWidget *w);
    void inputWidgetExited();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void installFocusGuard();
    void restoreFocusToDC();

    QPointer<QWidget> m_pluginRoot;
    QPointer<QWidget> m_primaryView;
    bool m_isActive;
    QPointer<QWidget> m_savedFocusWidget;
    QPointer<QWidget> m_activeInput;
    QSet<QWidget *> m_extraInputWidgets;

    QUndoStack *m_undoStack;
    QVector<ShortcutId> m_undoShortcutIds;

    struct RegisteredShortcut {
        ShortcutId id;
        QKeySequence keys;
        ShortcutContext ctx;
        std::function<bool()> handler;
    };
    QVector<RegisteredShortcut> m_shortcuts;
    ShortcutId m_nextShortcutId;
    static bool s_reloadFocusTarget;
};

} // namespace QtWlPlugin
