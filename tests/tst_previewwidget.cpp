#include "previewwidget.h"
#include "compositionsession.h"

#include <QApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtTest>

namespace {
QImage solid(const QColor &color)
{
    QImage image(QSize(64, 32), QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image;
}

QColor sample(const PreviewWidget *view, const QImage &image, qreal x, qreal y)
{
    if (image.isNull()) return {};
    const QRectF rect = view->canvasRect();
    const int px = qBound(0, qFloor((rect.x() + rect.width() * x) * image.width() / view->width()), image.width() - 1);
    const int py = qBound(0, qFloor((rect.y() + rect.height() * y) * image.height() / view->height()), image.height() - 1);
    return image.pixelColor(px, py);
}

bool near(const QColor &actual, const QColor &expected)
{
    return qAbs(actual.red() - expected.red()) <= 2 && qAbs(actual.green() - expected.green()) <= 2
        && qAbs(actual.blue() - expected.blue()) <= 2 && actual.alpha() == expected.alpha();
}

QString artifact(const QString &name, const PreviewWidget *view)
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(
        QString("preview-%1-%2.png").arg(name).arg(qRound(view->devicePixelRatioF() * 100)));
}

// Minimal application coordinator for the component test: starts the external
// worker from the consumer context, and stops BEFORE that context is destroyed.
struct Fixture {
    CompositionSession session;
    QWidget window;
    QPointer<PreviewWidget> view;
    QStringList errors;
    int readyCount = 0, releaseCount = 0;
    bool stopped = false;

    Fixture()
    {
        auto *layout = new QHBoxLayout(&window);
        layout->setContentsMargins(0, 0, 0, 0);
        view = new PreviewWidget(&window);
        layout->addWidget(view);
        view->setSession(&session);
        QObject::connect(view, &PreviewWidget::contextReady, &session, [this](QOpenGLContext *context) {
            ++readyCount;
            QString error;
            if (!session.start(context, &error)) errors.append(error);
        });
        QObject::connect(view, &PreviewWidget::contextAboutToBeReleased, &session, [this] {
            ++releaseCount;
            QString error;
            stopped = session.stop(&error);
            if (!stopped) errors.append(error);
        }, Qt::DirectConnection);
        QObject::connect(view, &PreviewWidget::graphicsError, &session,
                         [this](const QString &error) { errors.append(error); });
        window.resize(400, 300);
    }
    ~Fixture()
    {
        // Explicit ordering also keeps the signal coordinator alive during cleanup.
        delete view.data();
        session.stop();
    }
};
}

class PreviewWidgetTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void sharedPixelsAndCanvasFit();
    void surroundingColorChangesKeepCanvasAndFrame();
    void canvasColorBlendsTransparentPreview();
    void repaintReplacementAndDetach();
    void reparentRecreatesContext();
    void repeatedCreationAndDestruction();
    void quickWidgetCoexists();
};

void PreviewWidgetTest::initTestCase()
{
    Fixture fixture;
    fixture.window.show();
    QTRY_VERIFY(fixture.view->isReady());
    QTRY_VERIFY(fixture.session.isRunning());
    fixture.view->makeCurrent();
    auto *gl = fixture.view->context()->functions();
    qInfo() << "GL_VENDOR:" << reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR));
    qInfo() << "GL_RENDERER:" << reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER));
    qInfo() << "GL_VERSION:" << reinterpret_cast<const char *>(gl->glGetString(GL_VERSION));
    qInfo() << "Device pixel ratio:" << fixture.view->devicePixelRatioF();
    fixture.view->doneCurrent();
    QCOMPARE(QQuickWindow::graphicsApi(), QSGRendererInterface::OpenGL);
    QVERIFY(fixture.errors.isEmpty());
}

