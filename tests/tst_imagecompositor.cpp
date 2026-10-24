#include "imagecompositor.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
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

QTEST_MAIN(ImageCompositorTest)
#include "tst_imagecompositor.moc"
