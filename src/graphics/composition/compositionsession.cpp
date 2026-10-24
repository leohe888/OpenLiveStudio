#include "compositionsession.h"

#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QWaitCondition>
#include <optional>

namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
}

class CompositionSession::Private
{
public:
    struct Snapshot { QList<ImageCompositor::Layer> layers; QSize canvas; };
    mutable QMutex mutex;
    QWaitCondition wake;
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QThread> worker;
    ImageCompositor *compositor = nullptr; // Borrowed only while worker is ready.
    std::optional<Snapshot> pending;
    QSet<quint64> acquired;
    quint64 generation = 0;
    bool stopping = false;
    bool failed = false;
    bool notificationQueued = false;
    bool notifyStarted = false;
    bool notifyFrame = false;
    QString notifyError;
};

CompositionSession::CompositionSession(QObject *parent) : QObject(parent), d(std::make_unique<Private>()) {}

CompositionSession::~CompositionSession()
{
    QString error;
    if (!stop(&error)) qFatal("CompositionSession destruction: %s", qPrintable(error));
}

bool CompositionSession::guiThread(QString *error) const
{
    if (!qGuiApp || QThread::currentThread() != qGuiApp->thread() || thread() != qGuiApp->thread())
        return fail(error, QStringLiteral("CompositionSession must be used on the GUI thread."));
    return true;
}

bool CompositionSession::start(QOpenGLContext *shareContext, QString *error)
{
    if (error) error->clear();
    if (!guiThread(error)) return false;
    if (d->worker) return fail(error, QStringLiteral("Stop the previous session before starting again."));
    if (!shareContext || !shareContext->isValid() || shareContext->thread() != thread())
        return fail(error, QStringLiteral("A valid GUI-owned sharing context is required."));
    auto surface = std::make_unique<QOffscreenSurface>();
    surface->setFormat(shareContext->format());
    surface->create();
    if (!surface->isValid()) return fail(error, QStringLiteral("Cannot create the offscreen surface."));
    QMutexLocker lock(&d->mutex);
    d->surface = std::move(surface);
    d->stopping = d->failed = false;
    const quint64 generation = ++d->generation;
    d->worker.reset(QThread::create([this, shareContext, generation] {
        run(d->surface.get(), shareContext, generation);
    }));
    d->worker->setObjectName(QStringLiteral("compositionWorker"));
    d->worker->start();
    return true;
}

bool CompositionSession::isRunning() const
{
    if (!guiThread(nullptr)) return false;
    QMutexLocker lock(&d->mutex);
    return d->compositor && !d->stopping;
}

bool CompositionSession::submit(const QList<ImageCompositor::Layer> &layers,
                                const QSize &canvas, QString *error)
{
    if (error) error->clear();
    if (!guiThread(error)) return false;
    if (canvas.isEmpty()) return fail(error, QStringLiteral("Canvas dimensions must be positive."));
    for (const auto &layer : layers) {
        if (layer.visible && layer.image.isNull())
            return fail(error, QStringLiteral("A visible layer has no decoded image."));
    }
    QMutexLocker lock(&d->mutex);
    if (!d->worker || d->stopping || d->failed)
        return fail(error, QStringLiteral("The composition session is not accepting snapshots."));
    d->pending = Private::Snapshot{layers, canvas};
    d->wake.wakeOne();
    return true;
}

ImageCompositor::Frame CompositionSession::acquireFrame(QString *error)
{
    if (error) error->clear();
    if (!guiThread(error)) return {};
    QMutexLocker lock(&d->mutex);
    if (!d->compositor || d->stopping) {
        fail(error, QStringLiteral("The composition session is not ready."));
        return {};
    }
    const auto frame = d->compositor->acquireFrame(error);
    if (frame.token) d->acquired.insert(frame.token);
    return frame;
}

