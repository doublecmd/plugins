/*
 * Log Viewer WLX plugin — GTK3 UI layer.
 *
 * Built on LogEngine (src/core/) and LogTreeModel (this directory) — a
 * custom virtual GtkTreeModel preserving the mmap-based scalability that
 * is the entire point of LogEngine (materializing a huge log file into a
 * GtkListStore would defeat it). Feature set mirrors LogViewerWidget.cpp
 * (Qt6): regex search + filter mode, time-range filter, follow/tail,
 * copy/delete/extract selected lines, and a highlight-rules settings
 * dialog with color pickers.
 */

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <re2/re2.h>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <functional>

#include "wlxplugin.h"
#include "LogEngine.h"
#include "LogTreeModel.h"

namespace {

// Populated by ListSetDefaultParams from DC's DefaultIniName -- previously
// this plugin ignored that struct entirely and hardcoded its own path
// under ~/.config/doublecmd/plugins/wlx/, unlike every other WLX plugin in
// this repo that derives its ini path from the directory DC actually hands
// it.
std::string g_configPath;

// ── Time-range spinners ─────────────────────────────────────────────
//
// Qt's time slicer uses QDateTimeEdit (a single field with per-segment spin
// controls, live-updating on every keystroke/spin click via
// dateTimeChanged). GTK has no equivalent widget, so build the closest
// match: six GtkSpinButtons (Y/M/D/H/Min/Sec), each wired to
// "value-changed" so the filter updates live like Qt's does, instead of
// GTK's previous plain GtkEntry fields that only re-filtered on Enter.

constexpr int kDtSpinCount = 6; // year, month, day, hour, minute, second

// Zero-pads the displayed value ("3" -> "03") -- without this, a date read
// back as "2026-8-5 9:3:0", which barely parses as a date/time at a glance
// and was reported as "hard to understand". GtkSpinButton's "output" signal
// is the documented hook for overriding the default (unpadded) text.
gboolean spinOutputZeroPad(GtkSpinButton *spin, gpointer data)
{
    int digits = GPOINTER_TO_INT(data);
    int value = gtk_spin_button_get_value_as_int(spin);
    gchar *text = g_strdup_printf("%0*d", digits, value);
    if (g_strcmp0(gtk_entry_get_text(GTK_ENTRY(spin)), text) != 0)
        gtk_entry_set_text(GTK_ENTRY(spin), text);
    g_free(text);
    return TRUE;
}

GtkWidget *buildDateTimeSpinner(GtkWidget *spin[kDtSpinCount])
{
    // Each spinner keeps its own default entry frame -- GTK3 themes render
    // a GtkSpinButton's up/down arrows as part of that frame's own styling,
    // so stripping it via gtk_entry_set_has_frame(FALSE) (an earlier
    // attempt to visually group these into one bordered control, like Qt's
    // QDateTimeEdit) broke the arrows' rendering in practice: reported as
    // "can't reduce the value" and "big buttons side by side". A plain
    // horizontal box with small spacing, no enclosing GtkFrame, keeps every
    // spinner's normal, known-good rendering.
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

    // The theme's default vertical-spinbutton steppers render oversized, and
    // the "down" one draws as an X rather than a minus.
    //
    // Attempt 1: -gtk-icontheme("list-add-symbolic")/("list-remove-symbolic").
    // Looked like a missing-icon fallback glyph, but is not: confirmed live
    // that the active theme (breeze-dark) genuinely HAS an icon named
    // "list-remove-symbolic" and gtk_icon_theme_lookup_icon() resolves it
    // successfully -- its path data just draws an X-shaped glyph (breeze's
    // own design choice for that icon name), not a minus. Wrong icon
    // *concept* for what we want, not a broken lookup.
    //
    // Attempt 2: url("data:image/svg+xml,...") to sidestep icon-theme
    // lookup entirely. Failed for an unrelated reason: GTK's CSS url()
    // resolves through GIO's GFile, and confirmed live in this environment
    // that GIO has no "data" URI scheme handler at all
    // (Gio.File.new_for_uri("data:...") comes back a GDummyFile that always
    // reports g-io-error "Operation not supported") -- unrelated to gvfsd
    // being installed/running (it is); GIO simply never registers a data:
    // backend.
    //
    // Attempt 3: prepend a private directory (via
    // gtk_icon_theme_prepend_search_path) holding our own
    // "list-add/remove-symbolic.svg", keeping those same names. Also
    // failed, for the same reason attempt 1 did: a *search path* only
    // supplies icons the active theme doesn't already have -- since
    // breeze-dark already has both names (drawing its own +/X glyphs), our
    // private files were never even consulted.
    //
    // Fix: the "up"/plus glyph was never actually broken -- breeze-dark's
    // own "list-add-symbolic" is a perfectly good plus and was already
    // rendering correctly before any of this, so leave it alone. Only
    // "down" needs an icon name that cannot collide with any theme's own
    // icon set ("logview-spin-minus-symbolic" -- no theme ships this), so
    // the private search path is the only place it can ever resolve from,
    // guaranteeing our own glyph regardless of theme.
    static GtkCssProvider *spinCss = nullptr;
    if (!spinCss) {
        const char *cacheDir = g_get_user_cache_dir();
        // Flat directory, NOT a themed "16x16/actions" subtree -- confirmed
        // live that gtk_icon_theme_prepend_search_path()'s fallback
        // "unthemed" scan (no index.theme present) only finds icons placed
        // directly in the search-path directory itself; a size/context
        // subdirectory structure (the normal on-disk layout for an actual
        // installed icon theme) returned NULL from
        // gtk_icon_theme_lookup_icon() even though the file was right
        // there on disk.
        std::string iconDir = std::string(cacheDir) + "/doublecmd-logview-icons";
        g_mkdir_with_parents(iconDir.c_str(), 0755);
        std::string path = iconDir + "/logview-spin-minus-symbolic.svg";
        std::ofstream f(path, std::ios::trunc);
        f << "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path d='M2 8h12' stroke='#000' stroke-width='2' stroke-linecap='round'/></svg>";
        f.close();
        gtk_icon_theme_prepend_search_path(gtk_icon_theme_get_default(), iconDir.c_str());

        spinCss = gtk_css_provider_new();
        gtk_css_provider_load_from_data(spinCss,
            "spinbutton.vertical button {"
            "  min-height: 10px; min-width: 10px; padding: 0px;"
            "}"
            "spinbutton.vertical button.up {"
            "  -gtk-icon-source: -gtk-icontheme(\"list-add-symbolic\");"
            "  -gtk-icon-transform: scale(0.65);"
            "}"
            "spinbutton.vertical button.down {"
            "  -gtk-icon-source: -gtk-icontheme(\"logview-spin-minus-symbolic\");"
            "  -gtk-icon-transform: scale(0.65);"
            "}", -1, nullptr);
    }

    struct Field { int lo, hi, width, digits; const char *sep; const char *tooltip; };
    static const Field fields[kDtSpinCount] = {
        {1970, 9999, 5, 4, "-", "Year"},   {1, 12, 3, 2, "-", "Month"}, {1, 31, 3, 2, " ", "Day"},
        {0, 23, 3, 2, ":", "Hour"}, {0, 59, 3, 2, ":", "Minute"}, {0, 59, 3, 2, "", "Second"},
    };
    for (int i = 0; i < kDtSpinCount; ++i) {
        spin[i] = gtk_spin_button_new_with_range(fields[i].lo, fields[i].hi, 1);
        // GtkSpinButton implements GtkOrientable, and defaults to HORIZONTAL --
        // which Adwaita draws as [entry][-][+] with the two step buttons side
        // by side. That reads as a plain text box flanked by unrelated buttons
        // rather than a spinner at all (reported twice now: "big buttons side
        // by side", and "2026 x +" / "this is not a spinner"). VERTICAL stacks
        // them into the conventional up-over-down arrow pair, which is what
        // Qt's QDateTimeEdit shows and what a spinner is expected to look like.
        gtk_orientable_set_orientation(GTK_ORIENTABLE(spin[i]), GTK_ORIENTATION_VERTICAL);
        gtk_style_context_add_provider(gtk_widget_get_style_context(spin[i]),
            GTK_STYLE_PROVIDER(spinCss), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin[i]), TRUE);
        gtk_entry_set_width_chars(GTK_ENTRY(spin[i]), fields[i].width);
        gtk_entry_set_alignment(GTK_ENTRY(spin[i]), 0.5f);
        gtk_widget_set_tooltip_text(spin[i], fields[i].tooltip);
        g_signal_connect(spin[i], "output", G_CALLBACK(spinOutputZeroPad), GINT_TO_POINTER(fields[i].digits));
        gtk_box_pack_start(GTK_BOX(box), spin[i], FALSE, FALSE, 0);
        if (*fields[i].sep)
            gtk_box_pack_start(GTK_BOX(box), gtk_label_new(fields[i].sep), FALSE, FALSE, 0);
    }
    return box;
}

