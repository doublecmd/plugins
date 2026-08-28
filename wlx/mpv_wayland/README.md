# mpv_wayland Double Commander Plugin

A native WLX (Lister) plugin for Double Commander allowing video playback in the Quick View panel. This plugin uses `libmpv`'s Render API together with a hardware-accelerated OpenGL widget, offering seamless, high-performance video embedding, particularly well-suited for Wayland and HiDPI displays.

![Screenshot](mpv_wayland.png)

This plugin evolved from the `mpv_alt` plugin, modernizing video rendering by removing legacy X11 window-ID (`wid`) embedding, making it fully compatible with Wayland compositors.

This plugin ships as **two independent native builds** — one for DC's **GTK3** build, one for its **Qt6** build. Both share the same `mpv_wayland_core` engine (`src/core/MpvEngine.*` — the `mpv_render_context` setup and playback control) and have equivalent feature sets; the GTK3 variant deliberately mirrors the Qt6 variant's architecture (same `mpv_render_context` + OpenGL-widget pattern used by the `mdk` plugin's own GTK3 port). Install whichever one matches your Double Commander build — they cannot be mixed.

## Features (both variants)

- Hardware-accelerated rendering via `mpv_render_context` onto a native OpenGL widget (`GtkGLArea` on GTK3, `QOpenGLWidget` on Qt6).
- Wayland native rendering and HiDPI pixel ratio awareness (scale factor applied to both the render framebuffer size and forwarded mouse coordinates).
- Full On-Screen Controller (OSC) support via mouse event forwarding into `libmpv`.
- Dedicated keyboard handling bypassing Double Commander's global key intercepts.
- Synchronized rendering updates driven by `libmpv`'s render-update callback.

## Dependencies

### Both variants
- **libmpv**: `mpv` development libraries (`libmpv-dev` / `mpv`)
- **CMake**: `cmake` and `extra-cmake-modules` (KDE CMake modules)
- **Make** or **Ninja**

### GTK3 variant
- GTK3 (`gtk+-3.0`) development packages, with OpenGL support (`GtkGLArea`)

### Qt6 variant
- **Qt6**: Core, Gui, Widgets, OpenGLWidgets

**Debian/Ubuntu:**
```bash
sudo apt install build-essential cmake extra-cmake-modules libmpv-dev
# plus one of:
sudo apt install qt6-base-dev          # Qt6 variant
sudo apt install libgtk-3-dev          # GTK3 variant
```

**Arch Linux:**
```bash
sudo pacman -S base-devel cmake extra-cmake-modules mpv
# plus one of:
sudo pacman -S qt6-base   # Qt6 variant
sudo pacman -S gtk3       # GTK3 variant
```

## Compilation

```bash
cd wlx/mpv_wayland
mkdir build && cd build
cmake ..
make -j$(nproc) mpv_wayland_gtk3   # GTK3 build of DC
# or
make -j$(nproc) mpv_wayland_qt6    # Qt6 build of DC
```

Output: `mpv_wayland_gtk3.wlx` or `mpv_wayland_qt6.wlx` inside the `build/` directory.

## Installation

1. Open Double Commander.
2. Navigate to **Configuration > Options > Plugins > Plugins WLX**.
3. Click **Add** and select the `.wlx` file matching your DC build.
4. Ensure it has priority over other viewer plugins for video extensions (e.g., `.mkv`, `.mp4`).

## Keyboard and Controls

Hovering the mouse across the bottom of the video panel triggers `libmpv`'s On Screen Controller (OSC).
Alternatively, you may click on the video panel to grab the keyboard context. This enables `libmpv`'s default keybindings (e.g., Space for Play/Pause, arrow keys for seeking).
