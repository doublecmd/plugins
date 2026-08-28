#ifdef BUILD_GTK_TARGET

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string.h>
#include <cstdio>
#include <sys/resource.h>
#include <fstream>
#include <sstream>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <algorithm>

#include "wlxplugin.h"
#include "../core/markdown_engine.h"

#define PLUGNAME "markdownview"

namespace {

// Root cause of the live crash (caught under gdb with full symbols against
// the real dcgtk process, reproducible only there -- never in a standalone
// harness): libstdc++'s std::regex compiler (_M_disjunction/_M_alternative/
// _M_term/_M_atom, all mutually recursive-descent) is unusually stack-hungry.
// This runs on DC's OWN GUI thread, deep inside its call chain (the
// kastoolitems.pas/kasbutton.pas/customform.inc/wincontrol.inc frames present
// in every crash report) by the time it calls our ListLoad -- so the
// remaining stack headroom at that point, not the regex compile in
// isolation, is what determines whether this overflows.
//
// A first attempt moved the rendering work onto a dedicated pthread with an
// explicit large stack, isolating it from DC's stack depth entirely. That
// backfired: constructing ANY std::regex from that freshly-spawned thread
// crashed with SIGSEGV inside std::codecvt::do_unshift, called from the
// regex compiler's locale/facet setup -- reproduced twice, for two entirely
// different regex patterns (this file's codeBlockRe and diagramview's
// foreignObjRe), both at the identical faulting instruction. That points to
// a libstdc++ locale-facet thread-safety issue specific to a brand-new
// thread being the first to touch std::locale in a process that also does
// its own C setlocale() (DC does, per its own startup log) -- not something
// a bigger stack fixes.
//
// So: stay on DC's calling thread (where locale state is already consistent
// -- every prior test, on DC's main thread, succeeded up until the original
// stack-depth crash), and instead raise THIS thread's own stack ceiling via
// setrlimit(RLIMIT_STACK). Unlike a pthread's fixed-size mmap'd stack, the
// original/main thread's stack grows on demand via page faults up to
// RLIMIT_STACK -- raising the limit (even after the thread has been
// running and using stack for a while) gives the kernel room to keep
// growing it on the NEXT fault, which is exactly what's needed here.
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

bool isSystemDark()
{
    GtkSettings *settings = gtk_settings_get_default();
    if (!settings) return false;
    gboolean preferDark = FALSE;
    g_object_get(settings, "gtk-application-prefer-dark-theme", &preferDark, nullptr);
    return preferDark;
}

std::string trim(const std::string &s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// ── Settings: minimal "[section]\nkey=value" INI, same shape/idiom as
// diagramview's Settings::loadOrInitDefaults/save (DiagramRenderer.cpp) --
// this plugin never had persisted settings at all on the GTK side (no
// ListSetDefaultParams, no theme mode, no auto-reload toggle), unlike its
// Qt6 sibling.
struct Settings {
    std::string mode = "system"; // "system", "dark", "light"
    bool autoReloadEnabled = true;
    std::string themeFilePath;
    // Persisted "Save Zoom" font-size multiplier (1.0 = no change) --
    // distinct from WebKit's own transient webkit_web_view_get/set_zoom_
    // level(), which resets on reload/reopen. "Save Zoom" bakes the
    // current transient zoom into this instead, via a body { font-size:
    // N% } CSS rule (see markdown_engine.cpp's postProcessHtml), so it
    // survives reloads.
    double zoomMultiplier = 1.0;

    void loadOrInitDefaults(const std::string &iniPath, const std::string &pluginName)
    {
        std::ifstream f(iniPath);
        std::map<std::string, std::string> values;
        if (f) {
            std::string line, section;
            while (std::getline(f, line)) {
                line = trim(line);
                if (line.empty() || line[0] == ';' || line[0] == '#') continue;
                if (line.front() == '[' && line.back() == ']') {
                    section = line.substr(1, line.size() - 2);
                    continue;
                }
                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                values[section + "/" + trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
            }
        }
        auto getBool = [&](const std::string &key, bool def) {
            auto it = values.find(pluginName + "/" + key);
            if (it == values.end()) return def;
            return it->second == "true" || it->second == "1";
        };
        auto getStr = [&](const std::string &key, const std::string &def) {
            auto it = values.find(pluginName + "/" + key);
            return it == values.end() ? def : it->second;
        };
        auto getDouble = [&](const std::string &key, double def) {
            auto it = values.find(pluginName + "/" + key);
            if (it == values.end()) return def;
            try { return std::stod(it->second); } catch (...) { return def; }
        };
        mode = getStr("mode", mode);
        autoReloadEnabled = getBool("auto_reload", autoReloadEnabled);
        themeFilePath = getStr("theme_file_path", themeFilePath);
        zoomMultiplier = getDouble("zoom_multiplier", zoomMultiplier);
        save(iniPath, pluginName);
    }

    void save(const std::string &iniPath, const std::string &pluginName) const
    {
        std::ofstream f(iniPath, std::ios::trunc);
        if (!f) return;
        f << "[" << pluginName << "]\n";
        f << "mode=" << mode << "\n";
        f << "auto_reload=" << (autoReloadEnabled ? "true" : "false") << "\n";
        f << "theme_file_path=" << themeFilePath << "\n";
        f << "zoom_multiplier=" << zoomMultiplier << "\n";
    }
};

Settings g_settings;
std::string g_configPath;

bool resolveDarkMode()
{
    if (g_settings.mode == "dark") return true;
    if (g_settings.mode == "light") return false;
    return isSystemDark();
}

struct MarkdownState {
    GtkWidget *root = nullptr;
    GtkWidget *webView = nullptr;
    // In-document incremental search (Ctrl+F), matching kpartview's
    // markdownpart backend and mirroring Qt6's find bar -- a small overlay
    // on top of the webview, backed by WebKit's own find controller rather
    // than reimplementing text search.
    GtkWidget *findBar = nullptr;
    GtkWidget *findEntry = nullptr;
    std::string filePath;

    GFileMonitor *monitor = nullptr;
    guint debounceTimerId = 0;

    // Confirmed live via a symbolized GDB backtrace: the crash from
    // reloading (whether via Theme Mode or an external edit) is genuinely
    // INSIDE webkit_web_view_load_html() itself, with entirely valid state
    // one frame up (a sane `st`, correctly-built `html`, correct
    // `baseUri`) -- not a use-after-free in this file at all. WebKit
    // apparently doesn't tolerate being asked to load new content again
    // while a previous load hasn't finished settling internally, which is
    // very plausible here: the file's *initial* load (from ListLoad) may
    // still be in progress when an external edit triggers an
    // almost-immediate reload via the file watcher. Track load-in-flight
    // and never call load_html() reentrant; re-arm the debounce instead of
    // dropping the reload request.
    bool loadInFlight = false;

    // Set false in destroyState() before delete -- reloadContent() defers
    // its actual work via g_idle_add (see below), so a reload requested
    // just before the panel closes must be able to notice `st` is gone
    // rather than touch freed memory.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

// Confirmed live via a symbolized GDB backtrace: destroyState() was invoked
// a second time with an already-freed `st` pointer (the crash was the very
// first line, dereferencing a stale `st->alive`), immediately after an
// external edit to the open file -- something re-triggers the widget's
// "destroy" signal (or CallListCloseWindow) a second time for the same
// panel before/around our own file-watcher-driven reload. Rather than
// chase the exact re-entrant trigger blind a third time, make destroyState
// itself immune to it: track live instances independently of the pointer
// GTK/DC hands back, so a stale/duplicate invocation is a no-op instead of
// a use-after-free.
std::set<MarkdownState *> g_liveStates;

void reloadContent(MarkdownState *st); // forward decl -- onLoadChanged/onReloadIdle re-arm via this

void reloadContentNow(MarkdownState *st)
{
    if (st->filePath.empty()) return;
    if (st->loadInFlight) {
        // A previous load hasn't finished settling yet -- confirmed live
        // that calling webkit_web_view_load_html() again while one is
        // still in progress crashes WebKit outright. Try again shortly
        // instead of dropping this reload request or calling in reentrant.
        // Capturing the weak_ptr up front (not just the raw `st` pointer)
        // matters here: `st` itself could be freed before this 100ms timer
        // fires, and reading `st->alive` AT THAT POINT would already be a
        // use-after-free.
        auto *ctx = new std::pair<MarkdownState *, std::weak_ptr<bool>>(st, st->alive);
        g_timeout_add(100, +[](gpointer data) -> gboolean {
            auto *ctx = static_cast<std::pair<MarkdownState *, std::weak_ptr<bool>> *>(data);
            if (ctx->second.lock()) reloadContent(ctx->first);
            delete ctx;
            return G_SOURCE_REMOVE;
        }, ctx);
        return;
    }
    st->loadInFlight = true;
    bool activeDarkMode = resolveDarkMode();
    std::string html = MarkdownEngine::renderFileToHtml(st->filePath, activeDarkMode, g_settings.themeFilePath);
    std::string autoResolvedCss = MarkdownEngine::getLastAutoResolvedCssPath();
    if (!autoResolvedCss.empty() && autoResolvedCss != g_settings.themeFilePath) {
        g_settings.themeFilePath = autoResolvedCss;
        g_settings.save(g_configPath, PLUGNAME);
    }
    std::string baseUri = "file://" + st->filePath.substr(0, st->filePath.find_last_of('/') + 1);
    webkit_web_view_load_html(WEBKIT_WEB_VIEW(st->webView), html.c_str(), baseUri.c_str());
    // Persisted "Save Zoom" applied directly via WebKit's own zoom level,
    // not baked into the HTML/CSS -- webkit_web_view_set_zoom_level() is
    // exactly the same mechanism the transient scroll-zoom (onScroll,
    // above) already uses, so this is just making that setting survive a
    // reload/reopen instead of introducing a second, CSS-based path.
    webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(st->webView), g_settings.zoomMultiplier);
}

void onLoadChanged(WebKitWebView *, WebKitLoadEvent loadEvent, gpointer userData)
{
    if (loadEvent != WEBKIT_LOAD_FINISHED) return;
    static_cast<MarkdownState *>(userData)->loadInFlight = false;
}

gboolean onReloadIdle(gpointer userData)
{
    auto *ctx = static_cast<std::pair<MarkdownState *, std::weak_ptr<bool>> *>(userData);
    if (ctx->second.lock()) reloadContentNow(ctx->first);
    delete ctx;
    return G_SOURCE_REMOVE;
}

// Every caller of this used to invoke reloadContentNow() directly and
// synchronously -- from inside a context-menu item's "activate" handler
// (Theme Mode / Reload Document / Auto-Reload toggle) or from inside the
// GFileMonitor debounce timeout. Confirmed live: changing Theme Mode
// crashed doublecmd outright, and Auto-Reload's file-watcher path crashed
// with "Source ID N was not found", a GObject-unref assertion, and a
// glibc "free(): invalid pointer" -- all symptoms of reentrancy, not of
// renderFileToHtml/webkit_web_view_load_html themselves (both are used
// identically, and safely, elsewhere). Deferring the actual reload to the
// next idle iteration -- the same technique diagramview_gtk3 already
// relies on for its own background-render completion -- guarantees this
// never runs nested inside another signal handler's call stack.
void reloadContent(MarkdownState *st)
{
    auto *ctx = new std::pair<MarkdownState *, std::weak_ptr<bool>>(st, st->alive);
    g_idle_add(onReloadIdle, ctx);
}

void saveSettingsNow() { g_settings.save(g_configPath, PLUGNAME); }

gboolean onDebounceTimeout(gpointer userData)
{
    auto *st = static_cast<MarkdownState *>(userData);
    st->debounceTimerId = 0;
    if (g_settings.autoReloadEnabled)
        reloadContent(st);
    return G_SOURCE_REMOVE;
}

void onFileChanged(GFileMonitor *, GFile *, GFile *, GFileMonitorEvent, gpointer userData)
{
    auto *st = static_cast<MarkdownState *>(userData);
    if (st->debounceTimerId) g_source_remove(st->debounceTimerId);
    st->debounceTimerId = g_timeout_add(200, onDebounceTimeout, st);
}

void startWatching(MarkdownState *st)
{
    if (st->monitor) { g_object_unref(st->monitor); st->monitor = nullptr; }
    GFile *gfile = g_file_new_for_path(st->filePath.c_str());
    GError *error = nullptr;
    st->monitor = g_file_monitor_file(gfile, G_FILE_MONITOR_NONE, nullptr, &error);
    g_object_unref(gfile);
    if (st->monitor) {
        g_signal_connect(st->monitor, "changed", G_CALLBACK(onFileChanged), st);
    } else if (error) {
        g_error_free(error);
    }
}

// ── Zoom (Ctrl+wheel) ────────────────────────────────────────────────
// Matches Qt6's MarkdownViewerWidget::wheelEvent -- GTK has no
// "zoomIn(int)" equivalent on WebKitWebView, but webkit_web_view_set_zoom_
// level()/get_zoom_level() gives the same effect directly.
gboolean onScroll(GtkWidget *widget, GdkEventScroll *event, gpointer)
{
    if (!(event->state & GDK_CONTROL_MASK)) return FALSE;
    WebKitWebView *view = WEBKIT_WEB_VIEW(widget);
    gdouble zoom = webkit_web_view_get_zoom_level(view);
    if (event->direction == GDK_SCROLL_UP) {
        zoom += 0.1;
    } else if (event->direction == GDK_SCROLL_DOWN) {
        zoom = std::max(0.2, zoom - 0.1);
    } else if (event->direction == GDK_SCROLL_SMOOTH) {
        // Confirmed live: plain scroll (no zoom) with Ctrl held did nothing
        // at all -- most mice/touchpads under a modern libinput setup
        // report GDK_SCROLL_SMOOTH with a delta_y instead of the discrete
        // GDK_SCROLL_UP/DOWN this handler originally checked exclusively
        // for, so every real wheel event fell through to `return FALSE`
        // and scrolled the page normally instead of zooming.
        gdouble dx, dy;
        if (!gdk_event_get_scroll_deltas((GdkEvent *)event, &dx, &dy) || dy == 0.0) return FALSE;
        zoom = std::max(0.2, zoom - dy * 0.1);
    } else {
        return FALSE;
    }
    webkit_web_view_set_zoom_level(view, zoom);
    return TRUE;
}

// ── In-document find bar (Ctrl+F) ────────────────────────────────────
// Backed by WebKit's own WebKitFindController rather than reimplementing
// text search -- this searches the loaded document's own text, not the
// same thing as Double Commander's own Ctrl+F (that's DC's lister search
// UI driving ListSearchText below).
void showFindBar(MarkdownState *st) {
    gtk_widget_show(st->findBar);
    gtk_widget_grab_focus(st->findEntry);
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(st->findEntry));
    if (text && *text) {
        gtk_editable_select_region(GTK_EDITABLE(st->findEntry), 0, -1);
    }
}
void hideFindBar(MarkdownState *st) {
    gtk_widget_hide(st->findBar);
    webkit_find_controller_search_finish(webkit_web_view_get_find_controller(WEBKIT_WEB_VIEW(st->webView)));
    gtk_widget_grab_focus(st->webView);
}
void doFind(MarkdownState *st, bool backward) {
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(st->findEntry));
    if (!text || !*text) return;
    WebKitFindController *fc = webkit_web_view_get_find_controller(WEBKIT_WEB_VIEW(st->webView));
    guint32 options = WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE | WEBKIT_FIND_OPTIONS_WRAP_AROUND;
    if (backward) options |= WEBKIT_FIND_OPTIONS_BACKWARDS;
    webkit_find_controller_search(fc, text, options, G_MAXUINT);
}
gboolean onFindKeyPress(GtkWidget *, GdkEventKey *event, gpointer userData) {
    auto *st = static_cast<MarkdownState *>(userData);
    if (event->keyval == GDK_KEY_Escape) { hideFindBar(st); return TRUE; }
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        doFind(st, event->state & GDK_SHIFT_MASK);
        return TRUE;
    }
    return FALSE;
}
gboolean onWebViewKeyPress(GtkWidget *, GdkEventKey *event, gpointer userData) {
    auto *st = static_cast<MarkdownState *>(userData);
    if ((event->state & GDK_CONTROL_MASK) && (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F)) {
        showFindBar(st);
        return TRUE;
    }
    return FALSE;
}
GtkWidget *buildFindBar(MarkdownState *st) {
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(bar, GTK_ALIGN_END);
    gtk_widget_set_valign(bar, GTK_ALIGN_START);
    gtk_widget_set_margin_top(bar, 8);
    gtk_widget_set_margin_end(bar, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(bar), "background");

    st->findEntry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(st->findEntry), "Find in document...");
    gtk_widget_set_size_request(st->findEntry, 200, -1);
    g_signal_connect(st->findEntry, "search-changed", G_CALLBACK(+[](GtkSearchEntry *, gpointer userData) {
        doFind(static_cast<MarkdownState *>(userData), false);
    }), st);
    g_signal_connect(st->findEntry, "key-press-event", G_CALLBACK(onFindKeyPress), st);

    GtkWidget *prevBtn = gtk_button_new_with_label("Prev");
    GtkWidget *nextBtn = gtk_button_new_with_label("Next");
    GtkWidget *closeBtn = gtk_button_new_with_label("✕");
    g_signal_connect(prevBtn, "clicked", G_CALLBACK(+[](GtkButton *, gpointer userData) {
        doFind(static_cast<MarkdownState *>(userData), true);
    }), st);
    g_signal_connect(nextBtn, "clicked", G_CALLBACK(+[](GtkButton *, gpointer userData) {
        doFind(static_cast<MarkdownState *>(userData), false);
    }), st);
    g_signal_connect(closeBtn, "clicked", G_CALLBACK(+[](GtkButton *, gpointer userData) {
        hideFindBar(static_cast<MarkdownState *>(userData));
    }), st);

    gtk_box_pack_start(GTK_BOX(bar), st->findEntry, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(bar), prevBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), nextBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), closeBtn, FALSE, FALSE, 0);
    return bar;
}

