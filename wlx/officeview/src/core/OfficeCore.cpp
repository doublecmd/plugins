#include "OfficeCore.h"

#define LOK_USE_UNSTABLE_API
#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include <LibreOfficeKit/LibreOfficeKitInit.h>
#include <LibreOfficeKit/LibreOfficeKit.h>

#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <poll.h>
#include <signal.h>
#include <pwd.h>
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <map>

extern char **environ;

namespace OfficeCore {

// ======================================================================
// Small utilities
// ======================================================================

namespace {

struct ProcessResult {
    bool started = false;
    int exitCode = -1;
    std::string stdoutData;
};

ProcessResult runProcess(const std::string &exe, const std::vector<std::string> &args,
                          int timeoutMs, const std::string *cwd = nullptr,
                          const std::vector<std::string> *extraEnv = nullptr)
{
    ProcessResult result;
    int outPipe[2];
    if (pipe(outPipe) != 0) return result;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, outPipe[0]);
    posix_spawn_file_actions_addclose(&actions, outPipe[1]);
    if (cwd && !cwd->empty())
        posix_spawn_file_actions_addchdir_np(&actions, cwd->c_str());

    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(exe.c_str()));
    for (auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);

    std::vector<std::string> envStorage;
    std::vector<char *> envp;
    char **envUse = environ;
    if (extraEnv && !extraEnv->empty()) {
        for (char **e = environ; *e; ++e) envStorage.push_back(*e);
        for (auto &kv : *extraEnv) envStorage.push_back(kv);
        for (auto &s : envStorage) envp.push_back(const_cast<char *>(s.c_str()));
        envp.push_back(nullptr);
        envUse = envp.data();
    }

    pid_t pid = -1;
    int rc = posix_spawnp(&pid, exe.c_str(), &actions, nullptr, argv.data(), envUse);
    posix_spawn_file_actions_destroy(&actions);
    close(outPipe[1]);

    if (rc != 0) { close(outPipe[0]); return result; }
    result.started = true;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool timedOut = false;
    char buf[8192];
    while (true) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                              deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) { timedOut = true; break; }
        struct pollfd pfd { outPipe[0], POLLIN, 0 };
        int pr = poll(&pfd, 1, (int)remaining);
        if (pr < 0) break;
        if (pr == 0) { timedOut = true; break; }
        if (pfd.revents & (POLLIN | POLLHUP)) {
            ssize_t n = read(outPipe[0], buf, sizeof(buf));
            if (n > 0) result.stdoutData.append(buf, (size_t)n);
            else break;
        } else break;
    }
    close(outPipe[0]);
    if (timedOut) kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!timedOut && WIFEXITED(status)) result.exitCode = WEXITSTATUS(status);
    return result;
}

std::string readFile(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

bool writeFile(const std::string &path, const std::string &data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), (std::streamsize)data.size());
    return f.good();
}

bool copyFile(const std::string &src, const std::string &dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << in.rdbuf();
    return out.good();
}

std::string homeDir() {
    const char *h = getenv("HOME");
    if (h && *h) return h;
    struct passwd *pw = getpwuid(getuid());
    return pw ? pw->pw_dir : "";
}

std::string configDir() {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    std::string base = (xdg && *xdg) ? xdg : (homeDir() + "/.config");
    std::string dir = base + "/doublecmd";
    // mkdir -p (two levels deep at most, keep it simple)
    mkdir(base.c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    return dir;
}

std::string tempDir() {
    const char *t = getenv("TMPDIR");
    return (t && *t) ? t : "/tmp";
}

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Minimal INI reader: section -> key -> value.
using IniData = std::map<std::string, std::map<std::string, std::string>>;

IniData readIni(const std::string &path) {
    IniData data;
    std::ifstream f(path);
    std::string line, section;
    while (std::getline(f, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        data[section][trim(t.substr(0, eq))] = trim(t.substr(eq + 1));
    }
    return data;
}

std::string iniGet(const IniData &data, const std::string &section, const std::string &key,
                    const std::string &def = "") {
    auto sit = data.find(section);
    if (sit == data.end()) return def;
    auto kit = sit->second.find(key);
    if (kit == sit->second.end()) return def;
    return kit->second;
}

} // namespace

std::string extensionOf(const std::string &path) {
    auto slash = path.find_last_of('/');
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });
    return ext;
}

bool fileExists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

