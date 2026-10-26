#ifndef PREVIEWWIDGET_H
#define PREVIEWWIDGET_H

#include <QImage>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <memory>

class QOpenGLTexture;
class QOpenGLShaderProgram;
class QOpenGLBuffer;
class QOpenGLVertexArrayObject;

class PreviewWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget *parent = nullptr);
    ~PreviewWidget() override;

    // Call on the GUI thread. A null image clears the preview.
    void setImage(const QImage &image);
    QString lastError() const { return m_lastError; }

signals:
    void renderingError(const QString &message);

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    void cleanup();
    void reportError(const QString &message);

    QImage m_image;
    bool m_imageDirty = false;
    QString m_lastError;
    bool m_initialized = false;
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLBuffer> m_vertexBuffer;
    std::unique_ptr<QOpenGLVertexArrayObject> m_vertexArray;
    std::unique_ptr<QOpenGLTexture> m_texture;
    QMetaObject::Connection m_contextDestruction;
};

#endif // PREVIEWWIDGET_H