// ── Right-click context menu ─────────────────────────────────────────
// Matches Qt6's MarkdownViewerWidget::contextMenuEvent item-for-item:
// Copy Text, Select All, separator, Reload Document, Auto-Reload on Save
// (checkable), separator, Theme Mode submenu (System/Dark/Light,
// checkable group). GTK previously had NO custom context menu at all --
// right-click fell through to WebKitWebView's own default browser menu
// (Back/Forward/Reload page/Inspect Element/etc), exposing none of this.
void onCopyText(GtkMenuItem *, gpointer userData) {
    auto *st = static_cast<MarkdownState *>(userData);
    webkit_web_view_execute_editing_command(WEBKIT_WEB_VIEW(st->webView), WEBKIT_EDITING_COMMAND_COPY);
}
void onSelectAll(GtkMenuItem *, gpointer userData) {
    auto *st = static_cast<MarkdownState *>(userData);
    webkit_web_view_execute_editing_command(WEBKIT_WEB_VIEW(st->webView), WEBKIT_EDITING_COMMAND_SELECT_ALL);
}
// Prints the page WebKit already has loaded -- i.e. whatever theme/CSS
// reloadContent() most recently rendered into it (dark or light, default or
// custom), same as the Qt6 side prints its already-rendered QTextDocument.
void onPrint(GtkMenuItem *, gpointer userData) {
    auto *st = static_cast<MarkdownState *>(userData);
    WebKitPrintOperation *op = webkit_print_operation_new(WEBKIT_WEB_VIEW(st->webView));
    webkit_print_operation_run_dialog(op, nullptr);
    g_object_unref(op);
}
// Persists WebKit's current transient zoom level into
// g_settings.zoomMultiplier, then clears the transient zoom back to 1.0
// and reloads -- the visual size stays exactly the same, but it's now in
// g_settings.zoomMultiplier and survives a reload/reopen (see the
// webkit_web_view_set_zoom_level() call at the end of reloadContentNow()),
// unlike a transient webkit_web_view_set_zoom_level() call alone.
void onSaveZoom(GtkMenuItem *, gpointer userData) {
    auto *st = static_cast<MarkdownState *>(userData);
    gdouble zoom = webkit_web_view_get_zoom_level(WEBKIT_WEB_VIEW(st->webView));
    if (zoom == 1.0) return;
    g_settings.zoomMultiplier *= zoom;
    if (g_settings.zoomMultiplier < 0.1) g_settings.zoomMultiplier = 0.1;
    webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(st->webView), 1.0);
    saveSettingsNow();
    reloadContent(st);
}
// Back to the factory default size -- clears BOTH the transient in-view
// zoom and any persisted "Save Zoom" multiplier, unlike onSaveZoom() which
// folds the transient zoom into the persisted one.
void onResetZoom(GtkMenuItem *, gpointer userData) {
    auto *st = static_cast<MarkdownState *>(userData);
    webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(st->webView), 1.0);
    if (g_settings.zoomMultiplier != 1.0) {
        g_settings.zoomMultiplier = 1.0;
        saveSettingsNow();
        reloadContent(st);
    }
}
void onReloadDocument(GtkMenuItem *, gpointer userData) { reloadContent(static_cast<MarkdownState *>(userData)); }
void onToggleAutoReload(GtkCheckMenuItem *item, gpointer) {
    g_settings.autoReloadEnabled = gtk_check_menu_item_get_active(item);
    saveSettingsNow();
}
void onSetModeSystem(GtkMenuItem *, gpointer userData) {
    g_settings.mode = "system"; saveSettingsNow(); reloadContent(static_cast<MarkdownState *>(userData));
}
void onSetModeDark(GtkMenuItem *, gpointer userData) {
    g_settings.mode = "dark"; saveSettingsNow(); reloadContent(static_cast<MarkdownState *>(userData));
}
void onSetModeLight(GtkMenuItem *, gpointer userData) {
    g_settings.mode = "light"; saveSettingsNow(); reloadContent(static_cast<MarkdownState *>(userData));
}