LogTimestamp readTimestampFromSpinner(GtkWidget *const spin[kDtSpinCount])
{
    auto v = [&](int i) { return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin[i])); };
    return LogTimestamp{v(0), v(1), v(2), v(3), v(4), v(5), true};
}

void writeTimestampToSpinner(GtkWidget *const spin[kDtSpinCount], const LogTimestamp &ts)
{
    if (!ts.valid) return;
    int values[kDtSpinCount] = {ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second};
    for (int i = 0; i < kDtSpinCount; ++i)
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin[i]), values[i]);
}

bool tsLess(const LogTimestamp &a, const LogTimestamp &b)
{
    if (a.year != b.year) return a.year < b.year;
    if (a.month != b.month) return a.month < b.month;
    if (a.day != b.day) return a.day < b.day;
    if (a.hour != b.hour) return a.hour < b.hour;
    if (a.minute != b.minute) return a.minute < b.minute;
    return a.second < b.second;
}

struct LogViewerState {
    GtkWidget *root = nullptr;
    GtkWidget *treeView = nullptr;
    GtkWidget *searchEntry = nullptr;
    GtkWidget *btnSearchStop = nullptr;
    GtkWidget *timeStartSpin[kDtSpinCount] = {};
    GtkWidget *timeEndSpin[kDtSpinCount] = {};
    GtkWidget *chkFollow = nullptr;
    GtkWidget *chkFilterMode = nullptr;
    GtkWidget *statusLabel = nullptr;

    std::unique_ptr<LogEngine> engine;
    LogTreeModel *model = nullptr;
    std::vector<int> activeFilter; // engine row indices currently shown, when filtering is active
    bool filteringActive = false;
    bool timeFilterActive = false;

    std::string currentFile;
    std::string lastSearchQuery;
    int lastMatchRow = -1;

    std::vector<EngineHighlightRule> rules;
    std::string iniPath;

    // Source ID of the once-a-second Follow-mode poll (see its
    // g_timeout_add call below). MUST be removed in destroyState() before
    // this struct is freed -- g_timeout_add's return value was previously
    // discarded entirely, so the timer kept firing every second after
    // ListCloseWindow destroyed this state, dereferencing freed memory on
    // its next tick. That's what crashed on close: not always instantly
    // (freed memory can look briefly valid), which is why it read as
    // unpredictable.
    guint followTimerId = 0;
};

// Forward decls
void refreshFilter(LogViewerState *st);
void executeSearch(LogViewerState *st, bool jumpToNext);
void loadHighlightRules(LogViewerState *st);
void saveHighlightRules(LogViewerState *st);
void rebuildEngineHighlightRules(LogViewerState *st);

// ── Model row <-> tree path helpers ─────────────────────────────────

int viewSourceRowAtPath(LogViewerState *st, GtkTreePath *path)
{
    if (gtk_tree_path_get_depth(path) != 1) return -1;
    int modelRow = gtk_tree_path_get_indices(path)[0];
    return log_tree_model_to_engine_row(st->model, modelRow);
}

void scrollToSourceRow(LogViewerState *st, int sourceRow)
{
    int modelRow = sourceRow;
    if (st->model->filter) {
        auto it = std::find(st->activeFilter.begin(), st->activeFilter.end(), sourceRow);
        if (it == st->activeFilter.end()) return;
        modelRow = (int)std::distance(st->activeFilter.begin(), it);
    }
    GtkTreePath *path = gtk_tree_path_new_from_indices(modelRow, -1);
    gtk_tree_view_set_cursor(GTK_TREE_VIEW(st->treeView), path, nullptr, FALSE);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(st->treeView), path, nullptr, TRUE, 0.5, 0.0);
    gtk_tree_path_free(path);
}

