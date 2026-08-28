# MDK Wayland WLX Plugin for Double Commander

A high-performance multimedia viewer plugin (WLX) for Double Commander on Linux, powered by the [MDK SDK](https://github.com/wang-bin/mdk-sdk).

This plugin allows you to instantly preview video and audio files directly in Double Commander's Quick View panel. It is specifically designed to work flawlessly natively on Wayland without suffering from the LCL/toolkit compatibility crashes that plague other media plugins.

This plugin ships as **two independent native builds** — one for DC's **GTK3** build, one for its **Qt6** build. Both share the same `mdk_core` engine (`src/core/MdkEngine.*` — the `dlopen`-based MDK loading, playback control, and hardware-acceleration decoder selection) and have equivalent feature sets. Only the OpenGL rendering surface widget differs (see [Implementation Notes](#implementation-notes-gtk3-vs-qt6)). Install whichever one matches your Double Commander build — they cannot be mixed.

## Features (both variants)

- **Wayland Native**: Renders video correctly on Wayland compositors using hardware-accelerated OpenGL.
- **Out-of-Process Isolation**: Dynamically loads MDK via `dlopen(..., RTLD_LOCAL)` to ensure its internal `libc++` dependencies don't conflict with Double Commander's `libstdc++`.
- **Media Controls**: Includes a built-in control bar with Play/Pause, Infinite Loop (∞ ⟳), an interactive seek slider, and a time duration readout.
- **Hardware Acceleration**: Automatically attempts to use `VAAPI`, `VDPAU`, `CUDA`, `dav1d`, and `FFmpeg` decoders for silky smooth playback with low CPU usage.

## Supported Formats

The plugin automatically detects and plays a wide array of formats, including:
- **Video:** MP4, MKV, AVI, WEBM, FLV, MOV, WMV, MPEG, MPG, M4V, TS, VOB
- **Audio:** MP3, FLAC, WAV, OGG, M4A, AAC, WMA

## Implementation Notes (GTK3 vs Qt6)

No functional feature gaps between the two variants — every control and hardware-acceleration path above works identically on both. The only real difference is the OpenGL surface widget each toolkit renders MDK's video output onto:

| | GTK3 | Qt6 |
|---|---|---|
| OpenGL rendering surface | `GtkGLArea` | `QOpenGLWidget` |

Both bypass their toolkit's unstable `winId()`-based Wayland surface embedding by rendering directly via `MDK_RenderAPI_OpenGL` onto their respective OpenGL widget.

## Prerequisites

### Both variants
- **Double Commander**
- **MDK SDK**: You need the MDK SDK headers to build, and `libmdk.so.0` installed in your system library path (or alongside the plugin) to run.

### GTK3 variant
- GTK3 (`gtk+-3.0`) development packages, with OpenGL support (`GtkGLArea`)

### Qt6 variant
- Qt6 Development Packages (`qt6-base`, `qt6-opengl`)

## Build Instructions

1. Ensure the MDK SDK headers are available and point CMake/the Makefile at their location if it's not the default.
2. Build whichever target(s) you need:
   ```bash
   cd wlx/mdk
   mkdir build && cd build
   cmake ..
   make -j$(nproc) mdk_gtk3   # GTK3 build of DC
   # or
   make -j$(nproc) mdk_qt6    # Qt6 build of DC
   ```
3. Copy the resulting `.wlx` file (`mdk_gtk3.wlx` or `mdk_qt6.wlx`) into your Double Commander WLX plugins directory (`~/.config/doublecmd/plugins/wlx/`).

## Installation / Configuration in Double Commander

1. Open Double Commander.
2. Go to **Configuration > Options > Plugins > Plugins WLX (Lister)**.
3. Click **Add** and select the `.wlx` file matching your DC build.
4. Ensure it is placed high enough in your plugin list so it takes priority for media files. The plugin exposes its own detect string, so Double Commander will automatically route compatible media files to it.

## Architecture

Unlike previous versions that relied on Pascal/Lazarus bindings (which conflict when Double Commander re-initializes its LCL widgetset), this plugin is written entirely in C++, directly against each toolkit's native widget API. It builds an OpenGL-backed widget (`GtkGLArea` on GTK3, `QOpenGLWidget` on Qt6) as a child of the host panel and uses MDK's `MDK_RenderAPI_OpenGL` to render directly onto it, bypassing the unstable `winId()` Wayland surface conflicts either toolkit's higher-level video widgets would otherwise hit.
