#include "mupdfwidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QTabBar>
#include <cstdio>

// --- MuPdfWidget ---

MuPdfWidget::MuPdfWidget(const QString& pdfPath, QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);

    m_ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!m_ctx) return;
    fz_register_document_handlers(m_ctx);

    fz_try(m_ctx) {
        m_doc = fz_open_document(m_ctx, pdfPath.toUtf8().constData());
    }
    fz_catch(m_ctx) {
        printf("[OfficeView] MuPdfWidget: failed to open %s: %s\n",
               pdfPath.toUtf8().constData(), fz_caught_message(m_ctx));
        fflush(stdout);
        m_doc = nullptr;
    }

    if (m_doc)
        recomputeLayout();
}

MuPdfWidget::~MuPdfWidget() {
    clearImageCache();
    for (auto it = m_stextCache.begin(); it != m_stextCache.end(); ++it)
        fz_drop_stext_page(m_ctx, it.value());
    if (m_doc) fz_drop_document(m_ctx, m_doc);
    if (m_ctx) fz_drop_context(m_ctx);
}

void MuPdfWidget::clearImageCache() {
    m_imageCache.clear();
}

void MuPdfWidget::recomputeLayout() {
    if (!m_doc) return;

    clearImageCache(); // rendered at the old zoom, no longer valid
    m_pages.clear();
    m_totalHeight = 0;
    m_maxWidth = 0;

    int count = 0;
    fz_try(m_ctx) {
        count = fz_count_pages(m_ctx, m_doc);
    }
    fz_catch(m_ctx) {
        count = 0;
    }

    for (int i = 0; i < count; i++) {
        fz_rect bounds{};
        fz_try(m_ctx) {
            fz_page* page = fz_load_page(m_ctx, m_doc, i);
            bounds = fz_bound_page(m_ctx, page);
            fz_drop_page(m_ctx, page);
        }
        fz_catch(m_ctx) {
            continue;
        }

        MuPdfPageInfo info;
        info.index = i;
        info.pixelWidth = (int)((bounds.x1 - bounds.x0) * m_zoom);
        info.pixelHeight = (int)((bounds.y1 - bounds.y0) * m_zoom);
        info.pixelYOffset = m_totalHeight;

        m_totalHeight += info.pixelHeight + 8; // small gap between pages
        if (info.pixelWidth > m_maxWidth) m_maxWidth = info.pixelWidth;

        m_pages.push_back(info);
    }

    setFixedSize(m_maxWidth, m_totalHeight);
    update();
    emit layoutChanged();
}

int MuPdfWidget::pageYOffset(int index) const {
    if (index < 0 || index >= (int)m_pages.size()) return 0;
    return m_pages[index].pixelYOffset;
}

int MuPdfWidget::pageAtY(int y) const {
    for (int i = (int)m_pages.size() - 1; i >= 0; --i) {
        if (y >= m_pages[i].pixelYOffset)
            return i;
    }
    return 0;
}

const QImage& MuPdfWidget::pageImage(int index) {
    auto it = m_imageCache.find(index);
    if (it != m_imageCache.end())
        return it.value();

    // Render at the display's actual device pixel ratio, not just m_zoom.
    // Without this, on any fractional-scaling display the rendered pixmap
    // has fewer physical pixels than the screen needs to fill the widget's
    // logical-pixel area, so Qt stretches/interpolates it up to fit --
    // exactly the softness/aliasing being reported. setDevicePixelRatio()
    // on the resulting QImage tells Qt "this many pixels already accounts
    // for the display density," so drawImage() at the same logical
    // position/size draws it 1:1 in physical pixels instead of scaling.
    qreal dpr = devicePixelRatioF();
    QImage img;
    fz_try(m_ctx) {
        fz_page* page = fz_load_page(m_ctx, m_doc, index);
        fz_matrix ctm = fz_scale(m_zoom * dpr, m_zoom * dpr);
        fz_pixmap* pix = fz_new_pixmap_from_page(m_ctx, page, ctm, fz_device_rgb(m_ctx), 0);
        int w = fz_pixmap_width(m_ctx, pix);
        int h = fz_pixmap_height(m_ctx, pix);
        img = QImage(fz_pixmap_samples(m_ctx, pix), w, h, fz_pixmap_stride(m_ctx, pix), QImage::Format_RGB888).copy();
        img.setDevicePixelRatio(dpr);
        fz_drop_pixmap(m_ctx, pix);
        fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx) {
        printf("[OfficeView] MuPdfWidget: failed to render page %d: %s\n", index, fz_caught_message(m_ctx));
        fflush(stdout);
    }

    return *m_imageCache.insert(index, img);
}

fz_stext_page* MuPdfWidget::pageStext(int index) {
    auto it = m_stextCache.find(index);
    if (it != m_stextCache.end())
        return it.value();

    fz_stext_page* stext = nullptr;
    fz_try(m_ctx) {
        fz_page* page = fz_load_page(m_ctx, m_doc, index);
        stext = fz_new_stext_page_from_page(m_ctx, page, NULL);
        fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx) {
        stext = nullptr;
    }

    m_stextCache.insert(index, stext);
    return stext;
}

