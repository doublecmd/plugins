#pragma once

#include <string>
#include <vector>
#include <cstdint>

extern "C" {
#include <mupdf/fitz.h>
}

// Toolkit-neutral office-document rendering core: extracted out of what
// used to be Qt-only officeview.cpp / mupdfwidget.cpp. No Qt, no GTK — the
// UI layer (src/qt6/, src/gtk3/) owns the widget/rendering surface and
// calls into this for page images, structured-text selection, document
// conversion (x2t) and LibreOfficeKit rendering.
namespace OfficeCore {

// ---------------------------------------------------------------------
// Config (officeview.conf, INI format, replaces QSettings)
// ---------------------------------------------------------------------
struct Config {
    std::string configPath; // full path to officeview.conf, once resolved

    std::string libreOfficePath, euroOfficePath, onlyOfficePath;
    std::string engineForOOXML = "EuroOffice";
    std::string engineForODF = "LibreOffice";
    std::string engineForLegacyMS;
    std::string engineForGDrive;

    // extension (lowercase, no dot) -> max bytes; -1 = disabled entirely
    // Populated for: doc docx docm xls xlsx xlsm ppt pptx pptm odt ods odp
    std::vector<std::pair<std::string, long long>> fileSizeLimits;

    long long maxFileSizeBytes(const std::string &extLower) const;
};

/// Resolves ~/.config/doublecmd/officeview.conf, creating/migrating it with
/// all sections pre-populated (mirrors ensureConfigFileInitialized()).
/// Auto-detects LibreOffice/EuroOffice/OnlyOffice install paths on first run.
Config loadOrInitConfig();
void saveConfig(const Config &cfg);

// ---------------------------------------------------------------------
// PdfCore: MuPDF wrapper (Qt-free). One instance per open PDF.
// ---------------------------------------------------------------------
struct PageInfo {
    int index = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    int pixelYOffset = 0;
};

// Plain RGB (3 bytes/pixel) raster, row-major, top-to-bottom.
struct RasterImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgb;
    bool empty() const { return width <= 0 || height <= 0; }
};

struct QuadF { float ulx, uly, urx, ury, lrx, lry, llx, lly; };
struct PointF { float x = 0, y = 0; };

class PdfCore {
public:
    PdfCore();
    ~PdfCore();
    PdfCore(const PdfCore &) = delete;
    PdfCore &operator=(const PdfCore &) = delete;

    bool open(const std::string &pdfPath);
    bool isValid() const { return m_doc != nullptr; }

    int pageCount() const { return (int)m_pages.size(); }
    const std::vector<PageInfo> &pages() const { return m_pages; }
    int totalHeight() const { return m_totalHeight; }
    int maxWidth() const { return m_maxWidth; }

    int pageYOffset(int index) const;
    int pageAtY(int y) const;

    void setZoom(float zoom);
    float zoom() const { return m_zoom; }
    void zoomIn();
    void zoomOut();
    void zoomReset();

    // Renders (and caches) a page at `scale` extra device-pixel multiplier
    // (pass 1.0 for no supersampling).
    const RasterImage &pageImage(int index, float deviceScale = 1.0f);

    // Structured-text selection support.
    PointF widgetPosToPagePoint(int widgetX, int widgetY, int pageIndex) const;
    std::vector<QuadF> highlightQuads(int pageIndex, PointF start, PointF end);
    std::string copySelection(int pageIndex, PointF start, PointF end);
    std::string copyPageText(int pageIndex);

private:
    void recomputeLayout();
    void clearImageCache();
    fz_stext_page *stextForPage(int index);

    fz_context *m_ctx = nullptr;
    fz_document *m_doc = nullptr;
    std::vector<PageInfo> m_pages;
    std::vector<std::pair<int, RasterImage>> m_imageCache;
    std::vector<std::pair<int, fz_stext_page *>> m_stextCache;
    float m_zoom = 1.5f;
    int m_totalHeight = 0;
    int m_maxWidth = 0;
};

/// Lightweight page-count-only open (no rendering), used by the x2t
/// per-sheet-pagination merge bookkeeping.
int muPdfPageCount(const std::string &path);

// ---------------------------------------------------------------------
// X2TConverter: Euro-Office / OnlyOffice x2t subprocess backend
// ---------------------------------------------------------------------
class X2TConverter {
public:
    explicit X2TConverter(const std::string &preferredEngine); // "EuroOffice" or "OnlyOffice"

