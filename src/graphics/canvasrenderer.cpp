#include "graphics/canvasrenderer.h"
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QMatrix4x4>
#include <QScopeGuard>
#include <algorithm>

CanvasRenderer::CanvasRenderer(QObject *parent) : QObject(parent) {}
CanvasRenderer::~CanvasRenderer() { cleanup(); }

bool CanvasRenderer::initialize()
{
    initializeOpenGLFunctions();
    m_lastError.clear();
    m_initialized = false;
    if (QOpenGLContext::currentContext()->isOpenGLES() || QOpenGLContext::currentContext()->format().version() < qMakePair(3, 3)) {
        reportError(tr("合成需要 OpenGL 3.3 或更高版本。"));
        return false;
    }

    m_program = std::make_unique<QOpenGLShaderProgram>();
    if (!m_program->addShaderFromSourceFile(QOpenGLShader::Vertex,
                                           QStringLiteral(":/assets/shaders/texture.vert"))
        || !m_program->addShaderFromSourceFile(QOpenGLShader::Fragment,
                                              QStringLiteral(":/assets/shaders/texture.frag"))
        || !m_program->link() || !m_program->bind()) {
        reportError(tr("无法初始化合成着色器：%1").arg(m_program->log()));
        return false;
    }

    // Position (x, y), then top-left-origin image coordinates (u, v).
    static constexpr GLfloat vertices[] = {
        -1, -1, 0, 1,
         1, -1, 1, 1,
        -1,  1, 0, 0,
         1,  1, 1, 0,
    };
    m_vertexArray = std::make_unique<QOpenGLVertexArrayObject>();
    m_vertexBuffer = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    if (!m_vertexArray->create() || !m_vertexBuffer->create()) {
        m_program->release();
        reportError(tr("无法创建合成顶点资源。"));
        return false;
    }
    m_vertexArray->bind();
    if (!m_vertexBuffer->bind()) {
        m_vertexArray->release();
        m_program->release();
        reportError(tr("无法绑定合成顶点缓冲。"));
        return false;
    }
    m_vertexBuffer->setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_vertexBuffer->allocate(vertices, sizeof(vertices));
    const bool bufferAllocated = m_vertexBuffer->size() == sizeof(vertices);
    m_program->enableAttributeArray(0);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(GLfloat));
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(GLfloat), 2, 4 * sizeof(GLfloat));
    m_program->setUniformValue("sourceTexture", 0);
    m_vertexArray->release();
    m_vertexBuffer->release();
    m_program->release();
    if (!bufferAllocated) {
        reportError(tr("无法分配合成顶点缓冲。"));
        return false;
    }
    GLint previousDrawFramebuffer = 0;
    GLint previousReadFramebuffer = 0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    QOpenGLFramebufferObjectFormat canvasFormat;
    canvasFormat.setTextureTarget(GL_TEXTURE_2D);
    canvasFormat.setInternalTextureFormat(GL_RGBA8);
    canvasFormat.setAttachment(QOpenGLFramebufferObject::NoAttachment);
    canvasFormat.setSamples(0);
    m_canvasFbo = std::make_unique<QOpenGLFramebufferObject>(QSize(1920, 1080), canvasFormat);
    const bool canvasValid = m_canvasFbo->isValid();
    if (!canvasValid)
        m_canvasFbo.reset();
    else {
        glBindTexture(GL_TEXTURE_2D, m_canvasFbo->texture());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, GLuint(previousTexture));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(previousDrawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(previousReadFramebuffer));
    if (!canvasValid) {
        reportError(tr("无法创建 1920×1080 合成画布。"));
        return false;
    }
    m_initialized = true;
    return true;
}

