// GTK3 UI for officeview: continuous-scroll page rendering (MuPDF, via
// OfficeCore::PdfCore) or LibreOfficeKit tile rendering (OfficeCore::LokCore),
// with the same x2t/LOK engine selection, size-limit, and Google Drive/rclone
// logic as the Qt6 build. Sheet tabs for multi-sheet spreadsheets, click-drag
// text selection (PDF path) or select-all copy (LOK path), Ctrl+wheel /
// Ctrl+Plus/Minus/0 zoom, Ctrl+C copy — all via wlxbase_gtk's GtkFocusManager.

#include "../core/OfficeCore.h"
#include "wlxbase_gtk/GtkFocusManager.h"

#include <gtk/gtk.h>
#include <cstring>
#include <cstdio>
#include <memory>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <unistd.h>

#include "wlxplugin.h"

#define EXPORT __attribute__((visibility("default")))

#define _detectstring "EXT=\"ODT\" | EXT=\"DOC\" | EXT=\"DOCX\" | EXT=\"DOCM\" | EXT=\"ODS\" | EXT=\"XLS\" | EXT=\"XLSX\" | EXT=\"XLSM\" | EXT=\"ODP\" | EXT=\"PPT\" | EXT=\"PPTX\" | EXT=\"PPTM\""

using namespace OfficeCore;

namespace {

using StateIntPair = std::pair<struct OfficeViewState *, int>;

cairo_surface_t *rasterToSurface(const RasterImage &img) {
    if (img.empty()) return nullptr;
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, img.width);
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, img.width, img.height);
    cairo_surface_flush(surf);
    unsigned char *dst = cairo_image_surface_get_data(surf);
    for (int y = 0; y < img.height; y++) {
        uint32_t *row = (uint32_t *)(dst + (size_t)y * stride);
        const uint8_t *srow = img.rgb.data() + (size_t)y * img.width * 3;
        for (int x = 0; x < img.width; x++) {
            row[x] = (0xFFu << 24) | ((uint32_t)srow[x * 3] << 16) | ((uint32_t)srow[x * 3 + 1] << 8) | srow[x * 3 + 2];
        }
    }
    cairo_surface_mark_dirty(surf);
    return surf;
}

struct OfficeViewState {
    GtkWidget *root = nullptr;      // outer GtkBox (vertical)
    GtkWidget *tabBar = nullptr;    // GtkBox of toggle buttons, or nullptr
    GtkWidget *scrolled = nullptr;
    GtkWidget *drawing = nullptr;

    std::unique_ptr<PdfCore> pdf;   // set for the x2t/MuPDF path
    std::unique_ptr<LokCore> lok;   // set for the LibreOfficeKit path

    std::vector<std::string> sheetNames;
    std::vector<int> sheetStartPages; // page/part index each tab starts at
    std::vector<GtkWidget *> tabButtons;
    bool syncingTabs = false;

    bool hasSelection = false, dragging = false;
    int selPageIndex = -1;
    PointF selStart, selEnd;

    std::unique_ptr<GtkWlPlugin::GtkFocusManager> focusManager;

    // cleanup
    std::string tempSourcePath, tempPdfPath;

    ~OfficeViewState() {
        if (!tempSourcePath.empty()) unlink(tempSourcePath.c_str());
        if (!tempPdfPath.empty()) unlink(tempPdfPath.c_str());
    }
};

int contentHeight(OfficeViewState *st) { return st->pdf ? st->pdf->totalHeight() : (st->lok ? st->lok->totalHeight() : 0); }
int contentWidth(OfficeViewState *st) { return st->pdf ? st->pdf->maxWidth() : (st->lok ? st->lok->maxWidth() : 0); }

void relayout(OfficeViewState *st) {
    gtk_widget_set_size_request(st->drawing, std::max(1, contentWidth(st)), std::max(1, contentHeight(st)));
    gtk_widget_queue_draw(st->drawing);
}

