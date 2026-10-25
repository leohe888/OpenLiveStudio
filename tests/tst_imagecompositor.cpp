#include "imagecompositor.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QSemaphore>
#include <QThread>
#include <QtTest>

namespace {
QImage solid(const QSize &size, const QColor &color)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image;
}

bool near(const QColor &actual, const QColor &expected)
{
    return qAbs(actual.red() - expected.red()) <= 2
        && qAbs(actual.green() - expected.green()) <= 2
        && qAbs(actual.blue() - expected.blue()) <= 2
        && actual.alpha() == expected.alpha();
}

// Read the shared texture through a CONSUMER-LOCAL FBO. FBO names are not shared.
// Optional return occurs immediately after the GPU read command is submitted,
// before CPU readback, so its fence covers queued texture consumption.
QImage consume(const ImageCompositor::Frame &frame, ImageCompositor *returnTo = nullptr, bool preview = false)
{
    if (!frame.token) return {};
    auto *gl = QOpenGLContext::currentContext()->extraFunctions();
    QOpenGLFramebufferObject output(frame.size);
    GLuint input = 0;
    gl->glGenFramebuffers(1, &input);
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, input);
    gl->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, preview ? frame.previewTexture : frame.texture, 0);
    const bool complete = gl->glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete) {
        gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, output.handle());
        gl->glBlitFramebuffer(0, 0, frame.size.width(), frame.size.height(),
                              0, 0, frame.size.width(), frame.size.height(), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &input);
    if (returnTo && !returnTo->returnFrame(frame)) return {};
    return complete ? output.toImage() : QImage();
}

// Cleanup still returns all leases when a Qt assertion exits a test early.
struct SharedSession {
    QOpenGLContext consumer;
    ImageCompositor compositor;
    QOffscreenSurface *surface = nullptr;
    QList<ImageCompositor::Frame> leases;

    bool initialize(QOffscreenSurface *value, QString *error)
    {
        surface = value;
        consumer.setFormat(surface->format());
        return consumer.create() && compositor.initialize(surface, error, &consumer);
    }
    ImageCompositor::Frame acquire(QString *error = nullptr)
    {
        if (!consumer.makeCurrent(surface)) return {};
        const auto frame = compositor.acquireFrame(error);
        if (frame.token) leases.append(frame);
        return frame;
    }
    ~SharedSession()
    {
        if (surface && consumer.makeCurrent(surface)) {
            for (const auto &frame : leases) compositor.returnFrame(frame);
            consumer.doneCurrent();
        }
        compositor.release();
    }
};
}

class ImageCompositorTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void emptyCanvasAndDefaultSize();
    void aspectAndOrientation_data();
    void aspectAndOrientation();
    void transparencyOrderAndVisibility();
    void failuresAndRecovery();
    void resizeAndRepeatedRelease();
    void workerThread();
    void sharedPixelsAndLatestFrame();
    void transparentPreviewKeepsBlackOutput();
    void boundedBuffersAndResize();
    void invalidConsumersAndTokens();
    void sharedAcrossThreads();

private:
    QOffscreenSurface m_surface;
};

void ImageCompositorTest::initTestCase()
{
    QOpenGLContext probe;
    QVERIFY2(probe.create(), "Native OpenGL context creation failed; this is not a software UI test.");
    m_surface.setFormat(probe.format());
    m_surface.create();
    QVERIFY(m_surface.isValid());
    QVERIFY(probe.makeCurrent(&m_surface));
    auto *gl = probe.functions();
    qInfo() << "GL_VENDOR:" << reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR));
    qInfo() << "GL_RENDERER:" << reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER));
    qInfo() << "GL_VERSION:" << reinterpret_cast<const char *>(gl->glGetString(GL_VERSION));
    probe.doneCurrent();
}