bool CanvasRenderer::render(const QList<PreviewSource> &sources)
{
    if (!m_initialized) return false;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    const auto restore = qScopeGuard([&] {
        glDisable(GL_BLEND);
        glBindTexture(GL_TEXTURE_2D, 0);
        m_vertexArray->release();
        m_program->release();
    });
    const QSize canvasSize = m_canvasFbo->size();
    if (!m_canvasFbo->bind()) {
        reportError(tr("无法绑定合成画布。"));
        return false;
    }
    glViewport(0, 0, canvasSize.width(), canvasSize.height());
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    for (auto it = m_textures.begin(); it != m_textures.end();) {
        const bool retained = std::any_of(sources.cbegin(), sources.cend(),
            [&](const PreviewSource &source) { return source.id == it->first && source.hasFrame()
                    && (source.frame ? source.frame->generation == it->second.generation : it->second.generation == 0); });
        if (!retained) it = m_textures.erase(it);
        else ++it;
    }
    for (const auto &source : sources) {
        if (!source.visible || !source.hasFrame()) continue;
        auto &resource = m_textures[source.id];
        if (source.frame ? (resource.generation == source.frame->generation && resource.sequence == source.frame->sequence)
                         : resource.imageKey == source.image.cacheKey()) continue;
        resource.ready = false;
        resource.imageKey = source.image.cacheKey();
        resource.generation = source.frame ? source.frame->generation : 0;
        resource.sequence = source.frame ? source.frame->sequence : 0;
        GLint maxTextureSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
        QString error;
        if (source.pixelSize().width() > maxTextureSize || source.pixelSize().height() > maxTextureSize) {
            error = tr("合成图片尺寸超过显卡纹理限制。");
        } else {
            if (!resource.texture || QSize(resource.texture->width(), resource.texture->height()) != source.pixelSize()) {
                resource.texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
                resource.texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
                resource.texture->setSize(source.pixelSize().width(), source.pixelSize().height());
                resource.texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
                resource.texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
                resource.texture->setWrapMode(QOpenGLTexture::ClampToEdge);
            }
            if (source.frame && source.frame->gpu) {
                if (resource.transferFailed) continue;
                if (!resource.interop) resource.interop = std::make_unique<GpuInterop>();
                QString reason;
                if (!resource.interop->copy(*source.frame, resource.texture->textureId(), &reason)) {
                    resource.transferFailed = true;
                    emit gpuTransferFailed(source.id, source.frame->generation, reason);
                    continue;
                }
            } else {
                resource.interop.reset();
                const auto image = source.image.convertToFormat(QImage::Format_RGBA8888);
                resource.texture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, image.constBits());
            }
            const GLenum uploadError = glGetError();
            if (!resource.texture->isCreated() || !resource.texture->isStorageAllocated()
                || uploadError != GL_NO_ERROR) {
                resource.texture.reset();
                error = tr("无法创建合成图片纹理。");
            } else {
                resource.ready = true;
            }
        }
        if (!error.isEmpty() && error != resource.uploadError) emit sourceFailed(source.id, error);
        resource.uploadError = error;
    }
    if (!m_program->bind()) {
        m_initialized = false;
        reportError(tr("无法绑定合成着色器：%1").arg(m_program->log()));
        return false;
    }
    glActiveTexture(GL_TEXTURE0);
    m_vertexArray->bind();
    const auto drawTexture = [&](GLuint texture, const QMatrix4x4 &matrix, bool flipY) {
        m_program->setUniformValue("vertexTransform", matrix);
        m_program->setUniformValue("flipY", flipY);
        glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };
    QMatrix4x4 projection;
    projection.ortho(0.f, float(canvasSize.width()), float(canvasSize.height()), 0.f, -1.f, 1.f);

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    for (auto it = sources.crbegin(); it != sources.crend(); ++it) {
        const auto resource = m_textures.find(it->id);
        if (drawable(*it)) {
            bool flipY = false;
            const GLuint texture = filterTexture(*it, resource->second.texture->textureId(), flipY);
            m_canvasFbo->bind();
            glViewport(0, 0, canvasSize.width(), canvasSize.height());
            glEnable(GL_BLEND);
            m_program->setUniformValue("colorCorrection", false);
            QMatrix4x4 model(SourceGeometry::sourceToCanvas(it->transform));
            model.translate(float(it->transform.size.width() / 2), float(it->transform.size.height() / 2));
            model.scale(float(it->transform.size.width() / 2), float(-it->transform.size.height() / 2));
            drawTexture(texture, projection * model, flipY);
        }
    }

    return true;
}

GLuint CanvasRenderer::filterTexture(const PreviewSource &source, GLuint original, bool &flipY)
{
    flipY = false;
    auto &error = m_textures.at(source.id).filterError;
    const auto fail = [&](const QString &message) {
        if (error != message) {
            error = message;
            emit sourceFailed(source.id, message);
        }
        flipY = false;
        return original;
    };
    GLuint input = original;
    int pass = 0;
    for (const auto &filter : source.filters) {
        if (!filter.enabled) continue;
        if (!filter.isValid()) return fail(tr("色彩校正参数无效，已使用原图。"));
        if (filter.isIdentity()) continue;
        auto &target = m_filterFbos[pass % 2];
        if (!target || target->size() != source.pixelSize()) {
            QOpenGLFramebufferObjectFormat format;
            format.setInternalTextureFormat(GL_RGBA8);
            format.setAttachment(QOpenGLFramebufferObject::NoAttachment);
            target = std::make_unique<QOpenGLFramebufferObject>(source.pixelSize(), format);
            if (!target->isValid()) {
                target.reset();
                return fail(tr("无法创建滤镜缓冲，已使用原图。"));
            }
            glBindTexture(GL_TEXTURE_2D, target->texture());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        if (!target->bind()) return fail(tr("无法绑定滤镜缓冲，已使用原图。"));
        glViewport(0, 0, source.pixelSize().width(), source.pixelSize().height());
        glDisable(GL_BLEND);
        m_program->setUniformValue("vertexTransform", QMatrix4x4());
        m_program->setUniformValue("flipY", pass > 0);
        m_program->setUniformValue("colorCorrection", true);
        m_program->setUniformValue("correction", filter.brightness, filter.contrast, filter.saturation);
        glBindTexture(GL_TEXTURE_2D, input);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        if (glGetError() != GL_NO_ERROR) return fail(tr("滤镜处理失败，已使用原图。"));
        input = target->texture();
        ++pass;
    }
    error.clear();
    flipY = pass > 0;
    return input;
}

void CanvasRenderer::cleanup()
{
    m_initialized = false;
    m_canvasFbo.reset();
    for (auto &fbo : m_filterFbos) fbo.reset();
    m_textures.clear();
    m_vertexArray.reset();
    m_vertexBuffer.reset();
    m_program.reset();
}

bool CanvasRenderer::drawable(const PreviewSource &source) const
{
    const auto it = m_textures.find(source.id);
    if (it == m_textures.end()) return false;
    const auto &resource = it->second;
    return source.visible && source.hasFrame() && SourceGeometry::isValid(source.transform)
        && resource.ready && resource.texture
        && (source.frame ? resource.generation == source.frame->generation && resource.sequence == source.frame->sequence
                         : resource.generation == 0 && resource.imageKey == source.image.cacheKey());
}

QHash<QString, RenderedSource> CanvasRenderer::renderedSources() const
{
    QHash<QString, RenderedSource> result;
    for (const auto &[id, resource] : m_textures)
        if (resource.ready) result.insert(id, {resource.imageKey, resource.generation, resource.sequence});
    return result;
}

void CanvasRenderer::reportError(const QString &message)
{
    m_lastError = message;
    emit renderingFailed(message);
}