void PreviewWidgetTest::sharedPixelsAndCanvasFit()
{
    Fixture fixture;
    fixture.view->setSurroundingColor(QColor("#181A1D"));
    fixture.window.show();
    QTRY_VERIFY(fixture.session.isRunning());
    const QImage empty = fixture.view->grabFramebuffer();
    QVERIFY(!empty.isNull());
    QCOMPARE(sample(fixture.view, empty, .5, .5), QColor(Qt::black));
    QCOMPARE(empty.pixelColor(1, 1), QColor("#181A1D"));
    QVERIFY(qAbs(fixture.view->canvasRect().width() / fixture.view->canvasRect().height() - 16. / 9) < .001);

    QImage base = solid(Qt::red), overlay = solid(Qt::transparent);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 64; ++x) {
            if (y >= 16) base.setPixelColor(x, y, Qt::green);
            if (y < 16 && x >= 24 && x < 40) overlay.setPixelColor(x, y, QColor(0, 0, 255, 128));
        }
    }
    QVERIFY(fixture.session.submit({{solid(Qt::blue), false}, {overlay}, {base}}, QSize(64, 32)));
    QTRY_VERIFY(fixture.view->displayedFrameToken() != 0);
    const QImage image = fixture.view->grabFramebuffer();
    QCOMPARE(image.size(), fixture.view->size() * fixture.view->devicePixelRatioF());
    QCOMPARE(sample(fixture.view, image, .2, .25), QColor(Qt::red));
    QVERIFY(near(sample(fixture.view, image, .5, .25), QColor(127, 0, 128)));
    QCOMPARE(sample(fixture.view, image, .5, .75), QColor(Qt::green));
    QVERIFY(qAbs(fixture.view->canvasRect().width() / fixture.view->canvasRect().height() - 2) < .001);
    QVERIFY(image.save(artifact("pixels", fixture.view)));

    const auto token = fixture.view->displayedFrameToken();
    fixture.window.resize(240, 420);
    QTest::qWait(30);
    const QImage resized = fixture.view->grabFramebuffer();
    QCOMPARE(resized.size(), fixture.view->size() * fixture.view->devicePixelRatioF());
    QCOMPARE(fixture.view->displayedFrameToken(), token);
    QCOMPARE(sample(fixture.view, resized, .2, .25), QColor(Qt::red));
    QCOMPARE(sample(fixture.view, resized, .5, .75), QColor(Qt::green));
    const QRectF rect = fixture.view->canvasRect();
    QVERIFY(rect.left() >= 0 && rect.top() >= 0);
    QVERIFY(rect.right() <= fixture.view->width() && rect.bottom() <= fixture.view->height());
    QVERIFY(qAbs(rect.width() / rect.height() - 2) < .001);
    QVERIFY(resized.save(artifact("portrait", fixture.view)));
    QVERIFY(fixture.errors.isEmpty());
}

void PreviewWidgetTest::surroundingColorChangesKeepCanvasAndFrame()
{
    Fixture fixture;
    fixture.window.show();
    QTRY_VERIFY(fixture.session.isRunning());
    QPointer<QOpenGLContext> context = fixture.view->context();
    QSignalSpy painted(fixture.view, &QOpenGLWidget::frameSwapped);
    for (const auto &size : {QSize(640, 240), QSize(240, 420)}) {
        fixture.window.resize(size);
        QTRY_COMPARE(fixture.view->size(), size);
        for (const auto &color : {QColor("#181A1D"), QColor("#D4D7DC"), QColor("#3579A1")}) {
            painted.clear();
            fixture.view->setSurroundingColor(color);
            QCOMPARE(fixture.view->surroundingColor(), color);
            QTRY_VERIFY(!painted.isEmpty()); // A color change schedules a real repaint.
            const auto frame = fixture.view->grabFramebuffer();
            QVERIFY(!frame.isNull());
            QCOMPARE(frame.pixelColor(1, 1), color);
            QCOMPARE(sample(fixture.view, frame, .5, .5), QColor(Qt::black));
            QCOMPARE(fixture.view->context(), context.data());
            QVERIFY(frame.save(artifact(QString("surrounding-%1-%2").arg(color.name().mid(1)).arg(size.width()), fixture.view)));
        }
    }
    QVERIFY(fixture.session.submit({{solid(Qt::red)}}, QSize(64, 32)));
    QTRY_VERIFY(fixture.view->displayedFrameToken() != 0);
    const auto token = fixture.view->displayedFrameToken();
    for (const auto &color : {QColor("#181A1D"), QColor("#D4D7DC")}) {
        painted.clear();
        fixture.view->setSurroundingColor(color);
        QTRY_VERIFY(!painted.isEmpty());
        const auto frame = fixture.view->grabFramebuffer();
        QCOMPARE(frame.pixelColor(1, 1), color);
        QCOMPARE(sample(fixture.view, frame, .5, .5), QColor(Qt::red));
        QCOMPARE(fixture.view->displayedFrameToken(), token);
        QCOMPARE(fixture.view->context(), context.data());
    }
    QVERIFY(fixture.errors.isEmpty());
}

