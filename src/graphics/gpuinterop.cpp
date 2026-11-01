#include "graphics/gpuinterop.h"
#include "graphics/gpuframeresource.h"
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QScopeGuard>
#include <vector>
#include <algorithm>

#ifdef Q_OS_WIN
#include <GL/gl.h>

struct GpuInterop::Impl
{
    using Open = HANDLE(WINAPI *)(void *);
    using Close = BOOL(WINAPI *)(HANDLE);
    using Register = HANDLE(WINAPI *)(HANDLE, void *, GLuint, GLenum, GLenum);
    using Unregister = BOOL(WINAPI *)(HANDLE, HANDLE);
    using Lock = BOOL(WINAPI *)(HANDLE, GLint, HANDLE *);
    Open open = nullptr;
    Close close = nullptr;
    Register reg = nullptr;
    Unregister unreg = nullptr;
    Lock lock = nullptr, unlock = nullptr;
    HANDLE device = nullptr;
    ID3D11Device *nativeDevice = nullptr;

    struct Entry
    {
        std::shared_ptr<GpuFrameResource> resource;
        GLuint texture;
        HANDLE object;
    };

    std::vector<Entry> entries;
};
#else
struct GpuInterop::Impl
{
};
#endif

GpuInterop::GpuInterop() : m_impl(std::make_unique<Impl>())
{
}

GpuInterop::~GpuInterop()
{
#ifdef Q_OS_WIN
    auto &d = *m_impl;
    if (!d.device)
        return;
    auto *gl = QOpenGLContext::currentContext()->extraFunctions();
    for (auto &entry : d.entries) {
        d.unreg(d.device, entry.object);
        gl->glDeleteTextures(1, &entry.texture);
    }
    d.close(d.device);
#endif
}

bool GpuInterop::copy(const CaptureFrame &frame, GLuint destination, QString *error)
{
#ifdef Q_OS_WIN
    const auto fail = [&](const QString &message) {
        *error = message;
        return false;
    };
    if (!frame.gpu || qEnvironmentVariableIsSet("OPENLIVE_FORCE_INTEROP_FAILURE"))
        return fail(QStringLiteral("GPU interop disabled or no GPU frame"));
    auto &d = *m_impl;
    auto *context = QOpenGLContext::currentContext();
    auto *gl = context->extraFunctions();
    if (!d.device) {
        using Extensions = const char *(WINAPI *)(HDC);
        auto extensions =
            reinterpret_cast<Extensions>(context->getProcAddress("wglGetExtensionsStringARB"));
        const QByteArray names =
            extensions ? QByteArray(extensions(wglGetCurrentDC())) : QByteArray();
        if (!names.split(' ').contains("WGL_NV_DX_interop2"))
            return fail(QStringLiteral("WGL_NV_DX_interop2 unavailable"));
        d.open = reinterpret_cast<Impl::Open>(context->getProcAddress("wglDXOpenDeviceNV"));
        d.close = reinterpret_cast<Impl::Close>(context->getProcAddress("wglDXCloseDeviceNV"));
        d.reg = reinterpret_cast<Impl::Register>(context->getProcAddress("wglDXRegisterObjectNV"));
        d.unreg =
            reinterpret_cast<Impl::Unregister>(context->getProcAddress("wglDXUnregisterObjectNV"));
        d.lock = reinterpret_cast<Impl::Lock>(context->getProcAddress("wglDXLockObjectsNV"));
        d.unlock = reinterpret_cast<Impl::Lock>(context->getProcAddress("wglDXUnlockObjectsNV"));
        if (!d.open || !d.close || !d.reg || !d.unreg || !d.lock || !d.unlock)
            return fail(QStringLiteral("GPU interop entry points unavailable"));
        d.nativeDevice = frame.gpu->device.Get();
        d.device = d.open(d.nativeDevice);
        if (!d.device)
            return fail(QStringLiteral("D3D11/OpenGL device pairing failed"));
    }
    if (d.nativeDevice != frame.gpu->device.Get())
        return fail(QStringLiteral("D3D11 capture device changed"));
    // A resize replaces the pool. Unregister retired resources before importing more.
    for (auto it = d.entries.begin(); it != d.entries.end();) {
        if (it->resource.use_count() == 1) {
            d.unreg(d.device, it->object);
            gl->glDeleteTextures(1, &it->texture);
            it = d.entries.erase(it);
        } else
            ++it;
    }
    auto it = std::find_if(d.entries.begin(), d.entries.end(), [&](const auto &entry) {
        return entry.resource == frame.gpu;
    });
    if (it == d.entries.end()) {
        GLuint texture = 0;
        gl->glGenTextures(1, &texture);
        HANDLE object = d.reg(d.device, frame.gpu->texture.Get(), texture, GL_TEXTURE_2D,
                              0x0000 /* READ_ONLY_NV */);
        if (!object) {
            gl->glDeleteTextures(1, &texture);
            return fail(QStringLiteral("D3D11 texture registration failed"));
        }
        d.entries.push_back({frame.gpu, texture, object});
        it = std::prev(d.entries.end());
    }
    if (!d.lock(d.device, 1, &it->object))
        return fail(QStringLiteral("GPU texture lock failed"));
    GLint readFbo = 0, drawFbo = 0, texture = 0;
    gl->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    GLuint fbo = 0;
    gl->glGenFramebuffers(1, &fbo);
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    gl->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               it->texture, 0);
    const bool complete =
        gl->glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete) {
        gl->glBindTexture(GL_TEXTURE_2D, destination);
        gl->glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, frame.size.width(),
                                frame.size.height());
    }
    const GLenum copyError = gl->glGetError();
    gl->glBindTexture(GL_TEXTURE_2D, GLuint(texture));
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(readFbo));
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(drawFbo));
    gl->glDeleteFramebuffers(1, &fbo);
    const bool unlocked = d.unlock(d.device, 1, &it->object);
    if (!complete || copyError != GL_NO_ERROR || !unlocked)
        return fail(QStringLiteral("GPU texture copy/unlock failed"));
    return true;
#else
    Q_UNUSED(frame);
    Q_UNUSED(destination);
    *error = QStringLiteral("D3D11 interop requires Windows");
    return false;
#endif
}
