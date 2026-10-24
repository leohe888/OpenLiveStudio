#include "scenesnapshotbuilder.h"
#include "studiostate.h"
#include "compositionsession.h"

#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QPointer>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <atomic>

namespace {
using Snapshot = SceneSnapshotBuilder::Snapshot;
using Error = SceneSnapshotBuilder::SourceError;
using Result = SceneSnapshotBuilder::DecodeResult;

bool writeImage(const QString &path, const QColor &color)
{
    QImage image(QSize(16, 8), QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image.save(path, "PNG");
}
StudioState::SourceData source(const QString &id, const QString &path)
{
    return {id, "Source " + id, "image", path, QSize(16, 8)};
}
Snapshot last(const QSignalSpy &spy)
{
    return spy.isEmpty() ? Snapshot() : qvariant_cast<Snapshot>(spy.last().first());
}

struct Gate {
    QSemaphore entered, resume;
    std::atomic<int> calls{0}, active{0}, maximum{0};
    std::atomic<bool> guiSeen{false}, finished{false};
    bool obsoleteError = false;
    Result read(const QString &path)
    {
        guiSeen = guiSeen.load() || QThread::currentThread() == qGuiApp->thread();
        const int count = ++active;
        int previous = maximum.load();
        while (count > previous && !maximum.compare_exchange_weak(previous, count)) {}
        const bool first = ++calls == 1;
        if (first) { entered.release(); resume.acquire(); }
        Result result;
        if (first && obsoleteError) result.error = "obsolete decoder error";
        else {
            QImageReader reader(path);
            result.image = reader.read();
            if (result.image.isNull()) result.error = reader.errorString();
        }
        --active;
        finished = true;
        return result;
    }
};
struct ResumeOnExit {
    std::shared_ptr<Gate> gate;
    ~ResumeOnExit() { gate->resume.release(); }
};

struct Graphics {
    QOpenGLContext consumer;
    QOffscreenSurface surface;
    CompositionSession session;
    ImageCompositor::Frame held;
    QString error;
    bool initialize()
    {
        if (!consumer.create()) return false;
        surface.setFormat(consumer.format());
        surface.create();
        return surface.isValid() && session.start(&consumer, &error);
    }
    QImage awaitColor(const QColor &color)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            if (!consumer.makeCurrent(&surface)) return {};
            held = session.acquireFrame(&error);
            if (held.token) {
                auto *gl = consumer.extraFunctions();
                GLuint fbo = 0;
                gl->glGenFramebuffers(1, &fbo);
                gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, held.texture, 0);
                QImage image;
                if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                    image = QImage(held.size, QImage::Format_RGBA8888);
                    gl->glReadPixels(0, 0, held.size.width(), held.size.height(), GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
                }
                gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
                gl->glDeleteFramebuffers(1, &fbo);
                if (!session.returnFrame(held, &error)) return {};
                held = {};
                image = image.mirrored();
                if (!image.isNull()) {
                    const QColor actual = image.pixelColor(image.width() / 2, image.height() / 2);
                    if (qAbs(actual.red() - color.red()) <= 2 && qAbs(actual.green() - color.green()) <= 2
                        && qAbs(actual.blue() - color.blue()) <= 2 && actual.alpha() == color.alpha()) return image;
                }
            }
            QTest::qWait(5);
        }
        return {};
    }
    ~Graphics()
    {
        if (held.token && consumer.makeCurrent(&surface)) session.returnFrame(held);
        session.stop();
    }
};
}

class SceneSnapshotBuilderTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void initialAndOrdering();
    void visibilitySelectionAndRemoval();
    void sceneIsolationAndEmpty();
    void decodeErrorsAndRecovery();
    void latestRequestWins();
    void stateDestructionAndShutdown();
    void signalReentrancy();
    void sceneToComposition(); // Native OpenGL only: selected separately by CTest.
};

void SceneSnapshotBuilderTest::initTestCase()
{
    qRegisterMetaType<Snapshot>();
    qRegisterMetaType<Error>();
}

