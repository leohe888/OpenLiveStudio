#ifndef PREVIEWWIDGET_H
#define PREVIEWWIDGET_H

#include "imagecompositor.h"
#include <QOpenGLWidget>
#include <QPointer>
#include <QRectF>

class CompositionSession;
class QOpenGLExtraFunctions;

// Presentation only: never drives composition or accesses live scene models.
// GUI-thread use. The external coordinator starts the session on contextReady()
// and MUST stop it synchronously on contextAboutToBeReleased(), after this view
// has returned its frames. A bound session must outlive the view or be detached.
class PreviewWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget *parent = nullptr);
    ~PreviewWidget() override;

    bool setSession(CompositionSession *session, QString *error = nullptr);
    bool isReady() const { return m_gl != nullptr; }
    QRectF canvasRect() const; // Logical widget coordinates, not output pixels.
    quint64 displayedFrameToken() const { return m_displayedToken; }

signals:
    void contextReady(QOpenGLContext *context);
    void contextAboutToBeReleased();
    void graphicsError(const QString &message);

protected:
    void changeEvent(QEvent *event) override;
    void initializeGL() override;
    void paintGL() override;

private:
    bool returnFrames(QString *error);
    bool releaseGraphics(QString *error);
    void reportError(const QString &message);
    QPointer<CompositionSession> m_session;
    QMetaObject::Connection m_frameConnection, m_errorConnection, m_cleanupConnection;
    QOpenGLExtraFunctions *m_gl = nullptr;
    unsigned int m_readFramebuffer = 0;
    ImageCompositor::Frame m_frame, m_pending;
    quint64 m_displayedToken = 0;
    QString m_lastError;
};

#endif // PREVIEWWIDGET_H
