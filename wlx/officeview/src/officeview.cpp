#include <QApplication>
#include <QWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QPainter>
#include <QImage>
#include <QSettings>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDebug>
#include <QPaintEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QClipboard>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QMap>
#include <QLabel>
#include <QTabBar>
#include <vector>

#include "wlxplugin.h"
#include "focus/FocusManager.h"
#include "mupdfwidget.h"

#define LOK_USE_UNSTABLE_API
#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include <LibreOfficeKit/LibreOfficeKitInit.h>
#include <LibreOfficeKit/LibreOfficeKit.h>

#define _detectstring "EXT=\"ODT\" | EXT=\"DOC\" | EXT=\"DOCX\" | EXT=\"DOCM\" | EXT=\"ODS\" | EXT=\"XLS\" | EXT=\"XLSX\" | EXT=\"XLSM\" | EXT=\"ODP\" | EXT=\"PPT\" | EXT=\"PPTX\" | EXT=\"PPTM\""

// Extensions the plugin will actually attempt to render: MS legacy binary,
// OOXML (including macro-enabled, since those aren't templates), and ODF --
// explicitly excluding template variants (dot/dotx/dotm/xlt/xltx/xltm/
// pot/potx/potm/ott/ots/otp), which aren't in this list or _detectstring.
// Ordered (not a QSet) so officeview.conf's [FileSizeLimits] section is
// generated in a stable, readable order (legacy MS, OOXML, ODF).
static const QStringList kSizeLimitedExtensionsOrdered = {
    "doc", "docx", "docm", "xls", "xlsx", "xlsm", "ppt", "pptx", "pptm",
    "odt", "ods", "odp"
};
static const qint64 kDefaultMaxFileSizeBytes = 3 * 1024 * 1024; // 3MB

// --- Config Utilities ---
//
// officeview.conf layout (INI, QSettings::IniFormat):
//   [Paths]           LibreOfficePath / EuroOfficePath / OnlyOfficePath
//   [Engines]         EngineForOOXML / EngineForODF / EngineForLegacyMS
//   [FileSizeLimits]  one entry per extension, see ensureConfigFileInitialized()

QString getConfigValue(const QString& section, const QString& key, const QString& defaultValue = "") {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/doublecmd";
    QDir().mkpath(configDir);
    QSettings settings(configDir + "/officeview.conf", QSettings::IniFormat);
    return settings.value(section + "/" + key, defaultValue).toString();
}

void setConfigValue(const QString& section, const QString& key, const QString& value) {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/doublecmd";
    QSettings settings(configDir + "/officeview.conf", QSettings::IniFormat);
    settings.setValue(section + "/" + key, value);
}

// Per-extension size limit, in bytes, above which the plugin declines to
// process a file at all. All 12 extensions are pre-populated by
// ensureConfigFileInitialized() (called once at plugin startup), so this
// just reads -- it doesn't lazily create missing keys itself, unlike the
// engine-preference lookups below.
// Returns the configured limit, or -1 meaning "extension disabled entirely"
// (checked by the caller before the file-size comparison, not as part of
// it -- 0 used to be the disable sentinel, but that made a 0-byte file,
// e.g. an unmaterialized rclone/Google Drive stub, incorrectly pass a
// "disabled" extension's check, since 0 > 0 is false). Falls back to the
// 3MB default only when the configured value is missing/malformed, not
// when it's a deliberate 0 or -1.
qint64 getMaxFileSizeBytes(const QString& extLower) {
    QString value = getConfigValue("FileSizeLimits", extLower.toUpper(), "");
    bool ok = false;
    qint64 bytes = value.toLongLong(&ok);
    return (ok && bytes >= -1) ? bytes : kDefaultMaxFileSizeBytes;
}

// ensureConfigFileInitialized() is defined after X2TWrapper/findLibreOfficePath
// below, since it needs both for engine auto-detection; declared here so
// ListLoad (defined even later) can call it without forward-declaration noise.
void ensureConfigFileInitialized();

// A short, centered, no-op message in place of a real preview -- used for
// every "we're deliberately not going to try rendering this" case (over the
// size limit, format family disabled, empty/undownloadable file) so they
// all look and behave the same instead of each reinventing a QLabel.
QWidget* makeMessageWidget(const QString& text, QWidget* parent) {
    QLabel* label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->show();
    return label;
}

// --- LibreOfficeKit Backend ---

static LibreOfficeKit* pOffice = nullptr;
static const int TWIPS_PER_PIXEL = 15;

struct PartInfo {
    int index;
    long width_twips;
    long height_twips;
    int pixel_y_offset;
    int pixel_width;
    int pixel_height;
};

class LOKWidget : public QWidget {
    Q_OBJECT
public:
    LOKWidget(LibreOfficeKitDocument* doc, QTemporaryFile* sourceFile, QWidget* parent = nullptr) : QWidget(parent), pDoc(doc), m_sourceFile(sourceFile) {
        setFocusPolicy(Qt::StrongFocus);
        if (pDoc) {
            // Previously only called for presentations. LOK generally
            // requires this before UNO commands (SelectAll, etc.) and
            // text-selection queries work reliably, for any document type --
            // likely the reason ODT/ODS copy was silently failing or copying
            // stale/wrong-part data.
            pDoc->pClass->initializeForRendering(pDoc, "{}");
            recomputeLayout();
        }
    }

    ~LOKWidget() {
        if (pDoc) pDoc->pClass->destroy(pDoc);
        if (m_sourceFile) delete m_sourceFile;
    }

    int partCount() const { return (int)m_parts.size(); }

    // Vertical pixel offset of a given part (sheet/slide), for the tab bar's
    // click-to-scroll and the reverse scroll-to-active-tab mapping.
    int partYOffset(int index) const {
        if (index < 0 || index >= (int)m_parts.size()) return 0;
        return m_parts[index].pixel_y_offset;
    }

    // Which part is currently the topmost visible one at a given scroll
    // position -- used to keep the tab bar in sync while the user scrolls.
    int partAtY(int y) const {
        for (int i = (int)m_parts.size() - 1; i >= 0; --i) {
            if (y >= m_parts[i].pixel_y_offset)
                return i;
        }
        return 0;
    }

    // Select-all + copy via LOK's UNO command / text-selection API, scoped to
    // one part (sheet/slide; ignored for text documents, which only have
    // part 0). partIndex defaults to -1, meaning "whatever part LOK's
    // internal state currently points to" -- callers that know which part
    // the user actually means (a right-click position, or the part visible
    // in the viewport) should always pass it explicitly. Without this,
    // LOK's "current part" is left over from whatever paintEvent last called
    // setPart() on -- the last part painted, not necessarily what the user
    // clicked or is looking at, which was silently copying the wrong
    // sheet's data (and often incomplete: SelectAll+getTextSelection
    // against whatever cell/part LOK's cursor happened to be parked at).
    void copyAllText(int partIndex = -1) {
        if (!pDoc) {
            printf("[OfficeView] copyAllText: no pDoc\n"); fflush(stdout);
            return;
        }
        if (partIndex >= 0 && partIndex < (int)m_parts.size())
            pDoc->pClass->setPart(pDoc, partIndex);
        printf("[OfficeView] copyAllText: partIndex=%d currentPart=%d\n", partIndex, pDoc->pClass->getPart(pDoc));
        fflush(stdout);
        pDoc->pClass->postUnoCommand(pDoc, ".uno:SelectAll", nullptr, false);
        // SelectAll's effect on LOK's internal document state isn't
        // necessarily synchronous with this call returning. A single fixed
        // 50ms processEvents() wait wasn't reliable -- live logging showed
        // getTextSelection() still intermittently returning null even with
        // it in place. Retry with a short pump-and-check loop instead of a
        // single blind wait.
        char* usedMimeType = nullptr;
        char* text = nullptr;
        for (int attempt = 0; attempt < 8 && !text; ++attempt) {
            if (attempt > 0)
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 25);
            text = pDoc->pClass->getTextSelection(pDoc, "text/plain;charset=utf-8", &usedMimeType);
            if (!text && usedMimeType) { free(usedMimeType); usedMimeType = nullptr; }
        }
        printf("[OfficeView] copyAllText: getTextSelection returned %d chars\n", text ? (int)strlen(text) : -1);
        fflush(stdout);
        if (text) {
            QApplication::clipboard()->setText(QString::fromUtf8(text));
            free(text);
        }
        if (usedMimeType) free(usedMimeType);
        pDoc->pClass->postUnoCommand(pDoc, ".uno:Escape", nullptr, false);
    }

    void zoomIn() { m_zoomFactor = qMin(m_zoomFactor * 1.2, 8.0); recomputeLayout(); }
    void zoomOut() { m_zoomFactor = qMax(m_zoomFactor / 1.2, 0.1); recomputeLayout(); }
    void zoomReset() { m_zoomFactor = 1.0; recomputeLayout(); }

