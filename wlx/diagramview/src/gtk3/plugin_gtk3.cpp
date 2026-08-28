/*
 * Diagram (Mermaid/PlantUML) WLX plugin for Double Commander — GTK3 UI.
 *
 * Renders SVG via librsvg onto a GtkDrawingArea with Cairo, mirroring the
 * Qt6 build's QGraphicsView + QSvgRenderer approach. All CLI subprocess
 * invocation (mmdc/plantuml.jar/curl) and settings logic lives in
 * DiagramRenderer (src/core/) — shared with the Qt6 build, unmodified.
 */

#include <gtk/gtk.h>
#include <librsvg/rsvg.h>
#include <gio/gio.h>
#include <pango/pangocairo.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <exception>
#include <sys/resource.h>
#include <thread>
#include <memory>

#include "wlxplugin.h"
#include "DiagramRenderer.h"

namespace {

DiagramRenderer::Settings g_settings;
std::string g_configPath;

// ── Diagnostics ──────────────────────────────────────────────────────────
// Logs to the project scratch dir (not the deployed config dir, so it
// survives redeploys) for correlating with live crash reports -- DC's own
// doublecmd.err has been shown to misattribute outer stack frames to
// unrelated source lines under optimization, so it can't pinpoint where in
// this plugin a crash actually originates. Every extern "C" entry point
// logs on entry/exit, and every risky operation (subprocess render, SVG
// parse, Cairo draw) logs around itself, so the LAST line written before a
// crash is itself the diagnosis. Intentionally left in (not stripped after
// this investigation) until the field crash is confirmed fixed.
#define DV_LOG_PATH "/home/pplupo/repos/plugins/scratch/diagramview_debug.log"

void dvLog(const char *fmt, ...)
{
    FILE *f = fopen(DV_LOG_PATH, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

bool isSystemDark()
{
    GtkSettings *settings = gtk_settings_get_default();
    if (!settings) return false;
    gboolean preferDark = FALSE;
    g_object_get(settings, "gtk-application-prefer-dark-theme", &preferDark, nullptr);
    return preferDark;
}

struct DiagramState {
    GtkWidget *root = nullptr;
    GtkWidget *drawingArea = nullptr;
    RsvgHandle *handle = nullptr;
    std::string currentFilePath;
    std::string lastSvgData;

    double zoom = 1.0;
    double panX = 0.0;
    double panY = 0.0;
    bool dragging = false;
    double dragStartX = 0.0, dragStartY = 0.0;
    double panStartX = 0.0, panStartY = 0.0;
    bool fitted = false;

    GFileMonitor *monitor = nullptr;
    guint debounceTimerId = 0;

    std::string errorMessage; // shown inline in onDraw instead of a modal dialog -- see showError()

    // Real bug report: opening an .mmd file with no local mmdc froze the
    // ENTIRE app for the length of the local+npx fallback chain (up to
    // timeoutMs*3 = 45s by default) -- executeRender ran synchronously on
    // DC's own main GTK thread, which cannot process any other input,
    // repaint, or even be closed while blocked. renderInFlight guards
    // against piling up a second render (e.g. from auto-reload) while one
    // is already running in the background; alive lets the background
    // thread's completion callback detect the panel was closed before it
    // finishes, instead of touching freed memory.
    bool renderInFlight = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

void executeRender(DiagramState *st);

void setHandle(DiagramState *st, const std::string &svgData)
{
    dvLog("[setHandle] svgData.size()=%zu", svgData.size());
    if (st->handle) {
        g_object_unref(st->handle);
        st->handle = nullptr;
    }
    st->lastSvgData = svgData;
    if (svgData.empty()) { dvLog("[setHandle] empty SVG, no handle created"); return; }

    GError *error = nullptr;
    st->handle = rsvg_handle_new_from_data(
        reinterpret_cast<const guint8 *>(svgData.data()), svgData.size(), &error);
    if (!st->handle && error) {
        dvLog("[setHandle] rsvg parse FAILED: %s", error->message);
        g_warning("[diagramview_gtk3] SVG parse error: %s", error->message);
        g_error_free(error);
    } else {
        dvLog("[setHandle] rsvg_handle_new_from_data OK, handle=%p", (void *)st->handle);
    }
    st->fitted = false;
    gtk_widget_queue_draw(st->drawingArea);
}

// Renders the error inline in the drawing area instead of a modal
// GtkMessageDialog. gtk_dialog_run() starts a *nested* main loop, and
// this function is reachable synchronously from ListLoad() (via
// loadFile() -> executeRender()) -- at that point DC's own LCL code is
// still on the call stack constructing the very panel this widget lives
// in (confirmed via GDB: crashes surfaced deep inside DC's own
// control.inc/customform.inc/wincontrol.inc paint/layout code, called
// from *underneath* this plugin's own frames). Re-entering GTK's event
// loop mid-construction like that pumps DC's own pending
// paint/layout/realize callbacks out of order and corrupts whatever
// LCL's construction code was in the middle of -- a real, reproduced
// crash, not a hypothetical one. A modal dialog triggered by an
// explicit user action after the widget already exists and is fully
// realized (e.g. the Save As dialogs elsewhere in this file) doesn't
// have this problem and is left as-is.
void showError(DiagramState *st, const char *title, const char *msg)
{
    st->errorMessage = std::string(title) + ": " + msg;
    if (st->handle) { g_object_unref(st->handle); st->handle = nullptr; }
    if (st->drawingArea) gtk_widget_queue_draw(st->drawingArea);
}

// For errors from explicit user actions (Save As SVG/PNG) where a modal
// dialog is safe -- the widget already exists, is realized, and DC isn't
// mid-construction of it, unlike the ListLoad-triggered path above.
void showModalError(GtkWidget *parent, const char *title, const char *msg)
{
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(parent)),
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", msg);
    gtk_window_set_title(GTK_WINDOW(dlg), title);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

gboolean onDebounceTimeout(gpointer userData)
{
    auto *st = static_cast<DiagramState *>(userData);
    st->debounceTimerId = 0;
    if (g_settings.autoReloadEnabled)
        executeRender(st);
    return G_SOURCE_REMOVE;
}

void onFileChanged(GFileMonitor *, GFile *, GFile *, GFileMonitorEvent, gpointer userData)
{
    auto *st = static_cast<DiagramState *>(userData);
    if (st->debounceTimerId) g_source_remove(st->debounceTimerId);
    st->debounceTimerId = g_timeout_add(200, onDebounceTimeout, st);
}

std::string extensionOf(const std::string &path)
{
    auto slash = path.find_last_of('/');
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    std::string ext = path.substr(dot + 1);
    for (auto &c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return ext;
}

// Root cause of the live "quick view crashes" reports (caught under gdb with
// full symbols against the real dcgtk process -- never reproduced in a
// standalone harness calling ListLoad from a shallow main()): libstdc++'s
// std::regex compiler is unusually stack-hungry. This runs on DC's own GUI
// thread, deep inside its own call chain (the kastoolitems.pas/kasbutton.pas/
// customform.inc/wincontrol.inc frames present in every crash report) by the
// time it reaches our ListLoad -- so remaining stack headroom, not our own
// stack usage in isolation, decides whether it overflows. Confirmed to
// happen even for .puml (no regex involved at all in that path), so this is
// a general "not enough stack left for whatever we do here" problem, not
// specifically a regex one.
//
// A first attempt moved the rendering work onto a dedicated pthread with an
// explicit large stack, isolating it from DC's stack depth entirely. That
// backfired: constructing ANY std::regex from that freshly-spawned thread
// crashed with SIGSEGV inside std::codecvt::do_unshift, called from the
// regex compiler's locale/facet setup -- reproduced for two entirely
// different regex patterns (markdownview's codeBlockRe and this file's
// foreignObjRe, in fixMermaidSvgText), both at the identical faulting
// instruction. That points to a libstdc++ locale-facet thread-safety issue
// specific to a brand-new thread being the first to touch std::locale in a
// process that also does its own C setlocale() (DC does, per its own startup
// log) -- not something a bigger stack fixes.
//
// So: stay on DC's calling thread (where locale state is already consistent
// -- every prior test, on DC's main thread, succeeded up until the original
// stack-depth crash), and instead raise THIS thread's own stack ceiling via
// setrlimit(RLIMIT_STACK). Unlike a pthread's fixed-size mmap'd stack, the
// original/main thread's stack grows on demand via page faults up to
// RLIMIT_STACK -- raising the limit (even after the thread has been running
// and using stack for a while) gives the kernel room to keep growing it on
// the NEXT fault, which is exactly what's needed here.
void ensureLargeStackLimit()
{
    static bool done = false;
    if (done) return;
    done = true;

    struct rlimit rl;
    if (getrlimit(RLIMIT_STACK, &rl) != 0) return;

    const rlim_t want = 256ul * 1024 * 1024;
    rlim_t target = want;
    if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < target)
        target = rl.rlim_max; // can't exceed the hard limit without CAP_SYS_RESOURCE
    if (rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur >= target)
        return; // already generous enough

    rl.rlim_cur = target;
    setrlimit(RLIMIT_STACK, &rl); // best-effort; ignore failure, nothing else to fall back to
}

// Result handed from the background render thread back to the GTK main
// thread via g_idle_add. Owns everything the completion callback needs so it
// never has to reach back into the (possibly-already-closed) DiagramState
// from off the main thread.
struct RenderResult {
    std::weak_ptr<bool> aliveWeak;
    DiagramState *st; // only dereferenced after the alive check below
    std::string ext;
    std::string svg;
    bool threw = false;
    std::string exceptionMsg;
};

gboolean onRenderComplete(gpointer data)
{
    std::unique_ptr<RenderResult> r(static_cast<RenderResult *>(data));
    if (!r->aliveWeak.lock()) {
        dvLog("[onRenderComplete] panel closed before render finished, discarding result");
        return G_SOURCE_REMOVE;
    }
    DiagramState *st = r->st;
    st->renderInFlight = false;

    dvLog("[onRenderComplete] render returned %zu bytes (threw=%d)", r->svg.size(), (int)r->threw);
    if (r->threw) {
        dvLog("[onRenderComplete] EXCEPTION: %s", r->exceptionMsg.c_str());
        showError(st, "Diagram Viewer Error", (std::string("Internal error: ") + r->exceptionMsg).c_str());
        return G_SOURCE_REMOVE;
    }
    if (r->svg.empty()) {
        bool isMermaid = (r->ext == "mmd" || r->ext == "mermaid");
        const char *what = isMermaid ? "Mermaid" : "PlantUML";
        const char *tool = isMermaid
            ? "'@mermaid-js/mermaid-cli' is installed, 'npx' is available"
            : "Java/PlantUML is installed locally";
        std::string msg = std::string("Failed to render ") + what + " diagram.\nPlease ensure " + tool +
            ", or internet connection is active.";
        showError(st, "Diagram Viewer Error", msg.c_str());
        return G_SOURCE_REMOVE;
    }

    st->errorMessage.clear();
    setHandle(st, r->svg);
    dvLog("[onRenderComplete] EXIT OK");
    return G_SOURCE_REMOVE;
}

void executeRender(DiagramState *st)
{
    dvLog("[executeRender] ENTER path='%s'", st->currentFilePath.c_str());
    if (st->currentFilePath.empty()) { dvLog("[executeRender] empty path, skip"); return; }

    std::string ext = extensionOf(st->currentFilePath);
    bool activeDarkMode = g_settings.useSystemDarkMode ? isSystemDark() : g_settings.darkMode;
    dvLog("[executeRender] ext='%s' darkMode=%d mermaidRenderer='%s' plantumlRenderer='%s'",
        ext.c_str(), activeDarkMode, g_settings.mermaidRenderer.c_str(), g_settings.renderer.c_str());

    if (ext != "mmd" && ext != "mermaid" && ext != "puml" && ext != "plantuml") {
        dvLog("[executeRender] unsupported extension '%s'", ext.c_str());
        showError(st, "Diagram Viewer Error", ("Unsupported file extension: " + ext).c_str());
        return;
    }

    // Real bug report: this used to run synchronously right here, on DC's
    // own GTK main thread. The local-mmdc/npx/curl fallback chain can take
    // up to timeoutMs*3 (45s by default) to give up and try the next option
    // -- and DC's ENTIRE UI is unresponsive for the whole duration, since
    // its single main thread is blocked inside our call and cannot process
    // any other event, including closing the panel. Rendering now happens on
    // a detached background thread; the result is marshaled back via
    // g_idle_add, which runs on the main thread exactly like any other GTK
    // callback. Safe to do now (wasn't, for a different reason, a few fixes
    // ago): the crash that made a background thread unsafe here was
    // libstdc++ locale-facet corruption from statically linking libstdc++
    // into this .wlx, now removed; subprocess spawning itself is plain
    // POSIX and was never the issue.
    if (st->renderInFlight) {
        dvLog("[executeRender] a render is already in flight, skipping (e.g. auto-reload while pending)");
        return;
    }
    st->renderInFlight = true;
    gtk_widget_queue_draw(st->drawingArea); // show the "Rendering..." placeholder immediately
    ensureLargeStackLimit();

    std::string filePath = st->currentFilePath;
    std::weak_ptr<bool> aliveWeak = st->alive;
    std::thread([filePath, ext, activeDarkMode, aliveWeak, st]() {
        auto *result = new RenderResult{aliveWeak, st, ext, {}, false, {}};
        try {
            if (ext == "mmd" || ext == "mermaid") {
                result->svg = DiagramRenderer::renderMermaid(g_settings, filePath, activeDarkMode);
                if (!result->svg.empty()) result->svg = DiagramRenderer::fixMermaidSvgText(result->svg, activeDarkMode);
            } else {
                result->svg = DiagramRenderer::renderPlantUml(g_settings, filePath, activeDarkMode);
            }
        } catch (const std::exception &e) {
            result->threw = true;
            result->exceptionMsg = e.what();
        } catch (...) {
            result->threw = true;
            result->exceptionMsg = "unknown exception";
        }
        g_idle_add(onRenderComplete, result);
    }).detach();
    dvLog("[executeRender] render dispatched to background thread");
}

void loadFile(DiagramState *st, const std::string &path)
{
    st->currentFilePath = path;

    if (st->monitor) {
        g_object_unref(st->monitor);
        st->monitor = nullptr;
    }
    GFile *gfile = g_file_new_for_path(path.c_str());
    GError *error = nullptr;
    st->monitor = g_file_monitor_file(gfile, G_FILE_MONITOR_NONE, nullptr, &error);
    g_object_unref(gfile);
    if (st->monitor) {
        g_signal_connect(st->monitor, "changed", G_CALLBACK(onFileChanged), st);
    } else if (error) {
        g_error_free(error);
    }

    executeRender(st);
}

void getDocSize(DiagramState *st, double *w, double *h)
{
    *w = 800; *h = 600;
    if (!st->handle) return;
    gdouble dw = 0, dh = 0;
    if (rsvg_handle_get_intrinsic_size_in_pixels(st->handle, &dw, &dh) && dw > 0 && dh > 0) {
        *w = dw; *h = dh;
    }
}

void fitToView(DiagramState *st)
{
    if (!st->handle || st->fitted) return;
    double docW, docH;
    getDocSize(st, &docW, &docH);

    int allocW = gtk_widget_get_allocated_width(st->drawingArea);
    int allocH = gtk_widget_get_allocated_height(st->drawingArea);
    if (allocW < 10 || allocH < 10 || docW <= 0 || docH <= 0) return;

    double margin = 20.0;
    double sx = (allocW - margin) / docW;
    double sy = (allocH - margin) / docH;
    double zoom = std::min(sx, sy);
    // rsvg_handle_get_intrinsic_size_in_pixels() is trusted verbatim
    // elsewhere in this file; a malformed/pathological SVG (an absurd
    // viewBox, or one librsvg reports as 0-ish after unit conversion) can
    // make docW/docH tiny enough that zoom overflows to +-inf or becomes
    // NaN. cairo_scale() with a non-finite factor is undefined per Cairo's
    // own docs, not a guaranteed no-op -- clamp before it ever reaches
    // cairo_scale() in onDraw.
    if (!std::isfinite(zoom) || zoom <= 0.0) {
        dvLog("[fitToView] non-finite/degenerate zoom computed (docW=%.6f docH=%.6f sx=%.6f sy=%.6f) -- clamping to 1.0",
            docW, docH, sx, sy);
        zoom = 1.0;
    }
    st->zoom = zoom;
    st->panX = (allocW - docW * st->zoom) / 2.0;
    st->panY = (allocH - docH * st->zoom) / 2.0;
    st->fitted = true;
    dvLog("[fitToView] docW=%.2f docH=%.2f allocW=%d allocH=%d -> zoom=%.4f panX=%.2f panY=%.2f",
        docW, docH, allocW, allocH, st->zoom, st->panX, st->panY);
}

gboolean onDraw(GtkWidget *widget, cairo_t *cr, gpointer userData)
{
    auto *st = static_cast<DiagramState *>(userData);

    int allocW = gtk_widget_get_allocated_width(widget);
    int allocH = gtk_widget_get_allocated_height(widget);

    bool activeDarkMode = g_settings.useSystemDarkMode ? isSystemDark() : g_settings.darkMode;
    if (activeDarkMode)
        cairo_set_source_rgb(cr, 30 / 255.0, 30 / 255.0, 46 / 255.0);
    else
        cairo_set_source_rgb(cr, 248 / 255.0, 249 / 255.0, 250 / 255.0);
    cairo_paint(cr);

    // Dot grid, matching the Qt build's drawBackground().
    cairo_set_source_rgb(cr, activeDarkMode ? 45 / 255.0 : 226 / 255.0,
                              activeDarkMode ? 45 / 255.0 : 232 / 255.0,
                              activeDarkMode ? 68 / 255.0 : 240 / 255.0);
    cairo_set_line_width(cr, 1.0);
    double spacing = 20.0;
    for (double x = 0; x < allocW; x += spacing) {
        cairo_move_to(cr, x, 0); cairo_line_to(cr, x, allocH);
    }
    for (double y = 0; y < allocH; y += spacing) {
        cairo_move_to(cr, 0, y); cairo_line_to(cr, allocW, y);
    }
    cairo_stroke(cr);

    if (st->renderInFlight) {
        cairo_set_source_rgb(cr, activeDarkMode ? 0.75 : 0.35, activeDarkMode ? 0.75 : 0.35, activeDarkMode ? 0.75 : 0.35);
        PangoLayout *layout = pango_cairo_create_layout(cr);
        pango_layout_set_width(layout, (allocW - 40) * PANGO_SCALE);
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_text(layout, "Rendering diagram... (this can take a while the first time a "
                                       "local renderer needs to download tooling)", -1);
        cairo_move_to(cr, 20, 20);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        return FALSE;
    }

    if (!st->errorMessage.empty()) {
        cairo_set_source_rgb(cr, activeDarkMode ? 1.0 : 0.6, 0.2, 0.2);
        PangoLayout *layout = pango_cairo_create_layout(cr);
        pango_layout_set_width(layout, (allocW - 40) * PANGO_SCALE);
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_text(layout, st->errorMessage.c_str(), -1);
        cairo_move_to(cr, 20, 20);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        return FALSE;
    }

    if (!st->handle) return FALSE;

    fitToView(st);

    cairo_save(cr);
    cairo_translate(cr, st->panX, st->panY);
    cairo_scale(cr, st->zoom, st->zoom);

    double docW, docH;
    getDocSize(st, &docW, &docH);
    RsvgRectangle viewport{0, 0, docW, docH};
    GError *error = nullptr;
    if (!rsvg_handle_render_document(st->handle, cr, &viewport, &error)) {
        if (error) g_error_free(error);
    }
    cairo_restore(cr);
    return FALSE;
}

gboolean onScroll(GtkWidget *widget, GdkEventScroll *event, gpointer userData)
{
    auto *st = static_cast<DiagramState *>(userData);
    const double scaleFactor = 1.15;
    double factor = (event->direction == GDK_SCROLL_UP) ? scaleFactor
                   : (event->direction == GDK_SCROLL_DOWN) ? 1.0 / scaleFactor : 1.0;
    if (factor == 1.0) return TRUE;

    // Zoom anchored under the cursor.
    double mx = event->x, my = event->y;
    double docX = (mx - st->panX) / st->zoom;
    double docY = (my - st->panY) / st->zoom;
    double newZoom = st->zoom * factor;
    // No bound existed here at all: enough consecutive scroll-down events
    // drives zoom toward 0 (and, since docX/docY above divide by st->zoom,
    // the NEXT scroll event after that divides by a near-zero value and
    // heads toward +-inf). Both ends eventually reach cairo_scale() with a
    // non-finite factor. Clamped to a generous but bounded range.
    if (std::isfinite(newZoom) && newZoom > 0.001 && newZoom < 1000.0)
        st->zoom = newZoom;
    st->panX = mx - docX * st->zoom;
    st->panY = my - docY * st->zoom;

    gtk_widget_queue_draw(widget);
    return TRUE;
}

gboolean onButtonPress(GtkWidget *, GdkEventButton *event, gpointer userData)
{
    auto *st = static_cast<DiagramState *>(userData);
    if (event->button == GDK_BUTTON_PRIMARY) {
        st->dragging = true;
        st->dragStartX = event->x;
        st->dragStartY = event->y;
        st->panStartX = st->panX;
        st->panStartY = st->panY;
    }
    return TRUE;
}

gboolean onButtonRelease(GtkWidget *, GdkEventButton *event, gpointer userData)
{
    auto *st = static_cast<DiagramState *>(userData);
    if (event->button == GDK_BUTTON_PRIMARY)
        st->dragging = false;
    return TRUE;
}

gboolean onMotion(GtkWidget *widget, GdkEventMotion *event, gpointer userData)
{
    auto *st = static_cast<DiagramState *>(userData);
    if (st->dragging) {
        st->panX = st->panStartX + (event->x - st->dragStartX);
        st->panY = st->panStartY + (event->y - st->dragStartY);
        gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

void onReload(GtkMenuItem *, gpointer userData) { executeRender(static_cast<DiagramState *>(userData)); }

void onSaveSvg(GtkMenuItem *, gpointer userData)
{
    auto *st = static_cast<DiagramState *>(userData);
    if (st->lastSvgData.empty()) return;

    GtkWidget *dlg = gtk_file_chooser_dialog_new("Save as SVG", GTK_WINDOW(gtk_widget_get_toplevel(st->root)),
        GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), "diagram.svg");
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        GError *error = nullptr;
        if (!g_file_set_contents(path, st->lastSvgData.data(), st->lastSvgData.size(), &error)) {
            showModalError(st->root, "Error", error ? error->message : "Could not open file for writing.");
            if (error) g_error_free(error);
        }
        g_free(path);
    }
    gtk_widget_destroy(dlg);
}

cairo_surface_t *renderToImageSurface(DiagramState *st)
{
    if (!st->handle) return nullptr;
    double docW, docH;
    getDocSize(st, &docW, &docH);
    int w = static_cast<int>(std::max(1.0, docW));
    int h = static_cast<int>(std::max(1.0, docH));

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(surface);
    RsvgRectangle viewport{0, 0, static_cast<double>(w), static_cast<double>(h)};
    GError *error = nullptr;
    rsvg_handle_render_document(st->handle, cr, &viewport, &error);
    if (error) g_error_free(error);
    cairo_destroy(cr);
    return surface;
}

void onSavePng(GtkMenuItem *, gpointer userData)
{
    auto *st = static_cast<DiagramState *>(userData);
    if (st->lastSvgData.empty()) return;

    GtkWidget *dlg = gtk_file_chooser_dialog_new("Save as PNG", GTK_WINDOW(gtk_widget_get_toplevel(st->root)),
        GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), "diagram.png");
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        cairo_surface_t *surface = renderToImageSurface(st);
        if (surface) {
            if (cairo_surface_write_to_png(surface, path) != CAIRO_STATUS_SUCCESS)
                showModalError(st->root, "Error", "Could not save PNG file.");
            cairo_surface_destroy(surface);
        }
        g_free(path);
    }
    gtk_widget_destroy(dlg);
}

void onCopyToClipboard(GtkMenuItem *, gpointer userData)
{
    auto *st = static_cast<DiagramState *>(userData);
    cairo_surface_t *surface = renderToImageSurface(st);
    if (!surface) return;

    int w = cairo_image_surface_get_width(surface);
    int h = cairo_image_surface_get_height(surface);
    GdkPixbuf *pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, w, h);
    cairo_surface_destroy(surface);
    if (!pixbuf) return;

    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_image(clipboard, pixbuf);
    g_object_unref(pixbuf);
}

void saveSettingsNow() { g_settings.save(g_configPath, PLUGNAME); }

void onToggleAutoReload(GtkCheckMenuItem *item, gpointer) {
    g_settings.autoReloadEnabled = gtk_check_menu_item_get_active(item);
    saveSettingsNow();
}
void onToggleSystemDark(GtkCheckMenuItem *item, gpointer userData) {
    g_settings.useSystemDarkMode = gtk_check_menu_item_get_active(item);
    saveSettingsNow();
    executeRender(static_cast<DiagramState *>(userData));
}
void onToggleForceDark(GtkCheckMenuItem *item, gpointer userData) {
    g_settings.darkMode = gtk_check_menu_item_get_active(item);
    saveSettingsNow();
    executeRender(static_cast<DiagramState *>(userData));
}
void onSetMermaidLocal(GtkMenuItem *, gpointer userData) {
    g_settings.mermaidRenderer = "local"; saveSettingsNow();
    executeRender(static_cast<DiagramState *>(userData));
}
void onSetMermaidWeb(GtkMenuItem *, gpointer userData) {
    g_settings.mermaidRenderer = "web"; saveSettingsNow();
    executeRender(static_cast<DiagramState *>(userData));
}
void onSetPumlLocal(GtkMenuItem *, gpointer userData) {
    g_settings.renderer = "java"; saveSettingsNow();
    executeRender(static_cast<DiagramState *>(userData));
}
void onSetPumlWeb(GtkMenuItem *, gpointer userData) {
    g_settings.renderer = "web"; saveSettingsNow();
    executeRender(static_cast<DiagramState *>(userData));
}

gboolean onButtonPressForMenu(GtkWidget *widget, GdkEventButton *event, gpointer userData)
{
    auto *st = static_cast<DiagramState *>(userData);
    if (event->button != GDK_BUTTON_SECONDARY)
        return FALSE;

    GtkWidget *menu = gtk_menu_new();
    auto addItem = [&](const char *label, GCallback cb) {
        GtkWidget *item = gtk_menu_item_new_with_label(label);
        g_signal_connect(item, "activate", cb, st);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        return item;
    };
    auto addCheckItem = [&](const char *label, bool active, bool enabled, GCallback cb) {
        GtkWidget *item = gtk_check_menu_item_new_with_label(label);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), active);
        gtk_widget_set_sensitive(item, enabled);
        g_signal_connect(item, "toggled", cb, st);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        return item;
    };

    addItem("Reload Diagram", G_CALLBACK(onReload));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    addItem("Save as SVG...", G_CALLBACK(onSaveSvg));
    addItem("Save as PNG...", G_CALLBACK(onSavePng));
    addItem("Copy Image to Clipboard", G_CALLBACK(onCopyToClipboard));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    addCheckItem("Auto-Reload on Save", g_settings.autoReloadEnabled, true, G_CALLBACK(onToggleAutoReload));
    addCheckItem("Use System Dark Mode", g_settings.useSystemDarkMode, true, G_CALLBACK(onToggleSystemDark));
    addCheckItem("Force Dark Mode", g_settings.darkMode, !g_settings.useSystemDarkMode, G_CALLBACK(onToggleForceDark));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    GtkWidget *mermaidSub = gtk_menu_new();
    GSList *group = nullptr;
    GtkWidget *mLocal = gtk_radio_menu_item_new_with_label(group, "Local (mmdc/npx)");
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(mLocal));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mLocal), g_settings.mermaidRenderer == "local");
    g_signal_connect(mLocal, "activate", G_CALLBACK(onSetMermaidLocal), st);
    gtk_menu_shell_append(GTK_MENU_SHELL(mermaidSub), mLocal);
    GtkWidget *mWeb = gtk_radio_menu_item_new_with_label(group, "Web (mermaid.ink)");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mWeb), g_settings.mermaidRenderer == "web");
    g_signal_connect(mWeb, "activate", G_CALLBACK(onSetMermaidWeb), st);
    gtk_menu_shell_append(GTK_MENU_SHELL(mermaidSub), mWeb);
    GtkWidget *mermaidItem = gtk_menu_item_new_with_label("Mermaid Renderer");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(mermaidItem), mermaidSub);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mermaidItem);

    GtkWidget *pumlSub = gtk_menu_new();
    GSList *pgroup = nullptr;
    GtkWidget *pLocal = gtk_radio_menu_item_new_with_label(pgroup, "Local (native/java)");
    pgroup = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(pLocal));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(pLocal), g_settings.renderer == "java");
    g_signal_connect(pLocal, "activate", G_CALLBACK(onSetPumlLocal), st);
    gtk_menu_shell_append(GTK_MENU_SHELL(pumlSub), pLocal);
    GtkWidget *pWeb = gtk_radio_menu_item_new_with_label(pgroup, "Web (plantuml.com)");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(pWeb), g_settings.renderer == "web");
    g_signal_connect(pWeb, "activate", G_CALLBACK(onSetPumlWeb), st);
    gtk_menu_shell_append(GTK_MENU_SHELL(pumlSub), pWeb);
    GtkWidget *pumlItem = gtk_menu_item_new_with_label("PlantUML Renderer");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(pumlItem), pumlSub);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), pumlItem);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
    return TRUE;
}

