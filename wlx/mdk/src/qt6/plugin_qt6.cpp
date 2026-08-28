/*
 * MDK Wayland WLX plugin for Double Commander — Qt6 UI layer.
 *
 * Uses QOpenGLWidget for rendering, following the official MDK Qt
 * integration pattern (QMDKWidget). All libmdk loading/player-control
 * logic lives in MdkEngine (src/core/) — this file only wires that up to
 * Qt widgets and the WLX plugin ABI.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QtWidgets>
#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <cstdio>
#include <cstdint>

#include "wlxplugin.h"
#include "MdkEngine.h"

/* ── QOpenGLWidget-based video widget ────────────────────── */

class MdkVideoWidget : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit MdkVideoWidget(QWidget *parent, const QString &file)
        : QOpenGLWidget(parent)
    {
        m_engine.setFrameReadyCallback([this]() {
            QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
        });
        m_engine.open(file.toUtf8().toStdString());
    }

    ~MdkVideoWidget() override
    {
        makeCurrent();
        m_engine.close();
        doneCurrent();
    }

    MdkEngine &engine() { return m_engine; }

protected:
    void initializeGL() override
    {
        initializeOpenGLFunctions();

        if (!m_engine.isValid()) return;

        memset(&m_glApi, 0, sizeof(m_glApi));
        m_glApi.type = MDK_RenderAPI_OpenGL;
        m_glApi.fbo = -1;
        m_glApi.egl = -1;
        m_glApi.opengl = -1;
        m_glApi.opengles = -1;
        m_glApi.profile = 3;
        m_glApi.opaque = context();

        m_engine.setRenderAPI(reinterpret_cast<mdkRenderAPI *>(&m_glApi));
        m_engine.setBackgroundColor(0.0f, 0.0f, 0.0f, 1.0f);

        connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this]() {
            makeCurrent();
            m_engine.onGlContextDestroyed();
            doneCurrent();
        });
    }

    void resizeGL(int w, int h) override
    {
        auto dpr = devicePixelRatioF();
        m_engine.setVideoSurfaceSize(int(w * dpr), int(h * dpr));
    }

    void paintGL() override
    {
        /* Clear background to black so we don't see previous windows */
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        m_engine.renderVideo();
    }

private:
    MdkEngine m_engine;
    mdkGLRenderAPI m_glApi;
};

/* ── Custom Slider (Jumps to click position) ─────────────── */

class JumpSlider : public QSlider {
    Q_OBJECT
public:
    using QSlider::QSlider;

protected:
    void mousePressEvent(QMouseEvent *ev) override {
        if (ev->button() == Qt::LeftButton && orientation() == Qt::Horizontal) {
            int val = QStyle::sliderValueFromPosition(minimum(), maximum(), ev->pos().x(), width());
            setValue(val);

            /* By setting the value first, the handle moves under the cursor.
             * Then passing the event to QSlider causes it to start dragging
             * instead of doing a page-step. */
            QSlider::mousePressEvent(ev);
        } else {
            QSlider::mousePressEvent(ev);
        }
    }
};

/* ── Container with UI Controls ──────────────────────────── */

class MdkPlayerContainer : public QWidget {
    Q_OBJECT
public:
    explicit MdkPlayerContainer(QWidget *parent, const QString &file)
        : QWidget(parent), m_seeking(false)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        m_videoWidget = new MdkVideoWidget(this, file);
        layout->addWidget(m_videoWidget, 1);

        auto *controlsLayout = new QHBoxLayout();
        controlsLayout->setContentsMargins(8, 4, 8, 4);

        m_playPauseBtn = new QPushButton("Pause", this);
        m_loopBtn = new QPushButton("∞ ⟳", this);
        m_loopBtn->setCheckable(true);
        m_slider = new JumpSlider(Qt::Horizontal, this);
        m_timeLabel = new QLabel("00:00 / 00:00", this);

        controlsLayout->addWidget(m_playPauseBtn);
        controlsLayout->addWidget(m_loopBtn);
        controlsLayout->addWidget(m_slider);
        controlsLayout->addWidget(m_timeLabel);

        auto *controlsWidget = new QWidget(this);
        controlsWidget->setLayout(controlsLayout);
        controlsWidget->setStyleSheet("background-color: #1a1a1a; color: #f0f0f0;"
                                      "QPushButton { background: #333; border: none; padding: 4px 12px; }"
                                      "QPushButton:hover { background: #444; }"
                                      "QPushButton:checked { background: #0078d7; font-weight: bold; }");
        layout->addWidget(controlsWidget, 0);