void PreviewWidgetTest::canvasColorBlendsTransparentPreview()
{
    Fixture fixture;
    const QColor light("#F8FAFC"), surrounding("#D4D7DC");
    fixture.view->setCanvasColor(light);
    fixture.view->setSurroundingColor(surrounding);
    fixture.window.show();
    QTRY_VERIFY(fixture.session.isRunning());
    QCOMPARE(sample(fixture.view, fixture.view->grabFramebuffer(), .5, .5), light);
    QImage image = solid(Qt::transparent);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 16; x < 32; ++x) image.setPixelColor(x, y, QColor(0, 0, 255, 128));
        for (int x = 32; x < 48; ++x) image.setPixelColor(x, y, Qt::black);
        for (int x = 48; x < 64; ++x) image.setPixelColor(x, y, Qt::green);
    }
    QVERIFY(fixture.session.submit({{image}}, QSize(64, 32)));
    QTRY_VERIFY(fixture.view->displayedFrameToken() != 0);
    const auto token = fixture.view->displayedFrameToken();
    QPointer<QOpenGLContext> context = fixture.view->context();
    QSignalSpy painted(fixture.view, &QOpenGLWidget::frameSwapped);
    for (const auto &size : {QSize(640, 240), QSize(240, 420)}) {
        fixture.window.resize(size);
        QTRY_COMPARE(fixture.view->size(), size);
        for (const auto &color : {QColor(Qt::black), light}) {
            painted.clear();
            fixture.view->setCanvasColor(color);
            QTRY_VERIFY(!painted.isEmpty());
            const auto frame = fixture.view->grabFramebuffer();
            QVERIFY(!frame.isNull());
            QCOMPARE(frame.pixelColor(1, 1), surrounding);
            QCOMPARE(sample(fixture.view, frame, .125, .5), color);
            const QColor blended = color == light ? QColor(124, 125, 254) : QColor(0, 0, 128);
            QVERIFY(near(sample(fixture.view, frame, .375, .5), blended));
            QCOMPARE(sample(fixture.view, frame, .625, .5), QColor(Qt::black));
            QCOMPARE(sample(fixture.view, frame, .875, .5), QColor(Qt::green));
            QCOMPARE(fixture.view->displayedFrameToken(), token);
            QCOMPARE(fixture.view->context(), context.data());
            QVERIFY(frame.save(artifact(QString("canvas-%1-%2").arg(color.name().mid(1)).arg(size.width()), fixture.view)));
        }
    }
    QVERIFY(fixture.session.submit({}, QSize(64, 32)));
    QTRY_VERIFY(fixture.view->displayedFrameToken() != token);
    QCOMPARE(sample(fixture.view, fixture.view->grabFramebuffer(), .5, .5), light);
    QVERIFY(fixture.view->setSession(nullptr));
    QCOMPARE(sample(fixture.view, fixture.view->grabFramebuffer(), .5, .5), light);
    QCOMPARE(fixture.view->displayedFrameToken(), quint64(0));
    QVERIFY(fixture.errors.isEmpty());
}

void PreviewWidgetTest::repaintReplacementAndDetach()
{
    Fixture fixture;
    fixture.window.show();
    QTRY_VERIFY(fixture.session.isRunning());
    QVERIFY(fixture.session.submit({{solid(Qt::red)}}, QSize(64, 32)));
    QTRY_VERIFY(fixture.view->displayedFrameToken() != 0);
    const auto first = fixture.view->displayedFrameToken();
    for (int i = 0; i < 3; ++i) {
        fixture.view->update();
        QTest::qWait(10);
        const QImage repainted = fixture.view->grabFramebuffer();
        QCOMPARE(fixture.view->displayedFrameToken(), first);
        QCOMPARE(sample(fixture.view, repainted, .5, .5), QColor(Qt::red));
    }
    QString error;
    QVERIFY(!fixture.session.stop(&error)); // View must retain the texture for repaint.
    QVERIFY(!error.isEmpty());
    fixture.window.hide();
    QSignalSpy available(&fixture.session, &CompositionSession::frameAvailable);
    QVERIFY(fixture.session.submit({{solid(Qt::green)}}, QSize(64, 32)));
    QTRY_VERIFY(available.count() > 0); // Composition continues without visible paintGL.
    fixture.window.show();
    QTRY_VERIFY(fixture.view->displayedFrameToken() != first);
    const QImage replaced = fixture.view->grabFramebuffer();
    QCOMPARE(sample(fixture.view, replaced, .5, .5), QColor(Qt::green));
    QVERIFY(fixture.view->setSession(nullptr, &error));
    QVERIFY(error.isEmpty());
    QCOMPARE(fixture.view->displayedFrameToken(), quint64(0));
    QVERIFY(fixture.session.stop(&error));
    QCOMPARE(sample(fixture.view, fixture.view->grabFramebuffer(), .5, .5), QColor(Qt::black));
    QVERIFY(fixture.errors.isEmpty());
}

