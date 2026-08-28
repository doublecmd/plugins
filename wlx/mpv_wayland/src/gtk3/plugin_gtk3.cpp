/*
 * mpv_wayland WLX plugin for Double Commander — GTK3 UI layer.
 *
 * Uses GtkGLArea for rendering, mirroring mdk's GTK3 port (same pattern:
 * MpvEngine owns all libmpv state, this file only wires it to GTK
 * widgets and the WLX plugin ABI).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gtk/gtk.h>
#include <epoxy/gl.h>
#include <epoxy/egl.h>
#include <epoxy/glx.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <memory>
#include <utility>

#include "wlxplugin.h"
#include "MpvEngine.h"

namespace {

// ── Diagnostics ──────────────────────────────────────────────────────────
// Logs to the project scratch dir (survives redeploys). Reported symptom:
// video renders in F3's standalone Lister window but not in DC's embedded
// quick-view panel. Two confirmed, real gaps found by reading DC's own
// source (uquickviewpanel.pas / fviewer.pas) rather than guessing from the
// plugin side alone:
//   1. This plugin never exported ListLoadNext/ListLoadNextW. DC's quick
//      view calls CallListLoadNext on every subsequent file as you arrow
//      through the panel; when that's unavailable it does NOT retry
//      CallListLoad -- fviewer.pas's LoadNextFile calls ExitPluginMode then
//      its own generic LoadFile, permanently dropping out of this plugin
//      for the rest of the quick-view session. Fixed below.
//   2. Quick view's TQuickViewPanel.CreateViewer calls FViewer.LoadFile()
//      BEFORE FViewer.Show() (uquickviewpanel.pas:159-160) -- our ListLoad
//      (and its forced gtk_widget_realize()) therefore runs while the
//      ancestor chain up to the real top-level window may not be mapped
//      yet, unlike F3's standalone window which is shown first. Whether
//      that actually prevents a valid GL context on this system is exactly
//      what this logging is for -- onGlRealize/onGlRender below log the
//      widget's realized/mapped/visible state and GL error status at each
//      step, so a live quick-view test's log tells us definitively whether
//      realization is even happening, rather than inferring from symptoms.
#define MPV_LOG_PATH "/home/pplupo/repos/plugins/scratch/mpv_wayland_debug.log"

void mpvLog(const char *fmt, ...)
{
    FILE *f = fopen(MPV_LOG_PATH, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

struct MpvGtkPlayer {
    std::unique_ptr<MpvEngine> engine;
    GtkWidget *root = nullptr;
    GtkWidget *glArea = nullptr;
    bool active = false;
    int renderFrameCount = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

// epoxy_eglGetProcAddress() only resolves symbols for an EGL-backed GL
// context -- GtkGLArea's actual backend depends on the platform/GDK
// backend it negotiated (GLX is the default on a plain X11 session,
// EGL is typical on Wayland), which this plugin has no control over and
// previously didn't check -- it hardcoded the EGL resolver
// unconditionally. There is no single backend-agnostic libepoxy
// function for this (epoxy's whole design is that *generated GL/EGL/GLX
// call sites* self-dispatch per-context; there's no public
// "epoxy_get_proc_address" despite the name suggesting one exists), so
// this tries EGL first and falls back to GLX -- covering both backends
// GtkGLArea can actually produce on Linux. Using the EGL-only variant
// unconditionally meant every real GL function look-up mpv's renderer
// does could silently fail on a GLX-backed context (no error returned by
// mpv_render_context_create() itself, since proc resolution happens
// lazily per-call inside mpv's renderer) -- consistent with the reported
// symptom: the GtkGLArea itself works (renders its clear-color
// background), but no actual video frame ever appears.
void *glGetProcAddressWrapper(void *ctx, const char *name)
{
    (void)ctx;
    if (void *p = reinterpret_cast<void *>(epoxy_eglGetProcAddress(name)))
        return p;
    return reinterpret_cast<void *>(epoxy_glXGetProcAddress(reinterpret_cast<const GLubyte *>(name)));
}

gboolean onGlRealize(GtkGLArea *area, gpointer userData)
{
    auto *p = static_cast<MpvGtkPlayer *>(userData);
    GtkWidget *w = GTK_WIDGET(area);
    mpvLog("[onGlRealize] ENTER realized=%d mapped=%d visible=%d toplevel_visible=%d alloc=%dx%d",
        gtk_widget_get_realized(w), gtk_widget_get_mapped(w), gtk_widget_get_visible(w),
        gtk_widget_get_visible(gtk_widget_get_toplevel(w)),
        gtk_widget_get_allocated_width(w), gtk_widget_get_allocated_height(w));

    gtk_gl_area_make_current(area);
    GError *glErr = gtk_gl_area_get_error(area);
    if (glErr != nullptr) {
        mpvLog("[onGlRealize] gtk_gl_area_get_error: %s", glErr->message);
        return FALSE;
    }
    mpvLog("[onGlRealize] gtk_gl_area_make_current OK");

    if (!p->engine->isValid()) {
        mpvLog("[onGlRealize] engine not valid (mpv_create/mpv_initialize failed earlier)");
        return FALSE;
    }

    if (!p->engine->initRenderContext(glGetProcAddressWrapper, nullptr)) {
        mpvLog("[onGlRealize] initRenderContext FAILED");
        std::fprintf(stderr, "[mpv_wayland_gtk3] initRenderContext failed\n");
        return FALSE;
    }
    mpvLog("[onGlRealize] initRenderContext OK, renderContextReady=%d", p->engine->renderContextReady());

    std::weak_ptr<bool> aliveWeak = p->alive;
    GtkWidget *glAreaRaw = p->glArea;
    p->engine->setUpdateCallback([glAreaRaw, aliveWeak]() {
        auto *ctx = new std::pair<GtkWidget *, std::weak_ptr<bool>>(glAreaRaw, aliveWeak);
        g_idle_add(+[](gpointer data) -> gboolean {
            auto *ctx = static_cast<std::pair<GtkWidget *, std::weak_ptr<bool>> *>(data);
            if (auto locked = ctx->second.lock())
                gtk_widget_queue_draw(ctx->first);
            delete ctx;
            return G_SOURCE_REMOVE;
        }, ctx);
    });

    mpvLog("[onGlRealize] EXIT OK, update callback wired");
    return FALSE;
}

gboolean onGlRender(GtkGLArea *, GdkGLContext *, gpointer userData)
{
    auto *p = static_cast<MpvGtkPlayer *>(userData);
    int w = gtk_widget_get_allocated_width(p->glArea);
    int h = gtk_widget_get_allocated_height(p->glArea);
    gint scaleFactor = gtk_widget_get_scale_factor(p->glArea);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // GtkGLArea binds its own FBO (not necessarily 0) before "render"
    // fires — query it instead of assuming the default framebuffer.
    GLint fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);

    // Logged only for the first 5 frames (then every 60th) -- render can
    // fire dozens of times a second once playback starts, and flooding the
    // log defeats the point of it. The first frame's w/h/fbo/ready values
    // are the ones that actually matter: if w or h is 0 here, mpv is being
    // asked to render into a zero-size target and nothing will ever be
    // visible regardless of whether the render context itself is fine.
    p->renderFrameCount++;
    bool shouldLog = p->renderFrameCount <= 5 || (p->renderFrameCount % 60) == 0;
    if (shouldLog) {
        mpvLog("[onGlRender] frame=%d w=%d h=%d scale=%d fbo=%d renderContextReady=%d",
            p->renderFrameCount, w, h, scaleFactor, fbo, p->engine->renderContextReady());
    }

    p->engine->render(fbo, w * scaleFactor, h * scaleFactor);
    return TRUE;
}

gboolean onMotion(GtkWidget *widget, GdkEventMotion *event, gpointer userData)
{
    auto *p = static_cast<MpvGtkPlayer *>(userData);
    gint scaleFactor = gtk_widget_get_scale_factor(widget);
    char cmd[64];
    std::snprintf(cmd, sizeof(cmd), "mouse %d %d",
                  (int)(event->x * scaleFactor), (int)(event->y * scaleFactor));
    p->engine->commandString(cmd);
    return TRUE;
}

gboolean onButtonPress(GtkWidget *widget, GdkEventButton *event, gpointer userData)
{
    auto *p = static_cast<MpvGtkPlayer *>(userData);
    p->active = true;
    gtk_widget_grab_focus(widget);
    if (event->button == GDK_BUTTON_PRIMARY) p->engine->commandString("keydown MOUSE_BTN0");
    else if (event->button == GDK_BUTTON_SECONDARY) p->engine->commandString("keydown MOUSE_BTN2");
    return TRUE;
}

gboolean onButtonRelease(GtkWidget *, GdkEventButton *event, gpointer userData)
{
    auto *p = static_cast<MpvGtkPlayer *>(userData);
    if (event->button == GDK_BUTTON_PRIMARY) p->engine->commandString("keyup MOUSE_BTN0");
    else if (event->button == GDK_BUTTON_SECONDARY) p->engine->commandString("keyup MOUSE_BTN2");
    return TRUE;
}

gboolean onScroll(GtkWidget *, GdkEventScroll *event, gpointer userData)
{
    auto *p = static_cast<MpvGtkPlayer *>(userData);
    if (event->direction == GDK_SCROLL_UP) p->engine->commandString("keypress WHEEL_UP");
    else if (event->direction == GDK_SCROLL_DOWN) p->engine->commandString("keypress WHEEL_DOWN");
    return TRUE;
}

gboolean onLeave(GtkWidget *, GdkEventCrossing *, gpointer userData)
{
    auto *p = static_cast<MpvGtkPlayer *>(userData);
    p->engine->commandString("mouse -100 -100");
    return TRUE;
}

const char *mapGdkKeyToMpvKey(guint keyval)
{
    switch (keyval) {
        case GDK_KEY_space: return "SPACE";
        case GDK_KEY_Left: return "LEFT";
        case GDK_KEY_Right: return "RIGHT";
        case GDK_KEY_Up: return "UP";
        case GDK_KEY_Down: return "DOWN";
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter: return "ENTER";
        case GDK_KEY_Escape: return "ESC";
        case GDK_KEY_BackSpace: return "BS";
        case GDK_KEY_Page_Up: return "PGUP";
        case GDK_KEY_Page_Down: return "PGDWN";
        case GDK_KEY_Home: return "HOME";
        case GDK_KEY_End: return "END";
        case GDK_KEY_Tab: return "TAB";
    }
    return nullptr;
}

gboolean onKeyPress(GtkWidget *, GdkEventKey *event, gpointer userData)
{
    auto *p = static_cast<MpvGtkPlayer *>(userData);
    if (!p->active) return FALSE;

    if (event->keyval == GDK_KEY_Escape) {
        p->active = false;
        return TRUE;
    }
    if (event->keyval == GDK_KEY_q && (event->state & GDK_CONTROL_MASK)) {
        p->active = false;
        return FALSE; // let it propagate so DC's own Ctrl+Q handling can fire
    }

    if (const char *mpvKey = mapGdkKeyToMpvKey(event->keyval)) {
        char cmd[64];
        std::snprintf(cmd, sizeof(cmd), "keypress %s", mpvKey);
        p->engine->commandString(cmd);
        return TRUE;
    }
    // Printable character fallback
    guint32 unicode = gdk_keyval_to_unicode(event->keyval);
    if (unicode >= 0x20 && unicode < 0x110000) {
        char utf8[8] = {0};
        g_unichar_to_utf8(unicode, utf8);
        std::string cmd = std::string("keypress ") + utf8;
        p->engine->commandString(cmd);
        return TRUE;
    }
    return FALSE;
}

void destroyPlayer(gpointer data)
{
    auto *p = static_cast<MpvGtkPlayer *>(data);
    *p->alive = false;
    delete p;
}

// ── Menu bar ─────────────────────────────────────────────────────────────
// Every action here is a plain mpv input command sent via commandString(),
// the same mechanism onKeyPress already uses for keyboard input -- this
// menu is a second entry point onto the exact same command set, not a
// parallel implementation.

// A naked std::pair<A, B> trips up the preprocessor when used directly
// inside a G_CALLBACK(...) macro invocation -- G_CALLBACK only balances
// parens when splitting its argument list, so the comma inside the pair's
// template argument list gets misparsed as an extra macro argument (same
// class of bug already fixed elsewhere in this codebase). A type alias
// sidesteps it.
using MenuCmdCtx = std::pair<MpvGtkPlayer *, const char *>;

GtkWidget *addMenuItem(GtkWidget *menu, const char *label, const char *mpvCmd, MpvGtkPlayer *p)
{
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    // A raw const char* survives for the process lifetime (all callers pass
    // string literals), so capturing it directly avoids an allocation per
    // menu item just to satisfy g_signal_connect's void* data slot.
    auto *ctx = new MenuCmdCtx(p, mpvCmd);
    g_signal_connect_data(item, "activate", G_CALLBACK(+[](GtkMenuItem *, gpointer data) {
        auto *ctx = static_cast<MenuCmdCtx *>(data);
        ctx->first->engine->commandString(ctx->second);
    }), ctx, +[](gpointer data, GClosure *) { delete static_cast<MenuCmdCtx *>(data); },
        (GConnectFlags)0);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return item;
}

GtkWidget *addSubMenu(GtkWidget *parent, const char *label)
{
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    GtkWidget *sub = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
    return sub;
}

GtkWidget *buildMenuBar(MpvGtkPlayer *p)
{
    GtkWidget *menuBar = gtk_menu_bar_new();
    // Must never take focus from the video surface -- same rule
    // GtkPluginToolBar follows elsewhere in this codebase (DC hosts this
    // panel inside its own window; a menu bar stealing focus breaks
    // keyboard playback control, which onKeyPress depends on p->active for).
    gtk_widget_set_can_focus(menuBar, FALSE);

    GtkWidget *playback = addSubMenu(menuBar, "Playback");
    addMenuItem(playback, "Play / Pause", "cycle pause", p);
    addMenuItem(playback, "Stop", "stop", p);
    addMenuItem(playback, "Restart", "seek 0 absolute", p);
    gtk_menu_shell_append(GTK_MENU_SHELL(playback), gtk_separator_menu_item_new());
    addMenuItem(playback, "Seek Back 10s", "seek -10", p);
    addMenuItem(playback, "Seek Forward 10s", "seek 10", p);
    addMenuItem(playback, "Seek Back 60s", "seek -60", p);
    addMenuItem(playback, "Seek Forward 60s", "seek 60", p);
    gtk_menu_shell_append(GTK_MENU_SHELL(playback), gtk_separator_menu_item_new());
    addMenuItem(playback, "Frame Step", "frame-step", p);
    addMenuItem(playback, "Frame Back Step", "frame-back-step", p);
    addMenuItem(playback, "Speed 0.5x", "set speed 0.5", p);
    addMenuItem(playback, "Speed 1.0x (Normal)", "set speed 1.0", p);
    addMenuItem(playback, "Speed 1.5x", "set speed 1.5", p);
    addMenuItem(playback, "Speed 2.0x", "set speed 2.0", p);

    GtkWidget *audio = addSubMenu(menuBar, "Audio");
    addMenuItem(audio, "Volume Up", "add volume 5", p);
    addMenuItem(audio, "Volume Down", "add volume -5", p);
    addMenuItem(audio, "Mute", "cycle mute", p);
    gtk_menu_shell_append(GTK_MENU_SHELL(audio), gtk_separator_menu_item_new());
    addMenuItem(audio, "Cycle Audio Track", "cycle audio", p);
    addMenuItem(audio, "Cycle Audio Device", "cycle audio-device", p);

    GtkWidget *subs = addSubMenu(menuBar, "Subtitles");
    addMenuItem(subs, "Toggle Visibility", "cycle sub-visibility", p);
    addMenuItem(subs, "Cycle Track", "cycle sub", p);
    addMenuItem(subs, "Cycle Track (Backward)", "cycle sub down", p);
    gtk_menu_shell_append(GTK_MENU_SHELL(subs), gtk_separator_menu_item_new());
    addMenuItem(subs, "Delay +0.1s", "add sub-delay 0.1", p);
    addMenuItem(subs, "Delay -0.1s", "add sub-delay -0.1", p);

    GtkWidget *view = addSubMenu(menuBar, "View");
    addMenuItem(view, "Toggle Stats Overlay", "script-binding stats/display-stats-toggle", p);
    addMenuItem(view, "Toggle OSD", "cycle-values osd-level 3 1", p);
    addMenuItem(view, "Screenshot", "screenshot", p);
    gtk_menu_shell_append(GTK_MENU_SHELL(view), gtk_separator_menu_item_new());
    addMenuItem(view, "Zoom In", "add video-zoom 0.1", p);
    addMenuItem(view, "Zoom Out", "add video-zoom -0.1", p);
    addMenuItem(view, "Reset Zoom / Pan", "set video-zoom 0; set video-pan-x 0; set video-pan-y 0", p);

    return menuBar;
}

} // namespace

extern "C" {

HWND DCPCALL ListLoad(HWND ParentWin, char *FileToLoad, int ShowFlags)
{
    fprintf(stderr, "[mpv_wayland_gtk3] ListLoad: file=%s\n", FileToLoad);
    mpvLog("\n==== ListLoad('%s') ====", FileToLoad ? FileToLoad : "(null)");

    GtkWidget *parent = GTK_WIDGET(ParentWin);
    mpvLog("[ListLoad] parent realized=%d mapped=%d visible=%d toplevel_visible=%d",
        parent ? gtk_widget_get_realized(parent) : -1, parent ? gtk_widget_get_mapped(parent) : -1,
        parent ? gtk_widget_get_visible(parent) : -1,
        parent ? gtk_widget_get_visible(gtk_widget_get_toplevel(parent)) : -1);
    auto *p = new MpvGtkPlayer();
    p->engine = std::make_unique<MpvEngine>();

    // Vertical box instead of the previous plain GtkEventBox, so a menu bar
    // can sit above the video surface. GtkBox has no window of its own
    // (unlike GtkEventBox), which doesn't matter here -- glArea already
    // owns and handles all of its own input events directly.
    p->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    p->glArea = gtk_gl_area_new();

    // THE FIX (found via a debug build + logging, not the sizing theory the
    // comment below used to describe): every signal, especially "realize",
    // must be connected to glArea BEFORE it is added to any parent -- not
    // just before show_all()/realize(). gtk_container_add() below can
    // realize (and even map) a child SYNCHRONOUSLY, as a side effect, if the
    // container it's being added to is already part of a realized widget
    // tree. GTK only ever emits "realize" once per realize/unrealize cycle,
    // so a handler connected after that point never fires again for the
    // rest of this widget's life.
    //
    // This is exactly the F3-vs-quick-view split: F3 opens a brand new,
    // not-yet-shown TfrmViewer window, so DC's whole ancestor chain up to
    // our GtkLayout parent is UNREALIZED when ListLoad runs -- connecting
    // signals late still worked there by accident. Quick view's panel
    // (uquickviewpanel.pas) is a child of DC's ALWAYS-VISIBLE main window,
    // so the ancestor chain is already realized the instant ListLoad runs;
    // confirmed live via logging: gtk_widget_get_realized(glArea) was
    // already 1 at the g_signal_connect("realize", ...) call in the old
    // ordering, so onGlRealize (and therefore mpv's whole render-context
    // creation) never ran at all in that path -- not a sizing/timing race,
    // a hard ordering bug. onGlRender still fired (GTK's normal draw cycle
    // doesn't care whether realize ran), which is why the panel showed a
    // plain black GtkGLArea background rather than crashing or staying
    // empty in an obviously-broken way.
    gtk_widget_set_can_focus(p->glArea, TRUE);
    gtk_widget_add_events(p->glArea,
        GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
        GDK_SCROLL_MASK | GDK_KEY_PRESS_MASK | GDK_LEAVE_NOTIFY_MASK);
    // Real geometry isn't known yet at this point either way (DC's
    // ResizeWindow runs after ListLoad returns) -- kept as a placeholder so
    // GtkGLArea has a sane starting size instead of 0x0/1x1 regardless of
    // which of the two realization paths above actually applies.
    gtk_widget_set_size_request(p->glArea, 320, 240);

    gulong realizeHandlerId = g_signal_connect(p->glArea, "realize", G_CALLBACK(onGlRealize), p);
    mpvLog("[ListLoad] connected 'realize' handler id=%lu (0=failed) already_realized_at_connect_time=%d "
           "(should always be 0 now -- glArea has no parent yet at this point)",
        realizeHandlerId, gtk_widget_get_realized(p->glArea));
    g_signal_connect(p->glArea, "render", G_CALLBACK(onGlRender), p);
    g_signal_connect(p->glArea, "motion-notify-event", G_CALLBACK(onMotion), p);
    g_signal_connect(p->glArea, "button-press-event", G_CALLBACK(onButtonPress), p);
    g_signal_connect(p->glArea, "button-release-event", G_CALLBACK(onButtonRelease), p);
    g_signal_connect(p->glArea, "scroll-event", G_CALLBACK(onScroll), p);
    g_signal_connect(p->glArea, "leave-notify-event", G_CALLBACK(onLeave), p);
    g_signal_connect(p->glArea, "key-press-event", G_CALLBACK(onKeyPress), p);

    // Only now, with every signal already connected, does glArea become
    // part of any widget tree.
    GtkWidget *menuBar = buildMenuBar(p);
    gtk_box_pack_start(GTK_BOX(p->root), menuBar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(p->root), p->glArea, TRUE, TRUE, 0);
    // gtk_container_add() doesn't register the child the way GtkLayout
    // expects: DC's ResizeWindow later calls gtk_layout_move() on this
    // widget, which asserts the parent is exactly this GtkLayout -- only
    // gtk_layout_put() sets that up.
    gtk_layout_put(GTK_LAYOUT(parent), p->root, 0, 0);

    g_object_set_data_full(G_OBJECT(p->root), "mpv-player", p, destroyPlayer);

    gtk_widget_show_all(p->root);
    mpvLog("[ListLoad] after show_all: glArea realized=%d mapped=%d visible=%d alloc=%dx%d",
        gtk_widget_get_realized(p->glArea), gtk_widget_get_mapped(p->glArea),
        gtk_widget_get_visible(p->glArea), gtk_widget_get_allocated_width(p->glArea),
        gtk_widget_get_allocated_height(p->glArea));
    // Force realization synchronously rather than waiting for GTK's normal
    // map/size-allocate cycle to get around to it -- same reasoning as the
    // size_request above: don't leave mpv's render context creation
    // dependent on timing DC doesn't guarantee for the quick-view panel.
    gtk_widget_realize(p->glArea);
    mpvLog("[ListLoad] after forced realize: glArea realized=%d mapped=%d alloc=%dx%d",
        gtk_widget_get_realized(p->glArea), gtk_widget_get_mapped(p->glArea),
        gtk_widget_get_allocated_width(p->glArea), gtk_widget_get_allocated_height(p->glArea));

    if (p->engine->isValid()) {
        bool ok = p->engine->loadFile(std::string(FileToLoad));
        mpvLog("[ListLoad] engine->loadFile -> %d", (int)ok);
    } else {
        mpvLog("[ListLoad] engine NOT valid, skipping loadFile");
    }

    fprintf(stderr, "[mpv_wayland_gtk3] ListLoad: widget=%p\n", (void *)p->root);
    mpvLog("[ListLoad] EXIT root=%p", (void *)p->root);
    return reinterpret_cast<HWND>(p->root);
}

int DCPCALL ListLoadNext(HWND ParentWin, HWND PluginWin, char *FileToLoad, int ShowFlags)
{
    // Without this export, DC's quick-view panel (uquickviewpanel.pas ->
    // fviewer.pas TfrmViewer.LoadNextFile) calls CallListLoadNext on every
    // file the user arrows to; when the symbol doesn't exist it does NOT
    // fall back to calling ListLoad again -- it does ExitPluginMode and
    // switches to DC's own generic viewer, permanently abandoning this
    // plugin for the rest of that quick-view session. So even if the very
    // first file renders correctly, every subsequent file in quick view
    // silently stopped using mpv at all. F3's standalone Lister window
    // never calls LoadNextFile this way, which is why the two entry points
    // behaved differently even before touching the realize/mapped-state
    // question logged elsewhere in this file.
    mpvLog("\n==== ListLoadNext('%s') PluginWin=%p ====", FileToLoad ? FileToLoad : "(null)", (void *)PluginWin);
    GtkWidget *root = GTK_WIDGET(PluginWin);
    auto *p = root ? static_cast<MpvGtkPlayer *>(g_object_get_data(G_OBJECT(root), "mpv-player")) : nullptr;
    if (!p || !p->engine || !p->engine->isValid()) {
        mpvLog("[ListLoadNext] no reusable player state, returning LISTPLUGIN_ERROR (DC will fall back)");
        return LISTPLUGIN_ERROR;
    }
    bool ok = p->engine->loadFile(std::string(FileToLoad));
    mpvLog("[ListLoadNext] loadFile -> %d", (int)ok);
    return ok ? LISTPLUGIN_OK : LISTPLUGIN_ERROR;
}

int DCPCALL ListLoadNextW(HWND ParentWin, HWND PluginWin, WCHAR *FileToLoad, int ShowFlags)
{
    // Narrow to UTF-8 and delegate -- same simplification ListLoad already
    // makes by only exporting the narrow entry point (DC falls back to
    // ListLoad/ListLoadNext when the W variant is absent, but since we ARE
    // providing an explicit ListLoadNext, DC prefers ListLoadNextW when both
    // exist per uwlxmodule.pas's Assigned(ListLoadNextW) check -- so this
    // needs to actually exist, not be left absent hoping the narrow one is
    // used instead).
    char narrow[4096];
    int i = 0;
    for (; FileToLoad && FileToLoad[i] && i < (int)sizeof(narrow) - 1; ++i)
        narrow[i] = (FileToLoad[i] < 128) ? (char)FileToLoad[i] : '?';
    narrow[i] = '\0';
    return ListLoadNext(ParentWin, PluginWin, narrow, ShowFlags);
}

void DCPCALL ListCloseWindow(HWND ListWin)
{
    fprintf(stderr, "[mpv_wayland_gtk3] ListCloseWindow\n");
    mpvLog("[ListCloseWindow] ListWin=%p", (void *)ListWin);
    GtkWidget *root = GTK_WIDGET(ListWin);
    if (root) {
        auto *p = static_cast<MpvGtkPlayer *>(g_object_get_data(G_OBJECT(root), "mpv-player"));
        if (p && p->engine) p->engine->closeFile();
        gtk_widget_destroy(root);
    }
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
    snprintf(DetectString, maxlen,
        "EXT=\"MKV\" | EXT=\"MP4\" | EXT=\"AVI\" | EXT=\"MOV\" | EXT=\"MP3\" | "
        "EXT=\"FLAC\" | EXT=\"WAV\" | EXT=\"OGG\" | EXT=\"WEBM\"");
}

} // extern "C"
