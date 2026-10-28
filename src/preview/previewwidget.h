#ifndef PREVIEWWIDGET_H
#define PREVIEWWIDGET_H

#include "previewsource.h"
#include "capture/gpuinterop.h"

#include <QColor>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPolygonF>
#include <map>
#include <memory>

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
    // An unset transform uses the initial centered, fit-without-upscaling layout.
    void setSources(const QList<PreviewSource> &sources);
    void setSelectedSourceId(const QString &id);
    void setSelectionColor(const QColor &color);
    void setSurroundingColor(const QColor &color);
    QString lastError() const { return m_lastError; }

signals:
    void selectionRequested(const QString &id);
    void transformRequested(const QString &id, const SourceTransform &transform);
    void sourceError(const QString &sourceId, const QString &message);
    void renderingError(const QString &message);
    void gpuTransferFailed(const QString &sourceId, quint64 generation, const QString &message);

protected:
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void initializeGL() override;
    void paintGL() override;

private:
    friend class PreviewWidgetTest;

    const PreviewSource *selectedSource() const;
    bool selectable(const PreviewSource &source) const;
    bool drawable(const PreviewSource &source) const;
    QPointF toCanvas(const QPointF &point) const;
    QPolygonF selectionHandles(const PreviewSource &source) const;
    int handleAt(const PreviewSource &source, const QPointF &point) const;
    void paintSelection();
    void endGesture();
    void updateGesture(const QPointF &position);
    void updateCursor(const QPointF &position);
    void cleanup();
    void reportError(const QString &message);
    GLuint filterTexture(const PreviewSource &source, GLuint original, bool &flipY);

    QList<PreviewSource> m_sources;
    QString m_selectedSourceId;
    QColor m_selectionColor{QStringLiteral("#72AAFF")};
    enum class Gesture { None, Move, Resize, Rotate };
    Gesture m_gesture = Gesture::None;
    QString m_gestureSourceId;
    SourceTransform m_gestureStart;
    QPointF m_pressCanvas;
    int m_resizeHandle = -1;
    QColor m_surroundingColor = Qt::black;
    QString m_lastError;
    bool m_initialized = false;
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLBuffer> m_vertexBuffer;
    std::unique_ptr<QOpenGLVertexArrayObject> m_vertexArray;
    struct TextureResource {
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
    QMetaObject::Connection m_contextDestruction;
};

#endif // PREVIEWWIDGET_H
