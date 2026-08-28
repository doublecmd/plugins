#include "mpvwidget.h"
#include <QOpenGLContext>
#include <QWindow>
#include <QDebug>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QApplication>
#include <QTimer>
#include <QChildEvent>

MpvWidget::MpvWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_glReady(false)
{
    setWindowFlags(Qt::Widget);
    // LAYER 1: NEVER hold Qt focus. On Wayland, calling setFocus() on a
    // QOpenGLWidget creates a compositor-level subsurface focus lock that
    // cannot be released programmatically. Instead we intercept keyboard
    // events at the application level and forward them to mpv.
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);

    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installEventFilter(this);
    }

    m_engine = std::make_unique<MpvEngine>();
    if (!m_engine->isValid()) {
        qCritical() << "mpv_wayland: MpvEngine failed to initialize";
    }
}

MpvWidget::~MpvWidget()
{
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeEventFilter(this);
    }

    makeCurrent();
    m_engine.reset(); // MpvEngine's destructor frees the render context while GL is current
    doneCurrent();
}

void *MpvWidget::get_proc_address(void *ctx, const char *name)
{
    Q_UNUSED(ctx);
    QOpenGLContext *glctx = QOpenGLContext::currentContext();
    if (!glctx) return nullptr;
    return reinterpret_cast<void*>(glctx->getProcAddress(name));
}

void MpvWidget::initializeGL()
{
    if (!m_engine->isValid()) {
        qCritical() << "mpv_wayland: initializeGL called but engine is not valid";
        return;
    }

    if (!m_engine->initRenderContext(get_proc_address, nullptr)) {
        qCritical() << "mpv_wayland: initRenderContext failed";
        return;
    }

    m_engine->setUpdateCallback([this]() {
        // Thread-safe: callback comes from mpv's background thread, post to Qt main loop
        QMetaObject::invokeMethod(this, "onMpvUpdate", Qt::QueuedConnection);
    });
    m_glReady = true;

    if (!m_pendingFile.isEmpty()) {
        QMetaObject::invokeMethod(this, "doLoadFile", Qt::QueuedConnection);
    }
}

void MpvWidget::paintGL()
{
    if (!m_engine->renderContextReady()) return;

    // Use physical pixels for Wayland HiDPI scaling
    qreal dpr = devicePixelRatioF();
    int w = static_cast<int>(width() * dpr);
    int h = static_cast<int>(height() * dpr);
    int fbo = defaultFramebufferObject();

    m_engine->render(fbo, w, h);
}

void MpvWidget::resizeGL(int w, int h)
{
    Q_UNUSED(w);
    Q_UNUSED(h);
}

bool MpvWidget::loadFile(const QString &fileName)
{
    // LAYER 3: Save DC's currently focused widget so we can return to it
    m_savedFocusWidget = QApplication::focusWidget();
    installFocusGuard();

    if (!m_topLevelFilterInstalled) {
        if (QWidget *tlw = window()) {
            tlw->installEventFilter(this);
            m_topLevelFilterInstalled = true;
        }
    }

    if (!m_engine->isValid()) return false;

    m_pendingFile = fileName;

    if (m_glReady) {
        doLoadFile();
        QTimer::singleShot(100, this, [this]() { restoreFocusToDC(); });
        return true;
    }

    return true;
}

void MpvWidget::doLoadFile()
{
    if (!m_engine->isValid() || m_pendingFile.isEmpty()) return;

    m_engine->loadFile(m_pendingFile.toStdString());

    QTimer::singleShot(100, this, [this]() { restoreFocusToDC(); });
}

void MpvWidget::installFocusGuard()
{
    const auto children = findChildren<QWidget*>();
    for (QWidget *child : children) {
        child->setFocusPolicy(Qt::NoFocus);
        child->installEventFilter(this);
    }
}

void MpvWidget::restoreFocusToDC()
{
    if (m_savedFocusWidget) {
        m_savedFocusWidget->setFocus(Qt::OtherFocusReason);
    }
}

void MpvWidget::closeFile()
{
    m_engine->closeFile();
    m_pendingFile.clear();
}

void MpvWidget::onMpvUpdate()
{
    update();
}

void MpvWidget::mouseMoveEvent(QMouseEvent *event)
{
    int x = event->x() * devicePixelRatioF();
    int y = event->y() * devicePixelRatioF();
    m_engine->commandString(QString("mouse %1 %2").arg(x).arg(y).toStdString());
}