gboolean onContextMenu(WebKitWebView *, WebKitContextMenu *, GdkEvent *event, WebKitHitTestResult *, gpointer userData)
{
    auto *st = static_cast<MarkdownState *>(userData);

    GtkWidget *menu = gtk_menu_new();
    auto addItem = [&](const char *label, GCallback cb) {
        GtkWidget *item = gtk_menu_item_new_with_label(label);
        g_signal_connect(item, "activate", cb, st);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        return item;
    };

    addItem("Copy Text", G_CALLBACK(onCopyText));
    addItem("Select All", G_CALLBACK(onSelectAll));
    addItem("Find in Document...", G_CALLBACK(+[](GtkMenuItem *, gpointer userData) {
        showFindBar(static_cast<MarkdownState *>(userData));
    }));
    addItem("Print...", G_CALLBACK(onPrint));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    addItem("Save Zoom", G_CALLBACK(onSaveZoom));
    addItem("Reset Zoom", G_CALLBACK(onResetZoom));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    addItem("Reload Document", G_CALLBACK(onReloadDocument));

    GtkWidget *autoItem = gtk_check_menu_item_new_with_label("Auto-Reload on Save");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(autoItem), g_settings.autoReloadEnabled);
    g_signal_connect(autoItem, "toggled", G_CALLBACK(onToggleAutoReload), st);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), autoItem);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    // Reported live: merely OPENING this submenu (no click at all) could
    // silently flip the active mode back to whatever made it render dark.
    // Cause: a fresh GtkRadioMenuItem group's first member defaults to
    // active=TRUE, and gtk_check_menu_item_set_active() during setup can
    // trigger "activate" as a side effect of another member in the same
    // group being force-deactivated to satisfy the group's
    // exactly-one-active invariant -- connecting each item's handler
    // before the LAST item's set_active() call runs means an earlier
    // item's real user-facing callback can fire from pure construction,
    // not a click. Building unconnected first, THEN setting all three
    // initial states, THEN connecting handlers guarantees no callback
    // ever fires except from a genuine user activation.
    GtkWidget *modeSub = gtk_menu_new();
    GSList *group = nullptr;
    GtkWidget *mSystem = gtk_radio_menu_item_new_with_label(group, "System");
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(mSystem));
    gtk_menu_shell_append(GTK_MENU_SHELL(modeSub), mSystem);
    GtkWidget *mDark = gtk_radio_menu_item_new_with_label(group, "Dark");
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(mDark));
    gtk_menu_shell_append(GTK_MENU_SHELL(modeSub), mDark);
    GtkWidget *mLight = gtk_radio_menu_item_new_with_label(group, "Light");
    gtk_menu_shell_append(GTK_MENU_SHELL(modeSub), mLight);

    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mSystem), g_settings.mode == "system");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mDark), g_settings.mode == "dark");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mLight), g_settings.mode == "light");

    g_signal_connect(mSystem, "activate", G_CALLBACK(onSetModeSystem), st);
    g_signal_connect(mDark, "activate", G_CALLBACK(onSetModeDark), st);
    g_signal_connect(mLight, "activate", G_CALLBACK(onSetModeLight), st);
    GtkWidget *modeItem = gtk_menu_item_new_with_label("Theme Mode");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(modeItem), modeSub);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), modeItem);

    gtk_widget_show_all(menu);
    // Passing NULL here (instead of the WebKit-supplied `event`) as a
    // defensive lifetime precaution was WRONG -- confirmed live: doing so
    // crashed doublecmd on every right-click, immediately, before any menu
    // item was ever clicked. gtk_menu_popup_at_pointer(menu, NULL) falls
    // back to gtk_get_current_event(), which apparently isn't in a usable
    // state when called from inside WebKit's own "context-menu" signal
    // (unlike a plain native GTK signal's callback). The original event
    // reuse never actually caused a problem -- the earlier crash reports
    // were from reloadContent() running synchronously inside a menu-item
    // click, fixed separately via the g_idle_add deferral above.
    gtk_menu_popup_at_pointer(GTK_MENU(menu), event);
    return TRUE; // suppress WebKit's own default context menu
}