signals:
    void layoutChanged();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override {
        int clickedPart = partAtY(event->pos().y());
        QMenu menu(this);
        QAction* copyAction = menu.addAction(m_parts.size() > 1 ? "Copy this sheet's text" : "Copy all text");
        connect(copyAction, &QAction::triggered, this, [this, clickedPart]() { copyAllText(clickedPart); });
        menu.exec(event->globalPos());
    }

    void paintEvent(QPaintEvent* event) override {
        if (!pDoc) return;

        QRect rect = event->rect();
        QPainter painter(this);
        painter.fillRect(rect, Qt::lightGray);
        // Supersampling: ask LOK for more output pixels than the destination
        // rect (nCanvasWidth/Height) covering the *same* twips range
        // (nTileWidth/Height unchanged), then downscale with smooth
        // interpolation. paintTile's canvas size and tile twips-range are
        // independent parameters, so this doesn't change what content is
        // rendered, just its internal detail before we shrink it back down.
        // Attempt at the reported ODF font-aliasing issue -- LOK's own tile
        // rasterizer (built for fast edit-view redraws, not print fidelity)
        // is inherently lower quality than x2t's PDF rendering pipeline, so
        // this narrows the gap but may not fully close it.
        const int kSupersample = 2;
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        for (const auto& part : m_parts) {
            QRect partRect(0, part.pixel_y_offset, part.pixel_width, part.pixel_height);
            if (rect.intersects(partRect)) {
                QRect intersect = rect.intersected(partRect);

                painter.fillRect(intersect, Qt::white);

                int localX = intersect.x();
                int localY = intersect.y() - part.pixel_y_offset;

                int tilePosX = localX * m_effectiveTwipsPerPixel;
                int tilePosY = localY * m_effectiveTwipsPerPixel;
                int tileWidth = intersect.width() * m_effectiveTwipsPerPixel;
                int tileHeight = intersect.height() * m_effectiveTwipsPerPixel;

                int canvasWidth = intersect.width() * kSupersample;
                int canvasHeight = intersect.height() * kSupersample;

                QByteArray buffer;
                int stride = canvasWidth * 4;
                buffer.resize(canvasHeight * stride);
                buffer.fill((char)255);

                pDoc->pClass->setPart(pDoc, part.index);
                pDoc->pClass->paintTile(pDoc, (unsigned char*)buffer.data(), canvasWidth, canvasHeight, tilePosX, tilePosY, tileWidth, tileHeight);

                QImage image((const uchar*)buffer.constData(), canvasWidth, canvasHeight, stride, QImage::Format_ARGB32);
                painter.drawImage(intersect, image);
            }
        }
    }

private:
    // Recomputes part pixel geometry for the current zoom factor. A higher
    // zoom factor means more pixels per twip, i.e. a smaller effective
    // twips-per-pixel ratio -- inverse of TWIPS_PER_PIXEL / m_zoomFactor.
    void recomputeLayout() {
        if (!pDoc) return;

        m_effectiveTwipsPerPixel = qMax(1, (int)(TWIPS_PER_PIXEL / m_zoomFactor));

        m_parts.clear();
        m_totalHeight = 0;
        m_maxWidth = 0;

        int numParts = pDoc->pClass->getParts(pDoc);
        if (numParts <= 0) numParts = 1;

        for (int i = 0; i < numParts; ++i) {
            pDoc->pClass->setPart(pDoc, i);
            long w = 0, h = 0;
            pDoc->pClass->getDocumentSize(pDoc, &w, &h);

            PartInfo info;
            info.index = i;
            info.width_twips = w;
            info.height_twips = h;
            info.pixel_width = w / m_effectiveTwipsPerPixel;
            info.pixel_height = h / m_effectiveTwipsPerPixel;
            info.pixel_y_offset = m_totalHeight;

            m_totalHeight += info.pixel_height + 20; // 20px gap
            if (info.pixel_width > m_maxWidth) m_maxWidth = info.pixel_width;

            m_parts.push_back(info);
        }
        setFixedSize(m_maxWidth, m_totalHeight);
        pDoc->pClass->setPart(pDoc, 0);
        update();
        emit layoutChanged();
    }

    LibreOfficeKitDocument* pDoc;
    QTemporaryFile* m_sourceFile;
    std::vector<PartInfo> m_parts;
    int m_totalHeight = 0;
    int m_maxWidth = 0;
    double m_zoomFactor = 1.0;
    int m_effectiveTwipsPerPixel = TWIPS_PER_PIXEL;
};

// Wraps the QScrollArea + LOKWidget pair with an optional sheet-tab bar
// (ODS only -- LOK's getParts()/setPart() already gives per-sheet access,
// same mechanism it uses for presentation slides) and FocusManager, mirroring
// PdfViewerWidget's structure so both rendering paths have equivalent
// features and a uniform interface for ListSendCommand.
class LOKContainerWidget : public QWidget {
    Q_OBJECT
public:
    // sheetNames: one entry per LOK "part", in order. Empty for non-ODS
    // documents or when extraction fails -- no tab bar in that case.
    LOKContainerWidget(QScrollArea* scrollArea, LOKWidget* lokWidget, const QStringList& sheetNames, QWidget* parent = nullptr)
      : QWidget(parent), m_scrollArea(scrollArea), m_lokWidget(lokWidget) {
        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        if (sheetNames.size() > 1 && sheetNames.size() == lokWidget->partCount()) {
            m_tabBar = new QTabBar(this);
            m_tabBar->setExpanding(false);
            for (const QString& name : sheetNames)
                m_tabBar->addTab(name);
            layout->addWidget(m_tabBar);
            connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
                if (index >= 0)
                    m_scrollArea->verticalScrollBar()->setValue(m_lokWidget->partYOffset(index));
            });
            connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
                int active = m_lokWidget->partAtY(value);
                if (active != m_tabBar->currentIndex())
                    m_tabBar->setCurrentIndex(active);
            });
            // Zoom changes part pixel offsets; re-sync the active tab.
            connect(m_lokWidget, &LOKWidget::layoutChanged, this, [this]() {
                int active = m_lokWidget->partAtY(m_scrollArea->verticalScrollBar()->value());
                if (active != m_tabBar->currentIndex())
                    m_tabBar->setCurrentIndex(active);
            });
        }

        scrollArea->setParent(this);
        layout->addWidget(scrollArea);

        m_focusManager = new QtWlPlugin::FocusManager(this, lokWidget, this);
        m_focusManager->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus), QtWlPlugin::FocusManager::Always,
            [lokWidget]() { lokWidget->zoomIn(); return true; });
        m_focusManager->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), QtWlPlugin::FocusManager::Always,
            [lokWidget]() { lokWidget->zoomIn(); return true; });
        m_focusManager->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), QtWlPlugin::FocusManager::Always,
            [lokWidget]() { lokWidget->zoomOut(); return true; });
        m_focusManager->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), QtWlPlugin::FocusManager::Always,
            [lokWidget]() { lokWidget->zoomReset(); return true; });
        m_focusManager->registerShortcut(QKeySequence(Qt::CTRL | Qt::Key_C), QtWlPlugin::FocusManager::Always,
            [this]() { copyAllText(); return true; });

        // Same viewport-wheel-consumption issue as QPdfView (see
        // PdfViewerWidget's eventFilter comment) -- QScrollArea is also a
        // QAbstractScrollArea, so intercept on its viewport directly.
        scrollArea->viewport()->installEventFilter(this);
    }

    QtWlPlugin::FocusManager* focusManager() const { return m_focusManager; }

    // No click position available here (keyboard shortcut / DC's lc_copy
    // command), so use whichever part is currently visible at the top of
    // the viewport -- same logic the tab-bar sync uses.
    void copyAllText() {
        int visiblePart = m_lokWidget->partAtY(m_scrollArea->verticalScrollBar()->value());
        m_lokWidget->copyAllText(visiblePart);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (obj == m_scrollArea->viewport() && event->type() == QEvent::Wheel) {
            QWheelEvent* wheelEv = static_cast<QWheelEvent*>(event);
            if (wheelEv->modifiers() & Qt::ControlModifier) {
                if (wheelEv->angleDelta().y() > 0) m_lokWidget->zoomIn();
                else if (wheelEv->angleDelta().y() < 0) m_lokWidget->zoomOut();
                return true;
            }
        }
        return QWidget::eventFilter(obj, event);
    }