void scrollToBottom(LogViewerState *st)
{
    int n = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(st->model), nullptr);
    if (n == 0) return;
    GtkTreePath *path = gtk_tree_path_new_from_indices(n - 1, -1);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(st->treeView), path, nullptr, TRUE, 1.0, 0.0);
    gtk_tree_path_free(path);
}

std::vector<int> selectedSourceRows(LogViewerState *st)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(st->treeView));
    GList *rows = gtk_tree_selection_get_selected_rows(sel, nullptr);
    std::vector<int> result;
    for (GList *l = rows; l; l = l->next) {
        int src = viewSourceRowAtPath(st, static_cast<GtkTreePath *>(l->data));
        if (src >= 0) result.push_back(src);
    }
    g_list_free_full(rows, (GDestroyNotify)gtk_tree_path_free);
    std::sort(result.begin(), result.end());
    return result;
}

void setStatus(LogViewerState *st, const std::string &text)
{
    gtk_label_set_text(GTK_LABEL(st->statusLabel), text.c_str());
}

// ── Filtering (regex filter-mode AND/OR time range, intersected) ───

void refreshFilter(LogViewerState *st)
{
    if (!st->filteringActive && !st->timeFilterActive) {
        log_tree_model_set_filter(st->model, nullptr);
        return;
    }

    LogTimestamp startTs = readTimestampFromSpinner(st->timeStartSpin);
    LogTimestamp endTs = readTimestampFromSpinner(st->timeEndSpin);
    bool haveRange = st->timeFilterActive;

    st->activeFilter.clear();
    int total = st->engine->lineCount();
    for (int r = 0; r < total; ++r) {
        if (st->filteringActive && !st->engine->isMatch(r))
            continue;
        if (haveRange) {
            LogTimestamp rowTs = st->engine->getInterpolatedTimestamp(r);
            if (!rowTs.valid || tsLess(rowTs, startTs) || tsLess(endTs, rowTs))
                continue;
        }
        st->activeFilter.push_back(r);
    }
    log_tree_model_set_filter(st->model, &st->activeFilter);
}

// ── Search ───────────────────────────────────────────────────────────

void executeSearch(LogViewerState *st, bool jumpToNext)
{
    std::string query = gtk_entry_get_text(GTK_ENTRY(st->searchEntry));

    if (query != st->lastSearchQuery) {
        st->lastMatchRow = -1;
        st->lastSearchQuery = query;

        if (query.empty()) {
            gtk_widget_set_sensitive(st->btnSearchStop, FALSE);
            st->engine->startSearch("", [](int) {});
            if (st->filteringActive) refreshFilter(st);
            return;
        }

        gtk_widget_set_sensitive(st->btnSearchStop, TRUE);
        setStatus(st, "Searching...");
        st->engine->startSearch(query, [st](int matchCount) {
            // LogEngine invokes this from its own search thread — GTK is
            // not thread-safe, so marshal onto the main loop via
            // g_idle_add before touching any widgets.
            struct Ctx { LogViewerState *st; int matchCount; };
            auto *ctx = new Ctx{st, matchCount};
            g_idle_add(+[](gpointer data) -> gboolean {
                auto *c = static_cast<Ctx *>(data);
                LogViewerState *st = c->st;
                int matchCount = c->matchCount;
                delete c;

                gtk_widget_set_sensitive(st->btnSearchStop, FALSE);
                if (matchCount < 0) {
                    setStatus(st, "Invalid regex pattern");
                    st->lastSearchQuery.clear();
                    return G_SOURCE_REMOVE;
                }
                setStatus(st, "Matches: " + std::to_string(matchCount) + " / " +
                              std::to_string(st->engine->lineCount()) + " lines");
                if (matchCount > 0) {
                    int first = st->engine->nextMatch(-1);
                    if (first >= 0) {
                        st->lastMatchRow = first;
                        scrollToSourceRow(st, first);
                    }
                }
                if (st->filteringActive) refreshFilter(st);
                return G_SOURCE_REMOVE;
            }, ctx);
        });
        return;
    }

    if (query.empty() || !jumpToNext) return;

    if (st->engine->matchCount() > 0) {
        int next = st->engine->nextMatch(st->lastMatchRow);
        if (next >= 0) {
            st->lastMatchRow = next;
            scrollToSourceRow(st, next);
            setStatus(st, "Match at line " + std::to_string(next + 1) + " | " +
                          std::to_string(st->engine->matchCount()) + " total");
        }
    }
}

// ── Cell rendering: apply LogEngine's per-row colors ────────────────

void cellDataFunc(GtkTreeViewColumn *, GtkCellRenderer *cell, GtkTreeModel *treeModel, GtkTreeIter *iter, gpointer userData)
{
    auto *st = static_cast<LogViewerState *>(userData);
    // Same NULL-return hazard as the settings dialog's cell data func: GTK
    // hands us stale iters while rebuilding/tearing down the view, and both
    // of these return NULL for one. This func runs on every redraw of the
    // main log view -- including the one rebuildEngineHighlightRules()
    // explicitly queues after the settings dialog closes.
    GtkTreePath *path = gtk_tree_model_get_path(treeModel, iter);
    if (!path) return;
    const int *indices = gtk_tree_path_get_indices(path);
    if (!indices || gtk_tree_path_get_depth(path) < 1) {
        gtk_tree_path_free(path);
        return;
    }
    int modelRow = indices[0];
    gtk_tree_path_free(path);
    int engineRow = log_tree_model_to_engine_row(st->model, modelRow);

    LogColor bg, fg;
    st->engine->colorsForRow(engineRow, bg, fg);

    if (bg.valid) {
        GdkRGBA rgba{bg.r / 255.0, bg.g / 255.0, bg.b / 255.0, 1.0};
        g_object_set(cell, "background-rgba", &rgba, nullptr);
    } else {
        g_object_set(cell, "background-set", FALSE, nullptr);
    }
    if (fg.valid) {
        GdkRGBA rgba{fg.r / 255.0, fg.g / 255.0, fg.b / 255.0, 1.0};
        g_object_set(cell, "foreground-rgba", &rgba, nullptr);
    } else {
        g_object_set(cell, "foreground-set", FALSE, nullptr);
    }
}

