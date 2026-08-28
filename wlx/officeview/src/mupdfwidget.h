#pragma once

extern "C" {
#include <mupdf/fitz.h>
}

#include <QWidget>
#include <QScrollArea>
#include <QMap>
#include <QVector>
#include <QStringList>
#include <QTemporaryFile>
#include <vector>

#include "focus/FocusManager.h"

// Renders a PDF (produced by X2TWrapper's x2t conversion) via MuPDF instead
// of Qt's QPdfView/QPdfDocument. Built to replace QPdfView specifically
// because its page-layout/coordinate-mapping internals are private API with
// no public hook to map a click to a page + point-within-page -- which
// blocked real click-and-drag text selection. MuPDF's structured-text API
// (fz_highlight_selection / fz_copy_selection) gives that directly.
//
// Structurally mirrors LOKWidget: all pages are laid out continuously,
// stacked vertically, and only the pages intersecting the current paint
// rect are rendered/drawn -- same "just enough" rendering philosophy, using
// a per-page QImage cache instead of LOK's tile API (MuPDF doesn't have a
// tile callback; it renders a full page pixmap at a time).
struct MuPdfPageInfo {
    int index;
    int pixelWidth;
    int pixelHeight;
    int pixelYOffset;
};

class MuPdfWidget : public QWidget {
    Q_OBJECT
public:
    explicit MuPdfWidget(const QString& pdfPath, QWidget* parent = nullptr);
    ~MuPdfWidget();

    bool isValid() const { return m_doc != nullptr; }
    int pageCount() const { return (int)m_pages.size(); }
    int pageYOffset(int index) const;
    int pageAtY(int y) const;

    void zoomIn();
    void zoomOut();
    void zoomReset();

    // Copies the current selection's text, if any. Falls back to the whole
    // page passed in (or the page under the current selection/viewport if
    // targetPage < 0) when there's no active selection -- e.g. a right-click
    // with no prior drag, or a keyboard-triggered copy.
    void copySelectionOrPage(int targetPage = -1);

signals:
    void layoutChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void recomputeLayout();
    const QImage& pageImage(int index);
    fz_stext_page* pageStext(int index);
    void clearImageCache();
    fz_point widgetPosToPagePoint(const QPoint& pos, int pageIndex) const;
    void copyPageText(int pageIndex);

    fz_context* m_ctx = nullptr;
    fz_document* m_doc = nullptr;
    std::vector<MuPdfPageInfo> m_pages;
    QMap<int, QImage> m_imageCache;
    QMap<int, fz_stext_page*> m_stextCache;

    float m_zoom = 1.5f;
    int m_totalHeight = 0;
    int m_maxWidth = 0;

    // Selection is scoped to a single page for now -- a drag that crosses a
    // page boundary clamps to the page it started on rather than spanning
    // pages, which MuPDF's per-page structured text doesn't support without
    // extra bookkeeping this doesn't yet do.
    bool m_hasSelection = false;
    bool m_dragging = false;
    int m_selPageIndex = -1;
    fz_point m_selStart{};
    fz_point m_selEnd{};
};

// Wraps the QScrollArea + MuPdfWidget pair with an optional sheet-tab bar
// and FocusManager, mirroring LOKContainerWidget/PdfViewerWidget's structure
// so all three rendering paths share the same interface for ListSendCommand.
class MuPdfContainerWidget : public QWidget {
    Q_OBJECT
public:
    // sheetNames/sheetStartPages: see X2TWrapper::convertXlsxAllSheetsPaginated.
    // Empty sheetNames means no tab bar (non-spreadsheet documents, or a
    // spreadsheet where extraction failed/doesn't apply). sourceFile/tempPdf
    // are owned and deleted here (same lifecycle PdfViewerWidget used to
    // manage) -- tempPdf's file must stay on disk for as long as this widget
    // has the document open.
    MuPdfContainerWidget(const QString& pdfPath, const QStringList& sheetNames,
                         const QVector<int>& sheetStartPages, QTemporaryFile* sourceFile,
                         QTemporaryFile* tempPdf, QWidget* parent = nullptr);
    ~MuPdfContainerWidget();

    bool isValid() const { return m_pdfWidget && m_pdfWidget->isValid(); }
    QtWlPlugin::FocusManager* focusManager() const { return m_focusManager; }
    void copySelectionOrCurrentPage();

    // Transfers ownership of sourceFile back to the caller (sets the
    // internal pointer to null without deleting it) -- for when
    // construction failed to produce a valid document and the caller needs
    // to reuse the same tempSource for a fallback engine, e.g. LibreOfficeKit.
    void releaseSourceFile() { m_sourceFile = nullptr; }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QScrollArea* m_scrollArea;
    MuPdfWidget* m_pdfWidget;
    class QTabBar* m_tabBar = nullptr;
    QtWlPlugin::FocusManager* m_focusManager = nullptr;
    QTemporaryFile* m_sourceFile;
    QTemporaryFile* m_tempPdf;
};