fz_point MuPdfWidget::widgetPosToPagePoint(const QPoint& pos, int pageIndex) const {
    int localY = pos.y() - pageYOffset(pageIndex);
    return fz_point{ (float)(pos.x() / m_zoom), (float)(localY / m_zoom) };
}

void MuPdfWidget::zoomIn() {
    m_zoom = qMin(m_zoom * 1.2f, 8.0f);
    recomputeLayout();
}

void MuPdfWidget::zoomOut() {
    m_zoom = qMax(m_zoom / 1.2f, 0.2f);
    recomputeLayout();
}

void MuPdfWidget::zoomReset() {
    m_zoom = 1.5f;
    recomputeLayout();
}

void MuPdfWidget::copyPageText(int pageIndex) {
    fz_stext_page* stext = pageStext(pageIndex);
    if (!stext) return;

    fz_try(m_ctx) {
        fz_rect area = fz_infinite_rect;
        char* text = fz_copy_rectangle(m_ctx, stext, area, 0);
        if (text) {
            QApplication::clipboard()->setText(QString::fromUtf8(text));
            fz_free(m_ctx, text);
        }
    }
    fz_catch(m_ctx) {
        // leave clipboard untouched
    }
}

void MuPdfWidget::copySelectionOrPage(int targetPage) {
    if (m_hasSelection && m_selPageIndex >= 0) {
        fz_stext_page* stext = pageStext(m_selPageIndex);
        if (stext) {
            fz_try(m_ctx) {
                char* text = fz_copy_selection(m_ctx, stext, m_selStart, m_selEnd, 0);
                if (text) {
                    QApplication::clipboard()->setText(QString::fromUtf8(text));
                    fz_free(m_ctx, text);
                }
            }
            fz_catch(m_ctx) {}
            return;
        }
    }
    int page = targetPage >= 0 ? targetPage : pageAtY(0);
    copyPageText(page);
}

void MuPdfWidget::paintEvent(QPaintEvent* event) {
    if (!m_doc) return;

    QRect rect = event->rect();
    QPainter painter(this);
    painter.fillRect(rect, Qt::lightGray);

    for (const auto& p : m_pages) {
        QRect pageRect(0, p.pixelYOffset, p.pixelWidth, p.pixelHeight);
        if (!rect.intersects(pageRect))
            continue;

        const QImage& img = pageImage(p.index);
        if (!img.isNull())
            painter.drawImage(QPoint(0, p.pixelYOffset), img);
        else
            painter.fillRect(pageRect, Qt::white);

        if (m_hasSelection && m_selPageIndex == p.index) {
            fz_stext_page* stext = pageStext(p.index);
            if (stext) {
                fz_quad quads[512];
                int n = 0;
                fz_try(m_ctx) {
                    n = fz_highlight_selection(m_ctx, stext, m_selStart, m_selEnd, quads, 512);
                }
                fz_catch(m_ctx) { n = 0; }

                painter.setBrush(QColor(80, 140, 255, 90));
                painter.setPen(Qt::NoPen);
                for (int i = 0; i < n; i++) {
                    QPolygonF poly;
                    poly << QPointF(quads[i].ul.x * m_zoom, quads[i].ul.y * m_zoom + p.pixelYOffset)
                         << QPointF(quads[i].ur.x * m_zoom, quads[i].ur.y * m_zoom + p.pixelYOffset)
                         << QPointF(quads[i].lr.x * m_zoom, quads[i].lr.y * m_zoom + p.pixelYOffset)
                         << QPointF(quads[i].ll.x * m_zoom, quads[i].ll.y * m_zoom + p.pixelYOffset);
                    painter.drawPolygon(poly);
                }
            }
        }
    }
}

void MuPdfWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    int page = pageAtY(event->pos().y());
    m_selPageIndex = page;
    m_selStart = widgetPosToPagePoint(event->pos(), page);
    m_selEnd = m_selStart;
    m_hasSelection = true;
    m_dragging = true;
    update();
}

void MuPdfWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!m_dragging) return;
    // Clamp to the page the drag started on -- see header comment on
    // m_selPageIndex for why cross-page selection isn't supported yet.
    m_selEnd = widgetPosToPagePoint(event->pos(), m_selPageIndex);
    update();
}

void MuPdfWidget::mouseReleaseEvent(QMouseEvent* event) {
    Q_UNUSED(event);
    m_dragging = false;
}

void MuPdfWidget::contextMenuEvent(QContextMenuEvent* event) {
    int clickedPage = pageAtY(event->pos().y());
    bool hasRealSelection = m_hasSelection && m_selPageIndex == clickedPage &&
        (m_selStart.x != m_selEnd.x || m_selStart.y != m_selEnd.y);

    QMenu menu(this);
    QAction* copyAction = menu.addAction(hasRealSelection ? "Copy selection" : "Copy page text");
    connect(copyAction, &QAction::triggered, this, [this, clickedPage]() {
        copySelectionOrPage(clickedPage);
    });
    menu.exec(event->globalPos());
}

