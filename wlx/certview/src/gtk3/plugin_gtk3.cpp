#include <gtk/gtk.h>
#include <cstring>
#include <string>

#include "core/CertModel.h"
#include "core/CertParser.h"
#include "wlxplugin.h"

#define EXPORT __attribute__((visibility("default")))

namespace {

struct CertViewGtkState {
    GtkWidget *root = nullptr;
    GtkWidget *statusLabel = nullptr;
    GtkWidget *notebook = nullptr;
    GtkWidget *summaryGrid = nullptr;
    GtkWidget *extGrid = nullptr;
    GtkWidget *rawTextView = nullptr;
    certview::CertDocument doc;
};

static void addGridRow(GtkWidget *grid, int row, const char *label, const std::string &val) {
    GtkWidget *lbl = gtk_label_new(label);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(lbl), attrs);
    pango_attr_list_unref(attrs);

    GtkWidget *valLbl = gtk_label_new(val.c_str());
    gtk_widget_set_halign(valLbl, GTK_ALIGN_START);
    gtk_label_set_selectable(GTK_LABEL(valLbl), TRUE);

    gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), valLbl, 1, row, 1, 1);
}

static void onCopySummary(GtkButton*, gpointer user_data) {
    auto *state = static_cast<CertViewGtkState*>(user_data);
    GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(cb, state->doc.fullTextSummary.c_str(), -1);
}

} // namespace

