#ifndef COMPOSITIONSESSION_H
#define COMPOSITIONSESSION_H

#include "imagecompositor.h"
#include <QObject>
#include <memory>

// GUI-thread controller; no live scene models or file decoding on the worker.
// All public methods and destruction belong to the GUI thread. Keep the sharing
// context alive through stop(), and return every acquired frame before stopping.
class CompositionSession : public QObject
{
    Q_OBJECT
public:
    explicit CompositionSession(QObject *parent = nullptr);
    ~CompositionSession() override;

    // True means startup was scheduled, not that GL initialization succeeded.
    // Wait for started(), or errorOccurred(). Null/invalid contexts are rejected.
    bool start(QOpenGLContext *shareContext, QString *error = nullptr);
    bool isRunning() const;
    // Accepted during startup or running. Owns the latest pending snapshot only;
    // QImage implicit sharing preserves pixels when normal caller copies detach.
    // Externally owned pixel memory must stay immutable while used by the session.
    bool submit(const QList<ImageCompositor::Layer> &layers,
                const QSize &canvas = QSize(1920, 1080), QString *error = nullptr);
    // Same context/fence/lease rules as ImageCompositor. Notifications coalesce;
    // frameAvailable() means try the latest result, not one queued event per frame.
    ImageCompositor::Frame acquireFrame(QString *error = nullptr);
    bool returnFrame(const ImageCompositor::Frame &frame, QString *error = nullptr);
    // Refuses outstanding frames without stopping the session. Otherwise cancels
    // pending work, wakes/joins the worker, then destroys the GUI-owned surface.
    // Destruction with outstanding frames is a fatal lifecycle error.
    bool stop(QString *error = nullptr);

signals:
    void started();
    void frameAvailable();
    void errorOccurred(const QString &message);

private:
    bool guiThread(QString *error) const;
    void run(QOffscreenSurface *surface, QOpenGLContext *shareContext, quint64 generation);
    void notify(quint64 generation, bool started, bool frame, const QString &error = {});
    class Private;
    std::unique_ptr<Private> d;
};

#endif // COMPOSITIONSESSION_H