// Top-level entry names only (not recursive) -- used to symlink a real
// directory's contents into a tmpfs overlay standing in for it. "." and
// ".." are excluded.
std::vector<std::string> listDirTopLevel(const std::string &dir) {
    std::vector<std::string> names;
    DIR *d = opendir(dir.c_str());
    if (!d) return names;
    struct dirent *entry;
    while ((entry = readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        names.push_back(name);
    }
    closedir(d);
    return names;
}

long long fileSize(const std::string &path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return -1;
    return (long long)st.st_size;
}

const std::vector<std::string> kSizeLimitedExtensionsOrdered = {
    "doc", "docx", "docm", "xls", "xlsx", "xlsm", "ppt", "pptx", "pptm",
    "odt", "ods", "odp"
};

// ======================================================================
// Config
// ======================================================================

long long Config::maxFileSizeBytes(const std::string &extLower) const {
    std::string extUpper = extLower;
    std::transform(extUpper.begin(), extUpper.end(), extUpper.begin(), ::toupper);
    for (auto &kv : fileSizeLimits)
        if (kv.first == extUpper) return kv.second;
    return kDefaultMaxFileSizeBytes;
}

static std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

Config loadOrInitConfig() {
    Config cfg;
    cfg.configPath = configDir() + "/officeview.conf";

    IniData existing = readIni(cfg.configPath);
    bool hasCurrentFormat = existing.count("Paths") && existing.count("FileSizeLimits") &&
                             existing["Engines"].count("EngineForGDrive");

    cfg.libreOfficePath = iniGet(existing, "Paths", "LibreOfficePath",
                                  iniGet(existing, "Settings", "LibreOfficePath"));
    cfg.euroOfficePath = iniGet(existing, "Paths", "EuroOfficePath",
                                 iniGet(existing, "Settings", "EuroOfficePath"));
    cfg.onlyOfficePath = iniGet(existing, "Paths", "OnlyOfficePath",
                                 iniGet(existing, "Settings", "OnlyOfficePath"));
    cfg.engineForOOXML = iniGet(existing, "Engines", "EngineForOOXML",
                                 iniGet(existing, "Settings", "EngineForOOXML"));
    cfg.engineForODF = iniGet(existing, "Engines", "EngineForODF",
                               iniGet(existing, "Settings", "EngineForODF"));
    cfg.engineForLegacyMS = iniGet(existing, "Engines", "EngineForLegacyMS",
                                    iniGet(existing, "Settings", "EngineForLegacyMS"));
    cfg.engineForGDrive = iniGet(existing, "Engines", "EngineForGDrive");

    for (auto &ext : kSizeLimitedExtensionsOrdered) {
        std::string v = iniGet(existing, "FileSizeLimits", upper(ext),
                                iniGet(existing, "Settings", "MaxFileSizeBytes_" + upper(ext)));
        long long bytes = kDefaultMaxFileSizeBytes;
        if (!v.empty()) {
            try { bytes = std::stoll(v); } catch (...) {}
            if (bytes < -1) bytes = kDefaultMaxFileSizeBytes;
        }
        cfg.fileSizeLimits.push_back({ext, bytes});
    }

    if (cfg.engineForOOXML.empty()) {
        X2TConverter probe("EuroOffice");
        if (probe.isLoaded) {
            cfg.engineForOOXML = probe.loadedEngine;
            if (probe.loadedEngine == "EuroOffice") cfg.euroOfficePath = probe.libPath;
            else cfg.onlyOfficePath = probe.libPath;
        } else {
            cfg.engineForOOXML = "LibreOffice";
        }
    }
    if (cfg.engineForODF.empty()) cfg.engineForODF = "LibreOffice";
    if (cfg.engineForLegacyMS.empty()) cfg.engineForLegacyMS = cfg.engineForOOXML;
    if (cfg.engineForGDrive.empty()) cfg.engineForGDrive = cfg.engineForOOXML;
    if (cfg.libreOfficePath.empty()) cfg.libreOfficePath = findLibreOfficePath(cfg);

    if (!hasCurrentFormat)
        saveConfig(cfg);

    return cfg;
}

void saveConfig(const Config &cfg) {
    std::ofstream out(cfg.configPath, std::ios::trunc);
    if (!out) return;

    out << "[Paths]\n";
    out << "LibreOfficePath=" << cfg.libreOfficePath << "\n";
    out << "EuroOfficePath=" << cfg.euroOfficePath << "\n";
    out << "OnlyOfficePath=" << cfg.onlyOfficePath << "\n\n";

    out << "; Valid values: EuroOffice, OnlyOffice, LibreOffice, or Disabled\n";
    out << "; (skip this format family entirely, showing a short message\n";
    out << "; instead of attempting to render it).\n";
    out << "[Engines]\n";
    out << "EngineForOOXML=" << cfg.engineForOOXML << "\n";
    out << "EngineForODF=" << cfg.engineForODF << "\n";
    out << "EngineForLegacyMS=" << cfg.engineForLegacyMS << "\n";
    out << "; Native Google Docs/Sheets/Slides, exported on the fly via\n";
    out << "; rclone (requires an rclone mount and the rclone binary).\n";
    out << "EngineForGDrive=" << cfg.engineForGDrive << "\n\n";

    out << "; Size limit in bytes. Files larger than this are not opened at\n";
    out << "; all. Set to -1 to effectively disable the plugin for that\n";
    out << "; extension. 0 is a valid (if impractical) limit.\n";
    out << "[FileSizeLimits]\n";
    for (auto &kv : cfg.fileSizeLimits)
        out << upper(kv.first) << "=" << kv.second << "\n";
}

// ======================================================================
// PdfCore (MuPDF)
// ======================================================================

PdfCore::PdfCore() {
    m_ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (m_ctx) fz_register_document_handlers(m_ctx);
}

PdfCore::~PdfCore() {
    clearImageCache();
    for (auto &kv : m_stextCache) fz_drop_stext_page(m_ctx, kv.second);
    if (m_doc) fz_drop_document(m_ctx, m_doc);
    if (m_ctx) fz_drop_context(m_ctx);
}

bool PdfCore::open(const std::string &pdfPath) {
    if (!m_ctx) return false;
    fz_try(m_ctx) {
        m_doc = fz_open_document(m_ctx, pdfPath.c_str());
    }
    fz_catch(m_ctx) {
        fprintf(stderr, "[OfficeView] PdfCore: failed to open %s: %s\n",
                pdfPath.c_str(), fz_caught_message(m_ctx));
        m_doc = nullptr;
    }
    if (m_doc) recomputeLayout();
    return m_doc != nullptr;
}

void PdfCore::clearImageCache() { m_imageCache.clear(); }

void PdfCore::recomputeLayout() {
    if (!m_doc) return;
    clearImageCache();
    m_pages.clear();
    m_totalHeight = 0;
    m_maxWidth = 0;

    int count = 0;
    fz_try(m_ctx) { count = fz_count_pages(m_ctx, m_doc); }
    fz_catch(m_ctx) { count = 0; }

    for (int i = 0; i < count; i++) {
        fz_rect bounds{};
        fz_try(m_ctx) {
            fz_page *page = fz_load_page(m_ctx, m_doc, i);
            bounds = fz_bound_page(m_ctx, page);
            fz_drop_page(m_ctx, page);
        }
        fz_catch(m_ctx) { continue; }

        PageInfo info;
        info.index = i;
        info.pixelWidth = (int)((bounds.x1 - bounds.x0) * m_zoom);
        info.pixelHeight = (int)((bounds.y1 - bounds.y0) * m_zoom);
        info.pixelYOffset = m_totalHeight;
        m_totalHeight += info.pixelHeight + 8;
        if (info.pixelWidth > m_maxWidth) m_maxWidth = info.pixelWidth;
        m_pages.push_back(info);
    }
}

int PdfCore::pageYOffset(int index) const {
    if (index < 0 || index >= (int)m_pages.size()) return 0;
    return m_pages[index].pixelYOffset;
}

int PdfCore::pageAtY(int y) const {
    for (int i = (int)m_pages.size() - 1; i >= 0; --i)
        if (y >= m_pages[i].pixelYOffset) return i;
    return 0;
}

void PdfCore::setZoom(float zoom) { m_zoom = zoom; recomputeLayout(); }
void PdfCore::zoomIn() { m_zoom = std::min(m_zoom * 1.2f, 8.0f); recomputeLayout(); }
void PdfCore::zoomOut() { m_zoom = std::max(m_zoom / 1.2f, 0.2f); recomputeLayout(); }
void PdfCore::zoomReset() { m_zoom = 1.5f; recomputeLayout(); }

const RasterImage &PdfCore::pageImage(int index, float deviceScale) {
    for (auto &kv : m_imageCache)
        if (kv.first == index) return kv.second;

    RasterImage img;
    float scale = m_zoom * deviceScale;
    fz_try(m_ctx) {
        fz_page *page = fz_load_page(m_ctx, m_doc, index);
        fz_matrix ctm = fz_scale(scale, scale);
        fz_pixmap *pix = fz_new_pixmap_from_page(m_ctx, page, ctm, fz_device_rgb(m_ctx), 0);
        img.width = fz_pixmap_width(m_ctx, pix);
        img.height = fz_pixmap_height(m_ctx, pix);
        int stride = fz_pixmap_stride(m_ctx, pix);
        unsigned char *samples = fz_pixmap_samples(m_ctx, pix);
        img.rgb.resize((size_t)img.width * img.height * 3);
        for (int y = 0; y < img.height; y++)
            memcpy(img.rgb.data() + (size_t)y * img.width * 3, samples + (size_t)y * stride, (size_t)img.width * 3);
        fz_drop_pixmap(m_ctx, pix);
        fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx) {
        fprintf(stderr, "[OfficeView] PdfCore: failed to render page %d: %s\n", index, fz_caught_message(m_ctx));
    }

    m_imageCache.push_back({index, std::move(img)});
    return m_imageCache.back().second;
}

fz_stext_page *PdfCore::stextForPage(int index) {
    for (auto &kv : m_stextCache)
        if (kv.first == index) return kv.second;

    fz_stext_page *stext = nullptr;
    fz_try(m_ctx) {
        fz_page *page = fz_load_page(m_ctx, m_doc, index);
        stext = fz_new_stext_page_from_page(m_ctx, page, NULL);
        fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx) { stext = nullptr; }

    m_stextCache.push_back({index, stext});
    return stext;
}

PointF PdfCore::widgetPosToPagePoint(int widgetX, int widgetY, int pageIndex) const {
    int localY = widgetY - pageYOffset(pageIndex);
    return PointF{ widgetX / m_zoom, localY / m_zoom };
}

std::vector<QuadF> PdfCore::highlightQuads(int pageIndex, PointF start, PointF end) {
    std::vector<QuadF> out;
    fz_stext_page *stext = stextForPage(pageIndex);
    if (!stext) return out;
    fz_quad quads[512];
    int n = 0;
    fz_point a{start.x, start.y}, b{end.x, end.y};
    fz_try(m_ctx) { n = fz_highlight_selection(m_ctx, stext, a, b, quads, 512); }
    fz_catch(m_ctx) { n = 0; }
    for (int i = 0; i < n; i++) {
        out.push_back(QuadF{
            quads[i].ul.x, quads[i].ul.y, quads[i].ur.x, quads[i].ur.y,
            quads[i].lr.x, quads[i].lr.y, quads[i].ll.x, quads[i].ll.y});
    }
    return out;
}

std::string PdfCore::copySelection(int pageIndex, PointF start, PointF end) {
    fz_stext_page *stext = stextForPage(pageIndex);
    if (!stext) return {};
    std::string result;
    fz_point a{start.x, start.y}, b{end.x, end.y};
    fz_try(m_ctx) {
        char *text = fz_copy_selection(m_ctx, stext, a, b, 0);
        if (text) { result = text; fz_free(m_ctx, text); }
    }
    fz_catch(m_ctx) {}
    return result;
}

std::string PdfCore::copyPageText(int pageIndex) {
    fz_stext_page *stext = stextForPage(pageIndex);
    if (!stext) return {};
    std::string result;
    fz_try(m_ctx) {
        fz_rect area = fz_infinite_rect;
        char *text = fz_copy_rectangle(m_ctx, stext, area, 0);
        if (text) { result = text; fz_free(m_ctx, text); }
    }
    fz_catch(m_ctx) {}
    return result;
}

int muPdfPageCount(const std::string &path) {
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx) return 1;
    fz_register_document_handlers(ctx);
    int count = 1;
    fz_try(ctx) {
        fz_document *doc = fz_open_document(ctx, path.c_str());
        count = std::max(1, fz_count_pages(ctx, doc));
        fz_drop_document(ctx, doc);
    }
    fz_catch(ctx) { count = 1; }
    fz_drop_context(ctx);
    return count;
}

