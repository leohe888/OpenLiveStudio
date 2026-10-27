#ifndef PREVIEWWIDGET_H
#define PREVIEWWIDGET_H

#include "previewsource.h"

#include <QColor>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <memory>
#include <map>
class QOpenGLTexture;
class QOpenGLShaderProgram;
class QOpenGLBuffer;
class QOpenGLVertexArrayObject;
class QOpenGLFramebufferObject;

class PreviewWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget *parent = nullptr);
    ~PreviewWidget() override;

    // Call on the GUI thread. Topmost first; an empty list clears the preview.
    // Images retain their native size unless larger than the canvas.
    void setSources(const QList<PreviewSource> &sources);
    void setSurroundingColor(const QColor &color);
    QString lastError() const { return m_lastError; }

signals:
    void sourceError(const QString &sourceId, const QString &message);
    void renderingError(const QString &message);

protected:
    void showEvent(QShowEvent *event) override;
    void initializeGL() override;
    void paintGL() override;

private:
    friend class PreviewWidgetTest;

    void cleanup();
    void reportError(const QString &message);

    QList<PreviewSource> m_sources;
    QColor m_surroundingColor = Qt::black;
    QString m_lastError;
    bool m_initialized = false;
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLBuffer> m_vertexBuffer;
    std::unique_ptr<QOpenGLVertexArrayObject> m_vertexArray;
    struct TextureResource {
        qint64 imageKey = 0;
        std::unique_ptr<QOpenGLTexture> texture;
    };
    std::map<QString, TextureResource> m_textures;
    std::unique_ptr<QOpenGLFramebufferObject> m_canvasFbo;
    QMetaObject::Connection m_contextDestruction;
};

#endif // PREVIEWWIDGET_H
