#pragma once

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <memory>
#include <string>

#include "wlxbase_gtk/GtkFocusManager.h"
#include "wlxbase_gtk/GtkPluginToolBar.h"
#include "wlxbase_gtk/GtkFindReplacePanel.h"

namespace GtkWlPlugin { class GtkFocusManager; }

// GtkSourceView-based rich code/text editor widget -- the GTK analog of
// kate_qt6's EditorWidget (which wraps KDE's KTextEditor::View). See
// wlx/kate/src/editor_widget.h for the Qt6 sibling this mirrors.
class EditorWidget {
public:
    EditorWidget();
    ~EditorWidget();

    GtkWidget *rootWidget() const { return m_root; }
    GtkWidget *sourceView() const { return m_view; }

    bool loadFile(const std::string &path);
    bool save();
    bool saveAs(const std::string &path);
    /// Writes a copy to `path` in the given encoding (e.g. "ISO-8859-1")
    /// without changing currentFile()/the buffer's tracked encoding --
    /// the GTK analog of kate_qt6's "Save Copy As".
    bool saveCopyAs(const std::string &path, const std::string &encoding);

    void setReadOnly(bool readOnly);
    bool isReadOnly() const;
    void setWordWrap(bool wrap);
    bool wordWrap() const { return m_wordWrap; }
    void reload();
    void gotoLine(int oneBasedLine);

    bool isDirty() const;
    std::string currentFile() const { return m_currentFile; }
    std::string currentEncoding() const { return m_encoding; }

    GtkWlPlugin::GtkFocusManager *focusManager() const { return m_fm.get(); }

private:
    void setupMenuBar();
    void setupCompletion();
    void doPrint();
    void setupToolbar();
    void setupStatusBar();
    void setupFindReplace();
    void detectAndApplyLanguage();
    void applyStyleScheme();
    void updateStatusBar();
    void updateDirtyIndicator();
    void showDiskChangeBar(bool show);
    void doFind(bool forward);
    void doReplace();
    void doReplaceAll();

    void showGotoLineDialog();
    void showSaveAsDialog(bool copyOnly);
    void showEncodingPickerAndSave();
    void applyCaseTransform(int mode); // 0=upper 1=lower 2=title 3=proper 4=sentence 5=camel

    /// Decodes raw file bytes to UTF-8, trying UTF-8 first, then
    /// GtkSourceView's own standard candidate encoding list
    /// (gtk_source_encoding_get_all()) until one converts cleanly.
    /// Returns the UTF-8 text and sets `detectedEncoding` to whichever
    /// candidate matched (its GtkSourceView charset name, e.g.
    /// "ISO-8859-1").
    std::string decodeToUtf8(const std::string &rawBytes, std::string &detectedEncoding);
    /// Inverse of decodeToUtf8: encodes UTF-8 text back to `encoding`'s
    /// charset. Returns true and fills `out` on success.
    bool encodeFromUtf8(const std::string &utf8Text, const std::string &encoding, std::string &out);

    GtkWidget *m_root = nullptr;
    GtkWidget *m_menuBar = nullptr;
    GtkWidget *m_scrolled = nullptr;
    GtkWidget *m_view = nullptr; // GtkSourceView*
    GtkSourceBuffer *m_buffer = nullptr;

    std::unique_ptr<GtkWlPlugin::GtkFocusManager> m_fm;
    std::unique_ptr<GtkWlPlugin::GtkPluginToolBar> m_toolbar;
    std::unique_ptr<GtkWlPlugin::GtkFindReplacePanel> m_findPanel;

    GtkWidget *m_dirtyLabel = nullptr;
    GtkWidget *m_undoBtn = nullptr;
    GtkWidget *m_redoBtn = nullptr;
    GtkWidget *m_readOnlyToggle = nullptr;
    GtkWidget *m_wrapToggle = nullptr;

    GtkWidget *m_statusBar = nullptr;
    GtkWidget *m_posLabel = nullptr;
    GtkWidget *m_langLabel = nullptr;
    GtkWidget *m_modeLabel = nullptr;
    GtkWidget *m_encodingLabel = nullptr;

    GtkWidget *m_diskChangeBar = nullptr;
    GtkWidget *m_diskChangeLabel = nullptr;
    GFileMonitor *m_monitor = nullptr;
    gulong m_monitorHandler = 0;
    bool m_ignoreNextDiskChange = false;

    std::string m_currentFile;
    std::string m_encoding = "UTF-8";
    bool m_wordWrap = false;
    int m_findFromLine = 0, m_findFromCol = 0;
};
