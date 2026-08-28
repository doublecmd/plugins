/*
 * MDK video WLX plugin for Double Commander — GTK3 UI layer.
 *
 * Uses GtkGLArea for rendering, mirroring the Qt6 QOpenGLWidget approach in
 * src/qt6/plugin_qt6.cpp. All libmdk loading/player-control logic lives in
 * MdkEngine (src/core/) — shared verbatim with the Qt6 build — this file
 * only wires that up to GTK widgets and the WLX plugin ABI.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gtk/gtk.h>
#include <epoxy/gl.h>
#include <cstdio>
#include <cstring>
#include <memory>

#include "wlxplugin.h"
#include "MdkEngine.h"

namespace {

/// Everything GTK needs to drive one open video: the engine, the GL area,
/// the transport controls, and the periodic position-update timer. Held
/// alive for the lifetime of the plugin window (see ListLoad/ListCloseWindow).
struct MdkGtkPlayer {
    MdkEngine engine;
    GtkWidget *root = nullptr;       // returned as the HWND to Double Commander
    GtkWidget *glArea = nullptr;
    GtkWidget *playPauseBtn = nullptr;
    GtkWidget *loopBtn = nullptr;
    GtkWidget *scale = nullptr;
    GtkWidget *timeLabel = nullptr;
    mdkGLRenderAPI glApi{};
    guint timerId = 0;
    bool seeking = false;

    // Guards the g_idle_add-marshaled "frame ready -> redraw" callback
    // against firing after the widget has been torn down (GTK has no
    // built-in equivalent of Qt's automatic signal/slot disconnection on
    // object destruction).
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

std::string formatTime(int64_t ms)
{
    int totalSecs = static_cast<int>(ms / 1000);
    int mins = totalSecs / 60;
    int secs = totalSecs % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
    return buf;
}

gboolean onGlRealize(GtkGLArea *area, gpointer userData)
{
    auto *p = static_cast<MdkGtkPlayer *>(userData);
    gtk_gl_area_make_current(area);
    if (gtk_gl_area_get_error(area) != nullptr)
        return FALSE;

    if (!p->engine.isValid())
        return FALSE;

    std::memset(&p->glApi, 0, sizeof(p->glApi));
    p->glApi.type = MDK_RenderAPI_OpenGL;
    p->glApi.fbo = -1;
    p->glApi.egl = -1;
    p->glApi.opengl = -1;
    p->glApi.opengles = -1;
    p->glApi.profile = 3;
    p->glApi.opaque = area;

    p->engine.setRenderAPI(reinterpret_cast<mdkRenderAPI *>(&p->glApi));
    p->engine.setBackgroundColor(0.0f, 0.0f, 0.0f, 1.0f);
    return FALSE;
}

void onGlUnrealize(GtkGLArea *area, gpointer userData)
{
    auto *p = static_cast<MdkGtkPlayer *>(userData);
    gtk_gl_area_make_current(area);
    p->engine.onGlContextDestroyed();
}

void onGlResize(GtkGLArea *area, gint width, gint height, gpointer userData)
{
    auto *p = static_cast<MdkGtkPlayer *>(userData);
    gint scaleFactor = gtk_widget_get_scale_factor(GTK_WIDGET(area));
    p->engine.setVideoSurfaceSize(width * scaleFactor, height * scaleFactor);
}

gboolean onGlRender(GtkGLArea *, GdkGLContext *, gpointer userData)
{
    auto *p = static_cast<MdkGtkPlayer *>(userData);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    p->engine.renderVideo();
    return TRUE;
}

void togglePlayPause(GtkButton *, gpointer userData)
{
    auto *p = static_cast<MdkGtkPlayer *>(userData);
    if (!p->engine.isValid()) return;

    if (p->engine.isPlaying()) {
        p->engine.pause();
        gtk_button_set_label(GTK_BUTTON(p->playPauseBtn), "Play");
    } else {
        p->engine.play();
        gtk_button_set_label(GTK_BUTTON(p->playPauseBtn), "Pause");
    }
}

void toggleLoop(GtkToggleButton *btn, gpointer userData)
{
    auto *p = static_cast<MdkGtkPlayer *>(userData);
    p->engine.setLoop(gtk_toggle_button_get_active(btn));
}

gboolean onSeekButtonPress(GtkWidget *, GdkEvent *, gpointer userData)
{
    static_cast<MdkGtkPlayer *>(userData)->seeking = true;
    return FALSE; // let GtkScale still handle the click/drag itself
}

gboolean onSeekButtonRelease(GtkWidget *widget, GdkEvent *, gpointer userData)
{
    auto *p = static_cast<MdkGtkPlayer *>(userData);
    p->seeking = false;
    double val = gtk_range_get_value(GTK_RANGE(widget));
    p->engine.seek(static_cast<int64_t>(val));
    return FALSE;
}

gboolean onUpdateTick(gpointer userData)
{
    auto *p = static_cast<MdkGtkPlayer *>(userData);
    if (!p->engine.isValid())
        return G_SOURCE_CONTINUE;

    int64_t pos = p->engine.position();
    int64_t duration = p->engine.duration();

    std::string label = formatTime(pos) + " / " + formatTime(duration);
    gtk_label_set_text(GTK_LABEL(p->timeLabel), label.c_str());

    if (!p->seeking && duration > 0) {
        gtk_range_set_range(GTK_RANGE(p->scale), 0, static_cast<double>(duration));
        gtk_range_set_value(GTK_RANGE(p->scale), static_cast<double>(pos));
    }
    return G_SOURCE_CONTINUE;
}

void destroyPlayer(gpointer data)
{
    auto *p = static_cast<MdkGtkPlayer *>(data);
    *p->alive = false;
    if (p->timerId)
        g_source_remove(p->timerId);
    delete p;
}

} // namespace

extern "C" {

HWND DCPCALL ListLoad(HWND ParentWin, char *FileToLoad, int ShowFlags)
{
    fprintf(stderr, "[mdk_wlx_gtk3] ListLoad: file=%s\n", FileToLoad);

    GtkWidget *parent = GTK_WIDGET(ParentWin);
    auto *p = new MdkGtkPlayer();

    p->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(parent), p->root);

    // Frame-ready -> redraw, marshaled onto the GLib main loop the same way
    // the Qt build uses QMetaObject::invokeMethod(..., Qt::QueuedConnection)
    // — MDK calls this back from one of its own internal threads.
    GtkWidget *glAreaRaw = gtk_gl_area_new();
    p->glArea = glAreaRaw;
    std::weak_ptr<bool> aliveWeak = p->alive;
    p->engine.setFrameReadyCallback([glAreaRaw, aliveWeak]() {
        auto *ctx = new std::pair<GtkWidget *, std::weak_ptr<bool>>(glAreaRaw, aliveWeak);
        g_idle_add(+[](gpointer data) -> gboolean {
            auto *ctx = static_cast<std::pair<GtkWidget *, std::weak_ptr<bool>> *>(data);
            if (auto locked = ctx->second.lock())
                gtk_widget_queue_draw(ctx->first);
            delete ctx;
            return G_SOURCE_REMOVE;
        }, ctx);
    });

    p->engine.open(std::string(FileToLoad));

    gtk_widget_set_vexpand(p->glArea, TRUE);
    g_signal_connect(p->glArea, "realize", G_CALLBACK(onGlRealize), p);
    g_signal_connect(p->glArea, "unrealize", G_CALLBACK(onGlUnrealize), p);
    g_signal_connect(p->glArea, "resize", G_CALLBACK(onGlResize), p);
    g_signal_connect(p->glArea, "render", G_CALLBACK(onGlRender), p);
    gtk_box_pack_start(GTK_BOX(p->root), p->glArea, TRUE, TRUE, 0);

    // Transport controls, mirroring plugin_qt6.cpp's layout.
    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(controls), 4);

    p->playPauseBtn = gtk_button_new_with_label("Pause");
    g_signal_connect(p->playPauseBtn, "clicked", G_CALLBACK(togglePlayPause), p);
    gtk_box_pack_start(GTK_BOX(controls), p->playPauseBtn, FALSE, FALSE, 0);

    p->loopBtn = gtk_toggle_button_new_with_label("∞ ⟳");
    g_signal_connect(p->loopBtn, "toggled", G_CALLBACK(toggleLoop), p);
    gtk_box_pack_start(GTK_BOX(controls), p->loopBtn, FALSE, FALSE, 0);

    p->scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 1);
    gtk_scale_set_draw_value(GTK_SCALE(p->scale), FALSE);
    gtk_widget_set_hexpand(p->scale, TRUE);
    g_signal_connect(p->scale, "button-press-event", G_CALLBACK(onSeekButtonPress), p);
    g_signal_connect(p->scale, "button-release-event", G_CALLBACK(onSeekButtonRelease), p);
    gtk_box_pack_start(GTK_BOX(controls), p->scale, TRUE, TRUE, 0);

    p->timeLabel = gtk_label_new("00:00 / 00:00");
    gtk_box_pack_start(GTK_BOX(controls), p->timeLabel, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(p->root), controls, FALSE, FALSE, 0);

    p->timerId = g_timeout_add(250, onUpdateTick, p);

    // Tie MdkGtkPlayer's lifetime to the root widget so ListCloseWindow's
    // gtk_widget_destroy() cleans everything up in one place.
    g_object_set_data_full(G_OBJECT(p->root), "mdk-player", p, destroyPlayer);

    gtk_widget_show_all(p->root);

    fprintf(stderr, "[mdk_wlx_gtk3] ListLoad: widget=%p\n", (void *)p->root);
    return reinterpret_cast<HWND>(p->root);
}

void DCPCALL ListCloseWindow(HWND ListWin)
{
    fprintf(stderr, "[mdk_wlx_gtk3] ListCloseWindow\n");
    GtkWidget *root = GTK_WIDGET(ListWin);
    if (root)
        gtk_widget_destroy(root); // triggers destroyPlayer via g_object_set_data_full
}

int DCPCALL ListSearchDialog(HWND, int)
{
    return LISTPLUGIN_OK;
}

int DCPCALL ListSendCommand(HWND, int, int)
{
    return LISTPLUGIN_ERROR;
}

void DCPCALL ListSetDefaultParams(ListDefaultParamStruct *)
{
}

void DCPCALL ListGetDetectString(char *DetectString, int maxlen)
{
    const char *detect = "EXT=\"MP4\" | EXT=\"MKV\" | EXT=\"AVI\" | EXT=\"WEBM\" | EXT=\"FLV\" | EXT=\"MOV\" | EXT=\"WMV\" | EXT=\"MPEG\" | EXT=\"MPG\" | EXT=\"M4V\" | EXT=\"TS\" | EXT=\"VOB\" | EXT=\"MP3\" | EXT=\"FLAC\" | EXT=\"WAV\" | EXT=\"OGG\" | EXT=\"M4A\" | EXT=\"AAC\" | EXT=\"WMA\"";
    snprintf(DetectString, maxlen, "%s", detect);
}

} // extern "C"
