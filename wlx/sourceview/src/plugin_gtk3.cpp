// GTK3 WLX entry points for sourceview_gtk3 -- the GTK analog of kate_qt6
// (KTextEditor-based rich code viewer), here built on GtkSourceView 4.

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <memory>
#include <set>
#include <string>
#include <cstring>

#include "wlxplugin.h"
#include "EditorWidget.h"

namespace {

void destroyState(gpointer data)
{
    delete static_cast<EditorWidget *>(data);
}

// Mirrors kate_qt6's ListGetDetectString(), which enumerates
// KTextEditor's actual registered MIME types/globs rather than hardcode
// a list -- same idea here via GtkSourceLanguageManager's registered
// languages, each with a set of glob patterns like "*.py". Extracts the
// extension the same way kate_qt6 does: strip the "*." prefix, and if
// the remainder still has a further "." (e.g. "*.tar.gz"), keep only
// the part after the last one.
std::set<std::string> collectRegisteredExtensions()
{
    static bool inited = false;
    if (!inited) { gtk_source_init(); inited = true; }

    std::set<std::string> exts;
    GtkSourceLanguageManager *mgr = gtk_source_language_manager_get_default();
    const gchar * const *ids = gtk_source_language_manager_get_language_ids(mgr);
    if (!ids) return exts;

    for (int i = 0; ids[i]; ++i) {
        GtkSourceLanguage *lang = gtk_source_language_manager_get_language(mgr, ids[i]);
        if (!lang) continue;
        gchar **globs = gtk_source_language_get_globs(lang);
        if (!globs) continue;
        for (int j = 0; globs[j]; ++j) {
            const char *glob = globs[j];
            if (strncmp(glob, "*.", 2) != 0) continue;
            std::string ext = glob + 2;
            size_t lastDot = ext.find_last_of('.');
            if (lastDot != std::string::npos) ext = ext.substr(lastDot + 1);
            if (ext.empty() || ext.find('*') != std::string::npos || ext.find('[') != std::string::npos)
                continue; // skip remaining wildcard patterns (e.g. "*.[ch]")
            for (auto &c : ext) c = (char)toupper((unsigned char)c);
            exts.insert(ext);
        }
        g_strfreev(globs);
    }
    return exts;
}

} // namespace

extern "C" {

HWND DCPCALL ListLoad(HWND ParentWin, char *FileToLoad, int ShowFlags)
{
    GtkWidget *parent = GTK_WIDGET(ParentWin);
    auto *editor = new EditorWidget();

    // gtk_container_add() doesn't register the child the way GtkLayout
    // expects: DC's ResizeWindow later calls gtk_layout_move() on this
    // widget, which asserts the parent is exactly this GtkLayout -- only
    // gtk_layout_put() sets that up. (See the identical fix applied to
    // every other GTK3 plugin's ListLoad() this session.)
    gtk_layout_put(GTK_LAYOUT(parent), editor->rootWidget(), 0, 0);

    g_object_set_data_full(G_OBJECT(editor->rootWidget()), "sourceview-editor", editor, destroyState);

    editor->loadFile(std::string(FileToLoad));
    gtk_widget_show_all(editor->rootWidget());

    return reinterpret_cast<HWND>(editor->rootWidget());
}

void DCPCALL ListCloseWindow(HWND ListWin)
{
    GtkWidget *root = GTK_WIDGET(ListWin);
    if (root) gtk_widget_destroy(root);
}

int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter)
{
    GtkWidget *root = GTK_WIDGET(ListWin);
    auto *editor = static_cast<EditorWidget *>(g_object_get_data(G_OBJECT(root), "sourceview-editor"));
    if (!editor) return LISTPLUGIN_ERROR;

    if (Command == lc_newparams) {
        editor->reload();
        return LISTPLUGIN_OK;
    }
    if (Command == lc_copy) {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor->sourceView()));
        gtk_text_buffer_copy_clipboard(buf, gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
        return LISTPLUGIN_OK;
    }
    return LISTPLUGIN_ERROR;
}

int DCPCALL ListSearchText(HWND, char *, int)
{
    return LISTPLUGIN_ERROR;
}

void DCPCALL ListGetDetectString(char *DetectString, int maxlen)
{
    std::set<std::string> exts = collectRegisteredExtensions();
    if (exts.empty()) {
        // GtkSourceLanguageManager returned nothing registered (should
        // not happen given the vendored/statically-linked language
        // specs, but fail safe rather than produce an empty detect
        // string that matches no files at all).
        snprintf(DetectString, maxlen - 1,
            "EXT=\"TXT\" | EXT=\"C\" | EXT=\"CPP\" | EXT=\"H\" | EXT=\"PY\" | EXT=\"JS\" | "
            "EXT=\"HTML\" | EXT=\"CSS\" | EXT=\"JSON\" | EXT=\"XML\" | EXT=\"MD\" | EXT=\"SH\"");
        return;
    }

    std::string result;
    for (const auto &ext : exts) {
        std::string addition = "EXT=\"" + ext + "\"";
        if (!result.empty()) addition = " | " + addition;
        if ((int)(result.size() + addition.size()) >= maxlen - 1) break;
        result += addition;
    }
    snprintf(DetectString, maxlen - 1, "%s", result.c_str());
}

void DCPCALL ListSetDefaultParams(ListDefaultParamStruct *)
{
}

} // extern "C"
