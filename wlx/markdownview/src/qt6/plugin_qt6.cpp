#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTextBrowser>
#include <QTextDocument>
#include <QScrollBar>
#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QPalette>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QUrl>
#include <QPrinter>
#include <QPrintDialog>
#include <QPainter>
#include <QAbstractTextDocumentLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QTextBlock>
#include <QTextImageFormat>
#include <dlfcn.h>
#include <cmath>

#include "wlxplugin.h"
#include "../core/markdown_engine.h"

#define PLUGNAME "markdownview"

static bool g_autoReloadEnabled = true;
static QString g_mode = QStringLiteral("system"); // "system", "dark", "light"
static QString g_themeFilePath;
static QString g_configPath;
// Persisted "Save Zoom" font-size multiplier (1.0 = no change) -- distinct
// from m_zoomLevel below, which is QTextBrowser's own transient in-view
// zoom that resets on reload/reopen. "Save Zoom" bakes the current
// transient zoom into this instead, via a body { font-size: N% } CSS rule
// (see markdown_engine.cpp's postProcessHtml), so it survives reloads.
static double g_zoomMultiplier = 1.0;

static bool isSystemDark() {
    QPalette pal = QGuiApplication::palette();
    return pal.color(QPalette::Window).value() < 128;
}

static bool resolveDarkMode() {
    if (g_mode == QStringLiteral("dark")) return true;
    if (g_mode == QStringLiteral("light")) return false;
    return isSystemDark();
}

static void saveSettings() {
    if (g_configPath.isEmpty()) return;
    QSettings settings(g_configPath, QSettings::IniFormat);
    settings.setValue(PLUGNAME "/theme_file_path", g_themeFilePath);
    settings.setValue(PLUGNAME "/mode", g_mode);
    settings.setValue(PLUGNAME "/auto_reload", g_autoReloadEnabled);
    settings.setValue(PLUGNAME "/zoom_multiplier", g_zoomMultiplier);
    // Explicit sync rather than relying solely on ~QSettings() to flush --
    // no known bug requires this (QSettings' destructor already syncs),
    // but it removes any doubt while diagnosing zoom persistence.
    settings.sync();
}

class MarkdownViewerWidget : public QTextBrowser {
private:
    QString m_filePath;
    QFileSystemWatcher m_watcher;
    QTimer m_debounceTimer;
    int m_zoomLevel = 0;

    // In-document incremental search (Ctrl+F), matching kpartview's
    // markdownpart backend -- this searches the loaded document's own
    // text, not the same thing as Double Commander's own Ctrl+F (that's
    // ListSearchText below, DC's own lister search UI driving find()
    // directly). A small floating bar overlaid on the top-right corner,
    // shown/hidden on demand, rather than restructuring this widget into a
    // container+child layout -- keeps ListLoad's HWND contract (this
    // widget itself) unchanged.
    QWidget* m_findBar = nullptr;
    QLineEdit* m_findEdit = nullptr;

    void ensureFindBar() {
        if (m_findBar) return;
        m_findBar = new QWidget(this);
        m_findBar->setAutoFillBackground(true);
        m_findBar->setStyleSheet("QWidget { background-color: palette(window); border: 1px solid palette(mid); }");

        m_findEdit = new QLineEdit(m_findBar);
        m_findEdit->setPlaceholderText(tr("Find in document..."));
        m_findEdit->setClearButtonEnabled(true);

        QPushButton* prevBtn = new QPushButton(tr("Prev"), m_findBar);
        QPushButton* nextBtn = new QPushButton(tr("Next"), m_findBar);
        QPushButton* closeBtn = new QPushButton(tr("✕"), m_findBar);
        closeBtn->setFixedWidth(24);

        auto* layout = new QHBoxLayout(m_findBar);
        layout->setContentsMargins(6, 4, 6, 4);
        layout->addWidget(m_findEdit);
        layout->addWidget(prevBtn);
        layout->addWidget(nextBtn);
        layout->addWidget(closeBtn);

        connect(m_findEdit, &QLineEdit::textChanged, this, [this](const QString&) { findIncremental(false); });
        connect(m_findEdit, &QLineEdit::returnPressed, this, [this]() { findIncremental(false); });
        connect(nextBtn, &QPushButton::clicked, this, [this]() { findIncremental(false); });
        connect(prevBtn, &QPushButton::clicked, this, [this]() { findIncremental(true); });
        connect(closeBtn, &QPushButton::clicked, this, &MarkdownViewerWidget::hideFindBar);

        m_findBar->hide();
    }