void destroyState(gpointer data)
{
    auto *st = static_cast<MarkdownState *>(data);
    if (g_liveStates.find(st) == g_liveStates.end())
        return; // stale/duplicate "destroy" for a panel already torn down
    g_liveStates.erase(st);
    *st->alive = false; // must be set before delete -- see reloadContent()'s comment
    if (st->debounceTimerId) g_source_remove(st->debounceTimerId);
    if (st->monitor) g_object_unref(st->monitor);
    delete st;
}

} // namespace

extern "C" {

HWND DCPCALL ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags)
try {
    // Function-try-block: this had NO exception handling at all, unlike
    // diagramview's ListLoad. renderFileToHtml() runs md4c parsing,
    // MicroTeX LaTeX rendering, and diagram (mermaid/plantuml) rendering
    // synchronously right here -- any C++ exception thrown anywhere in that
    // chain (a std::bad_alloc from a pathological allocation, a std::regex
    // throw, anything) would unwind straight across this extern "C"
    // boundary into DC's Pascal caller, which is undefined behavior. Same
    // fix already applied to diagramview_gtk3's ListLoad for the same
    // reason.
    GtkWidget *parent = GTK_WIDGET(ParentWin);
    GtkWidget *scrolledWin = gtk_scrolled_window_new(NULL, NULL);
    // GTK_CONTAINER(parent)->add() doesn't register the child the way
    // GtkLayout expects: DC's own ResizeWindow (uwlxmodule.pas) later calls
    // gtk_layout_move() on this widget, which asserts the widget's parent
    // is exactly this GtkLayout -- only gtk_layout_put() sets that up.
    gtk_layout_put(GTK_LAYOUT(parent), scrolledWin, 0, 0);

    auto *st = new MarkdownState();
    g_liveStates.insert(st);
    st->root = scrolledWin;
    st->filePath = FileToLoad ? FileToLoad : "";
    st->webView = webkit_web_view_new();
    gtk_widget_set_name(st->webView, "markdown_webview");

    gtk_widget_add_events(st->webView, GDK_SCROLL_MASK);
    g_signal_connect(st->webView, "scroll-event", G_CALLBACK(onScroll), st);
    g_signal_connect(st->webView, "context-menu", G_CALLBACK(onContextMenu), st);
    g_signal_connect(st->webView, "load-changed", G_CALLBACK(onLoadChanged), st);
    g_signal_connect(st->webView, "key-press-event", G_CALLBACK(onWebViewKeyPress), st);

    ensureLargeStackLimit();
    reloadContent(st);
    startWatching(st);

    // GtkOverlay lets the find bar float on top of the webview without
    // restructuring the returned HWND -- scrolledWin (still the top-level
    // widget DC gets back) -> overlay -> webView, with the find bar as the
    // overlay's floating child.
    GtkWidget *overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(overlay), st->webView);
    st->findBar = buildFindBar(st);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), st->findBar);
    gtk_container_add(GTK_CONTAINER(scrolledWin), overlay);
    gtk_widget_show_all(scrolledWin);
    gtk_widget_hide(st->findBar);

    g_signal_connect(scrolledWin, "destroy", G_CALLBACK(destroyState), st);
    g_object_set_data(G_OBJECT(scrolledWin), "__markdownview_state_ptr", st);

    return (HWND)scrolledWin;
} catch (const std::exception &e) {
    fprintf(stderr, "[markdownview_gtk3] ListLoad EXCEPTION: %s\n", e.what());
    return nullptr;
} catch (...) {
    fprintf(stderr, "[markdownview_gtk3] ListLoad UNKNOWN EXCEPTION\n");
    return nullptr;
}

