#ifndef MARKDOWN_ENGINE_H
#define MARKDOWN_ENGINE_H

#include <string>

namespace MarkdownEngine {

/**
 * Initialize global sub-engines (e.g. MicroTeX LaTeX parser).
 * Safe to call multiple times.
 */
void init();

/**
 * Tell the engine which directory DC's DefaultIniName lives in, so the
 * CSS lookup chain's "plugin dir" candidate (markdownview.css) is anchored
 * to the location DC actually handed the plugin, rather than an
 * independently-guessed ~/.config/doublecmd/plugins/wlx path. Call once
 * from ListSetDefaultParams before any render call.
 */
void setPluginConfigDir(const std::string &dir);

/**
 * After a render call, returns the CSS file path the engine auto-resolved
 * to when customCssPath was empty or didn't point at an existing file
 * (i.e. it fell through to markdownview.css or ~/.config/markdownpart.css,
 * or just wrote a fresh markdownview.css). Empty if customCssPath itself
 * was used as-is. Callers should persist this back into their ini's
 * theme_file_path setting so the ini reflects what's actually in use.
 */
std::string getLastAutoResolvedCssPath();

/**
 * Parse a markdown file and render it to HTML string with inline LaTeX (MicroTeX)
 * images and embedded diagram SVGs/PNGs.
 *
 * @param filePath Path to the Markdown file.
 * @param darkMode Whether to apply dark mode styling.
 * @param customCssPath Optional path to a custom CSS stylesheet.
 * @return Formatted HTML string ready for display in QTextBrowser or WebKit.
 *
 * "Save Zoom" is NOT handled here -- confirmed live that a CSS `body {
 * font-size: N%; }` rule has zero effect on Qt's QTextDocument (measured
 * identical rendered text width at 50%/100%/182%). Each toolkit applies its
 * own persisted zoom afterwards via its own native zoom API instead
 * (QTextBrowser::zoomIn/zoomOut on Qt6, webkit_web_view_set_zoom_level on
 * GTK3) -- see reloadContent()/reloadContentNow() in the respective
 * plugin_*.cpp.
 */
std::string renderFileToHtml(const std::string& filePath, bool darkMode = false, const std::string& customCssPath = "");

/**
 * Render raw markdown string to HTML.
 */
std::string renderTextToHtml(const std::string& markdownText, bool darkMode = false, const std::string& customCssPath = "");

} // namespace MarkdownEngine

#endif // MARKDOWN_ENGINE_H