// ======================================================================
// X2TConverter
// ======================================================================

X2TConverter::X2TConverter(const std::string &preferredEngine) {
    std::vector<std::string> searchPaths;
    if (preferredEngine == "EuroOffice") {
        searchPaths = {"/opt/euro-office/desktopeditors", "/opt/onlyoffice/desktopeditors"};
    } else {
        searchPaths = {"/opt/onlyoffice/desktopeditors", "/opt/euro-office/desktopeditors"};
    }
    for (auto &basePath : searchPaths) {
        std::string bin = basePath + "/converter/x2t";
        if (fileExists(bin)) {
            x2tBin = bin;
            libPath = basePath;
            isLoaded = true;
            loadedEngine = basePath.find("euro") != std::string::npos ? "EuroOffice" : "OnlyOffice";
            break;
        }
    }
}

std::string X2TConverter::getFontsPath() const {
    std::string engineLower = loadedEngine;
    std::transform(engineLower.begin(), engineLower.end(), engineLower.begin(), ::tolower);
    if (engineLower == "eurooffice") engineLower = "euro-office";

    std::string home = homeDir();
    std::vector<std::string> candidates = {
        home + "/.local/share/" + engineLower + "/desktopeditors/data/fonts/AllFonts.js",
        home + "/.local/share/onlyoffice/desktopeditors/data/fonts/AllFonts.js",
        home + "/.local/share/euro-office/desktopeditors/data/fonts/AllFonts.js"};
    for (auto &c : candidates)
        if (fileExists(c)) return c;
    return "";
}