void copySelectionOrPage(OfficeViewState *st, int targetPage) {
    std::string text;
    if (st->pdf) {
        if (st->hasSelection && st->selPageIndex >= 0 &&
            (st->selStart.x != st->selEnd.x || st->selStart.y != st->selEnd.y)) {
            text = st->pdf->copySelection(st->selPageIndex, st->selStart, st->selEnd);
        } else {
            int page = targetPage >= 0 ? targetPage : st->pdf->pageAtY(0);
            text = st->pdf->copyPageText(page);
        }
    } else if (st->lok) {
        text = st->lok->copyAllText(targetPage);
    }
    if (!text.empty()) {
        GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_text(cb, text.c_str(), -1);
    }
}

int currentVisiblePage(OfficeViewState *st) {
    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(st->scrolled));
    int value = (int)gtk_adjustment_get_value(vadj);
    if (st->pdf) return st->pdf->pageAtY(value);
    if (st->lok) return st->lok->partAtY(value);
    return 0;
}

void syncActiveTab(OfficeViewState *st) {
    if (st->tabButtons.empty() || st->sheetStartPages.empty()) return;
    int currentPage = currentVisiblePage(st);
    int active = 0;
    for (size_t i = 0; i < st->sheetStartPages.size(); ++i) {
        if (currentPage >= st->sheetStartPages[i]) active = (int)i;
        else break;
    }
    st->syncingTabs = true;
    for (size_t i = 0; i < st->tabButtons.size(); ++i)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->tabButtons[i]), (int)i == active);
    st->syncingTabs = false;
}

gboolean onDraw(GtkWidget *, cairo_t *cr, gpointer data) {
    auto *st = (OfficeViewState *)data;

    double clipX1, clipY1, clipX2, clipY2;
    cairo_clip_extents(cr, &clipX1, &clipY1, &clipX2, &clipY2);

    cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
    cairo_paint(cr);

    if (st->pdf) {
        for (const auto &p : st->pdf->pages()) {
            if (p.pixelYOffset + p.pixelHeight < clipY1 || p.pixelYOffset > clipY2) continue;
            const RasterImage &img = st->pdf->pageImage(p.index);
            cairo_surface_t *surf = rasterToSurface(img);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_rectangle(cr, 0, p.pixelYOffset, p.pixelWidth, p.pixelHeight);
            cairo_fill(cr);
            if (surf) {
                cairo_save(cr);
                cairo_translate(cr, 0, p.pixelYOffset);
                cairo_set_source_surface(cr, surf, 0, 0);
                cairo_paint(cr);
                cairo_restore(cr);
                cairo_surface_destroy(surf);
            }
            if (st->hasSelection && st->selPageIndex == p.index) {
                auto quads = st->pdf->highlightQuads(p.index, st->selStart, st->selEnd);
                cairo_set_source_rgba(cr, 80 / 255.0, 140 / 255.0, 1.0, 90 / 255.0);
                float zoom = st->pdf->zoom();
                for (auto &q : quads) {
                    cairo_move_to(cr, q.ulx * zoom, q.uly * zoom + p.pixelYOffset);
                    cairo_line_to(cr, q.urx * zoom, q.ury * zoom + p.pixelYOffset);
                    cairo_line_to(cr, q.lrx * zoom, q.lry * zoom + p.pixelYOffset);
                    cairo_line_to(cr, q.llx * zoom, q.lly * zoom + p.pixelYOffset);
                    cairo_close_path(cr);
                    cairo_fill(cr);
                }
            }
        }
    } else if (st->lok) {
        for (const auto &part : st->lok->parts()) {
            if (part.pixelYOffset + part.pixelHeight < clipY1 || part.pixelYOffset > clipY2) continue;
            int y0 = std::max((int)clipY1, part.pixelYOffset);
            int y1 = std::min((int)clipY2, part.pixelYOffset + part.pixelHeight);
            int x0 = std::max(0, (int)clipX1);
            int x1 = std::min(part.pixelWidth, (int)clipX2);
            if (y1 <= y0 || x1 <= x0) continue;
            RasterImage img = st->lok->paintRect(x0, y0, x1 - x0, y1 - y0, 2);
            cairo_surface_t *surf = rasterToSurface(img);
            if (surf) {
                cairo_set_source_surface(cr, surf, x0, y0);
                cairo_paint(cr);
                cairo_surface_destroy(surf);
            }
        }
    }
    return TRUE;
}