extern "C" {

EXPORT HWND DCPCALL ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
    (void)ShowFlags;
    if (!FileToLoad) return nullptr;

    certview::CertDocument doc = certview::CertParser::parseFile(FileToLoad);
    if (doc.overallStatus == certview::CertStatus::Corrupted && doc.fileType == certview::FileType::Unknown) {
        return nullptr;
    }

    auto *state = new CertViewGtkState();
    state->doc = doc;

    state->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(state->root), 8);

    // Header Toolbar / Action Bar
    GtkWidget *headerBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    const char *statusText = "✓ CERTIFICATE VALID";
    if (doc.overallStatus == certview::CertStatus::Expired) statusText = "⚠ CERTIFICATE EXPIRED";
    else if (doc.overallStatus == certview::CertStatus::NotYetValid) statusText = "⚠ NOT YET VALID";
    else if (doc.overallStatus == certview::CertStatus::EncryptedContainer) statusText = "🔒 ENCRYPTED PKCS#12 ARCHIVE";

    state->statusLabel = gtk_label_new(statusText);
    gtk_box_pack_start(GTK_BOX(headerBox), state->statusLabel, FALSE, FALSE, 4);

    GtkWidget *copyBtn = gtk_button_new_with_label("Copy Summary");
    g_signal_connect(copyBtn, "clicked", G_CALLBACK(onCopySummary), state);
    gtk_box_pack_end(GTK_BOX(headerBox), copyBtn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(state->root), headerBox, FALSE, FALSE, 0);

    // Notebook (Tabs)
    state->notebook = gtk_notebook_new();

    // Tab 1: General
    GtkWidget *scrollSummary = gtk_scrolled_window_new(nullptr, nullptr);
    state->summaryGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(state->summaryGrid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(state->summaryGrid), 16);
    gtk_container_set_border_width(GTK_CONTAINER(state->summaryGrid), 10);

    if (!doc.certificates.empty()) {
        const auto &c = doc.certificates[0];
        int r = 0;
        addGridRow(state->summaryGrid, r++, "Subject:", c.subject);
        addGridRow(state->summaryGrid, r++, "Issuer:", c.issuer);
        addGridRow(state->summaryGrid, r++, "Valid From:", c.validFrom);
        addGridRow(state->summaryGrid, r++, "Valid Until:", c.validTo);
        addGridRow(state->summaryGrid, r++, "Serial Number:", c.serialNumber);
        addGridRow(state->summaryGrid, r++, "Public Key:", c.publicKeyAlgorithm + " (" + std::to_string(c.publicKeyBits) + " bits)");
        addGridRow(state->summaryGrid, r++, "Signature Alg:", c.signatureAlgorithm);
        addGridRow(state->summaryGrid, r++, "SHA-256:", c.sha256Fingerprint);
        addGridRow(state->summaryGrid, r++, "SHA-1:", c.sha1Fingerprint);
    } else {
        addGridRow(state->summaryGrid, 0, "Info:", doc.fullTextSummary);
    }

    gtk_container_add(GTK_CONTAINER(scrollSummary), state->summaryGrid);
    gtk_notebook_append_page(GTK_NOTEBOOK(state->notebook), scrollSummary, gtk_label_new("General"));

    // Tab 2: Extensions & SANs
    GtkWidget *scrollExt = gtk_scrolled_window_new(nullptr, nullptr);
    state->extGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(state->extGrid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(state->extGrid), 16);
    gtk_container_set_border_width(GTK_CONTAINER(state->extGrid), 10);

    if (!doc.certificates.empty()) {
        const auto &c = doc.certificates[0];
        int r = 0;
        for (const auto &san : c.subjectAltNames) {
            addGridRow(state->extGrid, r++, "SAN:", san);
        }
        for (const auto &ku : c.keyUsage) {
            addGridRow(state->extGrid, r++, "Key Usage:", ku);
        }
        for (const auto &eku : c.extKeyUsage) {
            addGridRow(state->extGrid, r++, "Ext Key Usage:", eku);
        }
    }
    gtk_container_add(GTK_CONTAINER(scrollExt), state->extGrid);
    gtk_notebook_append_page(GTK_NOTEBOOK(state->notebook), scrollExt, gtk_label_new("Extensions & SANs"));

    // Tab 3: Raw Details
    GtkWidget *scrollRaw = gtk_scrolled_window_new(nullptr, nullptr);
    state->rawTextView = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(state->rawTextView), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(state->rawTextView), TRUE);
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->rawTextView));
    std::string rawContent = doc.fullTextSummary;
    if (!doc.certificates.empty() && !doc.certificates[0].rawPem.empty()) {
        rawContent += "\n\n----- Raw PEM -----\n" + doc.certificates[0].rawPem;
    }
    gchar *validUtf8 = g_utf8_make_valid(rawContent.c_str(), rawContent.size());
    gtk_text_buffer_set_text(buf, validUtf8, -1);
    g_free(validUtf8);
    gtk_container_add(GTK_CONTAINER(scrollRaw), state->rawTextView);
    gtk_notebook_append_page(GTK_NOTEBOOK(state->notebook), scrollRaw, gtk_label_new("Details / PEM"));

    gtk_box_pack_start(GTK_BOX(state->root), state->notebook, TRUE, TRUE, 0);

    GtkWidget *parent = GTK_WIDGET(ParentWin);
    gtk_container_add(GTK_CONTAINER(parent), state->root);
    gtk_widget_show_all(state->root);

    return reinterpret_cast<HWND>(state);
}

EXPORT HWND DCPCALL ListLoadW(HWND ParentWin, WCHAR* FileToLoad, int ShowFlags) {
    if (!FileToLoad) return nullptr;
    char utf8Buf[4096];
    std::wcstombs(utf8Buf, reinterpret_cast<const wchar_t*>(FileToLoad), sizeof(utf8Buf));
    return ListLoad(ParentWin, utf8Buf, ShowFlags);
}

EXPORT void DCPCALL ListCloseWindow(HWND ListWin) {
    if (!ListWin) return;
    auto *state = reinterpret_cast<CertViewGtkState*>(ListWin);
    if (state->root) {
        gtk_widget_destroy(state->root);
    }
    delete state;
}

EXPORT void DCPCALL ListGetDetectString(char* DetectString, int maxlen) {
    const char* detect = "EXT=\"CER\" | EXT=\"CRT\" | EXT=\"DER\" | EXT=\"PEM\" | EXT=\"P12\" | EXT=\"PFX\"";
    std::snprintf(DetectString, maxlen, "%s", detect);
}

} // extern "C"
