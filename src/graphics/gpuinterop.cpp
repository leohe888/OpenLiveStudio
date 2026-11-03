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
    Register registerObject = nullptr;
    Unregister unregisterObject = nullptr;
    Lock lock = nullptr, unlock = nullptr;
    HANDLE interopDevice = nullptr;
    ID3D11Device *d3dDevice = nullptr;

    struct Entry
    {
        std::shared_ptr<GpuFrameResource> resource;
        GLuint texture;
        HANDLE interopObject;
    };

    std::vector<Entry> registeredTextures;
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
    auto &interop = *m_impl;
    if (!interop.interopDevice)
        return;
    auto *gl = QOpenGLContext::currentContext()->extraFunctions();
    for (auto &entry : interop.registeredTextures) {
        interop.unregisterObject(interop.interopDevice, entry.interopObject);
        gl->glDeleteTextures(1, &entry.texture);
    }
    interop.close(interop.interopDevice);
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
    auto &interop = *m_impl;
    auto *context = QOpenGLContext::currentContext();
    auto *gl = context->extraFunctions();
    if (!interop.interopDevice) {
        using Extensions = const char *(WINAPI *)(HDC);
        auto extensions =
            reinterpret_cast<Extensions>(context->getProcAddress("wglGetExtensionsStringARB"));
        const QByteArray names =
            extensions ? QByteArray(extensions(wglGetCurrentDC())) : QByteArray();
        if (!names.split(' ').contains("WGL_NV_DX_interop2"))
            return fail(QStringLiteral("WGL_NV_DX_interop2 unavailable"));
        interop.open = reinterpret_cast<Impl::Open>(context->getProcAddress("wglDXOpenDeviceNV"));
        interop.close =
            reinterpret_cast<Impl::Close>(context->getProcAddress("wglDXCloseDeviceNV"));
        interop.registerObject =
            reinterpret_cast<Impl::Register>(context->getProcAddress("wglDXRegisterObjectNV"));
        interop.unregisterObject =
            reinterpret_cast<Impl::Unregister>(context->getProcAddress("wglDXUnregisterObjectNV"));
        interop.lock = reinterpret_cast<Impl::Lock>(context->getProcAddress("wglDXLockObjectsNV"));
        interop.unlock =
            reinterpret_cast<Impl::Lock>(context->getProcAddress("wglDXUnlockObjectsNV"));
        if (!interop.open || !interop.close || !interop.registerObject ||
            !interop.unregisterObject || !interop.lock || !interop.unlock)
            return fail(QStringLiteral("GPU interop entry points unavailable"));
        interop.d3dDevice = frame.gpu->device.Get();
        interop.interopDevice = interop.open(interop.d3dDevice);
        if (!interop.interopDevice)
            return fail(QStringLiteral("D3D11/OpenGL device pairing failed"));
    }
    if (interop.d3dDevice != frame.gpu->device.Get())
        return fail(QStringLiteral("D3D11 capture device changed"));
    // 分辨率变化会替换纹理池，先注销仅由导入缓存持有的旧资源，避免长期占用显存。
    for (auto it = interop.registeredTextures.begin(); it != interop.registeredTextures.end();) {
        if (it->resource.use_count() == 1) {
            interop.unregisterObject(interop.interopDevice, it->interopObject);
            gl->glDeleteTextures(1, &it->texture);
            it = interop.registeredTextures.erase(it);
        } else
            ++it;
    }
    auto it = std::find_if(interop.registeredTextures.begin(), interop.registeredTextures.end(),
                           [&](const auto &entry) {
                               return entry.resource == frame.gpu;
                           });
    if (it == interop.registeredTextures.end()) {
        GLuint texture = 0;
        gl->glGenTextures(1, &texture);
        HANDLE interopObject =
            interop.registerObject(interop.interopDevice, frame.gpu->texture.Get(), texture,
                                   GL_TEXTURE_2D, 0x0000 /* 只读访问：GL 不修改采集纹理。 */);
        if (!interopObject) {
            gl->glDeleteTextures(1, &texture);
            return fail(QStringLiteral("D3D11 texture registration failed"));
        }
        interop.registeredTextures.push_back({frame.gpu, texture, interopObject});
        it = std::prev(interop.registeredTextures.end());
    }
    if (!interop.lock(interop.interopDevice, 1, &it->interopObject))
        return fail(QStringLiteral("GPU texture lock failed"));
    GLint previousReadFramebuffer = 0, previousDrawFramebuffer = 0, texture = 0;
    gl->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    GLuint readFramebuffer = 0;
    gl->glGenFramebuffers(1, &readFramebuffer);
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
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
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(previousReadFramebuffer));
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(previousDrawFramebuffer));
    gl->glDeleteFramebuffers(1, &readFramebuffer);
    const bool unlocked = interop.unlock(interop.interopDevice, 1, &it->interopObject);
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