gboolean onButtonPress(GtkWidget *w, GdkEventButton *ev, gpointer data) {
    auto *st = (OfficeViewState *)data;
    gtk_widget_grab_focus(w);
    if (ev->button == 1 && st->pdf) {
        int page = st->pdf->pageAtY((int)ev->y);
        st->selPageIndex = page;
        st->selStart = st->pdf->widgetPosToPagePoint((int)ev->x, (int)ev->y, page);
        st->selEnd = st->selStart;
        st->hasSelection = true;
        st->dragging = true;
        gtk_widget_queue_draw(st->drawing);
    } else if (ev->button == 3) {
        int clickedPage = st->pdf ? st->pdf->pageAtY((int)ev->y) : (st->lok ? st->lok->partAtY((int)ev->y) : 0);
        GtkWidget *menu = gtk_menu_new();
        bool hasRealSelection = st->pdf && st->hasSelection && st->selPageIndex == clickedPage &&
                                 (st->selStart.x != st->selEnd.x || st->selStart.y != st->selEnd.y);
        const char *label = hasRealSelection ? "Copy selection"
                             : (st->lok && st->lok->partCount() > 1) ? "Copy this sheet's text"
                             : "Copy text";
        GtkWidget *item = gtk_menu_item_new_with_label(label);
        g_signal_connect(item, "activate", G_CALLBACK(+[](GtkMenuItem *, gpointer d) {
            auto *pair = (StateIntPair *)d;
            copySelectionOrPage(pair->first, pair->second);
            delete pair;
        }), new StateIntPair(st, clickedPage));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)ev);
    }
    return TRUE;
}

gboolean onMotion(GtkWidget *, GdkEventMotion *ev, gpointer data) {
    auto *st = (OfficeViewState *)data;
    if (!st->dragging || !st->pdf) return FALSE;
    st->selEnd = st->pdf->widgetPosToPagePoint((int)ev->x, (int)ev->y, st->selPageIndex);
    gtk_widget_queue_draw(st->drawing);
    return TRUE;
}

gboolean onButtonRelease(GtkWidget *, GdkEventButton *, gpointer data) {
    ((OfficeViewState *)data)->dragging = false;
    return TRUE;
}

void doZoomIn(OfficeViewState *st) { if (st->pdf) st->pdf->zoomIn(); else if (st->lok) st->lok->zoomIn(); relayout(st); }
void doZoomOut(OfficeViewState *st) { if (st->pdf) st->pdf->zoomOut(); else if (st->lok) st->lok->zoomOut(); relayout(st); }
void doZoomReset(OfficeViewState *st) { if (st->pdf) st->pdf->zoomReset(); else if (st->lok) st->lok->zoomReset(); relayout(st); }

gboolean onScroll(GtkWidget *, GdkEventScroll *ev, gpointer data) {
    auto *st = (OfficeViewState *)data;
    if (!(ev->state & GDK_CONTROL_MASK)) return FALSE;
    if (ev->direction == GDK_SCROLL_UP) doZoomIn(st);
    else if (ev->direction == GDK_SCROLL_DOWN) doZoomOut(st);
    return TRUE;
}

void onVAdjustChanged(GtkAdjustment *, gpointer data) { syncActiveTab((OfficeViewState *)data); }

void onTabClicked(GtkToggleButton *btn, gpointer data) {
    auto *pair = (StateIntPair *)data;
    OfficeViewState *st = pair->first;
    int index = pair->second;
    if (st->syncingTabs || !gtk_toggle_button_get_active(btn)) return;
    if (index < 0 || index >= (int)st->sheetStartPages.size()) return;
    int targetOffset = st->pdf ? st->pdf->pageYOffset(st->sheetStartPages[index])
                                : (st->lok ? st->lok->partYOffset(st->sheetStartPages[index]) : 0);
    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(st->scrolled));
    gtk_adjustment_set_value(vadj, targetOffset);
    syncActiveTab(st);
}