void destroyState(gpointer data)
{
    auto *st = static_cast<DiagramState *>(data);
    // Must be set before delete: a background render thread launched by
    // executeRender may still be running (subprocess rendering can take
    // seconds), and its completion callback checks this via a weak_ptr
    // before touching `st` at all.
    *st->alive = false;
    if (st->debounceTimerId) g_source_remove(st->debounceTimerId);
    if (st->monitor) g_object_unref(st->monitor);
    if (st->handle) g_object_unref(st->handle);
    delete st;
}

} // namespace

extern "C" {

HWND DCPCALL ListLoad(HWND ParentWin, char *FileToLoad, int ShowFlags)
try {
    dvLog("[ListLoad] ENTER file='%s' ParentWin=%p", FileToLoad ? FileToLoad : "(null)", (void *)ParentWin);
    std::string path(FileToLoad ? FileToLoad : "");
    std::string ext;
    { auto dot = path.find_last_of('.'); if (dot != std::string::npos) ext = path.substr(dot + 1); }
    for (auto &c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    if (ext != "mmd" && ext != "mermaid" && ext != "puml" && ext != "plantuml") {
        dvLog("[ListLoad] extension '%s' not ours, returning null", ext.c_str());
        return nullptr;
    }

    GtkWidget *parent = GTK_WIDGET(ParentWin);
    dvLog("[ListLoad] parent GTK_IS_LAYOUT=%d GTK_IS_WIDGET=%d",
        parent ? GTK_IS_LAYOUT(parent) : -1, parent ? GTK_IS_WIDGET(parent) : -1);
    auto *st = new DiagramState();
    st->root = gtk_event_box_new();
    st->drawingArea = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(st->root), st->drawingArea);

    gtk_widget_add_events(st->drawingArea,
        GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(st->drawingArea, "draw", G_CALLBACK(onDraw), st);
    g_signal_connect(st->drawingArea, "scroll-event", G_CALLBACK(onScroll), st);
    g_signal_connect(st->drawingArea, "button-press-event", G_CALLBACK(onButtonPress), st);
    g_signal_connect(st->drawingArea, "button-release-event", G_CALLBACK(onButtonRelease), st);
    g_signal_connect(st->drawingArea, "motion-notify-event", G_CALLBACK(onMotion), st);
    g_signal_connect(st->drawingArea, "button-press-event", G_CALLBACK(onButtonPressForMenu), st);

    // gtk_container_add() doesn't register the child the way GtkLayout
    // expects: DC's ResizeWindow later calls gtk_layout_move() on this
    // widget, which asserts the parent is exactly this GtkLayout -- only
    // gtk_layout_put() sets that up.
    gtk_layout_put(GTK_LAYOUT(parent), st->root, 0, 0);
    g_object_set_data_full(G_OBJECT(st->root), "diagram-state", st, destroyState);

    gtk_widget_show_all(st->root);
    dvLog("[ListLoad] widgets constructed, calling loadFile...");
    loadFile(st, path);

    dvLog("[ListLoad] EXIT OK, root=%p", (void *)st->root);
    return reinterpret_cast<HWND>(st->root);
} catch (const std::exception &e) {
    // Last-resort net at the extern "C" boundary itself: whatever threw,
    // it must not unwind into DC's Pascal call frame. Function-try-block
    // (the "try {" right after the signature) is required here specifically
    // so this also catches exceptions thrown while constructing DiagramState
    // or during the g_signal_connect/gtk_layout_put sequence, not just
    // inside loadFile().
    dvLog("[ListLoad] EXCEPTION escaped to top level: %s", e.what());
    return nullptr;
} catch (...) {
    dvLog("[ListLoad] UNKNOWN EXCEPTION escaped to top level");
    return nullptr;
}

void DCPCALL ListCloseWindow(HWND ListWin)
try {
    dvLog("[ListCloseWindow] ENTER ListWin=%p", (void *)ListWin);
    GtkWidget *root = GTK_WIDGET(ListWin);
    if (root) gtk_widget_destroy(root);
    dvLog("[ListCloseWindow] EXIT OK");
} catch (const std::exception &e) {
    dvLog("[ListCloseWindow] EXCEPTION: %s", e.what());
} catch (...) {
    dvLog("[ListCloseWindow] UNKNOWN EXCEPTION");
}

int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter)
try {
    dvLog("[ListSendCommand] ENTER ListWin=%p Command=%d Parameter=%d", (void *)ListWin, Command, Parameter);
    GtkWidget *root = GTK_WIDGET(ListWin);
    auto *st = root ? static_cast<DiagramState *>(g_object_get_data(G_OBJECT(root), "diagram-state")) : nullptr;
    if (!st) { dvLog("[ListSendCommand] no state for this window"); return LISTPLUGIN_ERROR; }

    if (Command == lc_newparams) {
        executeRender(st);
        return LISTPLUGIN_OK;
    }
    if (Command == lc_copy) {
        onCopyToClipboard(nullptr, st);
        return LISTPLUGIN_OK;
    }
    return LISTPLUGIN_ERROR;
} catch (const std::exception &e) {
    dvLog("[ListSendCommand] EXCEPTION: %s", e.what());
    return LISTPLUGIN_ERROR;
} catch (...) {
    dvLog("[ListSendCommand] UNKNOWN EXCEPTION");
    return LISTPLUGIN_ERROR;
}

int DCPCALL ListSearchText(HWND, char *, int)
{
    return LISTPLUGIN_ERROR;
}

void DCPCALL ListGetDetectString(char *DetectString, int maxlen)
{
    dvLog("[ListGetDetectString] ENTER maxlen=%d", maxlen);
    snprintf(DetectString, maxlen - 1, "(EXT=\"PUML\" | EXT=\"PLANTUML\" | EXT=\"MMD\" | EXT=\"MERMAID\") & SIZE<30000000");
    dvLog("[ListGetDetectString] EXIT OK");
}

void DCPCALL ListSetDefaultParams(ListDefaultParamStruct *dps)
try {
    dvLog("[ListSetDefaultParams] ENTER dps=%p", (void *)dps);
    if (!dps) { dvLog("[ListSetDefaultParams] dps is NULL"); return; }
    std::string iniName(dps->DefaultIniName);
    dvLog("[ListSetDefaultParams] DefaultIniName='%s'", iniName.c_str());
    auto slash = iniName.find_last_of('/');
    std::string dir = slash == std::string::npos ? "." : iniName.substr(0, slash);
    g_configPath = dir + "/diagramview.ini";
    dvLog("[ListSetDefaultParams] loading settings from '%s'", g_configPath.c_str());
    g_settings.loadOrInitDefaults(g_configPath, PLUGNAME);
    dvLog("[ListSetDefaultParams] EXIT OK");
} catch (const std::exception &e) {
    dvLog("[ListSetDefaultParams] EXCEPTION: %s", e.what());
} catch (...) {
    dvLog("[ListSetDefaultParams] UNKNOWN EXCEPTION");
}

} // extern "C"