private:
    QScrollArea* m_scrollArea;
    LOKWidget* m_lokWidget;
    QTabBar* m_tabBar = nullptr;
    QtWlPlugin::FocusManager* m_focusManager = nullptr;
};

QString findLibreOfficePath() {
    QByteArray envPath = qgetenv("LO_PATH");
    if (!envPath.isEmpty()) {
        QFileInfo fi(envPath);
        if (fi.exists() && fi.isDir()) return QString(envPath);
    }
    
    QString confPath = getConfigValue("Paths", "LibreOfficePath");
    if (!confPath.isEmpty()) {
        QFileInfo fi(confPath);
        if (fi.exists() && fi.isDir()) return confPath;
    }

    QStringList fallbacks = {
        "/usr/lib/libreoffice/program",
        "/usr/lib64/libreoffice/program",
        "/opt/libreoffice/program"
    };
    for (const QString& fb : fallbacks) {
        QFileInfo fi(fb);
        if (fi.exists() && fi.isDir()) {
            setConfigValue("Paths", "LibreOfficePath", fb);
            return fb;
        }
    }
    return QString();
}

// --- Google Drive (via rclone) support ---
//
// rclone's Google Drive backend doesn't materialize native Google Docs/
// Sheets/Slides on read the way it does for regular files -- the mounted
// filesystem shows a 0-byte "stub" with a .docx/.xlsx/.pptx (or .odt/.ods/
// .odp, depending on the user's --drive-export-formats config) extension,
// and any normal file read gets nothing. rclone can still export the real
// content on demand via `rclone copyto <remote>:<path> <dest>`, which makes
// an API call to Google and writes back a real Office/ODF file -- that's
// what this does instead of trying to read the stub directly.

struct RcloneMount {
    QString mountPoint;   // e.g. /home/user/GoogleDrive
    QString remotePrefix; // e.g. "gdrive:" -- already includes the trailing
                          // colon, per /proc/mounts' device field for rclone
                          // FUSE mounts (confirmed against a live mount).
};

QVector<RcloneMount> findRcloneMounts() {
    QVector<RcloneMount> mounts;
    QFile f("/proc/mounts");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return mounts;

    // Use readAll() + split() rather than QTextStream::readLine(), since
    // QTextStream::atEnd() reports true immediately on /proc files (they
    // report size 0), which would make the loop below never execute.
    const QStringList lines = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.size() < 3)
            continue;
        QString device = parts[0];
        QString mountPoint = parts[1];
        QString fstype = parts[2];
        // /proc/mounts octal-escapes whitespace and backslashes in paths.
        mountPoint.replace("\\040", " ").replace("\\011", "\t").replace("\\012", "\n").replace("\\134", "\\");

        if (fstype == "fuse.rclone") {
            RcloneMount m;
            m.mountPoint = mountPoint;
            m.remotePrefix = device;
            mounts.push_back(m);
        }
    }
    return mounts;
}

// Returns the rclone remote path (e.g. "gdrive:Documents/file.docx") for a
// local path under an rclone mount, or an empty string if filePath isn't
// under any rclone mount at all -- the caller's signal that a 0-byte file
// is genuinely empty/corrupt rather than an unmaterialized Google Docs stub.
QString rcloneRemotePathFor(const QString& filePath) {
    for (const RcloneMount& m : findRcloneMounts()) {
        QString prefix = m.mountPoint;
        if (!prefix.endsWith('/')) prefix += '/';
        if (filePath.startsWith(prefix))
            return m.remotePrefix + filePath.mid(prefix.length());
    }
    return QString();
}

bool rcloneDownload(const QString& remotePath, const QString& destPath) {
    QString rcloneBin = QStandardPaths::findExecutable("rclone");
    if (rcloneBin.isEmpty()) {
        printf("[OfficeView] rcloneDownload: rclone binary not found in PATH\n");
        fflush(stdout);
        return false;
    }

    QProcess proc;
    proc.start(rcloneBin, QStringList() << "copyto" << remotePath << destPath);
    // A Drive API export call plus download can take a few seconds for a
    // large document; generous timeout rather than a fast-failing one.
    if (!proc.waitForFinished(60000)) {
        printf("[OfficeView] rcloneDownload: timed out downloading %s\n", remotePath.toUtf8().constData());
        fflush(stdout);
        return false;
    }
    if (proc.exitCode() != 0) {
        printf("[OfficeView] rcloneDownload: rclone exited %d for %s: %s\n",
               proc.exitCode(), remotePath.toUtf8().constData(), proc.readAllStandardError().constData());
        fflush(stdout);
        return false;
    }
    return QFileInfo::exists(destPath) && QFileInfo(destPath).size() > 0;
}

// --- Spreadsheet sheet-name extraction (for the sheet-tab UI) ---
// xlsx/ods are zip archives; shell out to `unzip -p` to pull the relevant
// manifest XML and parse sheet names with a lightweight regex rather than
// pulling in a full zip/XML dependency for this one lookup. Legacy .xls
// (BIFF binary, not a zip) isn't supported here -- falls back to no tabs.
//
// Hidden sheets are excluded: x2t's printPages:"all" only exports visible
// sheets to PDF, but this function was originally extracting every sheet
// name regardless of visibility -- for any file with a hidden sheet, that
// produced more names than actual PDF pages, silently failing
// PdfViewerWidget's names.size()==pageCount() safety check and hiding the
// tab bar entirely. Confirmed via a real 3-sheet (1 hidden) test file: 3
// names extracted, 2 pages produced.
// outRawIndices, when given, receives each visible sheet's 0-based position
// within the FULL <sheets> list (including hidden ones) -- needed to patch
// <workbookView activeTab="N"> correctly, since that attribute indexes into
// the raw sheet list, not the visible-only subset returned in the names list.
// Lightweight page-count-only helper for the qpdf merge's per-sheet
// bookkeeping (see X2TWrapper::convertXlsxAllSheetsPaginated) -- opens and
// immediately drops the document, no rendering.
int muPdfPageCount(const QString& path) {
    fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx) return 1;
    fz_register_document_handlers(ctx);
    int count = 1;
    fz_try(ctx) {
        fz_document* doc = fz_open_document(ctx, path.toUtf8().constData());
        count = qMax(1, fz_count_pages(ctx, doc));
        fz_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        count = 1;
    }
    fz_drop_context(ctx);
    return count;
}