GtkWidget *makeMessageWidget(const std::string &text) {
    GtkWidget *label = gtk_label_new(text.c_str());
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(label, TRUE);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_show(label);
    return label;
}

void destroyState(GtkWidget *, gpointer data) { delete (OfficeViewState *)data; }

} // namespace

extern "C" {

EXPORT HWND DCPCALL ListLoad(HWND ParentWin, char *FileToLoad, int ShowFlags) {
    std::string filePath = FileToLoad;
    std::string ext = extensionOf(filePath);

    Config cfg = loadOrInitConfig();

    std::string effectiveSourcePath = filePath;
    bool isGDriveExport = false;
    long long fsz = fileSize(filePath);
    bool sizeLimited = std::find(kSizeLimitedExtensionsOrdered.begin(), kSizeLimitedExtensionsOrdered.end(), ext) !=
                        kSizeLimitedExtensionsOrdered.end();

    if (fsz <= 0 && sizeLimited) {
        std::string remotePath = rcloneRemotePathFor(filePath);
        if (remotePath.empty())
            return (HWND)makeMessageWidget("File is empty.");
        if (cfg.engineForGDrive == "Disabled")
            return (HWND)makeMessageWidget("Google Drive support is disabled (EngineForGDrive=Disabled in officeview.conf).");

        std::string downloadPath = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp") +
                                    "/officeview_gdrive_" + std::to_string(getpid()) + "." + ext;
        if (!rcloneDownload(remotePath, downloadPath) || fileSize(downloadPath) <= 0) {
            unlink(downloadPath.c_str());
            return (HWND)makeMessageWidget("Could not export " + remotePath + " from Google Drive via rclone.\n"
                                            "Check that rclone is installed, the remote is configured, and you have network access.");
        }
        effectiveSourcePath = downloadPath;
        isGDriveExport = true;
        fsz = fileSize(downloadPath);
    }

    if (sizeLimited) {
        long long limit = cfg.maxFileSizeBytes(ext);
        if (limit < 0) {
            std::string extUp = ext; std::transform(extUp.begin(), extUp.end(), extUp.begin(), ::toupper);
            return (HWND)makeMessageWidget("." + ext + " files are disabled (FileSizeLimits." + extUp + " = -1 in officeview.conf).");
        }
        if (fsz > limit) {
            char buf[256];
            snprintf(buf, sizeof(buf), "File too large to preview (%.1f MB, limit %.1f MB for .%s files).\nAdjust [FileSizeLimits] in officeview.conf to change this.",
                     fsz / 1048576.0, limit / 1048576.0, ext.c_str());
            return (HWND)makeMessageWidget(buf);
        }
    }

    std::string tempSourcePath = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp") +
                                  "/officeview_src_" + std::to_string(getpid()) + "." + ext;
    { std::ifstream in(effectiveSourcePath, std::ios::binary); std::ofstream out(tempSourcePath, std::ios::binary);
      if (in && out) out << in.rdbuf(); }
    if (isGDriveExport) unlink(effectiveSourcePath.c_str());

    bool isOOXML = (ext == "docx" || ext == "xlsx" || ext == "pptx" || ext == "docm" || ext == "xlsm" || ext == "pptm");
    bool isODF = (ext == "odt" || ext == "ods" || ext == "odp");
    bool isLegacyMS = (ext == "doc" || ext == "xls" || ext == "ppt");
    bool isSpreadsheet = (ext == "xlsx" || ext == "xlsm" || ext == "ods");

    std::string selectedEngine = "LibreOffice";
    if (isGDriveExport) selectedEngine = cfg.engineForGDrive;
    else if (isOOXML) selectedEngine = cfg.engineForOOXML;
    else if (isODF) selectedEngine = cfg.engineForODF;
    else if (isLegacyMS) selectedEngine = cfg.engineForLegacyMS;

    if (selectedEngine == "Disabled") {
        unlink(tempSourcePath.c_str());
        return (HWND)makeMessageWidget("This document family is disabled in officeview.conf.");
    }

    std::vector<std::string> sheetNames;
    std::vector<int> sheetRawIndices;
    if (isSpreadsheet)
        sheetNames = extractSpreadsheetSheetNames(tempSourcePath, ext, (ext == "xlsx" || ext == "xlsm") ? &sheetRawIndices : nullptr);

    auto *st = new OfficeViewState();
    st->tempSourcePath = tempSourcePath;
    st->sheetNames = sheetNames;

    bool rendered = false;

    if (selectedEngine == "EuroOffice" || selectedEngine == "OnlyOffice" || selectedEngine == "Auto") {
        X2TConverter wrapper(selectedEngine);
        if (wrapper.isLoaded) {
            std::string outPath = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp") +
                                   "/officeview_" + std::to_string(getpid()) + ".pdf";
            std::vector<int> sheetStartPages;
            bool converted = false;

            if ((ext == "xlsx" || ext == "xlsm") && sheetRawIndices.size() > 1)
                converted = wrapper.convertXlsxAllSheetsPaginated(tempSourcePath, outPath, sheetRawIndices, sheetStartPages);

            if (!converted)
                converted = wrapper.convertToPdf(tempSourcePath, outPath, !sheetNames.empty());

            if (converted) {
                auto pdf = std::make_unique<PdfCore>();
                if (pdf->open(outPath)) {
                    st->pdf = std::move(pdf);
                    st->tempPdfPath = outPath;
                    st->sheetStartPages = sheetStartPages.empty() && !sheetNames.empty()
                        ? std::vector<int>() : sheetStartPages;
                    if (st->sheetStartPages.empty() && !sheetNames.empty() && (int)sheetNames.size() == st->pdf->pageCount()) {
                        for (int i = 0; i < (int)sheetNames.size(); i++) st->sheetStartPages.push_back(i);
                    }
                    if ((int)st->sheetStartPages.size() != (int)sheetNames.size()) { st->sheetStartPages.clear(); st->sheetNames.clear(); }
                    rendered = true;
                }
            } else {
                unlink(outPath.c_str());
            }
        }
    }

    if (!rendered) {
        std::string loPath = findLibreOfficePath(cfg);
        if (!loPath.empty()) {
            auto lok = std::make_unique<LokCore>();
            if (lok->open(loPath, tempSourcePath)) {
                st->lok = std::move(lok);
                if (!sheetNames.empty() && (int)sheetNames.size() == st->lok->partCount()) {
                    st->sheetNames = sheetNames;
                    for (int i = 0; i < (int)sheetNames.size(); i++) st->sheetStartPages.push_back(i);
                } else {
                    st->sheetNames.clear();
                }
                rendered = true;
            }
        }
    }

    if (!rendered) {
        delete st;
        unlink(tempSourcePath.c_str());
        return nullptr;
    }

    // --- Build the widget tree ---
    st->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    // GTK_CONTAINER(ParentWin)->add() doesn't register the child the way
    // GtkLayout expects: DC's own ResizeWindow (uwlxmodule.pas) later calls
    // gtk_layout_move() on this widget, which asserts the widget's parent
    // is exactly this GtkLayout -- only gtk_layout_put() sets that up. This
    // was missing entirely here, which is why the plugin "loaded" (status
    // bar showed its name, no crash) but drew nothing: the fully-built,
    // correctly-sized widget tree was never actually parented into DC's
    // panel, so it was never realized/mapped and "draw" never fired at
    // all. Same fix already applied to markdownview_gtk3/diagramview_gtk3.
    gtk_layout_put(GTK_LAYOUT(GTK_WIDGET(ParentWin)), st->root, 0, 0);

    if (st->sheetNames.size() > 1 && st->sheetNames.size() == st->sheetStartPages.size()) {
        st->tabBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        for (size_t i = 0; i < st->sheetNames.size(); i++) {
            GtkWidget *btn = gtk_toggle_button_new_with_label(st->sheetNames[i].c_str());
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), i == 0);
            g_signal_connect(btn, "toggled", G_CALLBACK(onTabClicked), new StateIntPair(st, (int)i));
            gtk_box_pack_start(GTK_BOX(st->tabBar), btn, FALSE, FALSE, 0);
            st->tabButtons.push_back(btn);
        }
        gtk_box_pack_start(GTK_BOX(st->root), st->tabBar, FALSE, FALSE, 0);
    }

    st->scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    st->drawing = gtk_drawing_area_new();
    gtk_widget_set_can_focus(st->drawing, TRUE);
    gtk_widget_add_events(st->drawing, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                        GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK | GDK_KEY_PRESS_MASK);
    gtk_container_add(GTK_CONTAINER(st->scrolled), st->drawing);
    gtk_box_pack_start(GTK_BOX(st->root), st->scrolled, TRUE, TRUE, 0);

    relayout(st);

    g_signal_connect(st->drawing, "draw", G_CALLBACK(onDraw), st);
    g_signal_connect(st->drawing, "button-press-event", G_CALLBACK(onButtonPress), st);
    g_signal_connect(st->drawing, "motion-notify-event", G_CALLBACK(onMotion), st);
    g_signal_connect(st->drawing, "button-release-event", G_CALLBACK(onButtonRelease), st);
    g_signal_connect(st->drawing, "scroll-event", G_CALLBACK(onScroll), st);
    g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(st->scrolled)), "value-changed",
                      G_CALLBACK(onVAdjustChanged), st);
    g_signal_connect(st->root, "destroy", G_CALLBACK(destroyState), st);
    g_object_set_data(G_OBJECT(st->root), "__officeview_state_ptr", st);

    st->focusManager = std::make_unique<GtkWlPlugin::GtkFocusManager>(st->root, st->drawing);
    st->focusManager->registerShortcut(GDK_KEY_plus, GDK_CONTROL_MASK, GtkWlPlugin::GtkFocusManager::Always,
        [st]() { doZoomIn(st); return true; });
    st->focusManager->registerShortcut(GDK_KEY_equal, GDK_CONTROL_MASK, GtkWlPlugin::GtkFocusManager::Always,
        [st]() { doZoomIn(st); return true; });
    st->focusManager->registerShortcut(GDK_KEY_minus, GDK_CONTROL_MASK, GtkWlPlugin::GtkFocusManager::Always,
        [st]() { doZoomOut(st); return true; });
    st->focusManager->registerShortcut(GDK_KEY_0, GDK_CONTROL_MASK, GtkWlPlugin::GtkFocusManager::Always,
        [st]() { doZoomReset(st); return true; });
    st->focusManager->registerShortcut(GDK_KEY_c, GDK_CONTROL_MASK, GtkWlPlugin::GtkFocusManager::Always,
        [st]() { copySelectionOrPage(st, currentVisiblePage(st)); return true; });

    gtk_widget_show_all(st->root);
    return (HWND)st->root;
}

EXPORT void DCPCALL ListCloseWindow(HWND ListWin) {
    GtkWidget *w = (GtkWidget *)ListWin;
    if (w) gtk_widget_destroy(w); // triggers destroyState() via "destroy"
}

EXPORT void DCPCALL ListGetDetectString(char *DetectString, int maxlen) {
    strncpy(DetectString, _detectstring, maxlen);
}

EXPORT int DCPCALL ListSearchText(HWND, char *, int) {
    return LISTPLUGIN_ERROR;
}

EXPORT int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter) {
    GtkWidget *w = (GtkWidget *)ListWin;
    if (!w) return LISTPLUGIN_ERROR;
    auto *st = (OfficeViewState *)g_object_get_data(G_OBJECT(w), "__officeview_state_ptr");
    if (!st) return LISTPLUGIN_ERROR;

    switch (Command) {
        case lc_focus:
            if (Parameter != 0) gtk_widget_grab_focus(st->drawing);
            return LISTPLUGIN_OK;
        case lc_copy:
            copySelectionOrPage(st, currentVisiblePage(st));
            return LISTPLUGIN_OK;
        case lc_newparams:
            return LISTPLUGIN_OK;
        default:
            return LISTPLUGIN_ERROR;
    }
}

} // extern "C"
