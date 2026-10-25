#include "compositionsession.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QPointer>
#include <QThread>
#include <QtTest>

namespace {
QImage solid(const QColor &color, const QSize &size = QSize(32, 32))
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image;
}

QImage pixels(const ImageCompositor::Frame &frame)
{
    auto *context = QOpenGLContext::currentContext();
    if (!context || !frame.token) return {};
    auto *gl = context->extraFunctions();
    GLuint fbo = 0;
    gl->glGenFramebuffers(1, &fbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frame.texture, 0);
    QImage image;
    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        image = QImage(frame.size, QImage::Format_RGBA8888);
        gl->glReadPixels(0, 0, frame.size.width(), frame.size.height(), GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
    }
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &fbo);
    return image.mirrored();
}

struct Fixture {
    QOpenGLContext consumer;
    QOffscreenSurface surface;
    CompositionSession session;
    QList<ImageCompositor::Frame> held;
    QString error;

    bool initialize()
    {
        if (!consumer.create()) return false;
        surface.setFormat(consumer.format());
        surface.create();
        return surface.isValid();
    }
    ImageCompositor::Frame awaitFrame()
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            if (!consumer.makeCurrent(&surface)) return {};
            const auto frame = session.acquireFrame(&error);
            if (frame.token) {
                held.append(frame);
                return frame;
            }
            QTest::qWait(5);
        }
        return {};
    }
    ~Fixture()
    {
        if (surface.isValid() && consumer.makeCurrent(&surface)) {
            for (const auto &frame : held) session.returnFrame(frame);
            consumer.doneCurrent();
        }
        session.stop();
    }
};
}

class CompositionSessionTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void startupAndPixels();
    void invalidRequestsAndRecovery();
    void latestSnapshotAndBackpressure();
    void stopRestartAndNotifications();
    void destructionWhileIdleOrPending();
    void notificationHandlersCanStopOrDelete();
};

void CompositionSessionTest::initTestCase()
{
    Fixture fixture;
    QVERIFY(fixture.initialize());
    QVERIFY(fixture.consumer.makeCurrent(&fixture.surface));
    auto *gl = fixture.consumer.functions();
    qInfo() << "GL_VENDOR:" << reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR));
    qInfo() << "GL_RENDERER:" << reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER));
    qInfo() << "GL_VERSION:" << reinterpret_cast<const char *>(gl->glGetString(GL_VERSION));
}

void CompositionSessionTest::startupAndPixels()
{
    Fixture fixture;
    QVERIFY(fixture.initialize());
    auto &session = fixture.session;
    QSignalSpy started(&session, &CompositionSession::started);
    QSignalSpy available(&session, &CompositionSession::frameAvailable);
    QSignalSpy errors(&session, &CompositionSession::errorOccurred);
    bool deliveredOnGui = true;
    connect(&session, &CompositionSession::frameAvailable, &session, [&] {
        deliveredOnGui &= QThread::currentThread() == qGuiApp->thread();
    });
    QVERIFY(session.start(&fixture.consumer, &fixture.error));
    // Submission is accepted during asynchronous startup. Editing the caller's
    // image afterwards must detach, rather than mutate the submitted snapshot.
    QImage source = solid(Qt::red, QSize(40, 20));
    const quint64 revision = (quint64(1) << 40) + 7;
    QVERIFY(session.submit({{source}}, QSize(80, 80), &fixture.error, revision));
    source.fill(Qt::blue);
    QTRY_COMPARE(started.count(), 1);
    QTRY_VERIFY(available.count() > 0);
    QVERIFY(session.isRunning());
    const auto frame = fixture.awaitFrame();
    QVERIFY2(frame.token, qPrintable(fixture.error));
    QCOMPARE(frame.revision, revision);
    const QImage image = pixels(frame);
    QCOMPARE(image.size(), QSize(80, 80));
    QCOMPARE(image.pixelColor(40, 5), QColor(Qt::black));
    QCOMPARE(image.pixelColor(40, 40), QColor(Qt::red));
    QVERIFY(session.returnFrame(frame, &fixture.error));
    // Reusing a caller revision still produces a distinct lease and matching pixels.
    QVERIFY(session.submit({{source}}, QSize(80, 80), &fixture.error, revision));
    const auto replacement = fixture.awaitFrame();
    QVERIFY(replacement.token && replacement.token != frame.token);
    QCOMPARE(replacement.revision, revision);
    QCOMPARE(pixels(replacement).pixelColor(40, 40), QColor(Qt::blue));
    QVERIFY(session.returnFrame(replacement));
    QVERIFY(session.submit({}, QSize(16, 16))); // Existing callers still default to 0.
    const auto unversioned = fixture.awaitFrame();
    QVERIFY(unversioned.token);
    QCOMPARE(unversioned.revision, quint64(0));
    QVERIFY(session.returnFrame(unversioned));
    QVERIFY(deliveredOnGui);
    QCOMPARE(errors.count(), 0);
}