    void positionFindBar() {
        if (!m_findBar) return;
        int w = qMin(320, width() - 16);
        m_findBar->setFixedWidth(w);
        m_findBar->adjustSize();
        m_findBar->move(width() - w - 8, 8);
    }

    void findIncremental(bool backward) {
        if (!m_findEdit) return;
        QString text = m_findEdit->text();
        if (text.isEmpty()) return;

        QTextDocument::FindFlags flags;
        if (backward) flags |= QTextDocument::FindBackward;

        if (find(text, flags)) return;

        // Wrap around: jump the cursor to the start (forward search) or
        // end (backward search) of the document and retry once, same
        // "wraps instead of stopping at the edge" behavior as a normal
        // incremental search bar.
        QTextCursor cursor = textCursor();
        cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        setTextCursor(cursor);
        find(text, flags);
    }

    void showFindBar() {
        ensureFindBar();
        positionFindBar();
        m_findBar->show();
        m_findBar->raise();
        m_findEdit->setFocus();
        m_findEdit->selectAll();
    }

    void hideFindBar() {
        if (m_findBar) m_findBar->hide();
        setFocus();
    }

    // Absolute font-size approach (not Qt's own relative zoomIn/zoomOut)
    // so it's idempotent no matter how many times reloadContent() runs --
    // no bookkeeping needed to "undo" a previous call before reapplying.
    // Confirmed live that a CSS `body { font-size: N%; }` rule has zero
    // effect on Qt's QTextDocument (measured identical rendered text width
    // at 50%/100%/182%), so this widget's own font is the only thing that
    // actually works here.
    qreal m_baseFontPointSize = 0;

    // Confirmed live that QTextBrowser::zoomIn()/zoomOut() (and, by
    // extension, the font-scaling applyZoom() does above) never touch
    // <img> sizing at all -- an image's width/height in QTextImageFormat
    // has to be scaled explicitly. Captured once right after each
    // setHtml() (see reloadContent()), before any zoom is applied, so
    // repeated applyZoom() calls (every wheel notch) always scale from the
    // true natural/unscaled size instead of compounding on top of an
    // already-scaled one.
    QVector<QSize> m_imageNaturalSizes;

    void captureImageNaturalSizes() {
        m_imageNaturalSizes.clear();
        QTextDocument *doc = document();
        if (!doc) return;
        for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                QTextFragment frag = it.fragment();
                if (!frag.isValid()) continue;
                QTextCharFormat fmt = frag.charFormat();
                if (fmt.isImageFormat()) {
                    QTextImageFormat imgFmt = fmt.toImageFormat();
                    m_imageNaturalSizes.append(QSize(qRound(imgFmt.width()), qRound(imgFmt.height())));
                }
            }
        }
    }

    void scaleImages(double multiplier) {
        if (m_imageNaturalSizes.isEmpty()) return;
        QTextDocument *doc = document();
        if (!doc) return;
        QTextCursor cursor(doc);
        int idx = 0;
        for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                QTextFragment frag = it.fragment();
                if (!frag.isValid()) continue;
                QTextCharFormat fmt = frag.charFormat();
                if (!fmt.isImageFormat()) continue;
                if (idx >= m_imageNaturalSizes.size()) break;
                QSize natural = m_imageNaturalSizes[idx++];
                if (natural.width() <= 0 || natural.height() <= 0) continue; // no explicit size to scale from
                QTextImageFormat imgFmt = fmt.toImageFormat();
                imgFmt.setWidth(natural.width() * multiplier);
                imgFmt.setHeight(natural.height() * multiplier);
                cursor.setPosition(frag.position());
                cursor.setPosition(frag.position() + frag.length(), QTextCursor::KeepAnchor);
                cursor.setCharFormat(imgFmt);
            }
        }
    }

    void applyZoom() {
        if (m_baseFontPointSize <= 0) return;
        double totalMultiplier = g_zoomMultiplier * (1.0 + 0.1 * m_zoomLevel);
        QFont f = font();
        f.setPointSizeF(m_baseFontPointSize * totalMultiplier);
        setFont(f);
        // setFont() alone does NOT retroactively rescale content already
        // loaded via setHtml() -- confirmed live via idealWidth() staying
        // identical before/after a setFont()-only call once content
        // exists. QTextDocument::setDefaultFont() is what actually forces
        // the relayout against the new base size.
        if (document()) document()->setDefaultFont(f);
        scaleImages(totalMultiplier);
    }

