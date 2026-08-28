#include "FocusManager.h"

#include <cstdio>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QFocusEvent>
#include <QChildEvent>
#include <QTimer>
#include <QUndoStack>

namespace QtWlPlugin {

FocusManager::FocusManager(QWidget *pluginRoot, QWidget *primaryView, QObject *parent)
    : QObject(parent)
    , m_pluginRoot(pluginRoot)
    , m_primaryView(primaryView)
    , m_isActive(false)
    , m_undoStack(nullptr)
    , m_nextShortcutId(1)
{
    installFocusGuard();

    // Detect focus entering/leaving the plugin hierarchy
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *old, QWidget *now) {
        bool oldInside = old && (old == m_pluginRoot || m_pluginRoot->isAncestorOf(old));
        bool nowInside = now && (now == m_pluginRoot || m_pluginRoot->isAncestorOf(now));

        if (m_isActive) {
            if (oldInside && !nowInside) {
                setActive(false);
            }
        } else {
            if (nowInside && !oldInside) {
                // Focus entering while inactive — bounce it back to the host
                if (old) {
                    QPointer<QWidget> pOld(old);
                    QTimer::singleShot(0, this, [this, pOld]() {
                        if (pOld) {
                            QWidget *currentFocus = QApplication::focusWidget();
                            if (currentFocus && (currentFocus == m_pluginRoot ||
                                                 m_pluginRoot->isAncestorOf(currentFocus))) {
                                pOld->setFocus(Qt::OtherFocusReason);
                            }
                        }
                    });
                } else {
                    QTimer::singleShot(0, this, [this]() {
                        QWidget *currentFocus = QApplication::focusWidget();
                        if (currentFocus && (currentFocus == m_pluginRoot ||
                                             m_pluginRoot->isAncestorOf(currentFocus))) {
                            restoreFocusToDC();
                        }
                    });
                }
            }
        }
    });
}

FocusManager::~FocusManager()
{
    if (qApp)
        qApp->removeEventFilter(this);
}

// --- Activation ---

bool FocusManager::isActive() const { return m_isActive; }

void FocusManager::setActive(bool active)
{
    if (m_isActive == active)
        return;

    m_isActive = active;
    if (active)
        m_lastInternalActivationTimer.restart();

    if (!active) {
        m_activeInput = nullptr;
        m_pluginRoot->clearFocus();
        if (m_pluginRoot->parentWidget())
            m_pluginRoot->parentWidget()->setFocus(Qt::OtherFocusReason);
        emit deactivated();
    } else {
        emit activated();
    }
}

void FocusManager::setActiveFromHost(bool active)
{
    static const qint64 kGraceMs = 300;
    if (!active && m_lastInternalActivationTimer.isValid() && m_lastInternalActivationTimer.elapsed() < kGraceMs) {
        return; // spurious deactivation immediately following our own click-based activation
    }
    setActive(active);
}

// --- Input widget tracking ---

void FocusManager::addInputWidget(QWidget *w)
{
    if (w)
        m_extraInputWidgets.insert(w);
}

void FocusManager::removeInputWidget(QWidget *w)
{
    m_extraInputWidgets.remove(w);
}

bool FocusManager::isInputWidget(QWidget *w) const
{
    if (!w)
        return false;
    if (m_extraInputWidgets.contains(w))
        return true;
    // A descendant of primaryView (but not primaryView itself) is an input widget
    // (e.g. a cell editor spawned inside a QTableWidget)
    return w != m_primaryView && m_primaryView->isAncestorOf(w);
}

QWidget *FocusManager::activeInput() const { return m_activeInput; }

// --- Focus proxy ---

void FocusManager::setFocusProxy(QWidget *proxy)
{
    m_pluginRoot->setFocusProxy(proxy);
}

void FocusManager::resetFocusProxy()
{
    m_pluginRoot->setFocusProxy(m_primaryView);
}

// --- Shortcut registration ---

FocusManager::ShortcutId FocusManager::registerShortcut(
    const QKeySequence &keys, ShortcutContext ctx, std::function<bool()> handler)
{
    ShortcutId id = m_nextShortcutId++;
    m_shortcuts.append({id, keys, ctx, std::move(handler)});
    return id;
}

void FocusManager::unregisterShortcut(ShortcutId id)
{
    m_shortcuts.erase(
        std::remove_if(m_shortcuts.begin(), m_shortcuts.end(),
                        [id](const RegisteredShortcut &s) { return s.id == id; }),
        m_shortcuts.end());
}

// --- Optional undo/redo ---

void FocusManager::setUndoStack(QUndoStack *stack)
{
    // Unregister previous undo shortcuts
    for (ShortcutId id : m_undoShortcutIds)
        unregisterShortcut(id);
    m_undoShortcutIds.clear();

    m_undoStack = stack;

    if (stack) {
        // Ctrl+Z → undo
        m_undoShortcutIds.append(registerShortcut(
            QKeySequence(Qt::CTRL | Qt::Key_Z), WhenNoInput,
            [stack]() {
                if (stack->canUndo()) { stack->undo(); return true; }
                return false;
            }));

        // Ctrl+Shift+Z → redo
        m_undoShortcutIds.append(registerShortcut(
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), WhenNoInput,
            [stack]() {
                if (stack->canRedo()) { stack->redo(); return true; }
                return false;
            }));

        // Ctrl+Y → redo
        m_undoShortcutIds.append(registerShortcut(
            QKeySequence(Qt::CTRL | Qt::Key_Y), WhenNoInput,
            [stack]() {
                if (stack->canRedo()) { stack->redo(); return true; }
                return false;
            }));
    }
}