void X2TConverter::syncFontCacheWorkaround() {
    if (fontCacheSynced || !isLoaded) return;
    fontCacheSynced = true;

    std::string fontsJsSrc = getFontsPath();
    if (fontsJsSrc.empty()) return;
    std::string fontsDirSrc = fontsJsSrc.substr(0, fontsJsSrc.find_last_of('/'));
    std::string selectionBinSrc = fontsDirSrc + "/font_selection.bin";
    if (!fileExists(selectionBinSrc)) return;

    std::string converterDir = libPath + "/converter";
    copyFile(fontsJsSrc, converterDir + "/AllFonts.js");
    copyFile(selectionBinSrc, converterDir + "/font_selection.bin");
}

bool X2TConverter::convertToPdf(const std::string &inputPath, const std::string &outputPath, bool allSheets) {
    if (!isLoaded) return false;
    syncFontCacheWorkaround();

    std::string configPath = tempDir() + "/x2t_config_" + std::to_string(getpid()) + "_" +
                              std::to_string((long)time(nullptr)) + ".xml";

    std::string fontPath = getFontsPath();
    std::string fontTag = fontPath.empty() ? "" : ("  <m_sAllFontsPath>" + fontPath + "</m_sAllFontsPath>\n");
    std::string jsonParamsTag = allSheets ? "  <m_sJsonParams>{\"printPages\":\"all\"}</m_sJsonParams>\n" : "";

    std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                       "<TaskQueueDataConvert xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">\n"
                       "  <m_sFileFrom>" + inputPath + "</m_sFileFrom>\n"
                       "  <m_sFileTo>" + outputPath + "</m_sFileTo>\n"
                       "  <m_nFormatTo>513</m_nFormatTo>\n"
                       "  <m_bIsNoBase64>true</m_bIsNoBase64>\n" + fontTag + jsonParamsTag +
                       "</TaskQueueDataConvert>";
    if (!writeFile(configPath, xml)) return false;

    std::vector<std::string> extraEnv = { "LD_LIBRARY_PATH=" + libPath };
    ProcessResult res;

    // sandboxed via bwrap when available and a font path is known
    std::string bwrapBin;
    { ProcessResult which = runProcess("which", {"bwrap"}, 3000); if (which.exitCode == 0) bwrapBin = trim(which.stdoutData); }

    if (!bwrapBin.empty() && !fontPath.empty()) {
        std::string fontSelectionBin = fontPath.substr(0, fontPath.find_last_of('/')) + "/font_selection.bin";
        std::string converterDir = libPath + "/converter";
        std::vector<std::string> args = {
            "--ro-bind", "/usr", "/usr",
            "--symlink", "usr/lib", "lib",
            "--symlink", "usr/lib64", "lib64",
            "--symlink", "usr/bin", "bin",
            "--ro-bind", "/opt", "/opt",
            "--ro-bind", "/etc", "/etc",
            "--bind", "/home", "/home",
            "--bind", "/tmp", "/tmp",
            "--dev", "/dev", "--proc", "/proc",
        };
        // Give converter/ its own writable tmpfs overlay inside the
        // sandbox instead of relying on the real (often root-owned)
        // install directory allowing a new AllFonts.js/font_selection.bin
        // mount point to be created under it -- confirmed live: without a
        // REAL font cache physically present beside x2t, its font
        // matching badly misresolves some fonts (e.g. substituting an
        // unrelated icon font for real text) even with m_sAllFontsPath
        // set correctly in the XML config, so this isn't optional
        // cosmetic behavior. bwrap resolves every --ro-bind SOURCE against
        // the real host filesystem regardless of what's already been
        // mounted in the sandbox under construction (confirmed live: an
        // intermediate "mirror the original elsewhere, symlink back from
        // there" approach fails with "Can't find source path" because the
        // mirror destination only exists inside the sandbox's own view,
        // not on the real host) -- so re-bind each of converter/'s real
        // top-level entries directly onto itself: the source read is
        // against the real (still readable, just not writable) directory,
        // and the target write succeeds because tmpfs already made that
        // path writable in the sandbox.
        args.push_back("--tmpfs"); args.push_back(converterDir);
        for (const auto &name : listDirTopLevel(converterDir)) {
            if (name == "AllFonts.js" || name == "font_selection.bin") continue;
            args.push_back("--ro-bind");
            args.push_back(converterDir + "/" + name);
            args.push_back(converterDir + "/" + name);
        }
        args.push_back("--ro-bind"); args.push_back(fontPath); args.push_back(converterDir + "/AllFonts.js");
        args.push_back("--ro-bind"); args.push_back(fontSelectionBin); args.push_back(converterDir + "/font_selection.bin");
        args.push_back("--chdir"); args.push_back(libPath);
        args.push_back(x2tBin); args.push_back(configPath);
        res = runProcess(bwrapBin, args, 10000, nullptr, &extraEnv);
    } else {
        std::string workDir = libPath + "/converter";
        res = runProcess(x2tBin, {configPath}, 10000, &workDir, &extraEnv);
    }
    unlink(configPath.c_str());

    if (fileExists(outputPath) && fileSize(outputPath) > 0) return true;
    fprintf(stderr, "[OfficeView] x2t conversion failed: exit=%d\n", res.exitCode);
    return false;
}