// --- MuPdfContainerWidget ---

MuPdfContainerWidget::MuPdfContainerWidget(const QString& pdfPath, const QStringList& sheetNames,
                                           const QVector<int>& sheetStartPages, QTemporaryFile* sourceFile,
                                           QTemporaryFile* tempPdf, QWidget* parent)
    : QWidget(parent), m_sourceFile(sourceFile), m_tempPdf(tempPdf) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_pdfWidget = new MuPdfWidget(pdfPath);

    bool useTabs = sheetNames.size() > 1 && sheetStartPages.size() == sheetNames.size();
    if (useTabs) {
        m_tabBar = new QTabBar(this);
        m_tabBar->setExpanding(false);
        for (const QString& name : sheetNames)
            m_tabBar->addTab(name);
        layout->addWidget(m_tabBar);
    }

    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidget(m_pdfWidget);
    m_scrollArea->setWidgetResizable(false);
    layout->addWidget(m_scrollArea);

    if (useTabs) {
        // sheetStartPages holds *page indices* (e.g. sheet 2 starts at page
        // 14), not pixel offsets -- comparing it directly against
        // verticalScrollBar()->value() (a pixel position, typically in the
        // hundreds/thousands) meant almost any scroll instantly exceeded a
        // page-index number like 14 or 15, so the tab jumped to the last
        // sheet after a few pixels of scrolling. Convert through
        // pageYOffset()/pageAtY() so both sides of every comparison are in
        // the same unit.
        connect(m_tabBar, &QTabBar::currentChanged, this, [this, sheetStartPages](int index) {
            if (index >= 0 && index < sheetStartPages.size())
                m_scrollArea->verticalScrollBar()->setValue(m_pdfWidget->pageYOffset(sheetStartPages[index]));
        });
        connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this, [this, sheetStartPages](int value) {
            int currentPage = m_pdfWidget->pageAtY(value);
            int active = 0;
            for (int i = 0; i < sheetStartPages.size(); ++i) {
                if (currentPage >= sheetStartPages[i]) active = i;
                else break;
            }
            if (active != m_tabBar->currentIndex())
                m_tabBar->setCurrentIndex(active);
        });
        connect(m_pdfWidget, &MuPdfWidget::layoutChanged, this, [this, sheetStartPages]() {
            int currentPage = m_pdfWidget->pageAtY(m_scrollArea->verticalScrollBar()->value());
            int active = 0;
            for (int i = 0; i < sheetStartPages.size(); ++i) {
                if (currentPage >= sheetStartPages[i]) active = i;
                else break;
            }
            if (active != m_tabBar->currentIndex())
                m_tabBar->setCurrentIndex(active);
        });
    }

    m_focusManager = new QtWlPlugin::FocusManager(this, m_pdfWidget, this);
    m_focusManager->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus), QtWlPlugin::FocusManager::Always,
        [this]() { m_pdfWidget->zoomIn(); return true; });
    m_focusManager->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), QtWlPlugin::FocusManager::Always,
        [this]() { m_pdfWidget->zoomIn(); return true; });
    m_focusManager->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), QtWlPlugin::FocusManager::Always,
        [this]() { m_pdfWidget->zoomOut(); return true; });
    m_focusManager->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), QtWlPlugin::FocusManager::Always,
        [this]() { m_pdfWidget->zoomReset(); return true; });
    m_focusManager->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_C), QtWlPlugin::FocusManager::Always,
        [this]() { copySelectionOrCurrentPage(); return true; });

    // Same viewport-wheel-consumption issue documented for the other two
    // rendering paths -- QScrollArea is a QAbstractScrollArea, intercept on
    // its viewport directly rather than relying on wheelEvent bubbling.
    m_scrollArea->viewport()->installEventFilter(this);
}

MuPdfContainerWidget::~MuPdfContainerWidget() {
    if (m_sourceFile) delete m_sourceFile;
    if (m_tempPdf) delete m_tempPdf;
}

void MuPdfContainerWidget::copySelectionOrCurrentPage() {
    int visiblePage = m_pdfWidget->pageAtY(m_scrollArea->verticalScrollBar()->value());
    m_pdfWidget->copySelectionOrPage(visiblePage);
}

bool MuPdfContainerWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_scrollArea->viewport() && event->type() == QEvent::Wheel) {
        QWheelEvent* wheelEv = static_cast<QWheelEvent*>(event);
        if (wheelEv->modifiers() & Qt::ControlModifier) {
            if (wheelEv->angleDelta().y() > 0) m_pdfWidget->zoomIn();
            else if (wheelEv->angleDelta().y() < 0) m_pdfWidget->zoomOut();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}