void CompositionSessionTest::invalidRequestsAndRecovery()
{
    Fixture fixture;
    QVERIFY(fixture.initialize());
    auto &session = fixture.session;
    QString error;
    QVERIFY(!session.submit({}, QSize(16, 16), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!session.start(nullptr, &error));
    QVERIFY(!error.isEmpty());
    QOpenGLContext invalid;
    QVERIFY(!session.start(&invalid, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(session.stop(&error));
    QVERIFY(error.isEmpty());
    QVERIFY(session.start(&fixture.consumer, &error));
    QVERIFY(!session.start(&fixture.consumer, &error));
    QVERIFY(!error.isEmpty());
    QTRY_VERIFY(session.isRunning());
    QVERIFY(!session.submit({}, QSize(0, 16), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!session.submit({{QImage()}}, QSize(16, 16), &error));
    QVERIFY(!error.isEmpty());
    QSignalSpy errors(&session, &CompositionSession::errorOccurred);
    QSignalSpy available(&session, &CompositionSession::frameAvailable);
    // Accepted snapshot; GL size validation must fail on the WORKER and report
    // a real error without emitting a fake successful frame notification.
    QVERIFY(session.submit({}, QSize(1000000, 16), &error, 500));
    QTRY_COMPARE(errors.count(), 1);
    QVERIFY(!errors.first().first().toString().isEmpty());
    QCOMPARE(available.count(), 0);
    QVERIFY(session.isRunning());
    QVERIFY(session.submit({{solid(Qt::green)}}, QSize(32, 32), &error, 37));
    QVERIFY(error.isEmpty());
    QVERIFY(!session.submit({}, QSize(0, 16), &error, 999));
    QVERIFY(!error.isEmpty()); // Invalid input must not replace the valid snapshot.
    const auto frame = fixture.awaitFrame();
    QVERIFY2(frame.token, qPrintable(fixture.error));
    QCOMPARE(pixels(frame).pixelColor(16, 16), QColor(Qt::green));
    QCOMPARE(frame.revision, quint64(37));
    auto changed = frame;
    changed.texture = 0;
    QVERIFY(!session.returnFrame(changed, &error));
    QVERIFY(!error.isEmpty());
    changed = frame;
    changed.revision = 999;
    QVERIFY(!session.returnFrame(changed, &error));
    QVERIFY(!error.isEmpty());
    fixture.consumer.doneCurrent();
    QVERIFY(!session.returnFrame(frame, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(fixture.consumer.makeCurrent(&fixture.surface));
    QVERIFY(session.returnFrame(frame, &error));
    QVERIFY(!session.returnFrame(frame, &error));
    QVERIFY(!error.isEmpty());
    // Wrong-thread requests must not touch GUI surfaces or change the session.
    bool rejected = false;
    auto *other = QThread::create([&] { rejected = !session.submit({}, QSize(16, 16), &error); });
    other->start();
    other->wait();
    delete other;
    QVERIFY(rejected);
    QVERIFY(!error.isEmpty());
    QVERIFY(session.stop());
}

void CompositionSessionTest::latestSnapshotAndBackpressure()
{
    Fixture fixture;
    QVERIFY(fixture.initialize());
    auto &session = fixture.session;
    QVERIFY(session.start(&fixture.consumer));
    QTRY_VERIFY(session.isRunning());
    const QList<QColor> colors{Qt::red, Qt::green, Qt::blue};
    QSignalSpy available(&session, &CompositionSession::frameAvailable);
    QList<ImageCompositor::Frame> held;
    for (const auto &color : colors) {
        const auto previousCount = available.count();
        const quint64 revision = 100 + held.size();
        QVERIFY(session.submit({{solid(color)}}, QSize(32, 32), nullptr, revision));
        QTRY_VERIFY(available.count() > previousCount);
        const auto frame = fixture.awaitFrame();
        QVERIFY2(frame.token, qPrintable(fixture.error));
        QCOMPARE(frame.revision, revision);
        held.append(frame);
    }
    const auto beforeFull = available.count();
    for (int i = 0; i < 100; ++i)
        QVERIFY(session.submit({{solid(Qt::cyan)}}, QSize(64, 32), nullptr, 900 + i));
    // Latest submission wins even when its revision is numerically smaller.
    QVERIFY(session.submit({{solid(Qt::yellow)}}, QSize(64, 32), nullptr, 77));
    QTest::qWait(40); // Let the worker encounter full buffers and timed retry.
    QVERIFY(fixture.consumer.makeCurrent(&fixture.surface));
    const auto unavailable = session.acquireFrame();
    QCOMPARE(unavailable.token, quint64(0));
    QCOMPARE(unavailable.revision, quint64(0));
    QCOMPARE(available.count(), beforeFull);
    QString error;
    QVERIFY(!session.stop(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(session.isRunning());
    for (int i = 0; i < held.size(); ++i) {
        QCOMPARE(pixels(held[i]).pixelColor(16, 16), colors[i]);
        QCOMPARE(held[i].revision, quint64(100 + i));
    }
    QVERIFY(session.returnFrame(held[0]));
    // No extra submission: a freed slot MUST resume the latest pending work.
    // A snapshot already in flight may finish first; return it and keep polling.
    QImage latest;
    quint64 latestRevision = 0;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        const auto frame = fixture.awaitFrame();
        if (!frame.token) break;
        latest = pixels(frame);
        QVERIFY(!latest.isNull());
        latestRevision = frame.revision;
        if (latest.pixelColor(16, 16) == QColor(Qt::cyan))
            QVERIFY(frame.revision >= 900 && frame.revision < 1000);
        else
            QCOMPARE(frame.revision, quint64(77));
        QVERIFY(session.returnFrame(frame));
        if (latest.pixelColor(16, 16) == QColor(Qt::yellow)) break;
    }
    QVERIFY(!latest.isNull());
    QCOMPARE(latest.size(), QSize(64, 32));
    QCOMPARE(latest.pixelColor(16, 16), QColor(Qt::yellow));
    QCOMPARE(latestRevision, quint64(77));
    QCOMPARE(pixels(held[1]).pixelColor(16, 16), QColor(Qt::green));
    QCOMPARE(pixels(held[2]).pixelColor(16, 16), QColor(Qt::blue));
    QVERIFY(session.returnFrame(held[1]));
    QVERIFY(session.returnFrame(held[2]));
    QVERIFY(session.stop());
}

void CompositionSessionTest::stopRestartAndNotifications()
{
    Fixture fixture;
    QVERIFY(fixture.initialize());
    auto &session = fixture.session;
    QSignalSpy started(&session, &CompositionSession::started);
    QSignalSpy available(&session, &CompositionSession::frameAvailable);
    QSignalSpy errors(&session, &CompositionSession::errorOccurred);
    for (int cycle = 0; cycle < 5; ++cycle) {
        QVERIFY(session.start(&fixture.consumer));
        QVERIFY(session.submit({}, QSize(16, 16), nullptr, 500 + cycle));
        // Stop without pumping GUI events: queued callbacks from this generation
        // must never appear after stop or leak into the next start.
        QVERIFY(session.stop());
        QCoreApplication::processEvents();
        QCOMPARE(started.count(), 0);
        QCOMPARE(available.count(), 0);
        QCOMPARE(errors.count(), 0);
        QVERIFY(!session.isRunning());
    }
    ImageCompositor::Frame stale;
    for (int cycle = 0; cycle < 5; ++cycle) {
        QVERIFY(session.start(&fixture.consumer));
        QVERIFY(session.submit({{solid(Qt::red)}}, QSize(32, 32)));
        QTRY_COMPARE(started.count(), cycle + 1);
        const auto frame = fixture.awaitFrame();
        QVERIFY2(frame.token, qPrintable(fixture.error));
        QCOMPARE(frame.revision, quint64(0)); // Canceled pre-restart revisions must not leak.
        QVERIFY(frame.token != stale.token);
        if (stale.token) QVERIFY(!session.returnFrame(stale));
        QVERIFY(session.returnFrame(frame));
        stale = frame;
        QVERIFY(session.stop());
        QVERIFY(session.stop());
        QVERIFY(fixture.consumer.makeCurrent(&fixture.surface));
        QVERIFY(!fixture.consumer.functions()->glIsTexture(frame.texture));
        fixture.consumer.doneCurrent();
    }
    QCOMPARE(errors.count(), 0);
}

void CompositionSessionTest::destructionWhileIdleOrPending()
{
    Fixture fixture;
    QVERIFY(fixture.initialize());
    for (bool pending : {false, true}) {
        auto session = std::make_unique<CompositionSession>();
        QPointer<CompositionSession> guard(session.get());
        QVERIFY(session->start(&fixture.consumer));
        QTRY_VERIFY(session->isRunning());
        if (pending) QVERIFY(session->submit({{solid(Qt::blue)}}, QSize(128, 64)));
        session.reset(); // Must join before the sharing context/surface disappears.
        QVERIFY(guard.isNull());
        QCoreApplication::processEvents(); // No stale callback may dereference it.
    }
}

void CompositionSessionTest::notificationHandlersCanStopOrDelete()
{
    Fixture fixture;
    QVERIFY(fixture.initialize());
    bool stopped = false;
    QSignalSpy available(&fixture.session, &CompositionSession::frameAvailable);
    connect(&fixture.session, &CompositionSession::started, &fixture.session, [&] {
        stopped = fixture.session.stop();
    });
    QVERIFY(fixture.session.start(&fixture.consumer));
    QVERIFY(fixture.session.submit({}, QSize(16, 16)));
    QTRY_VERIFY(stopped);
    QCoreApplication::processEvents();
    QCOMPARE(available.count(), 0);
    QVERIFY(!fixture.session.isRunning());

    auto *session = new CompositionSession;
    QPointer<CompositionSession> guard(session);
    connect(session, &CompositionSession::started, session, [session] { delete session; });
    QVERIFY(session->start(&fixture.consumer));
    QVERIFY(session->submit({}, QSize(16, 16)));
    QTRY_VERIFY(guard.isNull());
    QCoreApplication::processEvents();
}

QTEST_MAIN(CompositionSessionTest)
#include "tst_compositionsession.moc"
