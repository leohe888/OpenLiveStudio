#include "previewwidget.h"

#include <QDebug>
#include <algorithm>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QScopeGuard>
#include <QShowEvent>
#include <QSurfaceFormat>
#include <QVector2D>

PreviewWidget::PreviewWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
}

PreviewWidget::~PreviewWidget()
{
    cleanup();
}

void PreviewWidget::setSources(const QList<PreviewSource> &sources)
{
    m_sources = sources;
    update();
}

void PreviewWidget::setSurroundingColor(const QColor &color)
{
    if (!color.isValid() || m_surroundingColor == color)
        return;
    m_surroundingColor = color;
    update();
}

void PreviewWidget::showEvent(QShowEvent *event)
{
    QOpenGLWidget::showEvent(event);
    if (!isValid() && m_lastError.isEmpty())
        reportError(tr("无法创建 OpenGL 预览上下文。"));
}

void PreviewWidget::initializeGL()
{
    initializeOpenGLFunctions();
    m_contextDestruction = connect(context(), &QOpenGLContext::aboutToBeDestroyed,
                                  this, &PreviewWidget::cleanup, Qt::DirectConnection);
    m_lastError.clear();
    m_initialized = false;
    if (context()->isOpenGLES() || context()->format().version() < qMakePair(3, 3)) {
        reportError(tr("预览需要 OpenGL 3.3 或更高版本。"));
        return;
    }

    m_program = std::make_unique<QOpenGLShaderProgram>();
    if (!m_program->addShaderFromSourceFile(QOpenGLShader::Vertex,
                                           QStringLiteral(":/assets/shaders/texture.vert"))
        || !m_program->addShaderFromSourceFile(QOpenGLShader::Fragment,
                                              QStringLiteral(":/assets/shaders/texture.frag"))
        || !m_program->link() || !m_program->bind()) {
        reportError(tr("无法初始化预览着色器：%1").arg(m_program->log()));
        return;
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
        reportError(tr("无法创建预览顶点资源。"));
        return;
    }
    m_vertexArray->bind();
    if (!m_vertexBuffer->bind()) {
        m_vertexArray->release();
        m_program->release();
        reportError(tr("无法绑定预览顶点缓冲。"));
        return;
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
        reportError(tr("无法分配预览顶点缓冲。"));
        return;
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
        return;
    }
    m_initialized = true;
}

void PreviewWidget::paintGL()
{
    const QSize pixelSize(qRound(width() * devicePixelRatioF()),
                          qRound(height() * devicePixelRatioF()));
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, pixelSize.width(), pixelSize.height());
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glClearColor(m_surroundingColor.redF(), m_surroundingColor.greenF(),
                 m_surroundingColor.blueF(), 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!m_initialized || pixelSize.isEmpty())
        return;

    const auto restoreWidgetTarget = qScopeGuard([&] {
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glViewport(0, 0, pixelSize.width(), pixelSize.height());
        glDisable(GL_BLEND);
        glBindTexture(GL_TEXTURE_2D, 0);
        m_vertexArray->release();
        m_program->release();
    });
    const QSize canvasSize = m_canvasFbo->size();
    if (!m_canvasFbo->bind()) {
        reportError(tr("无法绑定合成画布。"));
        return;
    }
    glViewport(0, 0, canvasSize.width(), canvasSize.height());
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    for (auto it = m_textures.begin(); it != m_textures.end();) {
        const bool retained = std::any_of(m_sources.cbegin(), m_sources.cend(),
            [&](const PreviewSource &source) { return source.id == it->first && !source.image.isNull(); });
        if (!retained) it = m_textures.erase(it);
        else ++it;
    }
    for (const auto &source : m_sources) {
        if (!source.visible || source.image.isNull()) continue;
        auto &resource = m_textures[source.id];
        if (resource.imageKey == source.image.cacheKey()) continue;
        resource.texture.reset();
        resource.imageKey = source.image.cacheKey();
        GLint maxTextureSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
        QString error;
        if (source.image.width() > maxTextureSize || source.image.height() > maxTextureSize) {
            error = tr("预览图片尺寸超过显卡纹理限制。");
        } else {
            resource.texture = std::make_unique<QOpenGLTexture>(source.image, QOpenGLTexture::DontGenerateMipMaps);
            const GLenum uploadError = glGetError();
            if (!resource.texture->isCreated() || !resource.texture->isStorageAllocated()
                || uploadError != GL_NO_ERROR) {
                resource.texture.reset();
                error = tr("无法创建预览图片纹理。");
            } else {
                resource.texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
                resource.texture->setWrapMode(QOpenGLTexture::ClampToEdge);
            }
        }
        if (!error.isEmpty()) emit sourceError(source.id, error);
    }
    if (!m_program->bind()) {
        m_initialized = false;
        reportError(tr("无法绑定预览着色器：%1").arg(m_program->log()));
        return;
    }
    glActiveTexture(GL_TEXTURE0);
    m_vertexArray->bind();
    const auto drawTexture = [&](GLuint texture, const QSize &sourceSize,
                                  const QSize &targetSize, bool flipY, bool allowUpscale = true) {
        double scale = qMin(double(targetSize.width()) / sourceSize.width(),
                                  double(targetSize.height()) / sourceSize.height());
        if (!allowUpscale) scale = qMin(1.0, scale);
        m_program->setUniformValue("displayScale",
            QVector2D(sourceSize.width() * scale / targetSize.width(),
                      sourceSize.height() * scale / targetSize.height()));
        m_program->setUniformValue("flipY", flipY);
        glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    for (auto it = m_sources.crbegin(); it != m_sources.crend(); ++it) {
        const auto resource = m_textures.find(it->id);
        if (it->visible && resource != m_textures.end() && resource->second.texture)
            drawTexture(resource->second.texture->textureId(), it->image.size(), canvasSize,
                        false, false);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, pixelSize.width(), pixelSize.height());
    glDisable(GL_BLEND);
    // FBO textures use bottom-left-origin coordinates, unlike uploaded images.
    drawTexture(m_canvasFbo->texture(), canvasSize, pixelSize, true);
}

void PreviewWidget::cleanup()
{
    disconnect(m_contextDestruction);
    m_initialized = false;
    if (!context())
        return;

    makeCurrent();
    m_canvasFbo.reset();
    m_textures.clear();
    m_vertexArray.reset();
    m_vertexBuffer.reset();
    m_program.reset();
    doneCurrent();
    // Retain CPU images so a replacement context can upload them again.
}

void PreviewWidget::reportError(const QString &message)
{
    m_lastError = message;
    qWarning().noquote() << message;
    emit renderingError(message);
}