// ── Highlight rules settings dialog ─────────────────────────────────

struct RuleEditResult { bool accepted = false; std::string pattern; GdkRGBA fg{1,1,1,1}; GdkRGBA bg{0,0,0,1}; };

RuleEditResult runRuleDialog(GtkWindow *parent, const std::string &initialPattern, GdkRGBA fg, GdkRGBA bg)
{
    RuleEditResult result;
    GtkWidget *dlg = gtk_dialog_new_with_buttons("Edit Highlight Rule", parent, GTK_DIALOG_MODAL,
        "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, nullptr);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 8);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *patternRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(patternRow), gtk_label_new("Regex Pattern:"), FALSE, FALSE, 0);
    GtkWidget *patternEntry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(patternEntry), initialPattern.c_str());
    gtk_widget_set_hexpand(patternEntry, TRUE);
    gtk_box_pack_start(GTK_BOX(patternRow), patternEntry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), patternRow, FALSE, FALSE, 0);

    GtkWidget *colorRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *fgBtn = gtk_color_button_new_with_rgba(&fg);
    GtkWidget *bgBtn = gtk_color_button_new_with_rgba(&bg);
    GtkWidget *fgCol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(fgCol), gtk_label_new("Foreground"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fgCol), fgBtn, FALSE, FALSE, 0);
    GtkWidget *bgCol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(bgCol), gtk_label_new("Background"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bgCol), bgBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(colorRow), fgCol, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(colorRow), bgCol, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), colorRow, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(content), box);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        result.accepted = true;
        result.pattern = gtk_entry_get_text(GTK_ENTRY(patternEntry));
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(fgBtn), &result.fg);
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(bgBtn), &result.bg);
    }
    gtk_widget_destroy(dlg);
    return result;
}

LogColor toLogColor(const GdkRGBA &c) { return LogColor{(int)(c.red*255), (int)(c.green*255), (int)(c.blue*255), true}; }
GdkRGBA toRgba(const LogColor &c) { return GdkRGBA{c.r/255.0, c.g/255.0, c.b/255.0, 1.0}; }

// Matches SettingsDialog::onAddDefaults' default rule set (Qt6). Declared at
// file scope, not inline in the "Add Default Rules" handler below, because
// its array-literal commas would otherwise be misparsed as extra arguments
// to the enclosing G_CALLBACK(...) macro (G_CALLBACK only balances parens,
// not braces, when splitting macro arguments).
struct DefaultHighlightRule { const char *pat, *fg, *bg; };
const DefaultHighlightRule kDefaultHighlightRules[] = {
    { ".*TRACE.*", "#9CA3AF", "#000000" },
    { ".*DEBUG.*", "#60A5FA", "#000000" },
    { ".*INFO.*",  "#4ADE80", "#000000" },
    { ".*WARN.*",  "#FBBF24", "#000000" },
    { ".*ERROR.*", "#F87171", "#000000" },
    { ".*FATAL.*", "#C084FC", "#000000" },
};

void rebuildEngineHighlightRules(LogViewerState *st)
{
    std::vector<EngineHighlightRule> engineRules;
    for (auto &r : st->rules) {
        EngineHighlightRule er;
        er.pattern = r.pattern;
        er.foregroundColor = r.foregroundColor;
        er.backgroundColor = r.backgroundColor;
        er.compiledRegex = r.compiledRegex;
        engineRules.push_back(er);
    }
    st->engine->setHighlightRules(engineRules);
    gtk_widget_queue_draw(st->treeView);
}

// Selects the given row in the settings dialog's rule list -- used after
// Move Up/Down, whose refresh() (gtk_list_store_clear + repopulate)
// otherwise drops the selection entirely.
void selectRow(GtkWidget *listWidget, int row)
{
    if (row < 0) return;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(listWidget));
    GtkTreePath *path = gtk_tree_path_new_from_indices(row, -1);
    gtk_tree_selection_select_path(sel, path);
    gtk_tree_view_set_cursor(GTK_TREE_VIEW(listWidget), path, nullptr, FALSE);
    gtk_tree_path_free(path);
}