void SceneSnapshotBuilderTest::initialAndOrdering()
{
    QTemporaryDir dir;
    const QString red = dir.filePath("红色 #% image.png"), blue = dir.filePath("blue.png");
    QVERIFY(writeImage(red, Qt::red));
    QVERIFY(writeImage(blue, Qt::blue));
    StudioState state({{"a", "A", {source("top", red), source("bottom", blue)}}});
    SceneSnapshotBuilder builder(&state);
    QSignalSpy ready(&builder, &SceneSnapshotBuilder::snapshotReady);
    QSignalSpy errors(&builder, &SceneSnapshotBuilder::sourceError);
    QSignalSpy loading(&builder, &SceneSnapshotBuilder::loadingChanged);
    QVERIFY(builder.isLoading());
    QTRY_COMPARE(ready.count(), 1);
    QVERIFY(!builder.isLoading());
    QCOMPARE(loading.count(), 1);
    Snapshot snapshot = last(ready);
    QCOMPARE(snapshot.sceneId, "a");
    QCOMPARE(snapshot.canvas, QSize(1920, 1080));
    QCOMPARE(snapshot.sourceIds, QStringList({"top", "bottom"}));
    QCOMPARE(snapshot.layers[0].image.pixelColor(1, 1), QColor(Qt::red));
    QCOMPARE(snapshot.layers[1].image.pixelColor(1, 1), QColor(Qt::blue));
    const auto revision = snapshot.revision;
    QVERIFY(state.moveSource("a", "top", 1));
    QTRY_COMPARE(ready.count(), 2);
    snapshot = last(ready);
    QVERIFY(snapshot.revision > revision);
    QCOMPARE(snapshot.sourceIds, QStringList({"bottom", "top"}));
    QCOMPARE(snapshot.layers[0].image.pixelColor(1, 1), QColor(Qt::blue));
    QCOMPARE(errors.count(), 0);
}

void SceneSnapshotBuilderTest::visibilitySelectionAndRemoval()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("image.png");
    QVERIFY(writeImage(path, Qt::red));
    StudioState state({{"a", "A", {source("one", path), source("two", path)}}});
    SceneSnapshotBuilder builder(&state);
    QSignalSpy ready(&builder, &SceneSnapshotBuilder::snapshotReady);
    QTRY_COMPARE(ready.count(), 1);
    QVERIFY(state.selectSource("two"));
    QVERIFY(state.setSourceLocked("a", "two", true));
    QTest::qWait(30);
    QCOMPARE(ready.count(), 1);
    QVERIFY(!builder.isLoading());
    QVERIFY(state.setSourceVisible("a", "one", false));
    QTRY_COMPARE(ready.count(), 2);
    QCOMPARE(last(ready).sourceIds, QStringList({"two"}));
    QVERIFY(state.setSourceVisible("a", "two", false));
    QTRY_COMPARE(ready.count(), 3);
    QVERIFY(last(ready).layers.isEmpty());
    QVERIFY(state.setSourceVisible("a", "one", true));
    QTRY_COMPARE(ready.count(), 4);
    QCOMPARE(last(ready).sourceIds, QStringList({"one"}));
    QVERIFY(state.removeSource("a", "one"));
    QTRY_COMPARE(ready.count(), 5);
    QVERIFY(last(ready).layers.isEmpty());
    const QString added = state.addImageSource("a", "New image", path);
    QVERIFY(!added.isEmpty());
    QTRY_COMPARE(ready.count(), 6);
    QCOMPARE(last(ready).sourceIds, QStringList({added}));
}