public:
    MarkdownViewerWidget(QWidget* parent = nullptr) : QTextBrowser(parent) {
        setOpenExternalLinks(true);
        setOpenLinks(true);
        m_baseFontPointSize = font().pointSizeF();

        m_debounceTimer.setSingleShot(true);
        m_debounceTimer.setInterval(200);

        connect(&m_debounceTimer, &QTimer::timeout, this, &MarkdownViewerWidget::reloadContent);
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString&) {
            if (g_autoReloadEnabled) {
                m_debounceTimer.start();
            }
        });
    }

    void loadFile(const QString& filePath) {
        m_filePath = filePath;

        if (!m_watcher.files().isEmpty()) {
            m_watcher.removePaths(m_watcher.files());
        }
        if (QFile::exists(filePath)) {
            m_watcher.addPath(filePath);
        }

        reloadContent();
    }

    void reloadContent() {
        if (m_filePath.isEmpty() || !QFile::exists(m_filePath))
            return;

        bool activeDarkMode = resolveDarkMode();
        std::string html;
        try {
            // md4c parsing, MicroTeX LaTeX rendering, and diagram
            // (mermaid/plantuml) rendering all run synchronously in here --
            // any C++ exception thrown anywhere in that chain would unwind
            // straight across this extern "C"-adjacent call into DC's
            // Pascal caller, which is undefined behavior. Confirmed live:
            // DC's own recovery from that manifests as silently falling
            // back to the next WLX plugin registered for .md (kpartview
            // here) on the NEXT open, not a visible crash -- exactly why
            // markdownview_gtk3's ListLoad already wraps this same call the
            // same way.
            html = MarkdownEngine::renderFileToHtml(
                m_filePath.toStdString(),
                activeDarkMode,
                g_themeFilePath.toStdString()
            );
        } catch (const std::exception &e) {
            setHtml(QStringLiteral("<p>markdownview: failed to render this file: %1</p>").arg(QString::fromUtf8(e.what())));
            return;
        } catch (...) {
            setHtml(QStringLiteral("<p>markdownview: failed to render this file (unknown error).</p>"));
            return;
        }
        QString autoResolvedCss = QString::fromStdString(MarkdownEngine::getLastAutoResolvedCssPath());
        if (!autoResolvedCss.isEmpty() && autoResolvedCss != g_themeFilePath) {
            g_themeFilePath = autoResolvedCss;
            saveSettings();
        }

        // QTextBrowser paints its own viewport background from
        // QPalette::Base BEFORE the document is drawn on top -- a `body {
        // background-color: ... }` CSS rule only colors the document's
        // root frame, not the surrounding widget/viewport, so without this
        // any area the document doesn't fully cover (margins, a
        // shorter-than-viewport document) shows through as whatever the
        // ambient/system palette's Base color is. On a dark system theme
        // that made the "light" markdown theme look mostly dark outside
        // the actual text blocks. Match the same body background colors
        // DEFAULT_CSS uses so the two stay in sync.
        //
        // QPalette::Text/WindowText matter too, not just Base/Window --
        // QTextDocument's CSS engine doesn't reliably cascade a
        // class-scoped `body.theme-light { color: ... }` rule down to
        // every paragraph the way a real browser would, so any text that
        // doesn't inherit it falls back to the palette's default text
        // color. On a dark system theme that default is light/white,
        // which on the light markdown theme's white background rendered
        // as invisible text -- visible only once selected, since selection
        // painting uses a different color pair.
        QPalette pal = palette();
        QColor bg = activeDarkMode ? QColor("#0d1117") : QColor("#ffffff");
        QColor fg = activeDarkMode ? QColor("#c9d1d9") : QColor("#24292e");
        pal.setColor(QPalette::Base, bg);
        pal.setColor(QPalette::Window, bg);
        pal.setColor(QPalette::Text, fg);
        pal.setColor(QPalette::WindowText, fg);
        setPalette(pal);

        int currentScrollX = horizontalScrollBar() ? horizontalScrollBar()->value() : 0;
        int currentScrollY = verticalScrollBar() ? verticalScrollBar()->value() : 0;

        setHtml(QString::fromStdString(html));
        document()->setBaseUrl(QUrl::fromLocalFile(m_filePath).adjusted(QUrl::RemoveFilename));
        captureImageNaturalSizes();
        applyZoom();

        if (horizontalScrollBar()) horizontalScrollBar()->setValue(currentScrollX);
        if (verticalScrollBar()) verticalScrollBar()->setValue(currentScrollY);

        if (QFile::exists(m_filePath) && !m_watcher.files().contains(m_filePath)) {
            m_watcher.addPath(m_filePath);
        }
    }

    void copySelection() {
        if (textCursor().hasSelection()) {
            copy();
        }
    }

    // Prints the SAME QTextDocument this widget already has on screen --
    // whatever theme/CSS reloadContent() most recently applied (dark or
    // light, default or custom) is exactly what ends up on the page, with
    // no separate print stylesheet involved. NOT via QTextDocument::print()
    // directly, though: that call only paints the document's own laid-out
    // content, never the physical page itself -- on the dark theme the
    // printer's native white paper showed through as a border around the
    // (still dark-colored) text. Paint each page's background first, then
    // draw the document's content on top of it.
    void printDocument() {
        QPrinter printer;
        QPrintDialog dialog(&printer, this);
        if (dialog.exec() != QDialog::Accepted) return;

        QColor pageColor = resolveDarkMode() ? QColor("#0d1117") : QColor("#ffffff");

        QTextDocument *doc = document()->clone();
        QSizeF pageSize = printer.pageRect(QPrinter::DevicePixel).size();
        doc->setPageSize(pageSize);

        QPainter painter(&printer);
        int pageCount = doc->pageCount();
        for (int page = 0; page < pageCount; ++page) {
            if (page > 0) printer.newPage();
            painter.fillRect(QRectF(QPointF(0, 0), pageSize), pageColor);

            QAbstractTextDocumentLayout::PaintContext ctx;
            ctx.clip = QRectF(0, page * pageSize.height(), pageSize.width(), pageSize.height());
            painter.save();
            painter.translate(0, -page * pageSize.height());
            doc->documentLayout()->draw(&painter, ctx);
            painter.restore();
        }
        delete doc;
    }

    // Back to the factory default size -- clears BOTH the transient
    // in-view zoom and any persisted "Save Zoom" multiplier, unlike
    // saveZoom() which folds the transient zoom into the persisted one.
    void resetZoom() {
        m_zoomLevel = 0;
        bool wasPersisted = (g_zoomMultiplier != 1.0);
        g_zoomMultiplier = 1.0;
        applyZoom();
        if (wasPersisted) saveSettings();
    }

    // Persists the CURRENT transient zoom (each wheel notch = 10%) into
    // g_zoomMultiplier, then clears the transient zoom back to neutral --
    // the total effective zoom (persisted * transient) stays exactly the
    // same, but it's now entirely in g_zoomMultiplier and survives a
    // reload/reopen, unlike m_zoomLevel alone. Reset Zoom (above) discards
    // this back to the factory default instead.
    void saveZoom() {
        if (m_zoomLevel == 0) return;
        g_zoomMultiplier *= (1.0 + 0.1 * m_zoomLevel);
        if (g_zoomMultiplier < 0.1) g_zoomMultiplier = 0.1;
        m_zoomLevel = 0; // the delta is now folded into g_zoomMultiplier; don't double-apply it
        applyZoom();
        saveSettings();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QTextBrowser::resizeEvent(event);
        positionFindBar();
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->matches(QKeySequence::Find)) {
            showFindBar();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape && m_findBar && m_findBar->isVisible()) {
            hideFindBar();
            event->accept();
            return;
        }
        QTextBrowser::keyPressEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override {
        if (event->modifiers() & Qt::ControlModifier) {
            if (event->angleDelta().y() > 0) {
                m_zoomLevel++;
                applyZoom();
            } else if (event->angleDelta().y() < 0) {
                m_zoomLevel--;
                applyZoom();
            }
            event->accept();
        } else {
            QTextBrowser::wheelEvent(event);
        }
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        QMenu menu(this);

        QAction* copyAction = menu.addAction(tr("Copy Text"));
        copyAction->setEnabled(textCursor().hasSelection());
        connect(copyAction, &QAction::triggered, this, &MarkdownViewerWidget::copySelection);

        QAction* selectAllAction = menu.addAction(tr("Select All"));
        connect(selectAllAction, &QAction::triggered, this, &MarkdownViewerWidget::selectAll);

        QAction* findAction = menu.addAction(tr("Find in Document..."));
        findAction->setShortcut(QKeySequence::Find);
        connect(findAction, &QAction::triggered, this, &MarkdownViewerWidget::showFindBar);

        QAction* printAction = menu.addAction(tr("Print..."));
        connect(printAction, &QAction::triggered, this, &MarkdownViewerWidget::printDocument);

        menu.addSeparator();

        QAction* saveZoomAction = menu.addAction(tr("Save Zoom"));
        saveZoomAction->setEnabled(m_zoomLevel != 0);
        connect(saveZoomAction, &QAction::triggered, this, &MarkdownViewerWidget::saveZoom);

        QAction* resetZoomAction = menu.addAction(tr("Reset Zoom"));
        resetZoomAction->setEnabled(m_zoomLevel != 0 || g_zoomMultiplier != 1.0);
        connect(resetZoomAction, &QAction::triggered, this, &MarkdownViewerWidget::resetZoom);

        menu.addSeparator();

        QAction* reloadAction = menu.addAction(tr("Reload Document"));
        connect(reloadAction, &QAction::triggered, this, &MarkdownViewerWidget::reloadContent);

        QAction* toggleAutoAction = menu.addAction(tr("Auto-Reload on Save"));
        toggleAutoAction->setCheckable(true);
        toggleAutoAction->setChecked(g_autoReloadEnabled);
        connect(toggleAutoAction, &QAction::triggered, this, [](bool checked) {
            g_autoReloadEnabled = checked;
            saveSettings();
        });

        menu.addSeparator();

        QMenu* modeMenu = menu.addMenu(tr("Theme Mode"));
        QActionGroup* modeGroup = new QActionGroup(modeMenu);
        modeGroup->setExclusive(true);

        QAction* modeSystem = modeMenu->addAction(tr("System"));
        modeSystem->setCheckable(true);
        modeSystem->setChecked(g_mode == QStringLiteral("system"));
        modeGroup->addAction(modeSystem);
        connect(modeSystem, &QAction::triggered, this, [this]() {
            g_mode = QStringLiteral("system");
            saveSettings();
            reloadContent();
        });

        QAction* modeDark = modeMenu->addAction(tr("Dark"));
        modeDark->setCheckable(true);
        modeDark->setChecked(g_mode == QStringLiteral("dark"));
        modeGroup->addAction(modeDark);
        connect(modeDark, &QAction::triggered, this, [this]() {
            g_mode = QStringLiteral("dark");
            saveSettings();
            reloadContent();
        });

        QAction* modeLight = modeMenu->addAction(tr("Light"));
        modeLight->setCheckable(true);
        modeLight->setChecked(g_mode == QStringLiteral("light"));
        modeGroup->addAction(modeLight);
        connect(modeLight, &QAction::triggered, this, [this]() {
            g_mode = QStringLiteral("light");
            saveSettings();
            reloadContent();
        });

        menu.exec(event->globalPos());
    }
};

