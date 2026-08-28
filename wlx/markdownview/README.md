# MarkdownView Lister Plugin for Double Commander

A Lister (WLX) plugin for [Double Commander](https://doublecmd.sourceforge.io/) that provides rich, interactive previews of Markdown files (`.md`, `.markdown`, `.mdown`, `.mkd`).

This plugin ships as **two independent native builds** — one for DC's **GTK3** build, one for its **Qt6** build. Both share the same Markdown/LaTeX/diagram rendering core (`src/core/`), the same CSS theming system, and the same context menu, but each links against its host toolkit directly and has some real behavioral differences (see [Feature Differences](#feature-differences-gtk3-vs-qt6) below). Install whichever one matches your Double Commander build — they cannot be mixed.

---

## Features & Capabilities (both variants)

* **High-Fidelity Markdown Rendering**: Full support for CommonMark and GitHub-Flavored Markdown (tables, task lists, strikethrough, blockquotes, code blocks) powered by `md4c`.
* **LaTeX Math Equations**: Renders inline (`$E=mc^2$`) and block (`$$\int_0^\infty f(x) dx$$`) mathematical equations locally using **MicroTeX**.
* **Mermaid & PlantUML Diagrams**: Fetches and renders Mermaid and PlantUML diagrams into crisp high-DPI images, with a consistent accent color and automatic dark/light mode adaptation.
* **Live Auto-Reload**: Watches opened files for changes and automatically re-renders the document when saved in an external editor.
* **Theme Modes**: **System**, **Dark**, and **Light** rendering modes, switchable from the context menu.
* **Interactive Navigation**:
  * **Zooming**: `Ctrl` + Mouse Wheel to zoom in/out.
  * **Text Selection & Copy**: Native text selection with context menu and `Ctrl+C` copy support.
* **Context Menu**: Copy Text, Select All, Reload Document, Auto-Reload on Save (toggle), Theme Mode (System/Dark/Light).

---

## Configuration (`markdownview.ini`)

Both variants store their settings the same way and in the same location — the directory Double Commander itself hands the plugin at load time (its `DefaultIniName`), typically:

```path
~/.config/doublecmd/plugins/wlx/markdownview.ini
```

```ini
[markdownview]
# Path to a custom CSS stylesheet file. Takes precedence over default plugin CSS.
theme_file_path=

# Theme rendering mode: system | dark | light (default: system)
mode=system

# Live auto-reload on file save: true | false (default: true)
auto_reload=true
```

## CSS Styling & Customization

A single stylesheet covers both light and dark mode via `body.theme-light` / `body.theme-dark` class selectors on the rendered `<body>` — there is no separate `-dark`-suffixed file. This is deliberate: WebKitGTK (the GTK3 variant's renderer) supports `@media (prefers-color-scheme)`, but Qt's `QTextBrowser` (the Qt6 variant's renderer, a rich-text document view rather than a browser engine) does not support media queries at all, so a plain class selector is the one theming mechanism both toolkits honor identically.

### CSS Lookup Order

1. **`theme_file_path`** setting in `markdownview.ini`, if set and the file exists.
2. **`markdownview.css`** next to the ini file (same `DefaultIniName` directory), e.g. `~/.config/doublecmd/plugins/wlx/markdownview.css`.
3. **`~/.config/markdownpart.css`** — a lower-precedence fallback, useful for a theme shared across other plugins/tools.
4. **Built-in default**, compiled into the binary. If nothing above is found, this default is also written out to `markdownview.css` (step 2's location) so there's always a real, editable file going forward.

---

## Feature Differences (GTK3 vs Qt6)

| | GTK3 | Qt6 |
|---|---|---|
| Rendering engine | WebKitGTK (`webkit2gtk-4.1`) — a full browser engine | `QTextBrowser` (`QTextDocument`) — a rich-text document view, not a browser engine |
| CSS support | Full CSS as implemented by WebKit, including `@media` queries (unused by this plugin, see above) | A limited CSS2.1-ish subset; no `@media` support, and class-selector inheritance through elements is not as reliable as a real browser's — some rules need higher specificity or `!important` to consistently win |
| In-document search (`ListSearchText`) | **Not implemented** — DC's native in-viewer search does nothing in this plugin | Implemented |
| LaTeX rendering backend | MicroTeX's Cairo/Pango backend (`src/gtk3/latex_render_cairo.cpp`) | MicroTeX's Qt backend (`src/qt6/latex_render_qt.cpp`) — same underlying MicroTeX layout engine, different rasterizer |
| Zoom | `webkit_web_view_set/get_zoom_level()`, driven by `GDK_SCROLL_SMOOTH` delta events | `QTextBrowser::zoomIn/zoomOut`, driven by `QWheelEvent` |
| Link/runtime dependency | `libwebkit2gtk-4.1`, `cairomm-1.0`, `pangomm-1.4`, `fontconfig`, `freetype2` | Qt6 Core/Gui/Widgets only |

---

## Building and Installation

### Prerequisites (both variants)
* CMake 3.16+
* C++17 compiler (`g++` or `clang++`)
* `clatexmath` font files (for MicroTeX LaTeX rendering, e.g. `/usr/share/clatexmath`)
* `cairo`, `librsvg-2.0` (used by the shared toolkit-neutral core for diagram rasterization, regardless of which plugin target you build)

### GTK3 variant

Additional prerequisites: GTK3 (`gtk+-3.0`), `webkit2gtk-4.1`, `cairomm-1.0`, `pangomm-1.4`, `fontconfig`, `freetype2` development packages.

```bash
cd wlx/markdownview
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_QT6=OFF -DENABLE_GTK3=ON ..
make -j$(nproc)
```

Output: `markdownview_gtk3.wlx`

### Qt6 variant

Additional prerequisites: Qt6 Development Libraries (`Qt6Core`, `Qt6Gui`, `Qt6Widgets`).

```bash
cd wlx/markdownview
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_QT6=ON -DENABLE_GTK3=OFF ..
make -j$(nproc)
```

Output: `markdownview_qt6.wlx`

Both targets are enabled by default (`ENABLE_QT6=ON ENABLE_GTK3=ON`); a plain `cmake .. && make` builds whichever toolkits are actually found on the system, skipping the other with a CMake warning if its dependencies are missing.

### Installation

Copy the binary matching your Double Commander build to its plugin directory:

```bash
mkdir -p ~/.config/doublecmd/plugins/wlx
cp markdownview_gtk3.wlx ~/.config/doublecmd/plugins/wlx/   # GTK3 build of DC
# or
cp markdownview_qt6.wlx ~/.config/doublecmd/plugins/wlx/    # Qt6 build of DC
```

Register the plugin in Double Commander under **Options -> Plugins -> WLX (Lister Plugins)**, pointing to whichever `.wlx` file matches your DC build.

---

## License

MIT License.