void SceneSnapshotBuilderTest::sceneIsolationAndEmpty()
{
    QTemporaryDir dir;
    const QString red = dir.filePath("red.png"), blue = dir.filePath("blue.png");
    QVERIFY(writeImage(red, Qt::red));
    QVERIFY(writeImage(blue, Qt::blue));
    StudioState state({{"a", "A", {source("x", red)}}, {"b", "B", {source("y", red)}}, {"empty", "Empty", {}}});
    SceneSnapshotBuilder builder(&state);
    QSignalSpy ready(&builder, &SceneSnapshotBuilder::snapshotReady);
    QTRY_COMPARE(ready.count(), 1);
    QVERIFY(state.selectScene("b"));
    QVERIFY(state.setSourceVisible("b", "y", false));
    QTRY_VERIFY(last(ready).sceneId == "b");
    QVERIFY(last(ready).layers.isEmpty());
    QVERIFY(state.selectScene("a"));
    QTRY_VERIFY(last(ready).sceneId == "a");
    QCOMPARE(last(ready).sourceIds, QStringList({"x"}));
    const auto beforeInactive = ready.count();
    const QString added = state.addImageSource("b", "Blue", blue);
    QVERIFY(!added.isEmpty());
    QTest::qWait(30);
    QCOMPARE(ready.count(), beforeInactive);
    QVERIFY(state.selectScene("b"));
    QTRY_VERIFY(last(ready).sceneId == "b");
    QCOMPARE(last(ready).sourceIds, QStringList({added}));
    QCOMPARE(last(ready).layers.first().image.pixelColor(1, 1), QColor(Qt::blue));
    QVERIFY(state.removeSource("a", "x"));
    QVERIFY(state.selectScene("a"));
    QTRY_VERIFY(last(ready).sceneId == "a");
    QVERIFY(last(ready).layers.isEmpty());
    QVERIFY(state.selectScene("empty"));
    QTRY_VERIFY(last(ready).sceneId == "empty");
    QVERIFY(last(ready).layers.isEmpty());
    const auto count = ready.count();
    QVERIFY(state.selectScene("empty"));
    QTest::qWait(30);
    QCOMPARE(ready.count(), count);
}

void SceneSnapshotBuilderTest::decodeErrorsAndRecovery()
{
    QTemporaryDir dir;
    const QString good = dir.filePath("good.png"), missing = dir.filePath("missing.png"), bad = dir.filePath("bad.png");
    QVERIFY(writeImage(good, Qt::red));
    { QFile file(bad); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("not an image"); }
    auto unsupported = source("unsupported", good);
    unsupported.type = "camera";
    auto hidden = source("hidden", dir.filePath("never-read.png"));
    hidden.visible = false;
    StudioState state({{"a", "A", {source("good", good), source("missing", missing), source("bad", bad), unsupported, hidden}}});
    SceneSnapshotBuilder builder(&state);
    QSignalSpy ready(&builder, &SceneSnapshotBuilder::snapshotReady);
    QSignalSpy errors(&builder, &SceneSnapshotBuilder::sourceError);
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(last(ready).sourceIds, QStringList({"good"}));
    QCOMPARE(errors.count(), 3);
    QSet<QString> failed;
    for (const auto &row : errors) {
        const Error error = qvariant_cast<Error>(row.first());
        QCOMPARE(error.sceneId, "a");
        QCOMPARE(error.name, "Source " + error.sourceId);
        QVERIFY(!error.message.isEmpty());
        QVERIFY(!error.filePath.isEmpty());
        failed.insert(error.sourceId);
    }
    QCOMPARE(failed, QSet<QString>({"missing", "bad", "unsupported"}));
    QCOMPARE(state.sourceModel()->rowCount(), 5);
    QVERIFY(writeImage(missing, Qt::blue));
    QVERIFY(writeImage(bad, Qt::green));
    QVERIFY(state.setSourceVisible("a", "unsupported", false));
    QTRY_COMPARE(ready.count(), 2);
    QCOMPARE(last(ready).sourceIds, QStringList({"good", "missing", "bad"}));
    QCOMPARE(errors.count(), 3);
    QVERIFY(QFile::remove(good));
    builder.requestSnapshot();
    QTRY_COMPARE(ready.count(), 3);
    QCOMPARE(last(ready).sourceIds, QStringList({"missing", "bad"}));
    QCOMPARE(errors.count(), 4);
    QVERIFY(writeImage(good, Qt::red));
    builder.requestSnapshot();
    QTRY_COMPARE(ready.count(), 4);
    QCOMPARE(last(ready).sourceIds, QStringList({"good", "missing", "bad"}));
    QVERIFY(!builder.isLoading());
}

