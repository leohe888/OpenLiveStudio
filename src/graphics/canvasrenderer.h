#ifndef CANVASRENDERER_H
#define CANVASRENDERER_H

#include "graphics/previewsource.h"
#include "graphics/gpuinterop.h"
#include <QHash>
#include <QObject>
#include <QOpenGLFunctions>
#include <array>
#include <map>
#include <memory>

class QOpenGLTexture;
class QOpenGLShaderProgram;
class QOpenGLBuffer;
class QOpenGLVertexArrayObject;
class QOpenGLFramebufferObject;

struct RenderedSource
{
    qint64 imageKey = 0;
    quint64 generation = 0;
    quint64 sequence = 0;
};

// All methods, including destruction, require the owning GL context current.
// No widget or item model is accessed by the renderer.
class CanvasRenderer : public QObject, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit CanvasRenderer(QObject *parent = nullptr);
    ~CanvasRenderer() override;
    bool initialize();
    bool render(const QList<PreviewSource> &sources);
    void cleanup();

    QOpenGLFramebufferObject *canvas() const
    {
        return m_canvasFbo.get();
    }

    QHash<QString, RenderedSource> renderedSources() const;

    QString lastError() const
    {
        return m_lastError;
    }

signals:
    void sourceFailed(const QString &sourceId, const QString &message);
    void renderingFailed(const QString &message);
    void gpuTransferFailed(const QString &sourceId, quint64 generation, const QString &message);

private:
    friend class CanvasRendererTest;
    bool drawable(const PreviewSource &source) const;
    GLuint filterTexture(const PreviewSource &source, GLuint original, bool &flipY);
    void reportError(const QString &message);
    QString m_lastError;
    bool m_initialized = false;
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLBuffer> m_vertexBuffer;
    std::unique_ptr<QOpenGLVertexArrayObject> m_vertexArray;

    struct TextureResource
    {
        qint64 imageKey = 0;
        std::unique_ptr<QOpenGLTexture> texture;
        QString filterError;
        QString uploadError;
        std::unique_ptr<GpuInterop> interop;
        quint64 generation = 0, sequence = 0;
        bool ready = false;
        bool transferFailed = false;
    };

    std::map<QString, TextureResource> m_textures;
    std::unique_ptr<QOpenGLFramebufferObject> m_canvasFbo;
    std::array<std::unique_ptr<QOpenGLFramebufferObject>, 2> m_filterFbos;
};

#endif
