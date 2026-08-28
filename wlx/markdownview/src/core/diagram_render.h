#pragma once

#include <string>
#include <vector>
#include <cstdint>

/// Toolkit-neutral helpers for rendering Mermaid/PlantUML fenced code
/// blocks: fetch a rendered SVG from the public web renderer, patch it up,
/// and rasterize to PNG bytes via librsvg+Cairo. No Qt, no GTK -- used from
/// markdown_engine.cpp regardless of which UI target links it.
namespace DiagramRender {

/// Fetches a rendered SVG for Mermaid source via mermaid.ink. Returns empty
/// on failure (network error, service down, etc.).
std::string renderMermaidWeb(const std::string &code, bool darkMode);

/// Fetches a rendered SVG for PlantUML source via plantuml.com. Returns
/// empty on failure.
std::string renderPlantUmlWeb(const std::string &code, bool darkMode);

/// mermaid.js emits <tspan> y/dy in `em` units relative to the parent
/// <text>, which most SVG renderers (including librsvg) don't resolve the
/// way browsers do -- combines them into an absolute pixel y on the <text>
/// element itself, and replaces <foreignObject> HTML-label workarounds
/// with plain <text> elements librsvg can render directly.
std::string fixMermaidSvgText(const std::string &svg, bool darkMode);

/// Recolors PlantUML's default dark strokes/lines for a dark background.
std::string fixPlantUmlSvgDark(const std::string &svg);

/// Rasterizes SVG bytes to PNG bytes via librsvg + Cairo, at `scale`x the
/// SVG's intrinsic size (for a sharper embedded image).
std::vector<uint8_t> svgToHighDpiPng(const std::string &svg, float scale, bool darkMode,
                                      int &logicalWidth, int &logicalHeight);

} // namespace DiagramRender