bool CompositionSession::returnFrame(const ImageCompositor::Frame &frame, QString *error)
{
    if (error) error->clear();
    if (!guiThread(error)) return false;
    QMutexLocker lock(&d->mutex);
    if (!d->compositor || !d->acquired.contains(frame.token))
        return fail(error, QStringLiteral("The frame does not belong to this active session."));
    if (!d->compositor->returnFrame(frame, error)) return false;
    d->acquired.remove(frame.token);
    d->wake.wakeOne();
    return true;
}

bool CompositionSession::stop(QString *error)
{
    if (error) error->clear();
    if (!guiThread(error)) return false;
    {
        QMutexLocker lock(&d->mutex);
        if (!d->acquired.isEmpty())
            return fail(error, QStringLiteral("Return all acquired frames before stopping the session."));
        d->stopping = true;
        ++d->generation; // Invalidate queued notifications before joining.
        d->pending.reset();
        d->wake.wakeOne();
    }
    if (d->worker) d->worker->wait();
    d->worker.reset();
    d->surface.reset(); // GUI thread, after worker context/FBO destruction.
    QMutexLocker lock(&d->mutex);
    d->notificationQueued = d->notifyStarted = d->notifyFrame = false;
    d->notifyError.clear();
    return true;
}

void CompositionSession::run(QOffscreenSurface *surface, QOpenGLContext *shareContext, quint64 generation)
{
    ImageCompositor compositor; // Construction, GL initialization and destruction on worker.
    QString error;
    if (!compositor.initialize(surface, &error, shareContext)) {
        {
            QMutexLocker lock(&d->mutex);
            d->failed = true;
            d->pending.reset();
        }
        notify(generation, false, false, error);
        return;
    }
    {
        QMutexLocker lock(&d->mutex);
        d->compositor = &compositor;
    }
    notify(generation, true, false);
    for (;;) {
        Private::Snapshot snapshot;
        {
            QMutexLocker lock(&d->mutex);
            while (!d->stopping && !d->pending) d->wake.wait(&d->mutex);
            if (d->stopping) break;
            snapshot = std::move(*d->pending);
            d->pending.reset();
        }
        const bool rendered = compositor.render(snapshot.layers, snapshot.canvas, &error);
        if (rendered || !error.isEmpty()) {
            notify(generation, false, rendered, error);
        } else {
            QMutexLocker lock(&d->mutex);
            if (!d->stopping) {
                // Keep a newer submission if one arrived during rendering.
                if (!d->pending) d->pending = std::move(snapshot);
                // A returned GPU fence may signal after the return notification.
                // Timed retry avoids lost work without busy spinning when full.
                d->wake.wait(&d->mutex, 10);
            }
        }
    }
    {
        QMutexLocker lock(&d->mutex);
        d->compositor = nullptr;
    }
    // All leases were returned before stop. Destructor orders GPU cleanup here.
}

void CompositionSession::notify(quint64 generation, bool started, bool frame, const QString &error)
{
    QMutexLocker lock(&d->mutex);
    if (generation != d->generation || d->stopping) return;
    d->notifyStarted |= started;
    d->notifyFrame |= frame;
    if (!error.isEmpty()) d->notifyError = error;
    if (d->notificationQueued) return;
    d->notificationQueued = true;
    QMetaObject::invokeMethod(this, [this, generation] {
        bool started, frame;
        QString error;
        {
            QMutexLocker lock(&d->mutex);
            if (generation != d->generation) return;
            started = d->notifyStarted;
            frame = d->notifyFrame;
            error = d->notifyError;
            d->notificationQueued = d->notifyStarted = d->notifyFrame = false;
            d->notifyError.clear();
        }
        // A signal handler may stop/restart or delete this session.
        QPointer<CompositionSession> guard(this);
        if (started) emit this->started();
        if (!guard || generation != d->generation) return;
        if (!error.isEmpty()) emit errorOccurred(error);
        if (!guard || generation != d->generation) return;
        if (frame) emit frameAvailable();
    }, Qt::QueuedConnection);
}