QStringList extractSpreadsheetSheetNames(const QString& filePath, const QString& ext, QVector<int>* outRawIndices = nullptr) {
    QString innerXmlPath;
    QRegularExpression tagRe, nameRe, hiddenRe;
    if (ext == "xlsx" || ext == "xlsm") { // xlsm shares xlsx's internal zip/xml structure
        innerXmlPath = "xl/workbook.xml";
        tagRe = QRegularExpression("<sheet\\b[^>]*/?>");
        nameRe = QRegularExpression("\\bname=\"([^\"]*)\"");
        hiddenRe = QRegularExpression("\\bstate=\"(hidden|veryHidden)\"");
    } else if (ext == "ods") {
        innerXmlPath = "content.xml";
        tagRe = QRegularExpression("<table:table\\b[^>]*>");
        nameRe = QRegularExpression("\\btable:name=\"([^\"]*)\"");
        hiddenRe = QRegularExpression("\\btable:visibility=\"hidden\"");
    } else {
        return {};
    }

    QProcess unzip;
    unzip.start("unzip", QStringList() << "-p" << filePath << innerXmlPath);
    if (!unzip.waitForFinished(5000) || unzip.exitCode() != 0) {
        printf("[OfficeView] extractSpreadsheetSheetNames: unzip failed for %s (exit=%d, stderr=%s)\n",
               filePath.toUtf8().constData(), unzip.exitCode(), unzip.readAllStandardError().constData());
        fflush(stdout);
        return {};
    }

    QString xml = QString::fromUtf8(unzip.readAllStandardOutput());
    printf("[OfficeView] extractSpreadsheetSheetNames: unzip ok, xml length=%d bytes\n", xml.size());
    fflush(stdout);
    QStringList names;
    if (outRawIndices) outRawIndices->clear();
    int rawIndex = 0;
    QRegularExpressionMatchIterator tagIt = tagRe.globalMatch(xml);
    while (tagIt.hasNext()) {
        QString tag = tagIt.next().captured(0);
        int thisIndex = rawIndex++;
        if (hiddenRe.match(tag).hasMatch())
            continue;
        QRegularExpressionMatch nameMatch = nameRe.match(tag);
        if (!nameMatch.hasMatch())
            continue;
        QString name = nameMatch.captured(1);
        name.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"").replace("&apos;", "'");
        names << name;
        if (outRawIndices) outRawIndices->push_back(thisIndex);
    }
    return names;
}