bool X2TConverter::convertXlsxAllSheetsPaginated(const std::string &inputPath, const std::string &outputPath,
                                                  const std::vector<int> &rawSheetIndices,
                                                  std::vector<int> &outSheetStartPages) {
    outSheetStartPages.clear();
    if (rawSheetIndices.empty()) return false;

    std::string qpdfBin;
    { ProcessResult which = runProcess("which", {"qpdf"}, 3000); if (which.exitCode == 0) qpdfBin = trim(which.stdoutData); }
    if (qpdfBin.empty()) return false;

    std::string srcExt = extensionOf(inputPath);
    std::vector<std::string> perSheetPdfs;
    bool ok = true;

    for (int rawIndex : rawSheetIndices) {
        std::string patchedPath = tempDir() + "/officeview_sheet_" + std::to_string(getpid()) + "_" +
                                   std::to_string(rawIndex) + "." + srcExt;
        if (!patchXlsxActiveSheet(inputPath, patchedPath, rawIndex)) { ok = false; break; }

        std::string sheetPdfPath = tempDir() + "/officeview_sheetpdf_" + std::to_string(getpid()) + "_" +
                                    std::to_string(rawIndex) + ".pdf";
        if (!convertToPdf(patchedPath, sheetPdfPath, false)) { unlink(patchedPath.c_str()); ok = false; break; }

        perSheetPdfs.push_back(sheetPdfPath);
        unlink(patchedPath.c_str());
    }

    if (ok) {
        int running = 0;
        for (auto &p : perSheetPdfs) {
            outSheetStartPages.push_back(running);
            running += muPdfPageCount(p);
        }
    }

    if (ok && !perSheetPdfs.empty()) {
        std::vector<std::string> args = {"--empty", "--pages"};
        for (auto &p : perSheetPdfs) args.push_back(p);
        args.push_back("--");
        args.push_back(outputPath);
        ProcessResult merge = runProcess(qpdfBin, args, 15000);
        if (!fileExists(outputPath) || fileSize(outputPath) <= 0) ok = false;
    }

    for (auto &p : perSheetPdfs) unlink(p.c_str());
    return ok;
}

