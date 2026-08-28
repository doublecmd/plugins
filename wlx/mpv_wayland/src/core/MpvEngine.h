#pragma once

#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <string>
#include <functional>

/// Toolkit-neutral wrapper around libmpv's client + render-GL API —
/// extracted out of what used to be MpvWidget (QOpenGLWidget). Owns
/// mpv_handle lifecycle, option setup, and command dispatch. Rendering
/// stays toolkit-specific by necessity (mpv's render context needs a
/// live GL context and a getProcAddress callback that only the widget
/// layer can provide), so initRenderContext()/render() take those as
/// parameters rather than trying to hide them.
class MpvEngine {
public:
    MpvEngine();
    ~MpvEngine();

    MpvEngine(const MpvEngine &) = delete;
    MpvEngine &operator=(const MpvEngine &) = delete;

    bool isValid() const { return m_mpv != nullptr; }

    /// getProcAddress: same signature libmpv expects
    /// (void*(*)(void *ctx, const char *name)) — the toolkit layer
    /// supplies one bound to its own current GL context.
    bool initRenderContext(void *(*getProcAddress)(void *ctx, const char *name), void *ctx);
    bool renderContextReady() const { return m_mpvGL != nullptr; }

    void render(int fbo, int width, int height, bool flipY = true);

    /// Called from libmpv's own thread whenever a new frame is ready —
    /// the toolkit layer must marshal this onto its own UI thread
    /// (QMetaObject::invokeMethod for Qt, g_idle_add for GTK) before
    /// touching any widgets.
    using UpdateCallback = std::function<void()>;
    void setUpdateCallback(UpdateCallback cb);

    bool loadFile(const std::string &path);
    void closeFile();

    void command(const char *const args[]);
    void commandString(const std::string &cmd);

private:
    static void onUpdate(void *ctx);

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_mpvGL = nullptr;
    UpdateCallback m_updateCb;
};