// Creates a copy of an xlsx file with <workbookView activeTab="N"> set to
// rawSheetIndex, so x2t's *default* (no printPages param) PDF export -- which
// correctly paginates a sheet across as many pages as it needs, unlike
// printPages:"all" which forces every sheet onto exactly one page -- targets
// that specific sheet. Used to convert each sheet separately and merge the
// results, rather than trying to get all sheets out of a single x2t call.
bool patchXlsxActiveSheet(const QString& srcPath, const QString& dstPath, int rawSheetIndex) {
    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return false;

    QProcess unzip;
    unzip.start("unzip", QStringList() << "-q" << "-o" << srcPath << "-d" << tempDir.path());
    if (!unzip.waitForFinished(10000) || unzip.exitCode() != 0)
        return false;

    QString workbookPath = tempDir.filePath("xl/workbook.xml");
    QFile workbookFile(workbookPath);
    if (!workbookFile.open(QIODevice::ReadOnly))
        return false;
    QString xml = QString::fromUtf8(workbookFile.readAll());
    workbookFile.close();

    QString activeTabStr = QString("activeTab=\"%1\"").arg(rawSheetIndex);
    QRegularExpression activeTabRe("activeTab=\"\\d+\"");
    if (activeTabRe.match(xml).hasMatch()) {
        xml.replace(activeTabRe, activeTabStr);
    } else {
        QRegularExpression viewTagRe("<workbookView\\b");
        if (viewTagRe.match(xml).hasMatch()) {
            xml.replace(viewTagRe, QString("<workbookView %1 ").arg(activeTabStr));
        } else {
            // No workbookView element at all -- inject a minimal one right
            // after <bookViews> (creating that too if it's missing).
            QRegularExpression bookViewsRe("<bookViews>");
            QString injected = QString("<bookViews><workbookView %1 /></bookViews>").arg(activeTabStr);
            if (bookViewsRe.match(xml).hasMatch())
                xml.replace(bookViewsRe, injected.left(injected.indexOf("<workbookView")));
            else
                xml.replace("<sheets>", injected + "<sheets>");
        }
    }

    if (!workbookFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    workbookFile.write(xml.toUtf8());
    workbookFile.close();

    if (QFile::exists(dstPath))
        QFile::remove(dstPath);

    QProcess zip;
    zip.setWorkingDirectory(tempDir.path());
    zip.start("zip", QStringList() << "-q" << "-r" << dstPath << ".");
    if (!zip.waitForFinished(10000) || zip.exitCode() != 0)
        return false;

    return QFile::exists(dstPath);
}

// --- X2T Backend (Euro-Office / OnlyOffice) ---

class X2TWrapper {
public:
    bool isLoaded = false;
    QString loadedEngine = "";
    QString x2tBin = "";
    QString libPath = "";

    X2TWrapper(const QString& preferredEngine) {
        QStringList searchPaths;
        if (preferredEngine == "EuroOffice") {
            searchPaths << "/opt/euro-office/desktopeditors";
            searchPaths << "/opt/onlyoffice/desktopeditors";
        } else {
            searchPaths << "/opt/onlyoffice/desktopeditors";
            searchPaths << "/opt/euro-office/desktopeditors";
        }

        for (const QString& basePath : searchPaths) {
            QString bin = basePath + "/converter/x2t";
            if (QFileInfo::exists(bin)) {
                x2tBin = bin;
                libPath = basePath;
                isLoaded = true;
                loadedEngine = basePath.contains("euro") ? "EuroOffice" : "OnlyOffice";
                break;
            }
        }
    }

    QString getFontsPath() {
        QString engineLower = loadedEngine.toLower();
        if (engineLower == "eurooffice") engineLower = "euro-office";

        QString home = QDir::homePath();
        QStringList candidates = {
            home + "/.local/share/" + engineLower + "/desktopeditors/data/fonts/AllFonts.js",
            home + "/.local/share/onlyoffice/desktopeditors/data/fonts/AllFonts.js",
            home + "/.local/share/euro-office/desktopeditors/data/fonts/AllFonts.js"
        };
        for (const QString& c : candidates) {
            if (QFileInfo::exists(c)) return c;
        }
        return "";
    }

    // WORKAROUND for a packaging bug: the AllFonts.js/font_selection.bin shipped
    // next to x2t in <install>/converter/ are baked at build time with paths
    // from inside the Docker build container (e.g. /core-fonts/ASC.ttf), which
    // don't exist on any installed system. x2t's PDF-embedding stage (PdfWriter's
    // font manager) reads these bundled files directly and ignores the
    // m_sAllFontsPath override used elsewhere, so images/shapes render but all
    // text is silently dropped from the PDF. The user's own
    // ~/.local/share/<engine>/desktopeditors/data/fonts/AllFonts.js (and its
    // matching font_selection.bin) are regenerated correctly against the real
    // install path, so copying them over the broken bundled copies fixes font
    // embedding without touching core/x2t source. Runs once per process; safe
    // to call before every conversion since it's a cheap no-op once synced.
    bool fontCacheSynced = false;
    void syncFontCacheWorkaround() {
        if (fontCacheSynced || !isLoaded) return;
        fontCacheSynced = true;

        QString fontsJsSrc = getFontsPath();
        if (fontsJsSrc.isEmpty()) return;
        QString fontsDirSrc = QFileInfo(fontsJsSrc).absolutePath();
        QString selectionBinSrc = fontsDirSrc + "/font_selection.bin";
        if (!QFileInfo::exists(selectionBinSrc)) return;

        QString converterDir = libPath + "/converter";
        QString fontsJsDst = converterDir + "/AllFonts.js";
        QString selectionBinDst = converterDir + "/font_selection.bin";

        auto overwrite = [](const QString& src, const QString& dst) -> bool {
            QFile dstFile(dst);
            if (dstFile.exists() && !dstFile.remove()) {
                printf("[OfficeView] font cache workaround: could not remove %s\n", dst.toUtf8().constData());
                fflush(stdout);
                return false;
            }
            if (!QFile::copy(src, dst)) {
                printf("[OfficeView] font cache workaround: could not copy %s -> %s\n", src.toUtf8().constData(), dst.toUtf8().constData());
                fflush(stdout);
                return false;
            }
            return true;
        };

        bool ok = overwrite(fontsJsSrc, fontsJsDst) && overwrite(selectionBinSrc, selectionBinDst);
        printf("[OfficeView] font cache workaround: %s\n", ok ? "synced correct AllFonts.js/font_selection.bin into converter/" : "failed, text may still be missing from generated PDFs");
        fflush(stdout);
    }

    bool convertToPdf(const QString& inputPath, const QString& outputPath, bool allSheets = false) {
        if (!isLoaded) return false;

        syncFontCacheWorkaround();

        QTemporaryFile configXml;
        configXml.setFileTemplate(QDir::tempPath() + "/x2t_config_XXXXXX.xml");
        if (!configXml.open()) return false;

        QString fontPath = getFontsPath();
        QString fontTag = fontPath.isEmpty() ? "" : QString("  <m_sAllFontsPath>%1</m_sAllFontsPath>\n").arg(fontPath);
        // Spreadsheets: x2t's PDF export defaults to the active sheet only.
        // printPages:"all" makes it export every sheet as its own PDF page,
        // which the sheet-tab UI then maps back to sheet names.
        QString jsonParamsTag = allSheets ? "  <m_sJsonParams>{\"printPages\":\"all\"}</m_sJsonParams>\n" : "";

        QString xml = QString("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                              "<TaskQueueDataConvert xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">\n"
                              "  <m_sFileFrom>%1</m_sFileFrom>\n"
                              "  <m_sFileTo>%2</m_sFileTo>\n"
                              "  <m_nFormatTo>513</m_nFormatTo>\n"
                              "  <m_bIsNoBase64>true</m_bIsNoBase64>\n"
                              "%3"
                              "%4"
                              "</TaskQueueDataConvert>").arg(inputPath, outputPath, fontTag, jsonParamsTag);
                              
        configXml.write(xml.toUtf8());
        configXml.flush();
        // ensure it's written before QProcess runs, while keeping the temp file alive
        configXml.close(); 
        
        QProcess proc;
        proc.setWorkingDirectory(libPath + "/converter");
        
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("LD_LIBRARY_PATH", libPath);
        proc.setProcessEnvironment(env);
        
        QString bwrapBin = QStandardPaths::findExecutable("bwrap");
        if (!bwrapBin.isEmpty() && !fontPath.isEmpty()) {
            QString fontSelectionBin = QFileInfo(fontPath).absolutePath() + "/font_selection.bin";
            QString converterDir = libPath + "/converter";
            QStringList bwrapArgs;
            bwrapArgs << "--ro-bind" << "/usr" << "/usr"
                      << "--symlink" << "usr/lib" << "lib"
                      << "--symlink" << "usr/lib64" << "lib64"
                      << "--symlink" << "usr/bin" << "bin"
                      << "--ro-bind" << "/opt" << "/opt"
                      << "--ro-bind" << "/etc" << "/etc"
                      << "--bind" << "/home" << "/home"
                      << "--bind" << "/tmp" << "/tmp"
                      << "--dev" << "/dev" << "--proc" << "/proc";
            // Give converter/ its own writable tmpfs overlay inside the
            // sandbox instead of relying on the real (often root-owned)
            // install directory allowing a new AllFonts.js/font_selection.bin
            // mount point to be created under it -- confirmed live: without
            // a REAL font cache physically present beside x2t, its font
            // matching badly misresolves some fonts (e.g. substituting an
            // unrelated icon font for real text) even with m_sAllFontsPath
            // set correctly in the XML config above, so this isn't optional
            // cosmetic behavior. bwrap resolves every --ro-bind SOURCE
            // against the real host filesystem regardless of what's
            // already mounted in the sandbox under construction, so
            // re-bind each of converter/'s real top-level entries directly
            // onto itself: the source read is against the real (still
            // readable, just not writable) directory, and the target
            // write succeeds because tmpfs already made that path
            // writable in the sandbox. Same fix as GTK3's OfficeCore.cpp
            // -- this is a separate, duplicate implementation of the same
            // logic.
            bwrapArgs << "--tmpfs" << converterDir;
            const QStringList realEntries = QDir(converterDir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
            for (const QString &name : realEntries) {
                if (name == "AllFonts.js" || name == "font_selection.bin") continue;
                bwrapArgs << "--ro-bind" << (converterDir + "/" + name) << (converterDir + "/" + name);
            }
            bwrapArgs << "--ro-bind" << fontPath << (converterDir + "/AllFonts.js")
                      << "--ro-bind" << fontSelectionBin << (converterDir + "/font_selection.bin");
            bwrapArgs << "--chdir" << libPath
                      << x2tBin << configXml.fileName();
            proc.start(bwrapBin, bwrapArgs);
        } else {
            proc.start(x2tBin, QStringList() << configXml.fileName());
        }

        if (proc.waitForFinished(10000)) {
            if (QFileInfo::exists(outputPath) && QFileInfo(outputPath).size() > 0) {
                if (proc.exitCode() != 0) {
                    printf("[OfficeView] x2t exited with %d but generated a valid PDF. Proceeding!\n", proc.exitCode());
                    fflush(stdout);
                }
                return true;
            } else {
                printf("[OfficeView] x2t conversion failed: exit code %d, output missing or empty\n", proc.exitCode());
                fflush(stdout);
            }
        } else {
            printf("[OfficeView] x2t conversion timed out or crashed\n");
            fflush(stdout);
        }
        return false;
    }

    // Converts each of rawSheetIndices' sheets separately (via
    // patchXlsxActiveSheet + the default, properly-paginating conversion
    // mode -- not printPages:"all", which forces every sheet onto exactly
    // one page) and merges the results with qpdf. outSheetStartPages[i] is
    // the 0-based starting page of sheetNames[i]/rawSheetIndices[i] in the
    // merged output, since a sheet may now span multiple pages.
    //
    // Returns false if qpdf isn't available or any step fails; callers
    // should fall back to the single-call printPages:"all" behavior (or no
    // tabs at all) in that case.
    bool convertXlsxAllSheetsPaginated(const QString& inputPath, const QString& outputPath,
                                        const QVector<int>& rawSheetIndices, QVector<int>& outSheetStartPages) {
        outSheetStartPages.clear();
        if (rawSheetIndices.isEmpty()) return false;

        QString qpdfBin = QStandardPaths::findExecutable("qpdf");
        if (qpdfBin.isEmpty()) {
            printf("[OfficeView] convertXlsxAllSheetsPaginated: qpdf not found, falling back\n");
            fflush(stdout);
            return false;
        }

        QList<QTemporaryFile*> perSheetPdfs;
        QStringList mergeArgs;
        bool ok = true;
        QString srcExt = QFileInfo(inputPath).suffix(); // xlsx or xlsm -- keep patched copies' format detectable

        for (int rawIndex : rawSheetIndices) {
            QTemporaryFile patchedXlsx;
            patchedXlsx.setFileTemplate(QDir::tempPath() + "/officeview_sheet_XXXXXX." + srcExt);
            if (!patchedXlsx.open()) { ok = false; break; }
            QString patchedPath = patchedXlsx.fileName();
            patchedXlsx.close();

            if (!patchXlsxActiveSheet(inputPath, patchedPath, rawIndex)) {
                printf("[OfficeView] convertXlsxAllSheetsPaginated: failed to patch activeTab=%d\n", rawIndex);
                fflush(stdout);
                ok = false;
                break;
            }

            auto* sheetPdf = new QTemporaryFile();
            sheetPdf->setFileTemplate(QDir::tempPath() + "/officeview_sheetpdf_XXXXXX.pdf");
            if (!sheetPdf->open()) { delete sheetPdf; ok = false; break; }
            QString sheetPdfPath = sheetPdf->fileName();
            sheetPdf->close();

            // No allSheets/printPages here -- this is exactly the default
            // "convert the active sheet, paginate normally" mode confirmed
            // to correctly span multiple pages.
            if (!convertToPdf(patchedPath, sheetPdfPath, false)) {
                printf("[OfficeView] convertXlsxAllSheetsPaginated: conversion failed for activeTab=%d\n", rawIndex);
                fflush(stdout);
                delete sheetPdf;
                ok = false;
                break;
            }

            perSheetPdfs.push_back(sheetPdf);
            mergeArgs << sheetPdfPath;

            QFile::remove(patchedPath);
        }

        // Start page of each sheet = running total of preceding sheets'
        // actual page counts (not assumed to be 1 each).
        if (ok) {
            int running = 0;
            for (QTemporaryFile* f : perSheetPdfs) {
                outSheetStartPages.push_back(running);
                running += muPdfPageCount(f->fileName());
            }
        }

        if (ok && !mergeArgs.isEmpty()) {
            QProcess merge;
            QStringList args;
            args << "--empty" << "--pages" << mergeArgs << "--" << outputPath;
            merge.start(qpdfBin, args);
            if (!merge.waitForFinished(15000) || !QFileInfo::exists(outputPath) || QFileInfo(outputPath).size() == 0) {
                printf("[OfficeView] convertXlsxAllSheetsPaginated: qpdf merge failed, exit=%d\n", merge.exitCode());
                fflush(stdout);
                ok = false;
            }
        }

        for (QTemporaryFile* f : perSheetPdfs) delete f;
        return ok;
    }
};

// Creates officeview.conf with the full [Paths]/[Engines]/[FileSizeLimits]
// layout (including the extensions the user hasn't opened yet -- previously
// each MaxFileSizeBytes_<EXT> key only appeared after that extension's
// first use, which is why the file never had all 12 entries) if it doesn't
// already exist in this format. Preserves any values already present under
// the old flat [Settings] layout, or from a previous run of this function,
// rather than clobbering them. Engine/path values that are still unset fall
// back to the same auto-detection ListLoad used to do inline.
void ensureConfigFileInitialized() {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/doublecmd";
    QDir().mkpath(configDir);
    QString confPath = configDir + "/officeview.conf";

    QFile checkFile(confPath);
    if (checkFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString existing = QString::fromUtf8(checkFile.readAll());
        checkFile.close();
        // EngineForGDrive is checked too, not just the section headers, so
        // a config file from before Google Drive support was added gets one
        // more regeneration pass (which preserves every value the user
        // already set -- see the pull-forward reads below) instead of
        // silently missing the new key forever.
        if (existing.contains("[Paths]") && existing.contains("[FileSizeLimits]") && existing.contains("EngineForGDrive"))
            return; // already in the current format
    }

    // Pull forward any values already set, whether from the old flat
    // [Settings] layout or a fresh install with nothing yet.
    QString loPath = getConfigValue("Settings", "LibreOfficePath", getConfigValue("Paths", "LibreOfficePath", ""));
    QString euroPath = getConfigValue("Settings", "EuroOfficePath", getConfigValue("Paths", "EuroOfficePath", ""));
    QString onlyPath = getConfigValue("Settings", "OnlyOfficePath", getConfigValue("Paths", "OnlyOfficePath", ""));
    QString engineOOXML = getConfigValue("Settings", "EngineForOOXML", getConfigValue("Engines", "EngineForOOXML", ""));
    QString engineODF = getConfigValue("Settings", "EngineForODF", getConfigValue("Engines", "EngineForODF", ""));
    QString engineLegacyMS = getConfigValue("Settings", "EngineForLegacyMS", getConfigValue("Engines", "EngineForLegacyMS", ""));
    QString engineGDrive = getConfigValue("Engines", "EngineForGDrive", "");

    QMap<QString, qint64> sizeLimits;
    for (const QString& ext : kSizeLimitedExtensionsOrdered) {
        QString extUpper = ext.toUpper();
        QString v = getConfigValue("Settings", "MaxFileSizeBytes_" + extUpper,
                                    getConfigValue("FileSizeLimits", extUpper, ""));
        bool ok = false;
        qint64 bytes = v.toLongLong(&ok);
        sizeLimits[ext] = (ok && bytes >= -1) ? bytes : kDefaultMaxFileSizeBytes;
    }

    if (engineOOXML.isEmpty()) {
        X2TWrapper checkEngines("EuroOffice");
        if (checkEngines.isLoaded) {
            engineOOXML = checkEngines.loadedEngine;
            if (checkEngines.loadedEngine == "EuroOffice") euroPath = checkEngines.libPath;
            else onlyPath = checkEngines.libPath;
        } else {
            engineOOXML = "LibreOffice";
        }
    }
    if (engineODF.isEmpty()) engineODF = "LibreOffice";
    if (engineLegacyMS.isEmpty()) engineLegacyMS = engineOOXML;
    // Google Docs/Sheets/Slides export to OOXML (or ODF, depending on the
    // user's rclone --drive-export-formats setting) -- same file, same
    // rendering pipeline, but independently configurable since a user might
    // reasonably want a different engine for gdrive exports than for local
    // OOXML files.
    if (engineGDrive.isEmpty()) engineGDrive = engineOOXML;
    if (loPath.isEmpty()) loPath = findLibreOfficePath();

    QFile f(confPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    QTextStream out(&f);

    out << "[Paths]\n";
    out << "LibreOfficePath=" << loPath << "\n";
    out << "EuroOfficePath=" << euroPath << "\n";
    out << "OnlyOfficePath=" << onlyPath << "\n";

    out << "\n; Valid values: EuroOffice, OnlyOffice, LibreOffice, or\n";
    out << "; Disabled (skip this format family entirely, showing a short\n";
    out << "; message instead of attempting to render it).\n";
    out << "[Engines]\n";
    out << "EngineForOOXML=" << engineOOXML << "\n";
    out << "EngineForODF=" << engineODF << "\n";
    out << "EngineForLegacyMS=" << engineLegacyMS << "\n";
    out << "; Native Google Docs/Sheets/Slides, exported on the fly via\n";
    out << "; rclone (requires an rclone mount and the rclone binary --\n";
    out << "; see README.md).\n";
    out << "EngineForGDrive=" << engineGDrive << "\n";

    out << "\n; Size limit in bytes. Files larger than this are not opened at\n";
    out << "; all -- the plugin doesn't attempt to process them. Set a value\n";
    out << "; to -1 to effectively disable the plugin for that extension.\n";
    out << "; (0 is a valid, if impractical, limit -- only 0-byte files would\n";
    out << "; pass -- so it's no longer the disable sentinel: a 0-byte file\n";
    out << "; can legitimately be an unmaterialized rclone/Google Drive stub\n";
    out << "; that the plugin is about to export and re-check the size of,\n";
    out << "; not something to reject outright.)\n";
    out << "[FileSizeLimits]\n";
    for (const QString& ext : kSizeLimitedExtensionsOrdered)
        out << ext.toUpper() << "=" << sizeLimits[ext] << "\n";

    f.close();
}


// --- Plugin Entry Points ---

static void officeview_unload() {
    if (pOffice) {
        pOffice->pClass->destroy(pOffice);
        pOffice = nullptr;
    }
}

extern "C" {
    HWND DCPCALL ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
        QString filePath = QString::fromUtf8(FileToLoad);
        QString ext = QFileInfo(filePath).suffix().toLower();

        // Creates/migrates officeview.conf's [Paths]/[Engines]/[FileSizeLimits]
        // sections (all 12 extensions pre-populated) on first call; a no-op
        // once the file is already in that format.
        ensureConfigFileInitialized();

        // effectiveSourcePath is what actually gets copied into tempSource
        // below -- filePath itself, unless this turns out to be an
        // unmaterialized rclone/Google Drive stub, in which case it's the
        // path rclone exported the real content to.
        QString effectiveSourcePath = filePath;
        bool isGDriveExport = false;

        qint64 fileSize = QFileInfo(filePath).size();
        if (fileSize <= 0 && kSizeLimitedExtensionsOrdered.contains(ext)) {
            QString remotePath = rcloneRemotePathFor(filePath);
            if (remotePath.isEmpty()) {
                // Not under any rclone mount -- a genuinely empty or
                // corrupt file, not a Google Docs/Sheets/Slides stub.
                // Fail gracefully instead of handing x2t/LOK a 0-byte file
                // and letting them fail in some less obvious way.
                printf("[OfficeView] %s is empty and not under an rclone mount, skipping\n", filePath.toUtf8().constData());
                fflush(stdout);
                return (HWND)makeMessageWidget("File is empty.", (QWidget*)ParentWin);
            }

            QString engineGDrive = getConfigValue("Engines", "EngineForGDrive", "");
            if (engineGDrive == "Disabled") {
                return (HWND)makeMessageWidget(
                    "Google Drive support is disabled (EngineForGDrive=Disabled in officeview.conf).",
                    (QWidget*)ParentWin);
            }

            printf("[OfficeView] %s looks like an unmaterialized Google Docs/Sheets/Slides stub, exporting %s via rclone...\n",
                   filePath.toUtf8().constData(), remotePath.toUtf8().constData());
            fflush(stdout);

            QTemporaryFile gdriveDownload;
            gdriveDownload.setFileTemplate(QDir::tempPath() + "/officeview_gdrive_XXXXXX." + ext);
            if (!gdriveDownload.open()) {
                return (HWND)makeMessageWidget("Could not create a temporary file for the Google Drive export.", (QWidget*)ParentWin);
            }
            QString downloadPath = gdriveDownload.fileName();
            gdriveDownload.close();
            // gdriveDownload goes out of scope at the end of this block, and
            // a QTemporaryFile deletes its underlying file on destruction by
            // default -- but effectiveSourcePath isn't read into tempSource
            // until further down, well after that scope ends. Disable
            // auto-removal so downloadPath survives to be copied; it's
            // cleaned up explicitly below once that copy is done.
            gdriveDownload.setAutoRemove(false);

            if (!rcloneDownload(remotePath, downloadPath) || QFileInfo(downloadPath).size() <= 0) {
                QFile::remove(downloadPath);
                return (HWND)makeMessageWidget(
                    QString("Could not export %1 from Google Drive via rclone.\nCheck that rclone is installed, the remote is configured, and you have network access.")
                        .arg(remotePath),
                    (QWidget*)ParentWin);
            }

            effectiveSourcePath = downloadPath;
            isGDriveExport = true;
            fileSize = QFileInfo(downloadPath).size();
            printf("[OfficeView] rclone export succeeded: %s (%lld bytes)\n", downloadPath.toUtf8().constData(), (long long)fileSize);
            fflush(stdout);
        }

        // Per-extension size limit -- checked before any temp copy or
        // conversion work, so an oversized file costs nothing beyond a
        // stat() call (or, for a Google Drive export, right after the
        // download -- the limit still applies to what we'd actually be
        // rendering). Config-driven ([FileSizeLimits] section, see
        // getMaxFileSizeBytes), 3MB default, applies to every extension the
        // plugin handles except templates (dot/dotx/xlt/xltx/pot/potx/ott/
        // ots/otp -- none of which are in kSizeLimitedExtensionsOrdered or
        // _detectstring to begin with). A limit of -1 disables the
        // extension outright, regardless of the file's actual size.
        if (kSizeLimitedExtensionsOrdered.contains(ext)) {
            qint64 limit = getMaxFileSizeBytes(ext);
            if (limit < 0) {
                return (HWND)makeMessageWidget(
                    QString(".%1 files are disabled (FileSizeLimits.%2 = -1 in officeview.conf).").arg(ext, ext.toUpper()),
                    (QWidget*)ParentWin);
            }
            if (fileSize > limit) {
                printf("[OfficeView] %s (%lld bytes) exceeds the %lld byte limit for .%s, skipping\n",
                       effectiveSourcePath.toUtf8().constData(), (long long)fileSize, (long long)limit, ext.toUtf8().constData());
                fflush(stdout);
                return (HWND)makeMessageWidget(
                    QString("File too large to preview (%1 MB, limit %2 MB for .%3 files).\nAdjust [FileSizeLimits] %4 in officeview.conf to change this.")
                        .arg(fileSize / 1048576.0, 0, 'f', 1)
                        .arg(limit / 1048576.0, 0, 'f', 1)
                        .arg(ext)
                        .arg(ext.toUpper()),
                    (QWidget*)ParentWin);
            }
        }

        // ensureConfigFileInitialized() already populated these; the empty
        // fallbacks here are just defensive (e.g. if the user hand-edited
        // the file and blanked a value out).
        //
        // ODF always defaults to LibreOffice -- its ODF rendering fidelity is
        // meaningfully better than x2t's, and this is a deliberate format-family
        // split (LibreOffice for ODF, EuroOffice/OnlyOffice for OOXML), not an
        // oversight. The LOKWidget path has its own focus management, copy, and
        // context menu (see FocusManager wiring below and LOKWidget::copyAllText)
        // so it doesn't need to borrow PdfViewerWidget's features via x2t.
        QString enginePrefOOXML = getConfigValue("Engines", "EngineForOOXML", "EuroOffice");
        QString enginePrefODF = getConfigValue("Engines", "EngineForODF", "LibreOffice");
        QString enginePrefLegacyMS = getConfigValue("Engines", "EngineForLegacyMS", enginePrefOOXML);
        QString enginePrefGDrive = getConfigValue("Engines", "EngineForGDrive", enginePrefOOXML);

        // Copy to temp file to prevent doublecmd .lock file focus stealing loop
        QTemporaryFile* tempSource = new QTemporaryFile();
        tempSource->setFileTemplate(QDir::tempPath() + "/officeview_src_XXXXXX." + ext);
        if (!tempSource->open()) {
            delete tempSource;
            return nullptr;
        }

        QFile srcFile(effectiveSourcePath);
        if (srcFile.open(QIODevice::ReadOnly)) {
            tempSource->write(srcFile.readAll());
            srcFile.close();
        }
        tempSource->close(); // Close so external engines can read it

        if (isGDriveExport) {
            QFile::remove(effectiveSourcePath); // no longer needed once copied into tempSource
        }

        bool isOOXML = (ext == "docx" || ext == "xlsx" || ext == "pptx" || ext == "docm" || ext == "xlsm" || ext == "pptm");
        bool isODF = (ext == "odt" || ext == "ods" || ext == "odp");
        bool isLegacyMS = (ext == "doc" || ext == "xls" || ext == "ppt");
        bool isSpreadsheet = (ext == "xlsx" || ext == "xlsm" || ext == "ods"); // legacy .xls (BIFF) unsupported for tabs

        QString selectedEngine = "LibreOffice";
        if (isGDriveExport) selectedEngine = enginePrefGDrive;
        else if (isOOXML) selectedEngine = enginePrefOOXML;
        else if (isODF) selectedEngine = enginePrefODF;
        else if (isLegacyMS) selectedEngine = enginePrefLegacyMS;

        if (selectedEngine == "Disabled") {
            delete tempSource;
            QString familyName = isGDriveExport ? "Google Drive exports" : isOOXML ? ".docx/.xlsx/.pptx (and macro-enabled variants)"
                                  : isODF ? ".odt/.ods/.odp" : isLegacyMS ? "legacy .doc/.xls/.ppt" : ext;
            return (HWND)makeMessageWidget(
                QString("%1 are disabled in officeview.conf.").arg(familyName),
                (QWidget*)ParentWin);
        }

        // Computed once up front so both the x2t/PdfViewerWidget path and the
        // LibreOfficeKit/LOKContainerWidget path can use it for their tab bars.
        QStringList sheetNames;
        QVector<int> sheetRawIndices; // xlsx only, for activeTab patching
        if (isSpreadsheet)
            sheetNames = extractSpreadsheetSheetNames(tempSource->fileName(), ext, (ext == "xlsx" || ext == "xlsm") ? &sheetRawIndices : nullptr);
        printf("[OfficeView] sheet extraction: ext=%s isSpreadsheet=%d tempSource=%s sheetNames=%d sheetRawIndices=%d names=[%s]\n",
               ext.toUtf8().constData(), isSpreadsheet, tempSource->fileName().toUtf8().constData(),
               sheetNames.size(), sheetRawIndices.size(), sheetNames.join(",").toUtf8().constData());
        fflush(stdout);

        // Attempt x2t engines
        if (selectedEngine == "EuroOffice" || selectedEngine == "OnlyOffice" || selectedEngine == "Auto") {
            X2TWrapper wrapper(selectedEngine);
            if (wrapper.isLoaded) {
                printf("[OfficeView] Attempting to render %s with %s (x2t)...\n", filePath.toUtf8().constData(), wrapper.loadedEngine.toUtf8().constData());
                fflush(stdout);

                QTemporaryFile* tempPdf = new QTemporaryFile();
                tempPdf->setFileTemplate(QDir::tempPath() + "/officeview_XXXXXX.pdf");
                if (tempPdf->open()) {
                    QString outPath = tempPdf->fileName();
                    tempPdf->close();

                    QVector<int> sheetStartPages;
                    bool converted = false;

                    // Proper multi-page-per-sheet pagination: convert each
                    // sheet separately and merge. Only for xlsx (activeTab
                    // patching is xlsx-specific) with more than one visible
                    // sheet -- a single sheet needs no merging at all.
                    if ((ext == "xlsx" || ext == "xlsm") && sheetRawIndices.size() > 1) {
                        converted = wrapper.convertXlsxAllSheetsPaginated(tempSource->fileName(), outPath, sheetRawIndices, sheetStartPages);
                        if (!converted) {
                            printf("[OfficeView] Per-sheet paginated conversion unavailable/failed, falling back to printPages:all (sheets may be squeezed onto one page each)\n");
                            fflush(stdout);
                        }
                    }

                    if (!converted)
                        converted = wrapper.convertToPdf(tempSource->fileName(), outPath, !sheetNames.isEmpty());
                    if (converted) {
                        // pageCount() mismatch on the legacy fallback path
                        // (e.g. a sheet spanning multiple pages under
                        // printPages:"all") means the 1-sheet-per-page
                        // assumption doesn't hold; MuPdfContainerWidget
                        // silently skips the tab bar in that case rather
                        // than showing a wrong mapping.
                        MuPdfContainerWidget* pdfWidget = new MuPdfContainerWidget(
                            outPath, sheetNames, sheetStartPages, tempSource, tempPdf, (QWidget*)ParentWin);
                        if (pdfWidget->isValid()) {
                            printf("[OfficeView] Successfully rendered %s with %s (x2t)\n", filePath.toUtf8().constData(), wrapper.loadedEngine.toUtf8().constData());
                            fflush(stdout);
                            pdfWidget->show();
                            return (HWND)pdfWidget;
                        }
                        // tempSource is reused below for the LibreOfficeKit
                        // fallback if this whole x2t attempt fails; tempPdf
                        // (the x2t output specific to this failed attempt)
                        // is fine to let the widget's destructor clean up.
                        pdfWidget->releaseSourceFile();
                        delete pdfWidget;
                        tempPdf = nullptr;
                    }
                }
                delete tempPdf;

                // Fallback to LO if x2t conversion failed
                printf("[OfficeView] Falling back to LibreOfficeKit for %s\n", filePath.toUtf8().constData());
                fflush(stdout);
                selectedEngine = "LibreOffice";
            } else if (enginePrefOOXML == "Auto" || enginePrefODF == "Auto") {
                selectedEngine = "LibreOffice";
            }
        }
        
        // LibreOffice engine
        if (selectedEngine == "LibreOffice") {
            printf("[OfficeView] Rendering %s with LibreOfficeKit\n", filePath.toUtf8().constData());
            fflush(stdout);
            
            if (!pOffice) {
                QString loPath = findLibreOfficePath();
                if (!loPath.isEmpty()) {
                    QString profileUri = "file://" + QDir::tempPath() + "/lok_profile_officeview";
                    pOffice = lok_init_2(loPath.toUtf8().constData(), profileUri.toUtf8().constData());
                    if (pOffice) {
                        atexit(officeview_unload);
                    }
                }
            }
            
            if (pOffice) {
                LibreOfficeKitDocument* pDoc = pOffice->pClass->documentLoad(pOffice, tempSource->fileName().toUtf8().constData());
                if (pDoc) {
                    // scrollArea has no parent yet -- LOKContainerWidget's
                    // constructor reparents it into its own layout.
                    QScrollArea* scrollArea = new QScrollArea();
                    LOKWidget* lokWidget = new LOKWidget(pDoc, tempSource, scrollArea);
                    scrollArea->setWidget(lokWidget);
                    scrollArea->setWidgetResizable(false);
                    LOKContainerWidget* container = new LOKContainerWidget(scrollArea, lokWidget, sheetNames, (QWidget*)ParentWin);
                    container->show();
                    return (HWND)container;
                }
            }
        }
        
        delete tempSource;
        return nullptr;
    }

    void DCPCALL ListCloseWindow(HWND ListWin) {
        QWidget* widget = (QWidget*)ListWin;
        delete widget;
    }

    void DCPCALL ListGetDetectString(char* DetectString, int maxlen) {
        strncpy(DetectString, _detectstring, maxlen);
    }

    int DCPCALL ListSearchText(HWND ListWin, char* SearchString, int SearchParameter) {
        return LISTPLUGIN_ERROR;
    }

    int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter) {
        QWidget* widget = (QWidget*)ListWin;
        if (!widget) return LISTPLUGIN_ERROR;

        switch (Command) {
            case lc_focus:
                // DC uses lc_focus to hand focus to/reclaim focus from the plugin.
                // Parameter: 1 = focus gained, 0 = focus lost. All rendering
                // paths (x2t/MuPdfContainerWidget and LibreOfficeKit/LOKContainerWidget)
                // use QtWlPlugin::FocusManager, which also self-manages focus
                // via a QApplication-level event filter -- confirmed via live
                // logging that DC sends an explicit lc_focus(0) immediately
                // after a click that FocusManager had already correctly
                // activated internally, undoing it before the next keypress
                // arrived (the actual cause of a "must click twice" symptom).
                // setActiveFromHost() ignores deactivation requests that
                // arrive within a short grace period of such an activation.
                if (MuPdfContainerWidget* pdfWidget = qobject_cast<MuPdfContainerWidget*>(widget)) {
                    pdfWidget->focusManager()->setActiveFromHost(Parameter != 0);
                } else if (LOKContainerWidget* lokContainer = qobject_cast<LOKContainerWidget*>(widget)) {
                    lokContainer->focusManager()->setActiveFromHost(Parameter != 0);
                }
                return LISTPLUGIN_OK;
            case lc_copy:
                if (MuPdfContainerWidget* pdfWidget = qobject_cast<MuPdfContainerWidget*>(widget)) {
                    pdfWidget->copySelectionOrCurrentPage();
                    return LISTPLUGIN_OK;
                } else if (LOKContainerWidget* lokContainer = qobject_cast<LOKContainerWidget*>(widget)) {
                    lokContainer->copyAllText();
                    return LISTPLUGIN_OK;
                }
                return LISTPLUGIN_ERROR;
            case lc_newparams:
                // Accept and ignore to avoid DC destroying/recreating the plugin
                // on unrelated file-change notifications.
                return LISTPLUGIN_OK;
            default:
                return LISTPLUGIN_ERROR;
        }
    }
}

#include "officeview.moc"