std::vector<std::string> extractSpreadsheetSheetNames(const std::string &filePath, const std::string &ext,
                                                        std::vector<int> *outRawIndices) {
    std::string innerXmlPath;
    std::regex tagRe, nameRe, hiddenRe;
    if (ext == "xlsx" || ext == "xlsm") {
        innerXmlPath = "xl/workbook.xml";
        tagRe = std::regex("<sheet\\b[^>]*/?>");
        nameRe = std::regex("\\bname=\"([^\"]*)\"");
        hiddenRe = std::regex("\\bstate=\"(hidden|veryHidden)\"");
    } else if (ext == "ods") {
        innerXmlPath = "content.xml";
        tagRe = std::regex("<table:table\\b[^>]*>");
        nameRe = std::regex("\\btable:name=\"([^\"]*)\"");
        hiddenRe = std::regex("\\btable:visibility=\"hidden\"");
    } else {
        return {};
    }

    ProcessResult unzip = runProcess("unzip", {"-p", filePath, innerXmlPath}, 5000);
    if (unzip.exitCode != 0) return {};

    const std::string &xml = unzip.stdoutData;
    std::vector<std::string> names;
    if (outRawIndices) outRawIndices->clear();
    int rawIndex = 0;
    auto begin = std::sregex_iterator(xml.begin(), xml.end(), tagRe);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        std::string tag = it->str();
        int thisIndex = rawIndex++;
        if (std::regex_search(tag, hiddenRe)) continue;
        std::smatch m;
        if (!std::regex_search(tag, m, nameRe)) continue;
        std::string name = m[1].str();
        auto replaceAll = [](std::string &s, const std::string &from, const std::string &to) {
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) { s.replace(pos, from.size(), to); pos += to.size(); }
        };
        replaceAll(name, "&amp;", "&");
        replaceAll(name, "&lt;", "<");
        replaceAll(name, "&gt;", ">");
        replaceAll(name, "&quot;", "\"");
        replaceAll(name, "&apos;", "'");
        names.push_back(name);
        if (outRawIndices) outRawIndices->push_back(thisIndex);
    }
    return names;
}

bool patchXlsxActiveSheet(const std::string &srcPath, const std::string &dstPath, int rawSheetIndex) {
    std::string tmpDir = tempDir() + "/officeview_patch_" + std::to_string(getpid()) + "_" + std::to_string(rawSheetIndex);
    runProcess("rm", {"-rf", tmpDir}, 3000);
    if (mkdir(tmpDir.c_str(), 0755) != 0) return false;

    ProcessResult unzip = runProcess("unzip", {"-q", "-o", srcPath, "-d", tmpDir}, 10000);
    if (unzip.exitCode != 0) { runProcess("rm", {"-rf", tmpDir}, 3000); return false; }

    std::string workbookPath = tmpDir + "/xl/workbook.xml";
    std::string xml = readFile(workbookPath);
    if (xml.empty()) { runProcess("rm", {"-rf", tmpDir}, 3000); return false; }

    std::string activeTabStr = "activeTab=\"" + std::to_string(rawSheetIndex) + "\"";
    std::regex activeTabRe("activeTab=\"\\d+\"");
    if (std::regex_search(xml, activeTabRe)) {
        xml = std::regex_replace(xml, activeTabRe, activeTabStr);
    } else {
        std::regex viewTagRe("<workbookView\\b");
        if (std::regex_search(xml, viewTagRe)) {
            xml = std::regex_replace(xml, viewTagRe, "<workbookView " + activeTabStr + " ");
        } else {
            std::string injected = "<bookViews><workbookView " + activeTabStr + " /></bookViews>";
            auto pos = xml.find("<bookViews>");
            if (pos != std::string::npos) {
                auto endPos = xml.find("</bookViews>");
                if (endPos != std::string::npos) xml.replace(pos, endPos + 12 - pos, injected);
            } else {
                pos = xml.find("<sheets>");
                if (pos != std::string::npos) xml.insert(pos, injected);
            }
        }
    }
    writeFile(workbookPath, xml);

    if (fileExists(dstPath)) unlink(dstPath.c_str());
    ProcessResult zip = runProcess("zip", {"-q", "-r", dstPath, "."}, 10000, &tmpDir);
    runProcess("rm", {"-rf", tmpDir}, 3000);
    return fileExists(dstPath);
}