void openSettingsDialog(LogViewerState *st)
{
    GtkWidget *dlg = gtk_dialog_new_with_buttons("Highlight Rules", GTK_WINDOW(gtk_widget_get_toplevel(st->root)),
        GTK_DIALOG_MODAL, "_Close", GTK_RESPONSE_CLOSE, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 500, 400);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));

    // Qt's SettingsDialog lays this out as an HBox(table, VBox(buttons)) --
    // mirror that here instead of GTK's previous single VBox with a
    // horizontal button row underneath.
    GtkWidget *mainRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(mainRow), 8);

    // Column 0 = priority (1-based row position, display-only), column 1 = pattern.
    GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    for (size_t i = 0; i < st->rules.size(); ++i) {
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it, 0, std::to_string(i + 1).c_str(), 1, st->rules[i].pattern.c_str(), -1);
    }
    GtkWidget *listWidget = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    GtkCellRenderer *priorityRenderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(listWidget), -1, "Priority", priorityRenderer, "text", 0, nullptr);
    GtkCellRenderer *patternRenderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *patternCol = gtk_tree_view_column_new_with_attributes("Regex Pattern", patternRenderer, "text", 1, nullptr);
    gtk_tree_view_column_set_expand(patternCol, TRUE);
    // Show each rule's own colors on its row, matching Qt's
    // itemPattern->setBackground()/setForeground() -- previously this list
    // showed every rule (including "Add Default Rules"-loaded ones) as
    // plain uncolored text, which is what read to the user as "the
    // defaults didn't come with their colors".
    gtk_tree_view_column_set_cell_data_func(patternCol, patternRenderer,
        +[](GtkTreeViewColumn *, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
            auto *st = static_cast<LogViewerState *>(data);
            // Both of these return NULL against a stale iter -- which GTK hands
            // us while tearing the tree view down inside gtk_widget_destroy(),
            // after the store has started emptying. Dereferencing either one
            // there is what crashed all of doublecmd on closing this dialog,
            // but only when it had rules in it (an empty store never invokes
            // the cell data func at all, which is why it looked unreproducible).
            GtkTreePath *p = gtk_tree_model_get_path(model, iter);
            if (!p) return;
            const int *indices = gtk_tree_path_get_indices(p);
            if (!indices || gtk_tree_path_get_depth(p) < 1) {
                gtk_tree_path_free(p);
                return;
            }
            int row = indices[0];
            gtk_tree_path_free(p);
            if (row < 0 || row >= (int)st->rules.size()) return;
            const auto &r = st->rules[row];
            if (r.foregroundColor.valid) {
                GdkRGBA rgba = toRgba(r.foregroundColor);
                g_object_set(cell, "foreground-rgba", &rgba, nullptr);
            } else {
                g_object_set(cell, "foreground-set", FALSE, nullptr);
            }
            if (r.backgroundColor.valid) {
                GdkRGBA rgba = toRgba(r.backgroundColor);
                g_object_set(cell, "background-rgba", &rgba, nullptr);
            } else {
                g_object_set(cell, "background-set", FALSE, nullptr);
            }
        }, st, nullptr);
    gtk_tree_view_append_column(GTK_TREE_VIEW(listWidget), patternCol);
    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_size_request(scroll, -1, 250);
    gtk_container_add(GTK_CONTAINER(scroll), listWidget);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(mainRow), scroll, TRUE, TRUE, 0);

    auto refreshList = [store, st]() {
        gtk_list_store_clear(store);
        for (size_t i = 0; i < st->rules.size(); ++i) {
            GtkTreeIter it;
            gtk_list_store_append(store, &it);
            gtk_list_store_set(store, &it, 0, std::to_string(i + 1).c_str(), 1, st->rules[i].pattern.c_str(), -1);
        }
    };

    auto selectedIndex = [listWidget]() -> int {
        GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(listWidget));
        GtkTreeModel *m; GtkTreeIter it;
        if (!gtk_tree_selection_get_selected(sel, &m, &it)) return -1;
        GtkTreePath *p = gtk_tree_model_get_path(m, &it);
        int idx = gtk_tree_path_get_indices(p)[0];
        gtk_tree_path_free(p);
        return idx;
    };

    GtkWidget *btnCol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *btnAdd = gtk_button_new_with_label("Add");
    GtkWidget *btnEdit = gtk_button_new_with_label("Edit");
    GtkWidget *btnDelete = gtk_button_new_with_label("Delete");
    GtkWidget *btnDefault = gtk_button_new_with_label("Add Default Rules");
    GtkWidget *btnUp = gtk_button_new_with_label("Move Up");
    GtkWidget *btnDown = gtk_button_new_with_label("Move Down");
    for (GtkWidget *b : {btnAdd, btnEdit, btnDelete, btnDefault})
        gtk_box_pack_start(GTK_BOX(btnCol), b, FALSE, FALSE, 0);
    // A spacer matching Qt's addSpacing(20) between the rule-editing
    // buttons and the reordering buttons.
    GtkWidget *btnSpacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(btnSpacer, -1, 20);
    gtk_box_pack_start(GTK_BOX(btnCol), btnSpacer, FALSE, FALSE, 0);
    for (GtkWidget *b : {btnUp, btnDown})
        gtk_box_pack_start(GTK_BOX(btnCol), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mainRow), btnCol, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(content), mainRow);

    struct Ctx { LogViewerState *st; GtkWidget *listWidget; std::function<void()> refresh; std::function<int()> selIndex; GtkWidget *dlg; };
    auto *ctx = new Ctx{st, listWidget, refreshList, selectedIndex, dlg};

    g_signal_connect_data(btnAdd, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
        auto *ctx = static_cast<Ctx *>(data);
        auto res = runRuleDialog(GTK_WINDOW(ctx->dlg), "", GdkRGBA{1,1,1,1}, GdkRGBA{0,0,0,1});
        if (res.accepted && !res.pattern.empty()) {
            EngineHighlightRule r;
            r.pattern = res.pattern;
            r.foregroundColor = toLogColor(res.fg);
            r.backgroundColor = toLogColor(res.bg);
            r.compiledRegex = std::make_shared<re2::RE2>(r.pattern);
            ctx->st->rules.push_back(r);
            ctx->refresh();
        }
    }), ctx, nullptr, (GConnectFlags)0);

    g_signal_connect_data(btnEdit, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
        auto *ctx = static_cast<Ctx *>(data);
        int idx = ctx->selIndex();
        if (idx < 0 || idx >= (int)ctx->st->rules.size()) return;
        auto &r = ctx->st->rules[idx];
        auto res = runRuleDialog(GTK_WINDOW(ctx->dlg), r.pattern, toRgba(r.foregroundColor), toRgba(r.backgroundColor));
        if (res.accepted && !res.pattern.empty()) {
            r.pattern = res.pattern;
            r.foregroundColor = toLogColor(res.fg);
            r.backgroundColor = toLogColor(res.bg);
            r.compiledRegex = std::make_shared<re2::RE2>(r.pattern);
            ctx->refresh();
        }
    }), ctx, nullptr, (GConnectFlags)0);

    g_signal_connect_data(btnDelete, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
        auto *ctx = static_cast<Ctx *>(data);
        int idx = ctx->selIndex();
        if (idx < 0 || idx >= (int)ctx->st->rules.size()) return;
        ctx->st->rules.erase(ctx->st->rules.begin() + idx);
        ctx->refresh();
    }), ctx, nullptr, (GConnectFlags)0);

    g_signal_connect_data(btnDefault, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
        auto *ctx = static_cast<Ctx *>(data);
        int insertIdx = ctx->selIndex();
        if (insertIdx < 0) insertIdx = 0;

        for (int i = 5; i >= 0; --i) {
            const auto &d = kDefaultHighlightRules[i];
            bool exists = false;
            for (auto &r : ctx->st->rules) {
                if (r.pattern == d.pat) { exists = true; break; }
            }
            if (exists) continue;

            GdkRGBA fg;
            GdkRGBA bg;
            gdk_rgba_parse(&fg, d.fg);
            gdk_rgba_parse(&bg, d.bg);
            EngineHighlightRule r;
            r.pattern = d.pat;
            r.foregroundColor = toLogColor(fg);
            r.backgroundColor = toLogColor(bg);
            r.compiledRegex = std::make_shared<re2::RE2>(r.pattern);
            if (r.compiledRegex->ok())
                ctx->st->rules.insert(ctx->st->rules.begin() + insertIdx, r);
        }
        ctx->refresh();
    }), ctx, nullptr, (GConnectFlags)0);

    g_signal_connect_data(btnUp, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
        auto *ctx = static_cast<Ctx *>(data);
        int idx = ctx->selIndex();
        if (idx <= 0 || idx >= (int)ctx->st->rules.size()) return;
        std::swap(ctx->st->rules[idx], ctx->st->rules[idx - 1]);
        ctx->refresh();
        // refresh() clears and repopulates the store, which drops the
        // GtkTreeSelection entirely -- reselect the moved row at its new
        // position so repeated Move Up/Down clicks don't require
        // reselecting the row by hand each time.
        selectRow(ctx->listWidget, idx - 1);
    }), ctx, nullptr, (GConnectFlags)0);

    g_signal_connect_data(btnDown, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
        auto *ctx = static_cast<Ctx *>(data);
        int idx = ctx->selIndex();
        if (idx < 0 || idx >= (int)ctx->st->rules.size() - 1) return;
        std::swap(ctx->st->rules[idx], ctx->st->rules[idx + 1]);
        ctx->refresh();
        selectRow(ctx->listWidget, idx + 1);
    }), ctx, nullptr, (GConnectFlags)0);

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    delete ctx;

    rebuildEngineHighlightRules(st);
    saveHighlightRules(st);
}

