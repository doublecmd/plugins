#include "MdkEngine.h"

#include <dlfcn.h>
#include <cstdio>
#include <cstring>

namespace {

/* ── dlopen-based lazy MDK loader (unchanged from the original plugin.cpp,
 * just relocated — no Qt here, RTLD_LOCAL isolates libmdk's libc++ from
 * Double Commander's libstdc++ regardless of which toolkit's UI loads it) */

void *g_mdk_lib = nullptr;
bool g_mdk_tried = false;

using fn_new          = mdkPlayerAPI *(*)();
using fn_delete        = void (*)(mdkPlayerAPI **);
using fn_setopt_str    = void (*)(const char *, const char *);
using fn_setopt_ptr    = void (*)(const char *, void *);
using fn_gl_destroyed  = void (*)();

fn_new           p_new = nullptr;
fn_delete        p_delete = nullptr;
fn_setopt_str    p_setopt_str = nullptr;
fn_setopt_ptr    p_setopt_ptr = nullptr;
fn_gl_destroyed  p_gl_destroyed = nullptr;

} // namespace

bool MdkEngine::ensureLoaded()
{
    if (g_mdk_lib) return true;
    if (g_mdk_tried) return false;
    g_mdk_tried = true;

    g_mdk_lib = dlopen("libmdk.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (!g_mdk_lib)
        g_mdk_lib = dlopen("libmdk.so", RTLD_LAZY | RTLD_LOCAL);
    if (!g_mdk_lib) {
        std::fprintf(stderr, "[mdk_wlx] dlopen failed: %s\n", dlerror());
        return false;
    }

    p_new           = (fn_new)          dlsym(g_mdk_lib, "mdkPlayerAPI_new");
    p_delete        = (fn_delete)       dlsym(g_mdk_lib, "mdkPlayerAPI_delete");
    p_setopt_str    = (fn_setopt_str)   dlsym(g_mdk_lib, "MDK_setGlobalOptionString");
    p_setopt_ptr    = (fn_setopt_ptr)   dlsym(g_mdk_lib, "MDK_setGlobalOptionPtr");
    p_gl_destroyed  = (fn_gl_destroyed) dlsym(g_mdk_lib, "MDK_foreignGLContextDestroyed");

    if (!p_new || !p_delete) {
        std::fprintf(stderr, "[mdk_wlx] symbol lookup failed\n");
        dlclose(g_mdk_lib);
        g_mdk_lib = nullptr;
        return false;
    }

    if (p_setopt_str)
        p_setopt_str("logLevel", "Info");

    std::fprintf(stderr, "[mdk_wlx] MDK loaded OK\n");
    return true;
}

MdkEngine::MdkEngine() = default;

MdkEngine::~MdkEngine()
{
    close();
}

void MdkEngine::close()
{
    if (m_api) {
        m_api->setVideoSurfaceSize(m_api->object, -1, -1, nullptr);
        m_api->setState(m_api->object, MDK_State_Stopped);
        p_delete(&m_api);
        m_api = nullptr;
    }
}

bool MdkEngine::open(const std::string &filepath)
{
    if (!ensureLoaded())
        return false;

    m_api = p_new();
    if (!m_api) {
        std::fprintf(stderr, "[mdk_wlx] mdkPlayerAPI_new returned null\n");
        return false;
    }

    const char *decs[] = {"VAAPI", "VDPAU", "CUDA", "dav1d", "FFmpeg", nullptr};
    m_api->setVideoDecoders(m_api->object, decs);

    mdkRenderCallback cb;
    cb.opaque = this;
    cb.cb = [](void *, void *opaque) {
        auto *self = static_cast<MdkEngine *>(opaque);
        if (self->m_frameReadyCb)
            self->m_frameReadyCb();
    };
    m_api->setRenderCallback(m_api->object, cb);

    m_api->setMedia(m_api->object, filepath.c_str());
    m_api->setState(m_api->object, MDK_State_Playing);
    return true;
}

void MdkEngine::setRenderAPI(const mdkRenderAPI *api)
{
    if (m_api)
        m_api->setRenderAPI(m_api->object, api, nullptr);
}

void MdkEngine::setVideoSurfaceSize(int w, int h)
{
    if (m_api)
        m_api->setVideoSurfaceSize(m_api->object, w, h, nullptr);
}

void MdkEngine::renderVideo()
{
    if (m_api)
        m_api->renderVideo(m_api->object, nullptr);
}

void MdkEngine::setBackgroundColor(float r, float g, float b, float a)
{
    if (m_api)
        m_api->setBackgroundColor(m_api->object, r, g, b, a, nullptr);
}

void MdkEngine::onGlContextDestroyed()
{
    if (m_api)
        m_api->setVideoSurfaceSize(m_api->object, -1, -1, nullptr);
    else if (p_gl_destroyed)
        p_gl_destroyed();
}

void MdkEngine::play()
{
    if (m_api) m_api->setState(m_api->object, MDK_State_Playing);
}

void MdkEngine::pause()
{
    if (m_api) m_api->setState(m_api->object, MDK_State_Paused);
}

void MdkEngine::stop()
{
    if (m_api) m_api->setState(m_api->object, MDK_State_Stopped);
}

bool MdkEngine::isPlaying() const
{
    return m_api && m_api->state(m_api->object) == MDK_State_Playing;
}

void MdkEngine::setLoop(bool loop)
{
    if (m_api) m_api->setLoop(m_api->object, loop ? -1 : 0);
}

void MdkEngine::seek(int64_t posMs)
{
    if (m_api) m_api->seek(m_api->object, posMs, mdkSeekCallback{});
}

int64_t MdkEngine::position() const
{
    return m_api ? m_api->position(m_api->object) : 0;
}

int64_t MdkEngine::duration() const
{
    if (!m_api) return 0;
    const mdkMediaInfo *info = m_api->mediaInfo(m_api->object);
    return info ? info->duration : 0;
}

void MdkEngine::setFrameReadyCallback(FrameReadyCallback cb)
{
    m_frameReadyCb = std::move(cb);
}
