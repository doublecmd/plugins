#pragma once

#include <string>
#include <vector>
#include <cstdint>

/// Renders a LaTeX math expression to PNG bytes. Implemented twice, once
/// per UI target, so each plugin only links the graphics dependency it
/// already needs instead of forcing a single choice on both:
///   - src/qt6/latex_render_qt.cpp: MicroTeX's Qt Graphics2D backend
///     (QPainter/QPixmap) -- only linked into markdownview_qt6, which
///     already links Qt6::Gui for its own UI.
///   - src/gtk3/latex_render_cairo.cpp: MicroTeX's Cairo backend
///     (cairomm/pangomm) -- only linked into markdownview_gtk3, which
///     otherwise has zero Qt/Gui dependency.
/// markdown_engine.cpp (toolkit-neutral) calls this without knowing or
/// caring which implementation it links against.
///
/// Returns empty on failure (unparseable LaTeX, etc.) -- the caller falls
/// back to plain-text rendering of the source expression.
std::vector<uint8_t> renderLatexToPng(const std::string &tex, bool darkMode,
                                       int &logicalWidth, int &logicalHeight);