// ── Highlight rules persistence (simple INI, same shape as diagramview's) ──

void loadHighlightRules(LogViewerState *st)
{
    std::ifstream f(st->iniPath);
    if (!f) { rebuildEngineHighlightRules(st); return; }

    std::string line;
    EngineHighlightRule cur;
    bool inRule = false;
    auto flush = [&]() {
        if (inRule && !cur.pattern.empty()) {
            cur.compiledRegex = std::make_shared<re2::RE2>(cur.pattern);
            st->rules.push_back(cur);
        }
        cur = EngineHighlightRule{};
        inRule = false;
    };
    while (std::getline(f, line)) {
        if (line.rfind("[rule", 0) == 0) { flush(); inRule = true; continue; }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        if (key == "pattern") cur.pattern = val;
        else if (key == "fg") { int r,g,b; std::sscanf(val.c_str(), "%d,%d,%d", &r,&g,&b); cur.foregroundColor = LogColor{r,g,b,true}; }
        else if (key == "bg") { int r,g,b; std::sscanf(val.c_str(), "%d,%d,%d", &r,&g,&b); cur.backgroundColor = LogColor{r,g,b,true}; }
    }
    flush();
    rebuildEngineHighlightRules(st);
}

void saveHighlightRules(LogViewerState *st)
{
    std::ofstream f(st->iniPath, std::ios::trunc);
    if (!f) return;
    int i = 0;
    for (auto &r : st->rules) {
        f << "[rule" << i++ << "]\n";
        f << "pattern=" << r.pattern << "\n";
        f << "fg=" << r.foregroundColor.r << "," << r.foregroundColor.g << "," << r.foregroundColor.b << "\n";
        f << "bg=" << r.backgroundColor.r << "," << r.backgroundColor.g << "," << r.backgroundColor.b << "\n";
    }
}

// ── Copy / delete / extract / clean ─────────────────────────────────

void copySelectedLines(LogViewerState *st)
{
    auto rows = selectedSourceRows(st);
    if (rows.empty()) return;
    std::ostringstream out;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i > 0) out << "\n";
        out << st->engine->lineText(rows[i]);
    }
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), out.str().c_str(), -1);
    setStatus(st, "Copied " + std::to_string(rows.size()) + " line(s)");
}

void deleteSelectedLines(LogViewerState *st)
{
    auto rows = selectedSourceRows(st);
    if (rows.empty()) return;
    int count = (int)rows.size();
    st->engine->deleteRows(rows);
    if (st->filteringActive || st->timeFilterActive) refreshFilter(st);
    else log_tree_model_set_filter(st->model, nullptr);
    setStatus(st, "Deleted " + std::to_string(count) + " line(s)");
}

void extractSelectedLines(LogViewerState *st)
{
    auto rows = selectedSourceRows(st);
    if (rows.empty()) { setStatus(st, "No lines selected for extraction"); return; }

    GtkWidget *dlg = gtk_file_chooser_dialog_new("Extract Selected Lines", GTK_WINDOW(gtk_widget_get_toplevel(st->root)),
        GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        for (int r : rows) out << st->engine->lineText(r) << "\n";
        setStatus(st, "Extracted " + std::to_string(rows.size()) + " line(s) to " + path);
        g_free(path);
    }
    gtk_widget_destroy(dlg);
}

void clearLogFile(LogViewerState *st)
{
    if (st->currentFile.empty()) return;
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(st->root)), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
        "This will delete all contents of:\n%s\n\nThe file will be kept but emptied. Continue?", st->currentFile.c_str());
    int resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    if (resp != GTK_RESPONSE_YES) return;

    st->engine->clearFile();
    log_tree_model_set_filter(st->model, nullptr);
    setStatus(st, "Log cleaned: " + st->currentFile);
}

void destroyState(gpointer data)
{
    auto *st = static_cast<LogViewerState *>(data);
    if (st->followTimerId) g_source_remove(st->followTimerId);
    delete st;
}

} // namespace