void SceneSnapshotBuilderTest::latestRequestWins()
{
    QTemporaryDir dir;
    const QString red = dir.filePath("red.png"), blue = dir.filePath("blue.png");
    QVERIFY(writeImage(red, Qt::red));
    QVERIFY(writeImage(blue, Qt::blue));
    StudioState state({{"a", "A", {source("old-first", red), source("old-second", red)}},
                       {"b", "B", {source("middle", red)}}, {"c", "C", {source("removed", red)}}});
    auto gate = std::make_shared<Gate>();
    gate->obsoleteError = true;
    SceneSnapshotBuilder builder(&state, nullptr, [gate](const QString &path) { return gate->read(path); });
    ResumeOnExit release{gate};
    QSignalSpy ready(&builder, &SceneSnapshotBuilder::snapshotReady);
    QSignalSpy errors(&builder, &SceneSnapshotBuilder::sourceError);
    QTRY_VERIFY(gate->entered.available() > 0);
    QVERIFY(state.selectScene("b"));
    QVERIFY(state.selectScene("c"));
    QVERIFY(state.removeSource("c", "removed"));
    const QString latest = state.addImageSource("c", "Latest", blue);
    QVERIFY(!latest.isEmpty());
    for (int i = 0; i < 100; ++i) QVERIFY(state.setSourceVisible("c", latest, i % 2 != 0));
    QCoreApplication::processEvents(); // Capture just the latest pending request.
    QCOMPARE(ready.count(), 0);
    gate->resume.release();
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(last(ready).sceneId, "c");
    QCOMPARE(last(ready).sourceIds, QStringList({latest}));
    QCOMPARE(last(ready).layers.first().image.pixelColor(1, 1), QColor(Qt::blue));
    QCOMPARE(gate->calls.load(), 2); // Canceled batch never decodes old-second.
    QCOMPARE(gate->maximum.load(), 1);
    QVERIFY(!gate->guiSeen.load());
    QCOMPARE(errors.count(), 0); // No error from the obsolete decode either.
    QVERIFY(!builder.isLoading());
}

void SceneSnapshotBuilderTest::stateDestructionAndShutdown()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("image.png");
    QVERIFY(writeImage(path, Qt::red));
    auto state = std::make_unique<StudioState>(QList<StudioState::SceneData>{{"a", "A", {source("x", path)}}});
    auto gate = std::make_shared<Gate>();
    auto builder = std::make_unique<SceneSnapshotBuilder>(state.get(), nullptr,
        [gate](const QString &file) { return gate->read(file); });
    ResumeOnExit release{gate};
    QSignalSpy ready(builder.get(), &SceneSnapshotBuilder::snapshotReady);
    QTRY_VERIFY(gate->entered.available() > 0);
    state.reset();
    QCoreApplication::processEvents();
    gate->resume.release();
    QTRY_COMPARE(ready.count(), 1);
    QVERIFY(last(ready).sceneId.isEmpty());
    QVERIFY(last(ready).layers.isEmpty());
    builder.reset();

    StudioState live({{"a", "A", {source("x", path)}}});
    auto blocked = std::make_shared<Gate>();
    auto dying = std::make_unique<SceneSnapshotBuilder>(&live, nullptr,
        [blocked](const QString &file) { return blocked->read(file); });
    ResumeOnExit unblockOnFailure{blocked};
    QSignalSpy obsolete(dying.get(), &SceneSnapshotBuilder::snapshotReady);
    QTRY_VERIFY(blocked->entered.available() > 0);
    auto *resumer = QThread::create([blocked] { blocked->resume.release(); });
    resumer->start();
    dying.reset(); // Must join while worker may still post its canceled completion.
    resumer->wait();
    delete resumer;
    QVERIFY(blocked->finished.load());
    QCoreApplication::processEvents();
    QCOMPARE(obsolete.count(), 0);
}

