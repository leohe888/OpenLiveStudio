#ifndef CANVASRENDERER_H
#define CANVASRENDERER_H

#include "graphics/rendersourcesnapshot.h"
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

// 代次区分采集会话，序号区分会话内的帧，避免重启后误复用旧纹理。
struct RenderedSource
{
    qint64 imageKey = 0;
    quint64 generation = 0;
    quint64 sequence = 0;
};

// 所有方法及析构都要求所属 GL 上下文处于当前状态，否则资源创建和释放可能落在错误上下文。
// 渲染器只读取来源快照，不访问界面控件或来源管理器。
class CanvasRenderer : public QObject, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit CanvasRenderer(QObject *parent = nullptr);
    ~CanvasRenderer() override;
    bool initialize();
    bool render(const QList<RenderSourceSnapshot> &sources);
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


private:
    friend class CanvasRendererTest;
    bool uploadPixels(const CaptureFrame &frame, GLuint destination,
                      std::array<std::unique_ptr<QOpenGLTexture>, 3> &planes);
    std::unique_ptr<QOpenGLShaderProgram> m_pixelProgram;
    bool drawable(const RenderSourceSnapshot &source) const;
    GLuint filterTexture(const RenderSourceSnapshot &source, GLuint original, bool &flipY);
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
        std::array<std::unique_ptr<QOpenGLTexture>, 3> pixelPlanes;
        QString filterError;
        QString uploadError;
        quint64 generation = 0, sequence = 0;
        bool ready = false;
    };

    std::map<QString, TextureResource> m_textures;
    std::unique_ptr<QOpenGLFramebufferObject> m_canvasFbo;
    std::array<std::unique_ptr<QOpenGLFramebufferObject>, 2> m_filterFbos;
};

#endif
