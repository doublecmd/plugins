#pragma once

#include <string>

/// Toolkit-neutral diagram rendering core: CLI subprocess invocation
/// (mmdc / plantuml.jar / curl fallbacks), the Mermaid SVG text-baseline
/// post-processing regex fixup, and settings persistence — all extracted
/// out of what used to be a single Qt-only plugin.cpp. No Qt, no GTK; the
/// UI layer (src/qt6/, src/gtk3/) owns the actual widget/rendering surface
/// and just calls into this for "give me SVG bytes for this file".
namespace DiagramRenderer {

struct Settings {
    bool autoReloadEnabled = true;
    bool darkMode = false;
    bool useSystemDarkMode = true;

    std::string renderer = "java";            // "java" or "web" (PlantUML)
    std::string mermaidRenderer = "local";     // "local" or "web" (Mermaid)
    std::string plantumlPath = "plantuml";
    std::string javaPath = "java";
    std::string plantumlServerUrl = "http://www.plantuml.com/plantuml";
    std::string mmdcPath = "mmdc";
    std::string mermaidServerUrl = "https://mermaid.ink";
    int timeoutMs = 15000;

    /// Load from a simple "[section]\nkey=value" INI file, filling in and
    /// persisting any missing keys with current defaults (mirrors the
    /// original ListSetDefaultParams behavior). pluginName is the INI
    /// section name.
    void loadOrInitDefaults(const std::string &iniPath, const std::string &pluginName);
    void save(const std::string &iniPath, const std::string &pluginName) const;
};

/// Directory containing the running plugin binary (via dladdr on the
/// address of this function) — used to look for a co-located mmdc/
/// plantuml.jar. Empty string if it can't be determined.
std::string pluginDir();

/// Render a Mermaid (.mmd/.mermaid) file to SVG bytes. Tries mmdc (local
/// binary, several search locations, then npx as last resort) or the
/// mermaid.ink web renderer, in the order controlled by
/// settings.mermaidRenderer, falling back to the other on failure.
/// Returns empty string on total failure.
std::string renderMermaid(const Settings &settings, const std::string &inputPath, bool darkMode);

/// Render a PlantUML (.puml/.plantuml) file to SVG bytes, via local
/// plantuml/java -jar plantuml.jar or the plantuml.com web renderer, in
/// the order controlled by settings.renderer, falling back to the other
/// on failure. Returns empty string on total failure.
std::string renderPlantUml(const Settings &settings, const std::string &inputPath, bool darkMode);

/// Mermaid-specific SVG post-processing: mermaid.js emits <tspan> y/dy in
/// `em` units relative to the parent <text>, which most SVG renderers
/// (including Qt's and librsvg) don't resolve the way browsers do. This
/// combines them into an absolute pixel y on the <text> element itself.
std::string fixMermaidSvgText(const std::string &svgData, bool darkMode);

} // namespace DiagramRenderer