void ImageCompositorTest::emptyCanvasAndDefaultSize()
{
    ImageCompositor compositor;
    QString error;
    QVERIFY2(compositor.initialize(&m_surface, &error), qPrintable(error));
    QVERIFY2(compositor.render({}, QSize(1920, 1080), &error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(compositor.texture() != 0);
    const QImage frame = compositor.readback(&error);
    QVERIFY2(!frame.isNull(), qPrintable(error));
    QCOMPARE(frame.size(), QSize(1920, 1080));
    QCOMPARE(frame.pixelColor(0, 0), QColor(Qt::black));
    QCOMPARE(frame.pixelColor(960, 540), QColor(Qt::black));
    QCOMPARE(frame.pixelColor(1919, 1079), QColor(Qt::black));
}

void ImageCompositorTest::aspectAndOrientation_data()
{
    QTest::addColumn<QSize>("imageSize");
    QTest::addColumn<QPoint>("outside");
    QTest::addColumn<QPoint>("top");
    QTest::addColumn<QPoint>("bottom");
    QTest::newRow("wide") << QSize(80, 40) << QPoint(50, 5) << QPoint(50, 30) << QPoint(50, 70);
    QTest::newRow("tall") << QSize(40, 80) << QPoint(5, 50) << QPoint(50, 5) << QPoint(50, 95);
}

void ImageCompositorTest::aspectAndOrientation()
{
    QFETCH(QSize, imageSize);
    QFETCH(QPoint, outside);
    QFETCH(QPoint, top);
    QFETCH(QPoint, bottom);
    QImage source = solid(imageSize, Qt::red);
    for (int y = imageSize.height() / 2; y < imageSize.height(); ++y)
        for (int x = 0; x < imageSize.width(); ++x)
            source.setPixelColor(x, y, Qt::blue);
    ImageCompositor compositor;
    QVERIFY(compositor.initialize(&m_surface));
    QVERIFY(compositor.render({{source}}, QSize(100, 100)));
    const QImage frame = compositor.readback();
    QCOMPARE(frame.pixelColor(outside), QColor(Qt::black));
    QCOMPARE(frame.pixelColor(top), QColor(Qt::red));
    QCOMPARE(frame.pixelColor(bottom), QColor(Qt::blue));
}

void ImageCompositorTest::transparencyOrderAndVisibility()
{
    ImageCompositor compositor;
    QVERIFY(compositor.initialize(&m_surface));
    const QImage red = solid(QSize(40, 40), Qt::red);
    QImage overlay = solid(QSize(40, 40), Qt::transparent);
    for (int y = 0; y < 40; ++y) {
        for (int x = 10; x < 30; ++x)
            overlay.setPixelColor(x, y, QColor(0, 0, 255, 128));
        for (int x = 30; x < 40; ++x)
            overlay.setPixelColor(x, y, Qt::green);
    }
    QList<ImageCompositor::Layer> layers{{overlay}, {red}};
    QVERIFY(compositor.render(layers, QSize(40, 40)));
    QImage frame = compositor.readback();
    QCOMPARE(frame.pixelColor(5, 20), QColor(Qt::red));
    QVERIFY(near(frame.pixelColor(20, 20), QColor(127, 0, 128)));
    QCOMPARE(frame.pixelColor(35, 20), QColor(Qt::green));
    layers.swapItemsAt(0, 1);
    QVERIFY(compositor.render(layers, QSize(40, 40)));
    QCOMPARE(compositor.readback().pixelColor(35, 20), QColor(Qt::red));
    layers[0].visible = false;
    QVERIFY(compositor.render(layers, QSize(40, 40)));
    frame = compositor.readback();
    QCOMPARE(frame.pixelColor(5, 20), QColor(Qt::black));
    QCOMPARE(frame.pixelColor(35, 20), QColor(Qt::green));
    layers[1].visible = false;
    QVERIFY(compositor.render(layers, QSize(40, 40)));
    QCOMPARE(compositor.readback().pixelColor(35, 20), QColor(Qt::black));
}

void ImageCompositorTest::failuresAndRecovery()
{
    ImageCompositor compositor;
    QString error;
    QVERIFY(!compositor.render({}, QSize(16, 16), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!compositor.initialize(nullptr, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(compositor.initialize(&m_surface, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(!compositor.initialize(&m_surface, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(compositor.render({}, QSize(16, 16), &error));
    QVERIFY(error.isEmpty());
    QVERIFY(!compositor.render({}, QSize(0, 16), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(compositor.texture(), 0u);
    QVERIFY(compositor.readback(&error).isNull());
    QVERIFY(!error.isEmpty());
    QVERIFY(!compositor.render({{QImage()}}, QSize(16, 16), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!compositor.render({}, QSize(1000000, 16), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(compositor.render({{QImage(), false}}, QSize(16, 16), &error));
    QVERIFY(error.isEmpty());
    QCOMPARE(compositor.readback().pixelColor(8, 8), QColor(Qt::black));
}

void ImageCompositorTest::resizeAndRepeatedRelease()
{
    ImageCompositor compositor;
    for (int cycle = 0; cycle < 5; ++cycle) {
        QVERIFY(compositor.initialize(&m_surface));
        for (const QSize size : {QSize(64, 32), QSize(31, 71), QSize(64, 32)}) {
            QVERIFY(compositor.render({{solid(size, Qt::green)}}, size));
            const QImage frame = compositor.readback();
            QCOMPARE(frame.size(), size);
            QCOMPARE(frame.pixelColor(size.width() - 1, size.height() - 1), QColor(Qt::green));
        }
        compositor.release();
        compositor.release();
        QCOMPARE(compositor.texture(), 0u);
    }
}

void ImageCompositorTest::workerThread()
{
    QString error;
    QImage frame;
    const QImage image = solid(QSize(32, 32), Qt::blue);
    // Surface stays on GUI thread; context/FBO are created and destroyed here.
    auto *worker = QThread::create([&] {
        ImageCompositor compositor;
        if (compositor.initialize(&m_surface, &error)
            && compositor.render({{image}}, QSize(32, 32), &error))
            frame = compositor.readback(&error);
    });
    worker->start();
    // Always join before inspecting results or destroying the surface.
    worker->wait();
    delete worker;
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!frame.isNull());
    QCOMPARE(frame.pixelColor(16, 16), QColor(Qt::blue));
}

void ImageCompositorTest::sharedPixelsAndLatestFrame()
{
    SharedSession session;
    QString error;
    QVERIFY2(session.initialize(&m_surface, &error), qPrintable(error));
    const auto red = solid(QSize(40, 20), Qt::red);
    QImage overlay = solid(QSize(40, 20), Qt::transparent);
    overlay.setPixelColor(20, 2, QColor(0, 0, 255, 128));
    overlay.setPixelColor(20, 17, Qt::green);
    // Supersede many unconsumed frames without growing an output queue.
    for (int i = 0; i < 20; ++i)
        QVERIFY(session.compositor.render({{solid(QSize(4, 4), Qt::blue)}}, QSize(8, 8), nullptr, 100 + i));
    // Revisions identify submissions; the latest submission wins, not the largest number.
    QVERIFY(session.compositor.render({{overlay}, {red}}, QSize(40, 40), nullptr, 77));
    QVERIFY(!session.compositor.render({}, QSize(), &error, 999));
    QVERIFY(!error.isEmpty()); // A rejected submission must not relabel the ready result.
    const auto frame = session.acquire(&error);
    QVERIFY2(frame.token, qPrintable(error));
    QCOMPARE(frame.size, QSize(40, 40));
    QCOMPARE(frame.revision, quint64(77));
    const QImage pixels = consume(frame, &session.compositor);
    QVERIFY(!pixels.isNull());
    QCOMPARE(pixels.pixelColor(20, 5), QColor(Qt::black));
    QCOMPARE(pixels.pixelColor(5, 15), QColor(Qt::red));
    QVERIFY(near(pixels.pixelColor(20, 12), QColor(127, 0, 128)));
    QCOMPARE(pixels.pixelColor(20, 27), QColor(Qt::green));
    const auto noFrame = session.compositor.acquireFrame(&error);
    QCOMPARE(noFrame.token, quint64(0));
    QCOMPARE(noFrame.revision, quint64(0));
    QVERIFY(error.isEmpty());
}

void ImageCompositorTest::transparentPreviewKeepsBlackOutput()
{
    SharedSession session;
    QVERIFY(session.initialize(&m_surface, nullptr));
    const QSize size(40, 40);
    QImage source = solid(QSize(40, 20), Qt::transparent);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 10; x < 20; ++x) source.setPixelColor(x, y, QColor(0, 0, 255, 128));
        for (int x = 20; x < 30; ++x) source.setPixelColor(x, y, Qt::black);
        for (int x = 30; x < 40; ++x) source.setPixelColor(x, y, Qt::green);
    }
    QVERIFY(session.compositor.render({{solid(size, Qt::red), false}, {source}}, size));
    const auto frame = session.acquire();
    QVERIFY(frame.token && frame.texture && frame.previewTexture);
    QVERIFY(frame.texture != frame.previewTexture);
    const auto preview = consume(frame, nullptr, true);
    const auto output = consume(frame, &session.compositor);
    QVERIFY(!preview.isNull() && !output.isNull());
    // Aspect-fit margins and transparent source pixels retain alpha only in preview.
    QCOMPARE(preview.pixelColor(5, 5), QColor(Qt::transparent));
    QCOMPARE(preview.pixelColor(5, 20), QColor(Qt::transparent));
    QCOMPARE(output.pixelColor(5, 5), QColor(Qt::black));
    QCOMPARE(output.pixelColor(5, 20), QColor(Qt::black));
    QVERIFY(near(preview.pixelColor(15, 20), QColor(0, 0, 255, 128)));
    QVERIFY(near(output.pixelColor(15, 20), QColor(0, 0, 128)));
    const auto premultiplied = preview.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QVERIFY(qAbs(qBlue(premultiplied.pixel(15, 20)) - 128) <= 2);
    QCOMPARE(preview.pixelColor(25, 20), QColor(Qt::black)); // Real black stays opaque.
    QCOMPARE(preview.pixelColor(35, 20), QColor(Qt::green));
    QCOMPARE(output.pixelColor(35, 20), QColor(Qt::green));

    QVERIFY(session.compositor.render({{solid(size, QColor(0, 0, 255, 128))},
                                       {solid(size, QColor(255, 0, 0, 128))}}, size));
    const auto layered = session.acquire();
    QVERIFY(layered.token);
    const auto layeredPreview = consume(layered, nullptr, true);
    QVERIFY(!layeredPreview.isNull());
    QVERIFY(near(layeredPreview.pixelColor(20, 20), QColor(85, 0, 170, 192)));
    QVERIFY(near(consume(layered, &session.compositor).pixelColor(20, 20), QColor(64, 0, 128)));
    // Cycle through reused slots; neither alpha nor pixels may leak from older frames.
    for (int i = 0; i < 12; ++i) {
        QString error;
        bool rendered = false;
        QTRY_VERIFY((rendered = session.compositor.render({}, size, &error)) || !error.isEmpty());
        QVERIFY2(rendered, qPrintable(error));
        const auto empty = session.acquire();
        QVERIFY(empty.token);
        QCOMPARE(consume(empty, nullptr, true).pixelColor(20, 20), QColor(Qt::transparent));
        QCOMPARE(consume(empty, &session.compositor).pixelColor(20, 20), QColor(Qt::black));
    }
}

void ImageCompositorTest::boundedBuffersAndResize()
{
    SharedSession session;
    QString error;
    QVERIFY2(session.initialize(&m_surface, &error), qPrintable(error));
    QList<ImageCompositor::Frame> held;
    const QList<QColor> colors{Qt::red, Qt::green, Qt::blue};
    QSet<unsigned int> textures;
    for (int i = 0; i < ImageCompositor::BufferCount; ++i) {
        const QSize size(32 + i * 8, 24 + i * 8);
        QVERIFY(session.compositor.render({{solid(size, colors[i])}}, size, nullptr, 1000 + i));
        const auto frame = session.acquire(&error);
        QVERIFY2(frame.token, qPrintable(error));
        QCOMPARE(frame.revision, quint64(1000 + i));
        held.append(frame);
        textures.insert(frame.texture);
        textures.insert(frame.previewTexture);
    }
    QCOMPARE(textures.size(), ImageCompositor::BufferCount * 2);
    for (int i = 0; i < 20; ++i) {
        error = "previous error";
        QVERIFY(!session.compositor.render({}, QSize(200, 100), &error, 9000 + i));
        QVERIFY(error.isEmpty()); // Backpressure is a skip, not an error.
    }
    QVERIFY(!session.compositor.release(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(session.consumer.makeCurrent(&m_surface));
    for (int i = 0; i < held.size(); ++i) {
        const QImage pixels = consume(held[i]);
        QCOMPARE(pixels.size(), held[i].size);
        QCOMPARE(pixels.pixelColor(5, 5), colors[i]);
        QCOMPARE(consume(held[i], nullptr, true).pixelColor(5, 5), colors[i]);
        QCOMPARE(held[i].revision, quint64(1000 + i));
    }
    QVERIFY(session.compositor.returnFrame(held[0], &error));
    // Completion polling is nonblocking; retry until the real fence signals.
    bool rendered = false;
    QTRY_VERIFY((rendered = session.compositor.render(
        {{solid(QSize(200, 100), Qt::yellow)}}, QSize(200, 100), &error, 2000)) || !error.isEmpty());
    QVERIFY2(rendered, qPrintable(error));
    const auto resized = session.acquire(&error);
    QVERIFY2(resized.token, qPrintable(error));
    QCOMPARE(resized.size, QSize(200, 100));
    QCOMPARE(resized.revision, quint64(2000));
    textures.insert(resized.texture);
    textures.insert(resized.previewTexture);
    QCOMPARE(consume(resized, nullptr, true).pixelColor(5, 5), QColor(Qt::yellow));
    QCOMPARE(consume(resized, &session.compositor).pixelColor(5, 5), QColor(Qt::yellow));
    // Resizing a returned slot must preserve the other two outstanding frames.
    for (int i = 1; i < held.size(); ++i) {
        QCOMPARE(consume(held[i], nullptr, true).pixelColor(5, 5), colors[i]);
        const QImage pixels = consume(held[i], &session.compositor);
        QCOMPARE(pixels.size(), held[i].size);
        QCOMPARE(pixels.pixelColor(5, 5), colors[i]);
    }
    QVERIFY(session.compositor.release(&error));
    QVERIFY(error.isEmpty());
    QVERIFY(session.compositor.release());
    QVERIFY(session.consumer.makeCurrent(&m_surface));
    for (const auto texture : textures)
        QVERIFY(!session.consumer.functions()->glIsTexture(texture));
    session.consumer.doneCurrent();
    // Restart may reuse GL names, but an old token must never become valid again.
    QVERIFY(session.compositor.initialize(&m_surface, &error, &session.consumer));
    QVERIFY(session.compositor.render({}, QSize(16, 16)));
    const auto restarted = session.acquire();
    QVERIFY(restarted.token);
    QCOMPARE(restarted.revision, quint64(0)); // Legacy submission after restart has no stale revision.
    QVERIFY(restarted.token != held[0].token);
    QVERIFY(!session.compositor.returnFrame(held[0], &error));
    QVERIFY(!error.isEmpty());
}

void ImageCompositorTest::invalidConsumersAndTokens()
{
    SharedSession session;
    QString error;
    QOpenGLContext invalid;
    QVERIFY(!session.compositor.initialize(&m_surface, &error, &invalid));
    QVERIFY(!error.isEmpty());
    QVERIFY(session.initialize(&m_surface, &error));
    QVERIFY(session.compositor.render({}, QSize(16, 16)));
    QVERIFY(!QOpenGLContext::currentContext());
    QCOMPARE(session.compositor.acquireFrame(&error).token, quint64(0));
    QVERIFY(!error.isEmpty());
    QOpenGLContext foreign;
    foreign.setFormat(m_surface.format());
    QVERIFY(foreign.create());
    QVERIFY(foreign.makeCurrent(&m_surface));
    QCOMPARE(session.compositor.acquireFrame(&error).token, quint64(0));
    QVERIFY(!error.isEmpty());
    foreign.doneCurrent();
    const auto frame = session.acquire(&error);
    QVERIFY2(frame.token, qPrintable(error));
    QCOMPARE(frame.revision, quint64(0));
    auto changed = frame;
    changed.texture = 0;
    QVERIFY(!session.compositor.returnFrame(changed, &error));
    QVERIFY(!error.isEmpty());
    changed = frame;
    changed.previewTexture = 0;
    QVERIFY(!session.compositor.returnFrame(changed, &error));
    QVERIFY(!error.isEmpty());
    changed = frame;
    changed.revision = 123;
    QVERIFY(!session.compositor.returnFrame(changed, &error));
    QVERIFY(!error.isEmpty());
    ImageCompositor another;
    QVERIFY(another.initialize(&m_surface, &error, &session.consumer));
    QVERIFY(another.render({}, QSize(16, 16)));
    QVERIFY(session.consumer.makeCurrent(&m_surface));
    QVERIFY(!another.returnFrame(frame, &error));
    QVERIFY(!error.isEmpty());
    QOpenGLContext sibling;
    sibling.setFormat(m_surface.format());
    sibling.setShareContext(&session.consumer);
    QVERIFY(sibling.create());
    QVERIFY(sibling.makeCurrent(&m_surface));
    QVERIFY(!session.compositor.returnFrame(frame, &error));
    QVERIFY(!error.isEmpty()); // Sharing alone does not own the lease.
    QVERIFY(session.consumer.makeCurrent(&m_surface));
    QVERIFY(session.compositor.returnFrame(frame, &error));
    QVERIFY(!session.compositor.returnFrame(frame, &error));
    QVERIFY(!error.isEmpty());
}

void ImageCompositorTest::sharedAcrossThreads()
{
    QOpenGLContext consumer;
    consumer.setFormat(m_surface.format());
    QVERIFY(consumer.create());
    ImageCompositor compositor;
    QSemaphore produced, resume;
    QString workerError;
    bool firstRendered = false, secondRendered = false, released = false;
    auto *worker = QThread::create([&] {
        firstRendered = compositor.initialize(&m_surface, &workerError, &consumer)
            && compositor.render({{solid(QSize(32, 32), Qt::red)}}, QSize(32, 32), &workerError, (quint64(1) << 40) + 7);
        produced.release();
        resume.acquire();
        if (firstRendered)
            secondRendered = compositor.render({{solid(QSize(48, 24), Qt::blue)}},
                                                QSize(48, 24), &workerError, (quint64(1) << 40) + 8);
        produced.release();
        resume.acquire();
        released = compositor.release(&workerError);
    });
    worker->start();
    // No assertions/early exits until both leases are returned and worker joined.
    produced.acquire();
    consumer.makeCurrent(&m_surface);
    const auto first = compositor.acquireFrame();
    consumer.doneCurrent();
    resume.release();
    produced.acquire();
    consumer.makeCurrent(&m_surface);
    const auto second = compositor.acquireFrame();
    const QImage oldPreview = consume(first, nullptr, true);
    const QImage newPreview = consume(second, nullptr, true);
    const QImage oldPixels = consume(first, &compositor);
    const QImage newPixels = consume(second, &compositor);
    consumer.doneCurrent();
    resume.release();
    worker->wait();
    delete worker;
    QVERIFY2(workerError.isEmpty(), qPrintable(workerError));
    QVERIFY(firstRendered && secondRendered && released);
    QVERIFY(first.texture != second.texture);
    QCOMPARE(first.revision, (quint64(1) << 40) + 7);
    QCOMPARE(second.revision, (quint64(1) << 40) + 8);
    QVERIFY(!oldPixels.isNull() && !newPixels.isNull());
    QCOMPARE(oldPixels.pixelColor(16, 16), QColor(Qt::red));
    QCOMPARE(newPixels.size(), QSize(48, 24));
    QCOMPARE(newPixels.pixelColor(24, 12), QColor(Qt::blue));
    QCOMPARE(oldPreview.pixelColor(16, 16), QColor(Qt::red));
    QCOMPARE(newPreview.size(), QSize(48, 24));
    QCOMPARE(newPreview.pixelColor(24, 12), QColor(Qt::blue));
}

QTEST_MAIN(ImageCompositorTest)
#include "tst_imagecompositor.moc"
