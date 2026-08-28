#include "markdown_engine.h"
#include "diagram_render.h"
#include "latex_render.h"

#include <md4c-html.h>
#include "latex.h"

#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>
#include <sys/stat.h>

namespace MarkdownEngine {

namespace {

bool fileExists(const std::string &path) { struct stat st; return stat(path.c_str(), &st) == 0; }

std::string readFileUtf8(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

// Plain POSIX mkdir() per path component, not GLib's g_mkdir_with_parents
// -- this file is deliberately toolkit-neutral (no Qt, no GTK), shared
// identically by both plugin variants.
void mkdirParents(const std::string &dir) {
    std::string partial;
    for (size_t i = 1; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/') {
            partial = dir.substr(0, i);
            if (!partial.empty()) mkdir(partial.c_str(), 0755); // ignore EEXIST and other errors
        }
    }
}

bool writeFileUtf8(const std::string &path, const std::string &content) {
    auto slash = path.find_last_of('/');
    if (slash != std::string::npos) mkdirParents(path.substr(0, slash));
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << content;
    return static_cast<bool>(f);
}

std::string homeDir() {
    const char *h = std::getenv("HOME");
    return h ? h : "";
}

// Set via MarkdownEngine::setPluginConfigDir() (called once from
// ListSetDefaultParams with DefaultIniName's directory) and read by the CSS
// lookup chain below, so the "plugin dir" candidate is anchored to the
// location DC actually handed the plugin instead of an independently
// guessed ~/.config/doublecmd/plugins/wlx path.
std::string g_pluginConfigDir;

// Set whenever the CSS lookup chain resolves to something other than the
// caller-supplied customCssPath as-is (i.e. customCssPath was empty/stale
// and a fallback candidate -- or a freshly-written default -- was used
// instead). Read via MarkdownEngine::getLastAutoResolvedCssPath() so
// callers can persist the actual file in use back into their ini.
std::string g_lastAutoResolvedCssPath;

std::string htmlUnescape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        if (s.compare(i, 5, "&amp;") == 0) { out += '&'; i += 5; }
        else if (s.compare(i, 4, "&lt;") == 0) { out += '<'; i += 4; }
        else if (s.compare(i, 4, "&gt;") == 0) { out += '>'; i += 4; }
        else if (s.compare(i, 6, "&quot;") == 0) { out += '"'; i += 6; }
        else if (s.compare(i, 5, "&#39;") == 0) { out += '\''; i += 5; }
        else { out += s[i]; i += 1; }
    }
    return out;
}

std::string base64Encode(const std::vector<uint8_t> &data) {
    static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        unsigned v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += tbl[(v >> 18) & 0x3F]; out += tbl[(v >> 12) & 0x3F];
        out += tbl[(v >> 6) & 0x3F]; out += tbl[v & 0x3F];
    }
    if (i + 1 == data.size()) {
        unsigned v = data[i] << 16;
        out += tbl[(v >> 18) & 0x3F]; out += tbl[(v >> 12) & 0x3F]; out += "==";
    } else if (i + 2 == data.size()) {
        unsigned v = (data[i] << 16) | (data[i + 1] << 8);
        out += tbl[(v >> 18) & 0x3F]; out += tbl[(v >> 12) & 0x3F]; out += tbl[(v >> 6) & 0x3F]; out += "=";
    }
    return out;
}

void replaceAll(std::string &s, const std::string &from, const std::string &to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) { s.replace(pos, from.size(), to); pos += to.size(); }
}

std::string htmlEscape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c;
        }
    }
    return out;
}

// --- md_html() driver ---

void mdHtmlOutputCallback(const MD_CHAR *text, MD_SIZE size, void *userdata) {
    static_cast<std::string *>(userdata)->append(text, size);
}

