#include "previewwidget.h"

#include <QDebug>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
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
    m_initialized = true;
}

void PreviewWidget::paintGL()
{
    const QSize pixelSize(qRound(width() * devicePixelRatioF()),
                          qRound(height() * devicePixelRatioF()));
    glViewport(0, 0, pixelSize.width(), pixelSize.height());
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!m_initialized || pixelSize.isEmpty())
        return;

    if (m_imageDirty) {
        m_imageDirty = false;
        m_texture.reset();
        if (!m_image.isNull()) {
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
    if (!m_texture)
        return;

    const double scale = qMin(double(pixelSize.width()) / m_image.width(),
                              double(pixelSize.height()) / m_image.height());
    const QSizeF displaySize(m_image.width() * scale, m_image.height() * scale);
    if (!m_program->bind()) {
        m_initialized = false;
        reportError(tr("无法绑定预览着色器：%1").arg(m_program->log()));
        return;
    }
    m_program->setUniformValue("displayScale", QVector2D(displaySize.width() / pixelSize.width(),
                                                        displaySize.height() / pixelSize.height()));
    glEnable(GL_BLEND);
    // QOpenGLTexture's QImage upload uses straight RGBA; keep the background opaque.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    m_texture->bind(0);
    m_vertexArray->bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_vertexArray->release();
    m_texture->release(0);
    m_program->release();
    glDisable(GL_BLEND);
}

void PreviewWidget::cleanup()
{
    disconnect(m_contextDestruction);
    m_initialized = false;
    if (!context())
        return;

    makeCurrent();
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