// ======================================================================
// LokCore
// ======================================================================

static LibreOfficeKit *g_lokOffice = nullptr;
static const int kTwipsPerPixel = 15;

std::string findLibreOfficePath(Config &cfg) {
    const char *env = getenv("LO_PATH");
    if (env && *env && fileExists(env)) return env;

    if (!cfg.libreOfficePath.empty() && fileExists(cfg.libreOfficePath)) return cfg.libreOfficePath;

    std::vector<std::string> fallbacks = {
        "/usr/lib/libreoffice/program", "/usr/lib64/libreoffice/program", "/opt/libreoffice/program"};
    for (auto &fb : fallbacks) {
        if (fileExists(fb)) { cfg.libreOfficePath = fb; return fb; }
    }
    return "";
}

LokCore::LokCore() {}

LokCore::~LokCore() {
    if (m_doc) reinterpret_cast<LibreOfficeKitDocument *>(m_doc)->pClass->destroy(
        reinterpret_cast<LibreOfficeKitDocument *>(m_doc));
}

bool LokCore::open(const std::string &loInstallPath, const std::string &path) {
    if (!g_lokOffice) {
        std::string profileUri = "file://" + tempDir() + "/lok_profile_officeview";
        g_lokOffice = lok_init_2(loInstallPath.c_str(), profileUri.c_str());
    }
    if (!g_lokOffice) return false;

    LibreOfficeKitDocument *pDoc = g_lokOffice->pClass->documentLoad(g_lokOffice, path.c_str());
    if (!pDoc) return false;
    m_doc = pDoc;
    pDoc->pClass->initializeForRendering(pDoc, "{}");
    recomputeLayout();
    return true;
}

void LokCore::recomputeLayout() {
    if (!m_doc) return;
    auto *pDoc = reinterpret_cast<LibreOfficeKitDocument *>(m_doc);
    m_effectiveTwipsPerPixel = std::max(1, (int)(kTwipsPerPixel / m_zoomFactor));
    m_parts.clear();
    m_totalHeight = 0;
    m_maxWidth = 0;

    int numParts = pDoc->pClass->getParts(pDoc);
    if (numParts <= 0) numParts = 1;
    for (int i = 0; i < numParts; ++i) {
        pDoc->pClass->setPart(pDoc, i);
        long w = 0, h = 0;
        pDoc->pClass->getDocumentSize(pDoc, &w, &h);
        LokPartInfo info;
        info.index = i;
        info.widthTwips = w;
        info.heightTwips = h;
        info.pixelWidth = (int)(w / m_effectiveTwipsPerPixel);
        info.pixelHeight = (int)(h / m_effectiveTwipsPerPixel);
        info.pixelYOffset = m_totalHeight;
        m_totalHeight += info.pixelHeight + 20;
        if (info.pixelWidth > m_maxWidth) m_maxWidth = info.pixelWidth;
        m_parts.push_back(info);
    }
    pDoc->pClass->setPart(pDoc, 0);
}

int LokCore::partYOffset(int index) const {
    if (index < 0 || index >= (int)m_parts.size()) return 0;
    return m_parts[index].pixelYOffset;
}

int LokCore::partAtY(int y) const {
    for (int i = (int)m_parts.size() - 1; i >= 0; --i)
        if (y >= m_parts[i].pixelYOffset) return i;
    return 0;
}

void LokCore::setZoom(double zoom) { m_zoomFactor = zoom; recomputeLayout(); }
void LokCore::zoomIn() { m_zoomFactor = std::min(m_zoomFactor * 1.2, 8.0); recomputeLayout(); }
void LokCore::zoomOut() { m_zoomFactor = std::max(m_zoomFactor / 1.2, 0.1); recomputeLayout(); }
void LokCore::zoomReset() { m_zoomFactor = 1.0; recomputeLayout(); }

