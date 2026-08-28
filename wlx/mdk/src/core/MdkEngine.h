#pragma once

#include <cstdint>
#include <functional>
#include <string>

// MDK's own C API headers are toolkit-neutral (plain C structs/function
// pointers) — safe to include directly in the core, no Qt involved.
#include <mdk/c/Player.h>
#include <mdk/c/RenderAPI.h>
#include <mdk/c/MediaInfo.h>

/// Toolkit-neutral wrapper around libmdk's C API (mdkPlayerAPI), loaded via
/// dlopen at runtime (RTLD_LOCAL, to isolate libmdk's libc++ from Double
/// Commander's libstdc++ — see MdkEngine.cpp).
///
/// This is the de-Qtified extraction of what plugin.cpp used to do inline:
/// dlopen/dlsym loading, mdkPlayerAPI lifecycle, and all player control
/// (play/pause/seek/loop/position/duration). Rendering itself is
/// necessarily toolkit-specific (it needs a live GL context from whichever
/// widget owns the surface), so setRenderAPI()/renderVideo() just forward
/// the caller-populated mdkRenderAPI struct through to libmdk — the Qt (or
/// future GTK) layer is the one that knows how to fill that struct in.
class MdkEngine {
public:
    MdkEngine();
    ~MdkEngine();

    MdkEngine(const MdkEngine &) = delete;
    MdkEngine &operator=(const MdkEngine &) = delete;

    /// True once the dlopen'd libmdk symbols are loaded and mdkPlayerAPI
    /// has been created successfully.
    bool isValid() const { return m_api != nullptr; }

    bool open(const std::string &filepath);

    /// Explicit teardown, callable while a GL context is current (the
    /// destructor calls this too, but the toolkit layer needs to control
    /// exactly when it happens relative to its own makeCurrent()/
    /// doneCurrent() — MDK's teardown must run with the context current).
    void close();

    // --- Rendering (toolkit fills the struct, engine just forwards it) ---
    void setRenderAPI(const mdkRenderAPI *api);
    void setVideoSurfaceSize(int w, int h);
    void renderVideo();
    void setBackgroundColor(float r, float g, float b, float a);

    /// Called by the toolkit layer's GL-context-destroyed handler.
    void onGlContextDestroyed();

    // --- Playback control ---
    void play();
    void pause();
    void stop();
    bool isPlaying() const;
    void setLoop(bool loop);
    void seek(int64_t posMs);
    int64_t position() const;
    int64_t duration() const;

    /// Invoked (from an MDK-internal thread — the toolkit layer must marshal
    /// back onto its own UI thread, e.g. via QMetaObject::invokeMethod) when
    /// a new frame is ready to render.
    using FrameReadyCallback = std::function<void()>;
    void setFrameReadyCallback(FrameReadyCallback cb);

private:
    static bool ensureLoaded();

    mdkPlayerAPI *m_api = nullptr;
    FrameReadyCallback m_frameReadyCb;
};