extern "C" {

// Function-try-block: reloadContent() (called from loadFile() below) is
// itself exception-safe now, but widget construction/show() could still
// throw in principle -- same defensive boundary markdownview_gtk3's
// ListLoad already has, for the same reason (see reloadContent()'s comment
// above).
HWND DCPCALL ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags)
try {
    if (!QApplication::instance())
        return nullptr;

    QFileInfo fi(FileToLoad);
    QString ext = fi.suffix().toLower();
    if (ext != "md" && ext != "markdown" && ext != "mdown" && ext != "mkd") {
        return nullptr;
    }

    MarkdownViewerWidget* viewer = new MarkdownViewerWidget((QWidget*)ParentWin);
    viewer->loadFile(QString::fromUtf8(FileToLoad));
    viewer->show();

    return (HWND)viewer;
} catch (const std::exception &e) {
    fprintf(stderr, "[markdownview_qt6] ListLoad EXCEPTION: %s\n", e.what());
    return nullptr;
} catch (...) {
    fprintf(stderr, "[markdownview_qt6] ListLoad UNKNOWN EXCEPTION\n");
    return nullptr;
}

void DCPCALL ListCloseWindow(HWND ListWin)
{
    MarkdownViewerWidget* viewer = (MarkdownViewerWidget*)ListWin;
    if (viewer) {
        delete viewer;
    }
}