// md4c's MD_FLAG_LATEXMATHSPANS pairs `$...$` naively left-to-right with no
// notion of what makes a *plausible* math span, so a literal currency
// dollar (e.g. "$1.00") gets paired with the next unrelated dollar sign
// anywhere later in the text (e.g. "$4B"), swallowing everything in
// between as one bogus math span. Confirmed live: "Testing if 2 monetary
// values such as $1.00 and $4B work." rendered with "1.00 and " eaten as
// math and "$4B work." left dangling outside it.
//
// Since md4c's own pairing can't be reached from outside the library, this
// pre-processing pass runs before md_html() and backslash-escapes any `$`
// that Pandoc's tex_math_dollars rules (the closest thing to a documented,
// widely-used disambiguation heuristic for this exact ambiguity) would
// reject as a math delimiter, so md4c never sees it as a candidate in the
// first place. Per those rules, a `$` can only open math if the next
// character is non-space, and can only close math if the previous
// character is non-space AND the next character is not a digit -- that
// last clause is exactly what rules out "$4B" as a closer for "$1.00"
// (the char after it, '4', is a digit), leaving both dollar signs literal.
// Genuine math like "$x^2$" is unaffected: 'x' isn't a digit-adjacent
// closer, so its closing "$" still qualifies. `$$...$$` display-math
// delimiters are left untouched entirely (rare enough alongside literal
// currency to not need the same treatment, and doubling the heuristic
// above for a 2-char delimiter isn't worth the added risk here).
//
// Backtick code spans and fenced code blocks are skipped verbatim -- `$`
// inside a shell snippet like `$HOME` must never be touched.
std::string escapeStrayDollars(const std::string &md) {
    std::string out;
    out.reserve(md.size());

    auto isFenceLine = [&](size_t lineStart, size_t lineEnd, char &fenceChar, size_t &fenceLen) -> bool {
        size_t p = lineStart;
        int leadingSpaces = 0;
        while (p < lineEnd && md[p] == ' ' && leadingSpaces < 3) { ++p; ++leadingSpaces; }
        if (p >= lineEnd || (md[p] != '`' && md[p] != '~')) return false;
        char c = md[p];
        size_t start = p;
        while (p < lineEnd && md[p] == c) ++p;
        if (p - start < 3) return false;
        fenceChar = c;
        fenceLen = p - start;
        return true;
    };

    bool inFence = false;
    char fenceChar = '`';
    size_t fenceLen = 0;

    // Single monotonically-advancing index over the whole document -- NOT
    // a per-line outer loop wrapping a per-line inner scan. A valid math
    // span is allowed to cross a single '\n' (only a blank line ends a
    // paragraph), so the inner scan below can legitimately consume past the
    // end of the "current" line; a per-line outer loop that re-derives
    // lineEnd and resumes from lineEnd+1 afterwards would then re-walk
    // (and re-append) whatever the inner scan already consumed on the next
    // line. Checking for a fence line only at each actual line start avoids
    // that entirely.
    size_t i = 0;
    while (i < md.size()) {
        bool atLineStart = (i == 0 || md[i - 1] == '\n');
        if (atLineStart) {
            size_t lineEnd = md.find('\n', i);
            if (lineEnd == std::string::npos) lineEnd = md.size();
            char lineFenceChar; size_t lineFenceLen;
            if (isFenceLine(i, lineEnd, lineFenceChar, lineFenceLen)) {
                if (!inFence) {
                    inFence = true; fenceChar = lineFenceChar; fenceLen = lineFenceLen;
                } else if (lineFenceChar == fenceChar && lineFenceLen >= fenceLen) {
                    inFence = false;
                }
                out.append(md, i, lineEnd - i);
                if (lineEnd < md.size()) { out += '\n'; lineEnd += 1; }
                i = lineEnd;
                continue;
            }
        }

        if (inFence) {
            size_t lineEnd = md.find('\n', i);
            if (lineEnd == std::string::npos) lineEnd = md.size();
            out.append(md, i, lineEnd - i);
            if (lineEnd < md.size()) { out += '\n'; lineEnd += 1; }
            i = lineEnd;
            continue;
        }

        char c = md[i];
        if (c == '\n') { out += c; ++i; continue; }
        if (c == '\\' && i + 1 < md.size()) { out += c; out += md[i + 1]; i += 2; continue; }
        if (c == '`') {
            size_t start = i;
            while (i < md.size() && md[i] == '`') ++i;
            size_t tickLen = i - start;
            std::string ticks(tickLen, '`');
            size_t close = md.find(ticks, i);
            // Don't let a code span's closing run of backticks belong to a
            // longer run (CommonMark requires an exact-length match); scan
            // forward if this one falls short.
            while (close != std::string::npos) {
                size_t after = close + tickLen;
                if (after >= md.size() || md[after] != '`') break;
                close = md.find(ticks, after + 1);
            }
            if (close != std::string::npos) {
                out.append(md, start, close + tickLen - start);
                i = close + tickLen;
            } else {
                out.append(md, start, i - start);
            }
            continue;
        }
        if (c == '$') {
            if (i + 1 < md.size() && md[i + 1] == '$') { out += "$$"; i += 2; continue; }
            if (i + 1 >= md.size() || isspace((unsigned char)md[i + 1])) {
                out += "\\$"; ++i; continue;
            }
            // Candidate opener; look for a valid closer up to the next
            // blank line (math can't span a paragraph break).
            size_t paraEnd = md.find("\n\n", i);
            if (paraEnd == std::string::npos) paraEnd = md.size();
            size_t k = i + 1;
            bool found = false;
            while (k < paraEnd) {
                if (md[k] == '\\') { k += 2; continue; }
                if (md[k] == '$' && !(k + 1 < md.size() && md[k + 1] == '$')) {
                    bool prevNonSpace = !isspace((unsigned char)md[k - 1]);
                    bool nextNonDigit = !(k + 1 < md.size() && isdigit((unsigned char)md[k + 1]));
                    if (prevNonSpace && nextNonDigit) { found = true; break; }
                }
                ++k;
            }
            if (found) {
                out.append(md, i, k + 1 - i);
                i = k + 1;
            } else {
                out += "\\$";
                ++i;
            }
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

std::string parseMarkdownToHtml(const std::string &markdown) {
    std::string html;
    unsigned parserFlags = MD_DIALECT_GITHUB | MD_FLAG_LATEXMATHSPANS;
    std::string preprocessed = escapeStrayDollars(markdown);
    md_html(preprocessed.data(), (MD_SIZE)preprocessed.size(), mdHtmlOutputCallback, &html, parserFlags, 0);
    return html;
}

// --- Fenced code block post-processing: mermaid/plantuml -> rendered image ---

std::string renderDiagramImgTag(const std::string &lang, const std::string &code, bool darkMode) {
    std::string svg;
    if (lang == "mermaid") {
        svg = DiagramRender::renderMermaidWeb(code, darkMode);
        if (!svg.empty()) svg = DiagramRender::fixMermaidSvgText(svg, darkMode);
    } else { // plantuml / puml
        svg = DiagramRender::renderPlantUmlWeb(code, darkMode);
        if (!svg.empty() && darkMode) svg = DiagramRender::fixPlantUmlSvgDark(svg);
    }
    if (svg.empty()) return {};

    int w = 0, h = 0;
    std::vector<uint8_t> png = DiagramRender::svgToHighDpiPng(svg, 2.0f, darkMode, w, h);
    if (png.empty()) return {};

    std::string b64 = base64Encode(png);
    std::string tag = "<p align=\"center\">\n<img src=\"data:image/png;base64," + b64 + "\"";
    if (w > 0 && h > 0) tag += " width=\"" + std::to_string(w) + "\" height=\"" + std::to_string(h) + "\"";
    tag += " />\n</p>\n";
    return tag;
}

// Hand-written scanner replacing what used to be a `static const std::regex
// codeBlockRe(R"RX(<pre><code class="language-(mermaid|plantuml|puml)">
// ([\s\S]*?)</code></pre>\n?)RX")`. Confirmed via a live gdb session against
// the actual deployed plugin (full symbols, real crash): constructing this
// std::regex crashed with SIGSEGV inside std::codecvt::do_unshift, called
// from deep within libstdc++'s regex compiler's locale setup -- reproducible
// even after isolating the call onto its own thread with a 64MB stack (so
// this was NOT simply "not enough stack" as first suspected; libstdc++'s
// std::regex touches C++ locale facets on first construction, and a facet
// race/uninitialized-state issue on a freshly spawned thread in a process
// that also does its own C setlocale() -- DC does, per its own startup log
// -- is a known class of libstdc++ instability). The pattern itself is
// simple enough that a fixed-string scan removes the dependency on
// std::regex here entirely, rather than continuing to tune around an
// unreliable library facility.
struct CodeBlockMatch { size_t start, end; std::string lang, code; };

bool findNextCodeBlock(const std::string &html, size_t from, CodeBlockMatch &out) {
    static const std::string openPrefix = "<pre><code class=\"language-";
    while (true) {
        size_t openPos = html.find(openPrefix, from);
        if (openPos == std::string::npos) return false;
        size_t langStart = openPos + openPrefix.size();
        size_t langEnd = html.find('"', langStart);
        if (langEnd == std::string::npos) return false;
        std::string lang = html.substr(langStart, langEnd - langStart);
        // Must be immediately followed by `>` (matches the original
        // regex's literal `">` right after the captured language group).
        if (langEnd + 1 >= html.size() || html[langEnd + 1] != '>') { from = langEnd + 1; continue; }
        if (lang != "mermaid" && lang != "plantuml" && lang != "puml") { from = langEnd + 1; continue; }

        size_t codeStart = langEnd + 2;
        static const std::string closeTag = "</code></pre>";
        size_t closeTagPos = html.find(closeTag, codeStart);
        if (closeTagPos == std::string::npos) return false; // unterminated block, same as the regex simply not matching

        size_t blockEnd = closeTagPos + closeTag.size();
        if (blockEnd < html.size() && html[blockEnd] == '\n') ++blockEnd; // optional trailing \n, matching `\n?`

        out.start = openPos;
        out.end = blockEnd;
        out.lang = std::move(lang);
        out.code = html.substr(codeStart, closeTagPos - codeStart);
        return true;
    }
}

std::string replaceDiagramBlocks(const std::string &htmlIn, bool darkMode) {
    std::string out;
    size_t lastEnd = 0;
    size_t searchFrom = 0;
    CodeBlockMatch m;
    while (findNextCodeBlock(htmlIn, searchFrom, m)) {
        std::string code = htmlUnescape(m.code);
        std::string rendered = renderDiagramImgTag(m.lang, code, darkMode);
        out.append(htmlIn, lastEnd, m.start - lastEnd);
        out += rendered.empty() ? htmlIn.substr(m.start, m.end - m.start) : rendered; // fall back to the plain code block on failure
        lastEnd = m.end;
        searchFrom = m.end;
    }
    out.append(htmlIn, lastEnd, std::string::npos);
    return out;
}

// --- <x-equation> post-processing: LaTeX math -> rendered image ---
// md4c-html renders MD_FLAG_LATEXMATHSPANS spans as <x-equation>...</x-equation>
// (inline) / <x-equation type="display">...</x-equation> (display) -- a
// non-standard tag by design (HTML has no native math element), meant to be
// post-processed by the consumer. That's us.

std::string renderMathTag(const std::string &tex, bool isDisplay, bool darkMode) {
    int w = 0, h = 0;
    std::vector<uint8_t> png = renderLatexToPng(tex, darkMode, w, h);
    if (png.empty()) {
        // Fallback: plain text, same shape as the original md4qt-based code's
        // non-LaTeX/parse-failure fallback. `tex` is the raw (unescaped)
        // source at this point, so it needs re-escaping for safe embedding.
        std::string escaped = htmlEscape(tex);
        return isDisplay ? ("<pre class=\"math block\"><code>" + escaped + "</code></pre>\n")
                          : ("<span class=\"math inline\">$" + escaped + "$</span>");
    }
    std::string b64 = base64Encode(png);
    std::string imgTag = "<img src=\"data:image/png;base64," + b64 + "\"";
    // Without explicit width/height, Qt6's QTextImageFormat has nothing
    // to read a "natural size" from at all (confirmed live: it reads back
    // 0x0), so MarkdownViewerWidget::scaleImages() skips it entirely --
    // LaTeX formulas rendered without these attributes never scaled with
    // the rest of the document's zoom. w/h here are already the PNG's own
    // logical (post-oversampling) pixel size, same as renderDiagramImgTag
    // above, so this doesn't change how large the formula renders, only
    // makes that size explicit for the zoom feature to read.
    if (w > 0 && h > 0) imgTag += " width=\"" + std::to_string(w) + "\" height=\"" + std::to_string(h) + "\"";
    imgTag += " />";
    return isDisplay ? ("<p align=\"center\">" + imgTag + "</p>") : ("<span class=\"math inline\">" + imgTag + "</span>");
}

// Hand-written scanner replacing what used to be
// `static const std::regex mathRe(R"(<x-equation( type="display")?>
// ([\s\S]*?)</x-equation>)")`. This is the THIRD static const std::regex in
// this plugin, and the one that was missed in the first two rounds of the
// same fix (replaceDiagramBlocks's codeBlockRe and fixMermaidSvgText's
// foreignObjRe/textTspanRe/etc, both in diagram_render.cpp) -- explaining
// why the crash persisted even after those were removed: this file has real
// LaTeX content ($$...$$), so replaceMathTags runs on every test and hit
// this regex every time regardless of what else was fixed. Confirmed root
// cause (see diagram_render.cpp's fixMermaidSvgText for the full writeup):
// std::regex construction in this process crashes inside
// std::codecvt::do_unshift via regex_traits<char>'s locale/collate setup --
// not a stack or threading issue, and not something regex-pattern-specific.
bool findXEquationTag(const std::string &s, size_t from, size_t &matchStart, size_t &matchEnd,
                      bool &isDisplay, std::string &inner)
{
    static const std::string openTag = "<x-equation";
    static const std::string closeTag = "</x-equation>";
    size_t pos = from;
    while (true) {
        pos = s.find(openTag, pos);
        if (pos == std::string::npos) return false;
        size_t p = pos + openTag.size();
        bool display = false;
        static const std::string typeDisplay = " type=\"display\"";
        if (s.compare(p, typeDisplay.size(), typeDisplay) == 0) {
            display = true;
            p += typeDisplay.size();
        }
        if (p >= s.size() || s[p] != '>') { pos += openTag.size(); continue; }
        size_t contentStart = p + 1;
        size_t closePos = s.find(closeTag, contentStart);
        if (closePos == std::string::npos) return false; // unterminated, same as no regex match
        matchStart = pos;
        matchEnd = closePos + closeTag.size();
        isDisplay = display;
        inner = s.substr(contentStart, closePos - contentStart);
        return true;
    }
}

std::string replaceMathTags(const std::string &htmlIn, bool darkMode) {
    std::string out;
    size_t lastEnd = 0, searchFrom = 0;
    size_t ms, me; bool isDisplay; std::string rawTexEscaped;
    while (findXEquationTag(htmlIn, searchFrom, ms, me, isDisplay, rawTexEscaped)) {
        std::string rawTex = htmlUnescape(rawTexEscaped);
        out.append(htmlIn, lastEnd, ms - lastEnd);
        out += renderMathTag(rawTex, isDisplay, darkMode);
        lastEnd = me;
        searchFrom = me;
    }
    out.append(htmlIn, lastEnd, std::string::npos);
    return out;
}

// --- Theming + final document wrap ---

// Single merged stylesheet covering both themes via the `body.theme-light`
// / `body.theme-dark` class selectors postProcessHtml() below always stamps
// onto <body> -- NOT `@media (prefers-color-scheme)`, which Qt's
// QTextBrowser (a QTextDocument rich-text renderer, not a browser engine)
// doesn't support at all, unlike WebKitGTK which does. A plain class
// selector is the one mechanism both toolkits actually honor identically.
const char *DEFAULT_CSS = R"(
body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    line-height: 1.6;
    padding: 16px;
    /* Browsers (WebKit included) omit background colors when printing by
       default to save ink, unless a page explicitly opts back in -- without
       this, printing the dark theme drops body's background-color entirely
       and the physical page prints white behind the (still dark-colored)
       text. Qt's QTextDocument print path doesn't understand this
       property at all (harmless no-op there) and needs its own fix; see
       MarkdownViewerWidget::printDocument in plugin_qt6.cpp. */
    -webkit-print-color-adjust: exact;
    print-color-adjust: exact;
}
body.theme-light { color: #24292e; background-color: #ffffff; }
body.theme-dark { color: #c9d1d9; background-color: #0d1117; }
h1, h2, h3, h4, h5, h6 {
    margin-top: 24px;
    margin-bottom: 16px;
    font-weight: 600;
    line-height: 1.25;
    color: #58a6ff;
}
h1 { font-size: 2em; padding-bottom: .3em; }
h2 { font-size: 1.5em; padding-bottom: .3em; }
h3 { font-size: 1.25em; }
h4 { font-size: 1em; }
body.theme-light h1, body.theme-light h2 { border-bottom: 1px solid #eaecef; }
body.theme-dark h1, body.theme-dark h2 { border-bottom: 1px solid #21262d; }
code {
    font-family: SFMono-Regular, Consolas, "Liberation Mono", Menlo, Courier, monospace;
    font-size: 85%;
    border-radius: 3px;
    padding: .2em .4em;
}
body.theme-light code { background-color: #e4e4e4; color: #24292e; }
body.theme-dark code { background-color: #2d333b; color: #e6edf3; }
pre {
    font-family: SFMono-Regular, Consolas, "Liberation Mono", Menlo, Courier, monospace;
    font-size: 85%;
    border-radius: 6px;
    padding: 0;
    margin: 0;
    overflow: auto;
    line-height: 1.45;
}
body.theme-light pre { background-color: #bec0c4; color: #24292e; }
body.theme-dark pre { background-color: #2d333b; color: #e6edf3; }
pre *, code, code * {
    background-color: transparent !important;
}
body.theme-light table.blockquote { color: #808080; }
body.theme-dark table.blockquote { color: #aaaaaa; }
table {
    border-collapse: collapse;
    width: 100%;
    margin-top: 0;
    margin-bottom: 16px;
}
table th, table td {
    padding: 6px 13px;
}
/* !important here isn't decorative -- the themed border rule below is
   scoped under body.theme-light/body.theme-dark for color, which raises
   its specificity above a plain "table.blockquote td" selector. Without
   this, that generic themed rule wins and re-adds borders to the
   blockquote's accent-bar table and the code block's wrapper table (which
   is a plain, unclassed <table> for background-color reasons, so it needs
   excluding here too). */
table.blockquote td, table.codeblock td {
    border: none !important;
}
body.theme-light table th, body.theme-light table td { border: 1px solid #dfe2e5; }
body.theme-dark table th, body.theme-dark table td { border: 1px solid #30363d; }
body.theme-light table tr:nth-child(2n) { background-color: #f6f8fa; }
body.theme-dark table tr:nth-child(2n) { background-color: #161b22; }
img {
    max-width: 100%;
    box-sizing: content-box;
}
hr {
    height: .25em;
    padding: 0;
    margin: 24px 0;
    background-color: #58a6ff;
    border: 0;
}
a { color: #58a6ff; text-decoration: none; }
a:hover { text-decoration: underline; }
ol.footnotes { font-size: 0.9em; }
sup a { text-decoration: none; }
)";

std::string postProcessHtml(const std::string &rawHtml, bool darkMode, const std::string &customCssPath) {
    std::string html = rawHtml;

    // The blockquote/pre text color used to be a hardcoded inline
    // style="color:..." here, always winning over ANY loaded stylesheet's
    // own rule for it (inline style beats a CSS class selector no matter
    // what's loaded) -- confirmed live as the actual cause of "the
    // blockquote color changes when I switch light/dark even though my
    // own custom markdownpart.css sets table.blockquote { color: ... }":
    // the custom CSS's color was never reaching the page at all. Left the
    // accent-bar/background colors (structural, not something a
    // text-focused custom stylesheet would reasonably target) hardcoded
    // per mode, but the TEXT color is now left for whichever stylesheet
    // loads below (default or custom) to control via its own rules,
    // inherited normally by the child <td>/<pre> the way CSS is supposed
    // to work.
    if (darkMode) {
        replaceAll(html, "<blockquote>",
            "<table border=\"0\" class=\"blockquote\" width=\"100%\" cellspacing=\"0\" cellpadding=\"8\" style=\"margin-left: 20px;\"><tr><td width=\"4\" bgcolor=\"#58a6ff\" style=\"padding: 0;\"></td><td width=\"16\" style=\"padding: 0;\"></td><td>");
        replaceAll(html, "<pre>",
            "<table border=\"0\" class=\"codeblock\" width=\"100%\" cellspacing=\"0\" cellpadding=\"12\" bgcolor=\"#2d333b\" style=\"margin: 12px 0;\"><tr><td bgcolor=\"#2d333b\"><pre style=\"background-color:#2d333b; margin:0; padding:0;\">");
        replaceAll(html, "<pre class=",
            "<table border=\"0\" class=\"codeblock\" width=\"100%\" cellspacing=\"0\" cellpadding=\"12\" bgcolor=\"#2d333b\" style=\"margin: 12px 0;\"><tr><td bgcolor=\"#2d333b\"><pre style=\"background-color:#2d333b; margin:0; padding:0;\" class=");
    } else {
        replaceAll(html, "<blockquote>",
            "<table border=\"0\" class=\"blockquote\" width=\"100%\" cellspacing=\"0\" cellpadding=\"8\" style=\"margin-left: 20px;\"><tr><td width=\"4\" bgcolor=\"#58a6ff\" style=\"padding: 0;\"></td><td width=\"16\" style=\"padding: 0;\"></td><td>");
        replaceAll(html, "<pre>",
            "<table border=\"0\" class=\"codeblock\" width=\"100%\" cellspacing=\"0\" cellpadding=\"12\" bgcolor=\"#bec0c4\" style=\"margin: 12px 0;\"><tr><td bgcolor=\"#bec0c4\"><pre style=\"background-color:#bec0c4; margin:0; padding:0;\">");
        replaceAll(html, "<pre class=",
            "<table border=\"0\" class=\"codeblock\" width=\"100%\" cellspacing=\"0\" cellpadding=\"12\" bgcolor=\"#bec0c4\" style=\"margin: 12px 0;\"><tr><td bgcolor=\"#bec0c4\"><pre style=\"background-color:#bec0c4; margin:0; padding:0;\" class=");
    }

    replaceAll(html, "</blockquote>", "</td></tr></table>");
    replaceAll(html, "</pre>", "</pre></td></tr></table>");

    std::string hrReplacement = darkMode ? "<hr color=\"#58a6ff\" size=\"2\" />" : "<hr color=\"#58a6ff\" size=\"2\" />";
    replaceAll(html, "<hr />", hrReplacement);
    replaceAll(html, "<hr>", hrReplacement);

    // CSS lookup precedence: customCssPath (the ini's theme_file_path) ->
    // <dir of DefaultIniName>/markdownview.css -> ~/.config/markdownpart.css
    // -> built-in default (written out to <dir of DefaultIniName>/markdownview.css
    // if nothing was found anywhere, so there's always a real file to edit).
    // One file covers BOTH themes (see DEFAULT_CSS above) -- no more
    // "-dark"-suffixed sibling file lookup; the `body.theme-light`/
    // `body.theme-dark` class selector on <body> below does the
    // light/dark split within a single stylesheet.
    //
    // Whenever customCssPath itself isn't what ends up being used (empty,
    // stale, or nothing found at all), g_lastAutoResolvedCssPath records
    // the file that WAS used so the caller can write it back into the ini's
    // theme_file_path -- keeping the ini honest about what's actually
    // rendering instead of silently drifting from it.
    g_lastAutoResolvedCssPath.clear();

    std::string cssStr, targetCssFile;
    if (!customCssPath.empty() && fileExists(customCssPath)) {
        targetCssFile = customCssPath;
    }
    std::string dirCss;
    if (targetCssFile.empty()) {
        dirCss = g_pluginConfigDir + "/markdownview.css";
        std::string userPartCss = homeDir() + "/.config/markdownpart.css";
        if (fileExists(dirCss)) {
            targetCssFile = dirCss;
            g_lastAutoResolvedCssPath = dirCss;
        } else if (fileExists(userPartCss)) {
            targetCssFile = userPartCss;
            g_lastAutoResolvedCssPath = userPartCss;
        }
    }
    if (!targetCssFile.empty()) cssStr = readFileUtf8(targetCssFile);
    if (cssStr.empty()) {
        cssStr = DEFAULT_CSS;
        // Nothing on disk anywhere in the lookup chain -- seed a real,
        // user-editable file at <dir of DefaultIniName>/markdownview.css
        // instead of only ever using this in-memory constant, so there's
        // always something to customize from. Written once; every
        // subsequent render finds it via the normal fileExists() check
        // above and this branch never runs again.
        if (!dirCss.empty()) {
            writeFileUtf8(dirCss, DEFAULT_CSS);
            g_lastAutoResolvedCssPath = dirCss;
        }
    }

    // <body> always carries a class naming the active mode, so a single
    // custom theme file can hold both a light and a dark ruleset (scoped
    // as `body.theme-light { ... }` / `body.theme-dark { ... }`) instead of
    // needing two separate files. Deliberately NOT `@media
    // (prefers-color-scheme)` -- that's a real browser media query WebKit
    // (the GTK plugin) would honor but Qt's QTextBrowser (a rich-text
    // renderer, not a browser engine) does not support at all, which would
    // make a single theme file behave differently on the two platforms
    // that share this exact rendering code. A plain class selector is
    // supported by both.
    std::string bodyClass = darkMode ? "theme-dark" : "theme-light";
    return "<!DOCTYPE html><html><head><style>" + cssStr + "</style></head><body class=\"" + bodyClass + "\">" + html + "</body></html>";
}

// md4c has no concept of Pandoc/PHP-Markdown-Extra style footnotes: a
// `[^label]` inline reference plus a `[^label]: definition text` elsewhere
// (usually grouped under the document's own "References" heading) are both
// CommonMark-illegal syntax that md4c-html passes through completely
// untouched as literal text -- e.g. a manually-numbered References section
// written that way renders as broken raw "[^13]: Some Source, ..." text
// instead of numbered, clickable citations. No std::regex here -- same
// hand-written-scanner approach as findNextCodeBlock() above, for the same
// confirmed-live libstdc++ locale-facet crash reason.
struct FootnoteDef { std::string label, contentHtml; };
struct FootnoteDefMatch { size_t start, end; std::vector<FootnoteDef> defs; };

// True if `html[pos]` starts a "[^label]: " marker, and if so fills
// `labelEnd`/`markerEnd` with the position just past the label's "]" and
// just past the following ": ". A bare "[^" not immediately followed by
// "]: " is just incidental text, not a footnote marker.
bool matchFootnoteMarker(const std::string &html, size_t pos, size_t &labelEnd, size_t &markerEnd, std::string &label) {
    if (html.compare(pos, 2, "[^") != 0) return false;
    size_t labelStart = pos + 2;
    size_t end = html.find(']', labelStart);
    if (end == std::string::npos) return false;
    std::string lbl = html.substr(labelStart, end - labelStart);
    if (lbl.empty() || lbl.find('[') != std::string::npos || lbl.find(' ') != std::string::npos) return false;
    size_t afterLabel = end + 1;
    if (afterLabel + 1 >= html.size() || html[afterLabel] != ':' || html[afterLabel + 1] != ' ') return false;
    labelEnd = end;
    markerEnd = afterLabel + 2;
    label = std::move(lbl);
    return true;
}

// Splits one paragraph's inner text into one or more `[^label]: content`
// definitions. Needed because CommonMark joins consecutive non-blank
// source lines with no blank line between them into a SINGLE paragraph
// (a soft line break becomes a plain space, not a new paragraph) -- an
// extremely common way to write a References list, e.g.:
//   [^1]: Some Source, ...
//   [^2]: Another Source, ...
// with no blank line separating the two collapses into one giant <p>
// containing both definitions run together, not two separate <p>s. Each
// definition here runs until the next "[^label]: " marker found in the
// same paragraph, or the paragraph's end, whichever comes first.
void splitFootnoteDefs(const std::string &inner, std::vector<FootnoteDef> &out) {
    size_t labelEnd, markerEnd;
    std::string label;
    if (!matchFootnoteMarker(inner, 0, labelEnd, markerEnd, label)) return;
    size_t contentStart = markerEnd;

    while (true) {
        size_t nextMarkerPos = std::string::npos;
        size_t searchPos = contentStart;
        std::string nextLabel;
        size_t nLabelEnd, nMarkerEnd;
        while (true) {
            size_t candidate = inner.find("[^", searchPos);
            if (candidate == std::string::npos) break;
            if (matchFootnoteMarker(inner, candidate, nLabelEnd, nMarkerEnd, nextLabel)) {
                nextMarkerPos = candidate;
                break;
            }
            searchPos = candidate + 2;
        }

        size_t contentEnd = (nextMarkerPos == std::string::npos) ? inner.size() : nextMarkerPos;
        std::string content = inner.substr(contentStart, contentEnd - contentStart);
        // Trim the single trailing space CommonMark's soft-break-as-space
        // rule leaves right before the next marker.
        while (!content.empty() && content.back() == ' ') content.pop_back();
        out.push_back({label, content});

        if (nextMarkerPos == std::string::npos) break;
        label = std::move(nextLabel);
        contentStart = nMarkerEnd;
    }
}

// Finds the next `<p>[^label]: ...</p>` footnote-definition paragraph
// at/after `from` and splits it into however many definitions it actually
// contains (see splitFootnoteDefs).
bool findNextFootnoteDef(const std::string &html, size_t from, FootnoteDefMatch &out) {
    static const std::string openPrefix = "<p>[^";
    while (true) {
        size_t openPos = html.find(openPrefix, from);
        if (openPos == std::string::npos) return false;
        size_t innerStart = openPos + 3; // skip "<p>", leave the "[^..." for matchFootnoteMarker
        size_t labelEnd, markerEnd;
        std::string label;
        if (!matchFootnoteMarker(html, innerStart, labelEnd, markerEnd, label)) { from = openPos + openPrefix.size(); continue; }

        static const std::string closeTag = "</p>";
        size_t closePos = html.find(closeTag, markerEnd);
        if (closePos == std::string::npos) return false;

        out.start = openPos;
        out.end = closePos + closeTag.size();
        out.defs.clear();
        splitFootnoteDefs(html.substr(innerStart, closePos - innerStart), out.defs);
        return true;
    }
}

// Replaces every `[^label]` inline reference with a superscript anchor --
// only for a label that has a matching definition, so incidental literal
// "[^...]" text elsewhere in the document is never touched.
std::string replaceFootnoteRefs(const std::string &htmlIn, const std::vector<FootnoteDef> &defs) {
    std::string out = htmlIn;
    for (const auto &def : defs) {
        std::string token = "[^" + def.label + "]";
        std::string replacement = "<sup><a href=\"#fn-" + def.label + "\" id=\"fnref-" +
            def.label + "\">" + def.label + "</a></sup>";
        size_t pos = 0;
        while ((pos = out.find(token, pos)) != std::string::npos) {
            out.replace(pos, token.size(), replacement);
            pos += replacement.size();
        }
    }
    return out;
}

// Strips every footnote-definition paragraph out of the document, converts
// remaining inline [^label] references into superscript links, and drops a
// generated numbered list of the definitions (each with a back-link to its
// first reference) in where the first definition used to be -- matching
// how kpartview's KDE-native markdownpart backend already renders the same
// syntax. Returns the input unchanged if the document uses no footnotes.
std::string processFootnotes(const std::string &htmlIn) {
    static const std::string kPlaceholder = "\x01""FOOTNOTES_LIST\x01";
    std::vector<FootnoteDef> defs;
    std::string body;
    size_t lastEnd = 0, searchFrom = 0;
    bool placedMarker = false;
    FootnoteDefMatch m;
    while (findNextFootnoteDef(htmlIn, searchFrom, m)) {
        body.append(htmlIn, lastEnd, m.start - lastEnd);
        if (!placedMarker) { body += kPlaceholder; placedMarker = true; }
        for (auto &def : m.defs) defs.push_back(std::move(def));
        lastEnd = m.end;
        searchFrom = m.end;
    }
    body.append(htmlIn, lastEnd, std::string::npos);

    if (defs.empty()) return htmlIn;

    body = replaceFootnoteRefs(body, defs);

    std::string list = "<ol class=\"footnotes\">";
    for (const auto &def : defs) {
        list += "<li id=\"fn-" + def.label + "\">" + def.contentHtml +
            " <a href=\"#fnref-" + def.label + "\">&#8617;</a></li>";
    }
    list += "</ol>";

    size_t markerPos = body.find(kPlaceholder);
    if (markerPos != std::string::npos) body.replace(markerPos, kPlaceholder.size(), list);
    return body;
}

std::string renderMarkdown(const std::string &markdown, bool darkMode, const std::string &customCssPath) {
    std::string html = parseMarkdownToHtml(markdown);
    html = processFootnotes(html);
    html = replaceDiagramBlocks(html, darkMode);
    html = replaceMathTags(html, darkMode);
    std::string result = postProcessHtml(html, darkMode, customCssPath);
    return result;
}

} // namespace

void init() {
    static bool microtex_initialized = false;
    if (!microtex_initialized) {
        tex::LaTeX::init("/usr/share/clatexmath");
        microtex_initialized = true;
    }
}

void setPluginConfigDir(const std::string &dir) {
    g_pluginConfigDir = dir;
}

std::string getLastAutoResolvedCssPath() {
    return g_lastAutoResolvedCssPath;
}

std::string renderFileToHtml(const std::string &filePath, bool darkMode, const std::string &customCssPath) {
    init();
    std::string content = readFileUtf8(filePath);
    std::string result = renderMarkdown(content, darkMode, customCssPath);
    return result;
}

std::string renderTextToHtml(const std::string &markdownText, bool darkMode, const std::string &customCssPath) {
    init();
    return renderMarkdown(markdownText, darkMode, customCssPath);
}

} // namespace MarkdownEngine
