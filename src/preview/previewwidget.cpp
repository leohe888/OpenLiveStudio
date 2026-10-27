#include "previewwidget.h"

#include <QDebug>
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

void PreviewWidget::setImage(const QImage &image)
{
    m_image = image;
    m_imageDirty = true;
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
    m_imageDirty = true;
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

    if (m_imageDirty) {
        m_imageDirty = false;
        m_texture.reset();
        if (!m_image.isNull()) {
            GLint maxTextureSize = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
            if (m_image.width() > maxTextureSize || m_image.height() > maxTextureSize) {
                reportError(tr("预览图片尺寸超过显卡纹理限制。"));
                return;
            }
            m_texture = std::make_unique<QOpenGLTexture>(m_image, QOpenGLTexture::DontGenerateMipMaps);
            if (!m_texture->isCreated() || !m_texture->isStorageAllocated()) {
                m_texture.reset();
                reportError(tr("无法创建预览图片纹理。"));
                return;
            }
            m_texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
            m_texture->setWrapMode(QOpenGLTexture::ClampToEdge);
        }
        m_lastError.clear();
    }
    if (!m_program->bind()) {
        m_initialized = false;
        reportError(tr("无法绑定预览着色器：%1").arg(m_program->log()));
        return;
    }
    glActiveTexture(GL_TEXTURE0);
    m_vertexArray->bind();
    const auto drawTexture = [&](GLuint texture, const QSize &sourceSize,
                                  const QSize &targetSize, bool flipY) {
        const double scale = qMin(double(targetSize.width()) / sourceSize.width(),
                                  double(targetSize.height()) / sourceSize.height());
        m_program->setUniformValue("displayScale",
            QVector2D(sourceSize.width() * scale / targetSize.width(),
                      sourceSize.height() * scale / targetSize.height()));
        m_program->setUniformValue("flipY", flipY);
        glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };

    if (m_texture) {
        // Blend the straight-alpha image once onto an opaque canvas.
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        drawTexture(m_texture->textureId(), m_image.size(), canvasSize, false);
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
    m_texture.reset();
    m_vertexArray.reset();
    m_vertexBuffer.reset();
    m_program.reset();
    doneCurrent();
    // Retain the CPU image so a replacement context can upload it again.
    m_imageDirty = true;
}

void PreviewWidget::reportError(const QString &message)
{
    m_lastError = message;
    qWarning().noquote() << message;
    emit renderingError(message);
}