int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter)
{
    MarkdownViewerWidget* viewer = (MarkdownViewerWidget*)ListWin;
    if (!viewer) return LISTPLUGIN_ERROR;

    switch (Command) {
    case lc_copy:
        viewer->copySelection();
        return LISTPLUGIN_OK;
    case lc_selectall:
        viewer->selectAll();
        return LISTPLUGIN_OK;
    case lc_newparams:
        viewer->reloadContent();
        return LISTPLUGIN_OK;
    default:
        return LISTPLUGIN_ERROR;
    }
}

int DCPCALL ListSearchText(HWND ListWin, char* SearchString, int SearchParameter)
{
    MarkdownViewerWidget* viewer = (MarkdownViewerWidget*)ListWin;
    if (!viewer || !SearchString) return LISTPLUGIN_ERROR;

    QTextDocument::FindFlags flags;
    if (SearchParameter & lcs_matchcase)
        flags |= QTextDocument::FindCaseSensitively;
    if (SearchParameter & lcs_backwards)
        flags |= QTextDocument::FindBackward;
    if (SearchParameter & lcs_wholewords)
        flags |= QTextDocument::FindWholeWords;

    bool found = viewer->find(QString::fromUtf8(SearchString), flags);
    return found ? LISTPLUGIN_OK : LISTPLUGIN_ERROR;
}