QUndoStack *FocusManager::undoStack() const { return m_undoStack; }

// --- Saved focus ---

void FocusManager::saveFocusWidget(QWidget *w) { m_savedFocusWidget = w; }

// --- Access ---

QWidget *FocusManager::pluginRoot() const { return m_pluginRoot; }
QWidget *FocusManager::primaryView() const { return m_primaryView; }

// --- Focus restoration ---

void FocusManager::restoreViewFocus()
{
    m_primaryView->setFocus(Qt::OtherFocusReason);
}

void FocusManager::restoreFocusToDC()
{
    if (m_savedFocusWidget) {
        m_savedFocusWidget->setFocus(Qt::OtherFocusReason);
    } else {
        if (QWidget *fw = QApplication::focusWidget()) {
            if (fw == m_pluginRoot || fw->isAncestorOf(m_pluginRoot) ||
                m_pluginRoot->isAncestorOf(fw))
                fw->clearFocus();
        }
    }
}

void FocusManager::installFocusGuard()
{
    if (qApp)
        qApp->installEventFilter(this);
    m_pluginRoot->setFocusProxy(m_primaryView);
}

// --- Event filter (the critical focus/shortcut engine) ---

bool FocusManager::eventFilter(QObject *obj, QEvent *event)
{
    QWidget *w = qobject_cast<QWidget *>(obj);

    // --- Click detection ---
    // Originally used mapToGlobal()-based geometry to decide "inside vs
    // outside the plugin." Switched to widget ancestry (is the event's
    // target widget a descendant of the plugin root?) instead, which needs
    // no absolute screen coordinates -- a pure QObject-tree relationship,
    // unaffected by windowing-system position availability. This turned out
    // not to be the cause of the "must click twice before Ctrl+C works"
    // symptom (live logging showed the geometry check was already correct
    // on the first click), but is still strictly more robust than
    // mapToGlobal()-based geometry, which Wayland doesn't guarantee an app
    // can query reliably in general. The actual cause of that symptom was
    // the host sending an explicit deactivate call immediately after this
    // activation -- see setActiveFromHost()'s grace-period guard.
    if (event->type() == QEvent::MouseButtonPress) {
        bool clickedInsidePlugin = w && m_pluginRoot && (w == m_pluginRoot || m_pluginRoot->isAncestorOf(w));
        printf("[OfficeView] FocusManager MouseButtonPress: obj=%s w=%s clickedInsidePlugin=%d isActive=%d pluginRoot=%p\n",
               obj ? obj->metaObject()->className() : "null",
               w ? w->metaObject()->className() : "null(not-a-QWidget)",
               clickedInsidePlugin, m_isActive, (void*)m_pluginRoot);
        fflush(stdout);

        if (m_isActive && !clickedInsidePlugin) {
            // Click outside plugin — deactivate
            setActive(false);
            return false;
        } else if (!m_isActive && clickedInsidePlugin) {
            // Click inside plugin — activate
            m_isActive = true;
            m_lastInternalActivationTimer.restart();
            emit activated();
            if (w && (w->focusPolicy() & Qt::ClickFocus)) {
                w->setFocus(Qt::MouseFocusReason);
            } else {
                m_primaryView->setFocus(Qt::MouseFocusReason);
            }
        }
    }

    // --- FocusIn tracking ---
    if (event->type() == QEvent::FocusIn) {
        if (w && (w == m_pluginRoot || m_pluginRoot->isAncestorOf(w))) {
            auto *fe = static_cast<QFocusEvent *>(event);
            if (!m_isActive && fe->reason() == Qt::OtherFocusReason) {
                // Programmatic focus entry while inactive — don't activate
                return false;
            }
            if (!m_isActive) {
                m_isActive = true;
                m_lastInternalActivationTimer.restart();
                emit activated();
            }
            if (isInputWidget(w)) {
                m_activeInput = w;
                emit inputWidgetEntered(w);
            }
        }
    }

    // --- KeyPress: shortcut dispatch ---
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_C && (ke->modifiers() & Qt::ControlModifier) && !m_isActive) {
            printf("[OfficeView] FocusManager: Ctrl+C ignored, plugin not active (pluginRoot=%p)\n", (void*)m_pluginRoot);
            fflush(stdout);
        }
    }
    if (event->type() == QEvent::KeyPress && m_isActive) {
        auto *ke = static_cast<QKeyEvent *>(event);
        QKeySequence pressed(ke->modifiers() | ke->key());

        for (const auto &shortcut : m_shortcuts) {
            if (shortcut.keys.count() != 1)
                continue;
            if (pressed[0] != shortcut.keys[0])
                continue;

            // Check context
            if (shortcut.ctx == WhenNoInput && m_activeInput)
                continue;

            if (shortcut.handler && shortcut.handler())
                return true;
        }
    }

    // --- ChildAdded: NoFocus enforcement ---
    if (event->type() == QEvent::ChildAdded) {
        if (w && (w == m_pluginRoot || m_pluginRoot->isAncestorOf(w))) {
            auto *ce = static_cast<QChildEvent *>(event);
            if (auto *childWidget = qobject_cast<QWidget *>(ce->child())) {
                if (!isInputWidget(childWidget))
                    childWidget->setFocusPolicy(Qt::NoFocus);
            }
        }
    }

    return QObject::eventFilter(obj, event);
}

} // namespace QtWlPlugin