void SceneSnapshotBuilderTest::signalReentrancy()
{
    StudioState state({{"bad", "Bad", {source("missing", "missing-image-for-test.png")}}, {"empty", "Empty", {}}});
    SceneSnapshotBuilder builder(&state);
    QSignalSpy ready(&builder, &SceneSnapshotBuilder::snapshotReady);
    connect(&builder, &SceneSnapshotBuilder::sourceError, &state, [&] { state.selectScene("empty"); });
    bool again = true;
    connect(&builder, &SceneSnapshotBuilder::snapshotReady, &state, [&] {
        if (again) { again = false; builder.requestSnapshot(); }
    });
    QTRY_COMPARE(ready.count(), 2);
    for (const auto &row : ready) QCOMPARE(qvariant_cast<Snapshot>(row.first()).sceneId, "empty");
    QVERIFY(!builder.isLoading());
    disconnect(&builder, nullptr, &state, nullptr);
    bool refreshFromLoading = true;
    connect(&builder, &SceneSnapshotBuilder::loadingChanged, &state, [&] {
        if (builder.isLoading() && refreshFromLoading) {
            refreshFromLoading = false;
            builder.requestSnapshot();
            QCoreApplication::processEvents();
        }
    });
    builder.requestSnapshot();
    QTRY_COMPARE(ready.count(), 3);
    QTest::qWait(20);
    QCOMPARE(ready.count(), 3); // Reentrant loading handler must not duplicate capture.
    auto dying = std::make_unique<SceneSnapshotBuilder>(&state);
    connect(dying.get(), &SceneSnapshotBuilder::snapshotReady, &state, [&] { dying.reset(); });
    QTRY_VERIFY(!dying);
    QVERIFY(state.selectScene("bad"));
    auto errorDying = std::make_unique<SceneSnapshotBuilder>(&state);
    connect(errorDying.get(), &SceneSnapshotBuilder::sourceError, &state, [&] { errorDying.reset(); });
    QTRY_VERIFY(!errorDying);
    QCoreApplication::processEvents();
}

void SceneSnapshotBuilderTest::sceneToComposition()
{
    QTemporaryDir dir;
    const QString red = dir.filePath("red.png"), overlay = dir.filePath("overlay.png"), green = dir.filePath("green.png");
    QVERIFY(writeImage(red, Qt::red));
    QVERIFY(writeImage(overlay, QColor(0, 0, 255, 128)));
    QVERIFY(writeImage(green, Qt::green));
    Graphics graphics;
    QVERIFY2(graphics.initialize(), qPrintable(graphics.error));
    QTRY_VERIFY(graphics.session.isRunning());
    QVERIFY(graphics.consumer.makeCurrent(&graphics.surface));
    auto *gl = graphics.consumer.functions();
    qInfo() << "GL_RENDERER:" << reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER));
    qInfo() << "GL_VERSION:" << reinterpret_cast<const char *>(gl->glGetString(GL_VERSION));
    graphics.consumer.doneCurrent();
    StudioState state({{"a", "A", {source("overlay", overlay), source("base", red)}},
                       {"b", "B", {source("green", green)}}, {"empty", "Empty", {}}});
    SceneSnapshotBuilder builder(&state);
    QString submitError;
    connect(&builder, &SceneSnapshotBuilder::snapshotReady, &graphics.session, [&](const Snapshot &snapshot) {
        graphics.session.submit(snapshot.layers, snapshot.canvas, &submitError);
    });
    QSignalSpy errors(&builder, &SceneSnapshotBuilder::sourceError);
    QImage image = graphics.awaitColor(QColor(127, 0, 128));
    QVERIFY2(!image.isNull(), qPrintable(graphics.error));
    QCOMPARE(image.size(), QSize(1920, 1080));
    QCOMPARE(image.pixelColor(960, 10), QColor(Qt::black));
    QVERIFY(state.setSourceVisible("a", "overlay", false));
    QVERIFY(!graphics.awaitColor(Qt::red).isNull());
    QVERIFY(state.setSourceVisible("a", "overlay", true));
    QVERIFY(state.moveSource("a", "base", -1));
    QVERIFY(!graphics.awaitColor(Qt::red).isNull());
    QVERIFY(state.selectScene("b"));
    QVERIFY(!graphics.awaitColor(Qt::green).isNull());
    QVERIFY(state.selectScene("empty"));
    QVERIFY(!graphics.awaitColor(Qt::black).isNull());
    QVERIFY(submitError.isEmpty());
    QCOMPARE(errors.count(), 0);
}

QTEST_MAIN(SceneSnapshotBuilderTest)
#include "tst_scenesnapshotbuilder.moc"