void DCPCALL ListGetDetectString(char* DetectString, int maxlen)
{
    snprintf(DetectString, maxlen - 1, "(EXT=\"MD\" | EXT=\"MARKDOWN\" | EXT=\"MDOWN\" | EXT=\"MKD\") & SIZE<30000000");
}

void DCPCALL ListSetDefaultParams(ListDefaultParamStruct* dps)
{
    if (!dps) return;
    QFileInfo defini(QString::fromUtf8(dps->DefaultIniName));
    g_configPath = defini.absolutePath() + "/markdownview.ini";
    MarkdownEngine::setPluginConfigDir(defini.absolutePath().toStdString());
    QSettings settings(g_configPath, QSettings::IniFormat);

    if (!settings.contains(PLUGNAME "/theme_file_path"))
        settings.setValue(PLUGNAME "/theme_file_path", g_themeFilePath);
    else
        g_themeFilePath = settings.value(PLUGNAME "/theme_file_path").toString();

    if (!settings.contains(PLUGNAME "/mode"))
        settings.setValue(PLUGNAME "/mode", g_mode);
    else
        g_mode = settings.value(PLUGNAME "/mode").toString().toLower();

    if (!settings.contains(PLUGNAME "/auto_reload"))
        settings.setValue(PLUGNAME "/auto_reload", g_autoReloadEnabled);
    else
        g_autoReloadEnabled = settings.value(PLUGNAME "/auto_reload").toBool();

    if (!settings.contains(PLUGNAME "/zoom_multiplier"))
        settings.setValue(PLUGNAME "/zoom_multiplier", g_zoomMultiplier);
    else
        g_zoomMultiplier = settings.value(PLUGNAME "/zoom_multiplier").toDouble();
}

} // extern "C"