    bool isLoaded = false;
    std::string loadedEngine;
    std::string x2tBin, libPath;

    bool convertToPdf(const std::string &inputPath, const std::string &outputPath, bool allSheets);

    // Converts each sheet in rawSheetIndices separately (properly paginated)
    // and merges with qpdf; fills outSheetStartPages. Returns false (caller
    // should fall back to convertToPdf(..., allSheets=true)) if qpdf/any
    // step is unavailable.
    bool convertXlsxAllSheetsPaginated(const std::string &inputPath, const std::string &outputPath,
                                        const std::vector<int> &rawSheetIndices,
                                        std::vector<int> &outSheetStartPages);

private:
    std::string getFontsPath() const;
    void syncFontCacheWorkaround();
    bool fontCacheSynced = false;
};

/// xlsx/xlsm/ods sheet-name extraction via `unzip -p` + regex (no full
/// zip/XML dependency). Hidden sheets excluded. outRawIndices (if given)
/// receives each visible sheet's 0-based position in the FULL sheet list.
std::vector<std::string> extractSpreadsheetSheetNames(const std::string &filePath, const std::string &ext,
                                                        std::vector<int> *outRawIndices = nullptr);

/// Patches <workbookView activeTab="N"> in an xlsx/xlsm copy so x2t's
/// default (paginating) PDF export targets that sheet.
bool patchXlsxActiveSheet(const std::string &srcPath, const std::string &dstPath, int rawSheetIndex);

// ---------------------------------------------------------------------
// LokCore: LibreOfficeKit wrapper (dlopen-based via LibreOfficeKitInit.h)
// ---------------------------------------------------------------------
struct LokPartInfo {
    int index = 0;
    long widthTwips = 0, heightTwips = 0;
    int pixelYOffset = 0, pixelWidth = 0, pixelHeight = 0;
};

class LokCore {
public:
    LokCore();
    ~LokCore();
    LokCore(const LokCore &) = delete;
    LokCore &operator=(const LokCore &) = delete;

    // Loads the shared LOK office instance (once per process) from
    // findLibreOfficePath(), then opens `path` as a document.
    bool open(const std::string &loInstallPath, const std::string &path);
    bool isValid() const { return m_doc != nullptr; }

    int partCount() const { return (int)m_parts.size(); }
    const std::vector<LokPartInfo> &parts() const { return m_parts; }
    int totalHeight() const { return m_totalHeight; }
    int maxWidth() const { return m_maxWidth; }
    int partYOffset(int index) const;
    int partAtY(int y) const;

    void setZoom(double zoom);
    void zoomIn();
    void zoomOut();
    void zoomReset();

    // Renders the given widget-space rect (px) into an RGB raster, at
    // `supersample`x the pixel density before the caller downsamples it.
    RasterImage paintRect(int x, int y, int w, int h, int supersample = 2);

    std::string copyAllText(int partIndex = -1);

private:
    void recomputeLayout();
    void *m_doc = nullptr; // LibreOfficeKitDocument*
    std::vector<LokPartInfo> m_parts;
    int m_totalHeight = 0, m_maxWidth = 0;
    double m_zoomFactor = 1.0;
    int m_effectiveTwipsPerPixel = 15;
};

/// Auto-detects a LibreOffice install (LO_PATH env, config, common
/// fallback paths), persisting a discovered fallback into cfg.
std::string findLibreOfficePath(Config &cfg);

// ---------------------------------------------------------------------
// Google Drive / rclone support
// ---------------------------------------------------------------------
struct RcloneMount { std::string mountPoint, remotePrefix; };
std::vector<RcloneMount> findRcloneMounts();
std::string rcloneRemotePathFor(const std::string &filePath); // "" if not under a mount
bool rcloneDownload(const std::string &remotePath, const std::string &destPath);

// ---------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------
extern const std::vector<std::string> kSizeLimitedExtensionsOrdered;
constexpr long long kDefaultMaxFileSizeBytes = 3LL * 1024 * 1024;
std::string extensionOf(const std::string &path); // lowercase, no dot
bool fileExists(const std::string &path);
long long fileSize(const std::string &path);

} // namespace OfficeCore