RasterImage LokCore::paintRect(int x, int y, int w, int h, int supersample) {
    RasterImage img;
    if (!m_doc || w <= 0 || h <= 0) return img;
    auto *pDoc = reinterpret_cast<LibreOfficeKitDocument *>(m_doc);

    // Find which part this rect belongs to (rect is expected to be fully
    // within one part -- caller clips per-part like the Qt path does).
    int partIndex = partAtY(y);
    if (partIndex < 0 || partIndex >= (int)m_parts.size()) return img;
    const LokPartInfo &part = m_parts[partIndex];

    int localX = x;
    int localY = y - part.pixelYOffset;
    int tilePosX = localX * m_effectiveTwipsPerPixel;
    int tilePosY = localY * m_effectiveTwipsPerPixel;
    int tileWidth = w * m_effectiveTwipsPerPixel;
    int tileHeight = h * m_effectiveTwipsPerPixel;

    int canvasWidth = w * supersample;
    int canvasHeight = h * supersample;
    std::vector<unsigned char> buffer((size_t)canvasHeight * canvasWidth * 4, 255);

    pDoc->pClass->setPart(pDoc, part.index);
    pDoc->pClass->paintTile(pDoc, buffer.data(), canvasWidth, canvasHeight, tilePosX, tilePosY, tileWidth, tileHeight);

    // Downsample BGRA/ARGB (LOK gives premultiplied ARGB in native byte
    // order; we only need RGB for display, ignore alpha) supersample x
    // supersample -> w x h, nearest-neighbour is enough since it's already
    // supersampled for quality.
    img.width = w; img.height = h;
    img.rgb.resize((size_t)w * h * 3);
    for (int oy = 0; oy < h; oy++) {
        for (int ox = 0; ox < w; ox++) {
            int sx = ox * supersample, sy = oy * supersample;
            size_t srcOff = ((size_t)sy * canvasWidth + sx) * 4;
            size_t dstOff = ((size_t)oy * w + ox) * 3;
            // LibreOfficeKit tile format is BGRA on little-endian.
            img.rgb[dstOff + 0] = buffer[srcOff + 2];
            img.rgb[dstOff + 1] = buffer[srcOff + 1];
            img.rgb[dstOff + 2] = buffer[srcOff + 0];
        }
    }
    return img;
}

std::string LokCore::copyAllText(int partIndex) {
    if (!m_doc) return {};
    auto *pDoc = reinterpret_cast<LibreOfficeKitDocument *>(m_doc);
    if (partIndex >= 0 && partIndex < (int)m_parts.size()) pDoc->pClass->setPart(pDoc, partIndex);

    pDoc->pClass->postUnoCommand(pDoc, ".uno:SelectAll", nullptr, false);
    char *usedMimeType = nullptr;
    char *text = nullptr;
    for (int attempt = 0; attempt < 8 && !text; ++attempt) {
        if (attempt > 0) usleep(25000);
        text = pDoc->pClass->getTextSelection(pDoc, "text/plain;charset=utf-8", &usedMimeType);
        if (!text && usedMimeType) { free(usedMimeType); usedMimeType = nullptr; }
    }
    std::string result;
    if (text) { result = text; free(text); }
    if (usedMimeType) free(usedMimeType);
    pDoc->pClass->postUnoCommand(pDoc, ".uno:Escape", nullptr, false);
    return result;
}

// ======================================================================
// rclone / Google Drive
// ======================================================================

std::vector<RcloneMount> findRcloneMounts() {
    std::vector<RcloneMount> mounts;
    std::ifstream f("/proc/mounts");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string device, mountPoint, fstype;
        if (!(iss >> device >> mountPoint >> fstype)) continue;
        auto unescape = [](std::string s) {
            auto replaceAll = [](std::string &s, const std::string &from, const std::string &to) {
                size_t pos = 0;
                while ((pos = s.find(from, pos)) != std::string::npos) { s.replace(pos, from.size(), to); pos += to.size(); }
            };
            replaceAll(s, "\\040", " ");
            replaceAll(s, "\\011", "\t");
            replaceAll(s, "\\012", "\n");
            replaceAll(s, "\\134", "\\");
            return s;
        };
        mountPoint = unescape(mountPoint);
        if (fstype == "fuse.rclone") mounts.push_back({mountPoint, device});
    }
    return mounts;
}

std::string rcloneRemotePathFor(const std::string &filePath) {
    for (auto &m : findRcloneMounts()) {
        std::string prefix = m.mountPoint;
        if (prefix.empty() || prefix.back() != '/') prefix += '/';
        if (filePath.rfind(prefix, 0) == 0) return m.remotePrefix + filePath.substr(prefix.size());
    }
    return "";
}

bool rcloneDownload(const std::string &remotePath, const std::string &destPath) {
    ProcessResult which = runProcess("which", {"rclone"}, 3000);
    if (which.exitCode != 0) return false;
    std::string rcloneBin = trim(which.stdoutData);

    ProcessResult res = runProcess(rcloneBin, {"copyto", remotePath, destPath}, 60000);
    if (res.exitCode != 0) return false;
    return fileExists(destPath) && fileSize(destPath) > 0;
}

} // namespace OfficeCore
