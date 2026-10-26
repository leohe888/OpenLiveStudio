#include "previewwidget.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLTexture>
#include <QOpenGLTextureBlitter>

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
    m_blitter = std::make_unique<QOpenGLTextureBlitter>();
    if (!m_blitter->create())
        reportError(tr("无法初始化预览纹理绘制程序。"));
}

void PreviewWidget::paintGL()
{
    const QSize pixelSize(qRound(width() * devicePixelRatioF()),
                          qRound(height() * devicePixelRatioF()));
    glViewport(0, 0, pixelSize.width(), pixelSize.height());
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!m_blitter || !m_blitter->isCreated() || pixelSize.isEmpty())
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
    const QRectF target((pixelSize.width() - displaySize.width()) / 2,
                        (pixelSize.height() - displaySize.height()) / 2,
                        displaySize.width(), displaySize.height());
    glEnable(GL_BLEND);
    // QOpenGLTexture's QImage upload uses straight RGBA; keep the background opaque.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    m_blitter->bind();
    m_blitter->blit(m_texture->textureId(),
                   QOpenGLTextureBlitter::targetTransform(target, QRect(QPoint(), pixelSize)),
                   QOpenGLTextureBlitter::OriginTopLeft);
    m_blitter->release();
    glDisable(GL_BLEND);
}

void PreviewWidget::cleanup()
{
    disconnect(m_contextDestruction);
    if (!context())
        return;

    makeCurrent();
    m_texture.reset();
    m_blitter.reset();
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
