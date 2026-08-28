#include "MpvEngine.h"

#include <clocale>
#include <cstdio>

MpvEngine::MpvEngine()
{
    // mpv requires LC_NUMERIC=C or it will refuse to initialize.
    setlocale(LC_NUMERIC, "C");

    m_mpv = mpv_create();
    if (!m_mpv) {
        std::fprintf(stderr, "mpv_wayland: mpv_create() failed\n");
        return;
    }

    // Use the Render API — no native window embedding.
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "hwdec", "no");
    mpv_set_option_string(m_mpv, "osc", "yes");
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "msg-level", "all=no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "yes");
    mpv_set_option_string(m_mpv, "input-vo-keyboard", "yes");

    int err = mpv_initialize(m_mpv);
    if (err < 0) {
        std::fprintf(stderr, "mpv_wayland: mpv_initialize() failed: %d\n", err);
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

MpvEngine::~MpvEngine()
{
    if (m_mpvGL) {
        mpv_render_context_free(m_mpvGL);
        m_mpvGL = nullptr;
    }
    if (m_mpv) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

void MpvEngine::onUpdate(void *ctx)
{
    // Called from libmpv's own thread.
    auto *self = static_cast<MpvEngine *>(ctx);
    if (self->m_updateCb)
        self->m_updateCb();
}

bool MpvEngine::initRenderContext(void *(*getProcAddress)(void *ctx, const char *name), void *ctx)
{
    if (!m_mpv) return false;

    mpv_opengl_init_params glInitParams{getProcAddress, ctx};
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_OPENGL},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitParams},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    int err = mpv_render_context_create(&m_mpvGL, m_mpv, params);
    if (err < 0) {
        std::fprintf(stderr, "mpv_wayland: mpv_render_context_create() failed: %d\n", err);
        return false;
    }

    mpv_render_context_set_update_callback(m_mpvGL, onUpdate, this);
    return true;
}

void MpvEngine::render(int fbo, int width, int height, bool flipY)
{
    if (!m_mpvGL) return;

    mpv_opengl_fbo mpfbo{fbo, width, height, 0};
    int flip = flipY ? 1 : 0;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    mpv_render_context_render(m_mpvGL, params);
}

void MpvEngine::setUpdateCallback(UpdateCallback cb)
{
    m_updateCb = std::move(cb);
}

bool MpvEngine::loadFile(const std::string &path)
{
    if (!m_mpv) return false;
    const char *args[] = {"loadfile", path.c_str(), nullptr};
    int err = mpv_command(m_mpv, args);
    if (err < 0) {
        std::fprintf(stderr, "mpv_wayland: loadfile failed: %d (%s)\n", err, path.c_str());
        return false;
    }
    return true;
}

void MpvEngine::closeFile()
{
    if (!m_mpv) return;
    const char *args[] = {"stop", nullptr};
    mpv_command(m_mpv, args);
}

void MpvEngine::command(const char *const args[])
{
    if (m_mpv) mpv_command(m_mpv, const_cast<const char **>(args));
}

void MpvEngine::commandString(const std::string &cmd)
{
    if (m_mpv) mpv_command_string(m_mpv, cmd.c_str());
}
