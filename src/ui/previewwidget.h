#ifndef PREVIEWWIDGET_H
#define PREVIEWWIDGET_H

#include "graphics/previewsource.h"
#include "graphics/compositor.h"
#include <QTimer>

#include <QColor>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPolygonF>
#include <memory>

class QOpenGLShaderProgram;
class QOpenGLBuffer;
class QOpenGLVertexArrayObject;

class PreviewWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor)
    Q_PROPERTY(QColor surroundingColor READ surroundingColor WRITE setSurroundingColor)

public:
    explicit PreviewWidget(QWidget *parent = nullptr);
    ~PreviewWidget() override;

    // Call on the GUI thread. Topmost first; applied on the next composition tick.
    // An unset transform uses the initial centered, fit-without-upscaling layout.
    void setSources(const QList<PreviewSource> &sources);
    void setSelectedSourceId(const QString &id);

    QColor selectionColor() const
    {
        return m_selectionColor.isValid() ? m_selectionColor : palette().color(QPalette::Highlight);
    }

    QColor surroundingColor() const
    {
        return m_surroundingColor;
    }

    void setSelectionColor(const QColor &color);
    void setSurroundingColor(const QColor &color);

    QString lastError() const
    {
        return m_lastError;
    }

    Compositor *compositor()
    {
        return &m_compositor;
    }

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
    QPointF toCanvas(const QPointF &point) const;
    QPolygonF selectionHandles(const PreviewSource &source) const;
    int handleAt(const PreviewSource &source, const QPointF &point) const;
    void paintSelection();
    void endGesture();
    void updateGesture(const QPointF &position);
    void updateCursor(const QPointF &position);
    void cleanup();
    void reportError(const QString &message);

    QList<PreviewSource> m_sources;
    QString m_selectedSourceId;
    QColor m_selectionColor;
    enum class Gesture {
        None,
        Move,
        Resize,
        Rotate
    };
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
    Compositor m_compositor;
    QTimer m_framePoll;
    std::shared_ptr<const CompositionFrame> m_presentedFrame;
    QMetaObject::Connection m_contextDestruction;
};

#endif // PREVIEWWIDGET_H