void MpvWidget::mousePressEvent(QMouseEvent *event)
{
    // Activate input mode. We do NOT call setFocus() — that would create
    // a Wayland subsurface focus lock. Instead, the application-level
    // event filter in eventFilter() forwards keyboard events to mpv.
    m_isActive = true;

    if (event->button() == Qt::LeftButton) {
        m_engine->commandString("keydown MOUSE_BTN0");
    } else if (event->button() == Qt::RightButton) {
        m_engine->commandString("keydown MOUSE_BTN2");
    }
}

void MpvWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_engine->commandString("keyup MOUSE_BTN0");
    } else if (event->button() == Qt::RightButton) {
        m_engine->commandString("keyup MOUSE_BTN2");
    }
}

void MpvWidget::wheelEvent(QWheelEvent *event)
{
    if (event->angleDelta().y() > 0) {
        m_engine->commandString("keypress WHEEL_UP");
    } else {
        m_engine->commandString("keypress WHEEL_DOWN");
    }
}

void MpvWidget::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    // Move mouse out of frame to hide OSC
    m_engine->commandString("mouse -100 -100");
}

QString MpvWidget::mapQtKeyToMpvKey(QKeyEvent *event)
{
    switch(event->key()) {
        case Qt::Key_Space: return "SPACE";
        case Qt::Key_Left: return "LEFT";
        case Qt::Key_Right: return "RIGHT";
        case Qt::Key_Up: return "UP";
        case Qt::Key_Down: return "DOWN";
        case Qt::Key_Enter:
        case Qt::Key_Return: return "ENTER";
        case Qt::Key_Escape: return "ESC";
        case Qt::Key_Backspace: return "BS";
        case Qt::Key_PageUp: return "PGUP";
        case Qt::Key_PageDown: return "PGDWN";
        case Qt::Key_Home: return "HOME";
        case Qt::Key_End: return "END";
        case Qt::Key_Tab: return "TAB";
    }
    QString text = event->text();
    if (!text.isEmpty()) {
        return text;
    }
    return "";
}

void MpvWidget::keyPressEvent(QKeyEvent *event)
{
    // Not used — keyboard handling is done in eventFilter() to avoid
    // needing Qt focus (which causes Wayland subsurface lock).
    Q_UNUSED(event);
}

void MpvWidget::keyReleaseEvent(QKeyEvent *event)
{
    Q_UNUSED(event);
}

bool MpvWidget::eventFilter(QObject *obj, QEvent *event)
{
    // ── Outside-click detector ──────────────────────────────────────────
    if (event->type() == QEvent::MouseButtonPress && m_isActive) {
        auto *me = static_cast<QMouseEvent*>(event);
        QPoint localPos = mapFromGlobal(me->globalPosition().toPoint());
        if (!rect().contains(localPos)) {
            m_isActive = false;
            return false;
        }
    }

    // ── Keyboard forwarding ─────────────────────────────────────────────
    if (event->type() == QEvent::KeyPress && m_isActive) {
        auto *ke = static_cast<QKeyEvent*>(event);

        if (ke->key() == Qt::Key_Escape) {
            m_isActive = false;
            return true;
        }

        if (ke->key() == Qt::Key_Q && (ke->modifiers() & Qt::ControlModifier)) {
            m_isActive = false;
            restoreFocusToDC();
            if (QWidget *top = window()) {
                QKeyEvent *newKe = new QKeyEvent(QEvent::KeyPress, Qt::Key_Q, Qt::ControlModifier);
                QCoreApplication::postEvent(top, newKe);
            }
            return true;
        }
        QString mpvKey = mapQtKeyToMpvKey(ke);
        if (!mpvKey.isEmpty()) {
            m_engine->commandString(QString("keypress %1").arg(mpvKey).toStdString());
            return true;
        }
    }

    // ── FocusIn interceptor ──────────────────────────────────────────────
    auto *w = qobject_cast<QWidget*>(obj);
    if (event->type() == QEvent::FocusIn && w && (w == this || this->isAncestorOf(w))) {
        QTimer::singleShot(0, this, [this]() { restoreFocusToDC(); });
        return false;
    }

    // ── ChildAdded: guard dynamically-created children ──────────────────
    if (event->type() == QEvent::ChildAdded) {
        auto *ce = static_cast<QChildEvent*>(event);
        if (auto *childWidget = qobject_cast<QWidget*>(ce->child())) {
            childWidget->setFocusPolicy(Qt::NoFocus);
            childWidget->installEventFilter(this);
        }
    }

    return QOpenGLWidget::eventFilter(obj, event);
}