        connect(m_playPauseBtn, &QPushButton::clicked, this, &MdkPlayerContainer::togglePlayPause);
        connect(m_loopBtn, &QPushButton::toggled, this, &MdkPlayerContainer::toggleLoop);

        connect(m_slider, &QSlider::sliderPressed, this, [this]() { m_seeking = true; });
        connect(m_slider, &QSlider::sliderReleased, this, [this]() {
            m_seeking = false;
            seekTo(m_slider->value());
        });

        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &MdkPlayerContainer::updateControls);
        m_timer->start(250);
    }

private slots:
    void toggleLoop(bool checked)
    {
        m_videoWidget->engine().setLoop(checked);
    }
    void togglePlayPause()
    {
        auto &engine = m_videoWidget->engine();
        if (!engine.isValid()) return;

        if (engine.isPlaying()) {
            engine.pause();
            m_playPauseBtn->setText("Play");
        } else {
            engine.play();
            m_playPauseBtn->setText("Pause");
        }
    }

    void seekTo(int posMs)
    {
        m_videoWidget->engine().seek(posMs);
    }

    QString formatTime(int64_t ms)
    {
        int totalSecs = ms / 1000;
        int mins = totalSecs / 60;
        int secs = totalSecs % 60;
        return QString("%1:%2").arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
    }

    void updateControls()
    {
        auto &engine = m_videoWidget->engine();
        if (!engine.isValid()) return;

        int64_t pos = engine.position();
        int64_t duration = engine.duration();

        m_timeLabel->setText(formatTime(pos) + " / " + formatTime(duration));

        if (!m_seeking && duration > 0) {
            m_slider->setRange(0, duration);
            m_slider->setValue(pos);
        }
    }

private:
    MdkVideoWidget *m_videoWidget;
    QPushButton *m_playPauseBtn;
    QPushButton *m_loopBtn;
    QSlider *m_slider;
    QLabel *m_timeLabel;
    QTimer *m_timer;
    bool m_seeking;
};

#include "plugin_qt6.moc"

/* ── WLX exports ─────────────────────────────────────────── */

HANDLE DCPCALL ListLoad(HANDLE ParentWin, char *FileToLoad, int ShowFlags)
{
    fprintf(stderr, "[mdk_wlx] ListLoad: file=%s\n", FileToLoad);

    auto *parent = reinterpret_cast<QWidget*>(ParentWin);
    auto *w = new MdkPlayerContainer(parent, QString::fromUtf8(FileToLoad));

    auto *existingLayout = parent->layout();
    if (!existingLayout) {
        auto *layout = new QVBoxLayout(parent);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(w);
    } else {
        existingLayout->addWidget(w);
    }

    w->show();

    fprintf(stderr, "[mdk_wlx] ListLoad: widget=%p\n", (void*)w);
    return w;
}

void DCPCALL ListCloseWindow(HWND ListWin)
{
    fprintf(stderr, "[mdk_wlx] ListCloseWindow\n");
    auto *w = reinterpret_cast<MdkPlayerContainer*>(ListWin);
    delete w;
}

int DCPCALL ListSearchDialog(HWND, int)
{
    return LISTPLUGIN_OK;
}

int DCPCALL ListSendCommand(HWND, int, int)
{
    return LISTPLUGIN_ERROR;
}

void DCPCALL ListSetDefaultParams(ListDefaultParamStruct*)
{
}

void DCPCALL ListGetDetectString(char* DetectString, int maxlen)
{
    const char* detect = "EXT=\"MP4\" | EXT=\"MKV\" | EXT=\"AVI\" | EXT=\"WEBM\" | EXT=\"FLV\" | EXT=\"MOV\" | EXT=\"WMV\" | EXT=\"MPEG\" | EXT=\"MPG\" | EXT=\"M4V\" | EXT=\"TS\" | EXT=\"VOB\" | EXT=\"MP3\" | EXT=\"FLAC\" | EXT=\"WAV\" | EXT=\"OGG\" | EXT=\"M4A\" | EXT=\"AAC\" | EXT=\"WMA\"";
    snprintf(DetectString, maxlen, "%s", detect);
}