extern "C" {

#define EXPORT __attribute__((visibility("default")))

EXPORT HWND DCPCALL ListLoad(HWND ParentWin, char *FileToLoad, int ShowFlags)
{
    GtkWidget *parent = GTK_WIDGET(ParentWin);
    auto *st = new LogViewerState();
    st->currentFile = FileToLoad;
    st->iniPath = g_configPath;

    st->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    // gtk_container_add() doesn't register the child the way GtkLayout
    // expects: DC's ResizeWindow later calls gtk_layout_move() on this
    // widget, which asserts the parent is exactly this GtkLayout -- only
    // gtk_layout_put() sets that up.
    gtk_layout_put(GTK_LAYOUT(parent), st->root, 0, 0);

    // ── Header ──
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(header), 2);

    st->searchEntry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(st->searchEntry), "Regex search...");
    gtk_widget_set_hexpand(st->searchEntry, TRUE);
    gtk_box_pack_start(GTK_BOX(header), st->searchEntry, TRUE, TRUE, 0);

    GtkWidget *btnFilter = gtk_button_new_with_label("▽ Filter");
    GtkWidget *btnSearchNext = gtk_button_new_with_label("⌕ Search / Next");
    st->btnSearchStop = gtk_button_new_with_label("■ Stop");
    gtk_widget_set_sensitive(st->btnSearchStop, FALSE);
    for (GtkWidget *b : {btnFilter, btnSearchNext, st->btnSearchStop})
        gtk_box_pack_start(GTK_BOX(header), b, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(header), gtk_label_new("From:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), buildDateTimeSpinner(st->timeStartSpin), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), gtk_label_new("To:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), buildDateTimeSpinner(st->timeEndSpin), FALSE, FALSE, 0);

    st->chkFilterMode = gtk_check_button_new_with_label("Filter");
    GtkWidget *btnResetFilter = gtk_button_new_with_label("✖ Reset");
    st->chkFollow = gtk_check_button_new_with_label("Follow");
    GtkWidget *btnExtract = gtk_button_new_with_label("\U0001F5AB Extract");
    // Matches Qt's btnClearLog label (LogViewerWidget.cpp) -- this glyph
    // was missing here entirely, the "missing icon" the user reported.
    GtkWidget *btnClearLog = gtk_button_new_with_label("⎚︎ Clean");
    GtkWidget *btnSettings = gtk_button_new_with_label("⚙ Settings");
    for (GtkWidget *b : {st->chkFilterMode, btnResetFilter, st->chkFollow, btnExtract, btnClearLog, btnSettings})
        gtk_box_pack_start(GTK_BOX(header), b, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(st->root), header, FALSE, FALSE, 0);

    // ── Log view ──
    st->engine = std::make_unique<LogEngine>();
    st->model = log_tree_model_new(st->engine.get());

    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_vexpand(scroll, TRUE);
    st->treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(st->model));
    // gtk_tree_view_new_with_model() takes its own ref on the model; drop
    // ours (log_tree_model_new()'s g_object_new() ref) so the treeview owns
    // it solely, matching the pattern already used for GtkListStore in
    // GtkEditableGridWidget.cpp. Without this the model's refcount never
    // reaches 0 when the treeview is torn down -- it leaks, outliving
    // LogViewerState's destruction, with model->engine and model->filter
    // left dangling into freed memory (this was reproducibly crashing
    // doublecmd on close, deep inside libgobject/libgtk-3's own signal
    // machinery, since st->model stays "alive" per GObject bookkeeping
    // with pointers into memory that's already been freed).
    g_object_unref(st->model);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(st->treeView), FALSE);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(st->treeView)), GTK_SELECTION_MULTIPLE);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Line", renderer, "text", LOG_TREE_MODEL_COL_TEXT, nullptr);
    gtk_tree_view_column_set_cell_data_func(col, renderer, cellDataFunc, st, nullptr);
    gtk_tree_view_append_column(GTK_TREE_VIEW(st->treeView), col);

    gtk_container_add(GTK_CONTAINER(scroll), st->treeView);
    gtk_box_pack_start(GTK_BOX(st->root), scroll, TRUE, TRUE, 0);

    // ── Status bar ──
    st->statusLabel = gtk_label_new("Ready.");
    gtk_widget_set_halign(st->statusLabel, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(st->root), st->statusLabel, FALSE, FALSE, 2);

    // ── Context menu (right-click: copy/delete/extract) ──
    g_signal_connect_data(st->treeView, "button-press-event", G_CALLBACK(+[](GtkWidget *, GdkEventButton *event, gpointer data) -> gboolean {
        if (event->button != GDK_BUTTON_SECONDARY) return FALSE;
        auto *st = static_cast<LogViewerState *>(data);
        bool hasSelection = !selectedSourceRows(st).empty();

        GtkWidget *menu = gtk_menu_new();
        GtkWidget *copyItem = gtk_menu_item_new_with_label("⧉ Copy");
        GtkWidget *deleteItem = gtk_menu_item_new_with_label("✕ Delete Selected Lines");
        GtkWidget *extractItem = gtk_menu_item_new_with_label("\U0001F5AB Extract");
        gtk_widget_set_sensitive(deleteItem, hasSelection);
        gtk_widget_set_sensitive(extractItem, hasSelection);
        g_signal_connect_swapped(copyItem, "activate", G_CALLBACK(+[](gpointer d) { copySelectedLines(static_cast<LogViewerState *>(d)); }), st);
        g_signal_connect_swapped(deleteItem, "activate", G_CALLBACK(+[](gpointer d) { deleteSelectedLines(static_cast<LogViewerState *>(d)); }), st);
        g_signal_connect_swapped(extractItem, "activate", G_CALLBACK(+[](gpointer d) { extractSelectedLines(static_cast<LogViewerState *>(d)); }), st);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), copyItem);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), deleteItem);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), extractItem);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }), st, nullptr, (GConnectFlags)0);

    // ── Wiring ──
    g_signal_connect_swapped(btnFilter, "clicked", G_CALLBACK(+[](gpointer d) {
        auto *st = static_cast<LogViewerState *>(d);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->chkFilterMode), TRUE);
        executeSearch(st, false);
    }), st);
    g_signal_connect_swapped(btnSearchNext, "clicked", G_CALLBACK(+[](gpointer d) {
        auto *st = static_cast<LogViewerState *>(d);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->chkFollow), FALSE);
        executeSearch(st, true);
    }), st);
    g_signal_connect_swapped(st->searchEntry, "activate", G_CALLBACK(+[](gpointer d) { executeSearch(static_cast<LogViewerState *>(d), true); }), st);
    g_signal_connect_swapped(st->btnSearchStop, "clicked", G_CALLBACK(+[](gpointer d) {
        auto *st = static_cast<LogViewerState *>(d);
        st->engine->stopSearch();
        gtk_widget_set_sensitive(st->btnSearchStop, FALSE);
        setStatus(st, "Search interrupted");
    }), st);
    g_signal_connect_swapped(st->chkFilterMode, "toggled", G_CALLBACK(+[](gpointer d) {
        auto *st = static_cast<LogViewerState *>(d);
        st->filteringActive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(st->chkFilterMode));
        refreshFilter(st);
    }), st);
    // Live-updating, like Qt's QDateTimeEdit::dateTimeChanged -- fires on
    // every spin click or typed digit, not just on Enter.
    for (GtkWidget *spin : st->timeStartSpin)
        g_signal_connect_swapped(spin, "value-changed", G_CALLBACK(+[](gpointer d) {
            auto *st = static_cast<LogViewerState *>(d);
            st->timeFilterActive = true;
            refreshFilter(st);
        }), st);
    for (GtkWidget *spin : st->timeEndSpin)
        g_signal_connect_swapped(spin, "value-changed", G_CALLBACK(+[](gpointer d) {
            auto *st = static_cast<LogViewerState *>(d);
            st->timeFilterActive = true;
            refreshFilter(st);
        }), st);
    g_signal_connect_swapped(btnResetFilter, "clicked", G_CALLBACK(+[](gpointer d) {
        auto *st = static_cast<LogViewerState *>(d);
        gtk_entry_set_text(GTK_ENTRY(st->searchEntry), "");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->chkFilterMode), FALSE);
        LogTimestamp first = st->engine->firstTimestamp();
        LogTimestamp last = st->engine->lastTimestamp();
        // Writing spin values fires "value-changed" (unlike the old
        // gtk_entry_set_text(), which didn't fire "activate"), so those
        // handlers will flip timeFilterActive back on -- reset it after.
        writeTimestampToSpinner(st->timeStartSpin, first);
        writeTimestampToSpinner(st->timeEndSpin, last);
        st->timeFilterActive = false;
        st->filteringActive = false;
        refreshFilter(st);
        executeSearch(st, false);
    }), st);
    g_signal_connect_swapped(st->chkFollow, "toggled", G_CALLBACK(+[](gpointer d) {
        auto *st = static_cast<LogViewerState *>(d);
        bool on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(st->chkFollow));
        st->engine->setFollowEnabled(on);
        if (on) scrollToBottom(st);
    }), st);
    g_signal_connect_swapped(btnClearLog, "clicked", G_CALLBACK(+[](gpointer d) { clearLogFile(static_cast<LogViewerState *>(d)); }), st);
    g_signal_connect_swapped(btnExtract, "clicked", G_CALLBACK(+[](gpointer d) { extractSelectedLines(static_cast<LogViewerState *>(d)); }), st);
    g_signal_connect_swapped(btnSettings, "clicked", G_CALLBACK(+[](gpointer d) { openSettingsDialog(static_cast<LogViewerState *>(d)); }), st);

    g_object_set_data_full(G_OBJECT(st->root), "logview-state", st, destroyState);

    gtk_widget_show_all(st->root);

    // ── Load ──
    loadHighlightRules(st);
    if (st->engine->loadFile(st->currentFile)) {
        log_tree_model_rows_changed(st->model);
        LogTimestamp first = st->engine->firstTimestamp(), last = st->engine->lastTimestamp();
        // Same "value-changed" fires-on-set concern as the Reset handler --
        // populate the spinners, then make sure the filter stays off until
        // the user actually edits a value or clicks Reset.
        writeTimestampToSpinner(st->timeStartSpin, first);
        writeTimestampToSpinner(st->timeEndSpin, last);
        st->timeFilterActive = false;
        setStatus(st, "Lines: " + std::to_string(st->engine->lineCount()));
    }

    // Poll for file growth every second while "Follow" is on (GTK has no
    // built-in file-watcher signal the way QFileSystemWatcher does — this
    // is the simplest correct equivalent; a GFileMonitor-based push
    // notification, as diagramview's GTK build uses, would also work but
    // isn't necessary for a once-a-second tail check).
    st->followTimerId = g_timeout_add(1000, +[](gpointer data) -> gboolean {
        auto *st = static_cast<LogViewerState *>(data);
        if (st->engine->followEnabled()) {
            auto result = st->engine->refreshTail();
            if (result.grew) {
                log_tree_model_rows_inserted(st->model, result.oldLineCount, result.newLineCount - 1);
                if (st->filteringActive || st->timeFilterActive) refreshFilter(st);
                setStatus(st, "Lines: " + std::to_string(st->engine->lineCount()) + " (following)");
                scrollToBottom(st);
            }
        }
        return G_SOURCE_CONTINUE;
    }, st);

    return reinterpret_cast<HWND>(st->root);
}