void DCPCALL ListCloseWindow(HWND ListWin)
{
    if (ListWin) {
        gtk_widget_destroy(GTK_WIDGET(ListWin));
    }
}

void DCPCALL ListGetDetectString(char* DetectString, int maxlen)
{
    snprintf(DetectString, maxlen - 1, "(EXT=\"MD\" | EXT=\"MARKDOWN\" | EXT=\"MDOWN\" | EXT=\"MKD\") & SIZE<30000000");
}

int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter)
{
    if (!ListWin) return LISTPLUGIN_ERROR;
    auto *st = static_cast<MarkdownState *>(g_object_get_data(G_OBJECT(ListWin), "__markdownview_state_ptr"));

    switch (Command) {
    case lc_copy:
        if (st) webkit_web_view_execute_editing_command(WEBKIT_WEB_VIEW(st->webView), WEBKIT_EDITING_COMMAND_COPY);
        return LISTPLUGIN_OK;
    case lc_selectall:
        if (st) webkit_web_view_execute_editing_command(WEBKIT_WEB_VIEW(st->webView), WEBKIT_EDITING_COMMAND_SELECT_ALL);
        return LISTPLUGIN_OK;
    case lc_newparams:
        if (st) reloadContent(st);
        return LISTPLUGIN_OK;
    default:
        return LISTPLUGIN_ERROR;
    }
}

void DCPCALL ListSetDefaultParams(ListDefaultParamStruct *dps)
{
    if (!dps) return;
    std::string iniName(dps->DefaultIniName);
    auto slash = iniName.find_last_of('/');
    std::string dir = slash == std::string::npos ? "." : iniName.substr(0, slash);
    g_configPath = dir + "/markdownview.ini";
    g_settings.loadOrInitDefaults(g_configPath, PLUGNAME);
    MarkdownEngine::setPluginConfigDir(dir);
}

} // extern "C"

#endif // BUILD_GTK_TARGET