void PreviewWidgetTest::reparentRecreatesContext()
{
    Fixture fixture;
    fixture.view->setCanvasColor(QColor("#F8FAFC"));
    fixture.window.show();
    QTRY_VERIFY(fixture.session.isRunning());
    QVERIFY(fixture.session.submit({{solid(Qt::red)}}, QSize(64, 32)));
    QTRY_VERIFY(fixture.view->displayedFrameToken() != 0);
    const auto oldToken = fixture.view->displayedFrameToken();
    QWidget otherWindow;
    auto *layout = new QHBoxLayout(&otherWindow);
    fixture.view->setParent(&otherWindow);
    layout->addWidget(fixture.view);
    otherWindow.resize(360, 240);
    otherWindow.show();
    // Test main intentionally leaves AA_ShareOpenGLContexts OFF, so moving to a
    // new top-level window exercises Qt's context destruction/reinitialization.
    QTRY_VERIFY(fixture.readyCount >= 2);
    QVERIFY(fixture.releaseCount >= 1);
    QCOMPARE(fixture.view->canvasColor(), QColor("#F8FAFC"));
    QTRY_VERIFY(fixture.session.isRunning());
    QVERIFY(fixture.session.submit({{solid(Qt::blue)}}, QSize(64, 32)));
    QTRY_VERIFY(fixture.view->displayedFrameToken() != 0 && fixture.view->displayedFrameToken() != oldToken);
    const QImage image = fixture.view->grabFramebuffer();
    QCOMPARE(sample(fixture.view, image, .5, .5), QColor(Qt::blue));
    QVERIFY(fixture.errors.isEmpty());
}

void PreviewWidgetTest::repeatedCreationAndDestruction()
{
    for (int cycle = 0; cycle < 3; ++cycle) {
        Fixture fixture;
        fixture.window.show();
        QTRY_VERIFY(fixture.session.isRunning());
        QVERIFY(fixture.session.submit({{solid(Qt::green)}}, QSize(64, 32)));
        QTRY_VERIFY(fixture.view->displayedFrameToken() != 0);
        delete fixture.view.data();
        QVERIFY(fixture.view.isNull());
        QVERIFY(fixture.stopped);
        QCOMPARE(fixture.releaseCount, 1);
        QVERIFY(!fixture.session.isRunning());
        QVERIFY(fixture.errors.isEmpty());
    }
}

void PreviewWidgetTest::quickWidgetCoexists()
{
    // External engine outlives the widget and its components, including cleanup
    // after an assertion while asynchronous import loading is still in progress.
    QQmlEngine engine;
    Fixture fixture;
    auto *quick = new QQuickWidget(&engine, &fixture.window);
    quick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    quick->setMinimumSize(120, 120);
    auto *component = new QQmlComponent(quick->engine(), quick);
    const QUrl source("inline:PreviewCoexist.qml");
    component->setData("import QtQuick\nRectangle { color: '#ff00ff'; implicitWidth: 120; implicitHeight: 120;"
                       " MouseArea { anchors.fill: parent; onClicked: parent.color = '#00ffff' } }", source);
    QTRY_VERIFY(component->status() != QQmlComponent::Loading);
    QVERIFY2(component->isReady(), qPrintable(component->errorString()));
    auto *root = qobject_cast<QQuickItem *>(component->create());
    QVERIFY(root);
    quick->setContent(source, component, root);
    fixture.window.layout()->addWidget(quick);
    fixture.window.resize(520, 300);
    fixture.window.show();
    QTRY_VERIFY(fixture.session.isRunning());
    QTRY_COMPARE(quick->status(), QQuickWidget::Ready);
    QCOMPARE(quick->quickWindow()->rendererInterface()->graphicsApi(), QSGRendererInterface::OpenGL);
    QVERIFY(fixture.session.submit({{solid(Qt::green)}}, QSize(64, 32)));
    QTRY_VERIFY(fixture.view->displayedFrameToken() != 0);
    const QImage preview = fixture.view->grabFramebuffer();
    QCOMPARE(sample(fixture.view, preview, .5, .5), QColor(Qt::green));
    QImage panel = quick->grabFramebuffer();
    QVERIFY(!panel.isNull());
    QCOMPARE(panel.pixelColor(panel.width() / 2, panel.height() / 2), QColor(Qt::magenta));
    QVERIFY(fixture.window.grab().save(artifact("mixed", fixture.view)));
    QTest::mouseClick(quick, Qt::LeftButton, Qt::NoModifier, quick->rect().center());
    QTRY_COMPARE(root->property("color").value<QColor>(), QColor(Qt::cyan));
    panel = quick->grabFramebuffer();
    QCOMPARE(panel.pixelColor(panel.width() / 2, panel.height() / 2), QColor(Qt::cyan));
    QCOMPARE(sample(fixture.view, fixture.view->grabFramebuffer(), .5, .5), QColor(Qt::green));
    QVERIFY(fixture.errors.isEmpty());
}

int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    PreviewWidgetTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_previewwidget.moc"