EXPORT void DCPCALL ListCloseWindow(HWND ListWin)
{
    GtkWidget *root = GTK_WIDGET(ListWin);
    if (root) gtk_widget_destroy(root);
}

EXPORT int DCPCALL ListSendCommand(HWND, int, int)
{
    return LISTPLUGIN_ERROR;
}

EXPORT int DCPCALL ListSearchText(HWND ListWin, char *SearchString, int)
{
    GtkWidget *root = GTK_WIDGET(ListWin);
    auto *st = static_cast<LogViewerState *>(g_object_get_data(G_OBJECT(root), "logview-state"));
    if (!st) return LISTPLUGIN_ERROR;
    gtk_entry_set_text(GTK_ENTRY(st->searchEntry), SearchString);
    executeSearch(st, true);
    return LISTPLUGIN_OK;
}

EXPORT void DCPCALL ListGetDetectString(char *DetectString, int maxlen)
{
    snprintf(DetectString, maxlen - 1, "EXT=\"LOG\" | EXT=\"TXT\"");
}

EXPORT void DCPCALL ListSetDefaultParams(ListDefaultParamStruct *dps)
{
    if (!dps) return;
    std::string iniName(dps->DefaultIniName);
    auto slash = iniName.find_last_of('/');
    std::string dir = slash == std::string::npos ? "." : iniName.substr(0, slash);
    g_configPath = dir + "/logview.ini";
}

} // extern "C"
