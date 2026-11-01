#include "ui/previewwidget.h"
#include "graphics/gpuframeresource.h"

#include <QApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSignalSpy>
#include <QSurfaceFormat>
#include <QTest>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>

class PreviewWidgetTest : public QObject
{
    Q_OBJECT

private slots:

    void hiddenAndMinimizedPreviewKeepsComposing()
    {
        PreviewWidget widget;
        widget.resize(480, 270);
        QImage image(32, 16, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        widget.setSources({{"image", image, true, {{960, 540}, {1920, 1080}, 0}}});
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QCOMPARE(sample(grabPreview(widget), .5, .5), QColor(Qt::red));
        for (int mode = 0; mode < 2; ++mode) {
            if (mode == 0)
                widget.showMinimized();
            else
                widget.hide();
            QCoreApplication::processEvents();
            const auto before = widget.compositor()->latestFrame()->sequence;
            QThread::msleep(180);
            QVERIFY(widget.compositor()->latestFrame()->sequence >= before + 3);
            image.fill(mode == 0 ? Qt::green : Qt::blue);
            PreviewSource source{"image", image, true, {{960, 540}, {1920, 1080}, 0}};
            SourceFilter filter{"brightness"};
            filter.brightness = -.2f;
            source.filters = {filter};
            widget.setSources({source});
            const auto canvas = readCanvas(widget);
            const QColor expected = mode == 0 ? QColor(0, 204, 0) : QColor(0, 0, 204);
            QCOMPARE(sample(canvas, .5, .5), expected);
            widget.showNormal();
            QVERIFY(QTest::qWaitForWindowExposed(&widget));
            QCOMPARE(sample(grabPreview(widget), .5, .5), expected);
        }
        widget.hide();
        widget.setSources({});
        QCOMPARE(sample(readCanvas(widget), .5, .5), QColor(Qt::black));
    }

    void sourceFiltersRunBeforeComposition()
    {
        PreviewWidget widget;
        widget.resize(480, 270);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QSignalSpy errors(&widget, &PreviewWidget::sourceFailed);
        QImage image(32, 16, QImage::Format_RGBA8888);
        image.fill(QColor(64, 128, 192));
        PreviewSource source{"image", image, true, {{960, 540}, {1920, 1080}, 0}};
        SourceFilter first{"first"};
        source.filters = {first};
        const auto render = [&] {
            widget.setSources({source});
            grabPreview(widget);
            return sample(readCanvas(widget), .5, .5);
        };
        QCOMPARE(render(), QColor(64, 128, 192));
        source.filters[0].brightness = .2f;
        QCOMPARE(render(), QColor(115, 179, 243));
        source.filters[0].brightness = 0;
        source.filters[0].contrast = 0;
        const auto gray = render();
        QVERIFY(qAbs(gray.red() - 128) <= 1 && gray.red() == gray.green() &&
                gray.red() == gray.blue());
        source.filters[0].contrast = 1;
        source.filters[0].saturation = 0;
        const auto desaturated = render();
        QVERIFY(qAbs(desaturated.red() - 119) <= 1);
        QCOMPARE(desaturated.red(), desaturated.green());
        QCOMPARE(desaturated.green(), desaturated.blue());
        source.filters[0].enabled = false;
        QCOMPARE(render(), QColor(64, 128, 192));

        source.image.fill(QColor(102, 102, 102));
        first.brightness = .2f;
        SourceFilter second{"second"};
        second.contrast = 2;
        source.filters = {first, second};
        const auto forward = render();
        source.filters = {second, first};
        const auto reverse = render();
        QVERIFY(qAbs(forward.red() - 179) <= 2);
        QVERIFY(qAbs(reverse.red() - 128) <= 2);
        QVERIFY(forward.red() > reverse.red() + 40);

        // Both intermediate passes must preserve image orientation and alpha.
        source.image.fill(QColor(128, 0, 0, 128));
        for (int y = source.image.height() / 2; y < source.image.height(); ++y)
            for (int x = 0; x < source.image.width(); ++x)
                source.image.setPixelColor(x, y, QColor(0, 0, 128, 128));
        first.brightness = .1f;
        second.contrast = .5f;
        source.filters = {first, second};
        QImage background(16, 16, QImage::Format_RGBA8888);
        background.fill(Qt::green);
        PreviewSource other{"background", background, true, source.transform};
        widget.setSources({source, other});
        grabPreview(widget);
        const auto frame = readCanvas(widget);
        const auto top = sample(frame, .5, .25);
        const auto bottom = sample(frame, .5, .75);
        QVERIFY(top.red() > top.blue() + 20);
        QVERIFY(bottom.blue() > bottom.red() + 20);
        QVERIFY(qAbs(top.green() - 165) <= 2);
        source.visible = false;
        widget.setSources({source, other});
        grabPreview(widget);
        QCOMPARE(sample(readCanvas(widget), .5, .5), QColor(Qt::green));
        source.visible = true;
        source.transform.rotation = 180;
        widget.resize(300, 300);
        widget.setSources({source, other});
        grabPreview(widget);
        QVERIFY(sample(readCanvas(widget), .5, .25).blue() >
                sample(readCanvas(widget), .5, .25).red());
        widget.setSources({other, source});
        grabPreview(widget);
        QCOMPARE(sample(readCanvas(widget), .5, .5), QColor(Qt::green));
        // Invalid input from a non-model caller falls back without affecting other sources.
        source.filters[0].brightness = 4;
        source.transform.rotation = 0;
        widget.setSources({source});
        grabPreview(widget);
        QVERIFY(errors.count() > 0);
        QVERIFY(sample(readCanvas(widget), .5, .25).red() > 60);
        widget.setSources({});
        grabPreview(widget);
        widget.cleanup();
    }

    void initTestCase()
    {
        PreviewWidget widget;
        widget.resize(240, 200);
        widget.show();
        // Windows launchers may pass SW_HIDE for the first native ShowWindow.
        // Show again so this GPU suite does not silently skip a usable desktop.
        widget.hide();
        widget.show();
        if (!QTest::qWaitForWindowExposed(&widget) || !widget.isValid())
            QSKIP("Desktop OpenGL context unavailable; GPU rendering was not verified.");
        widget.makeCurrent();
        qInfo() << "OpenGL renderer:"
                << reinterpret_cast<const char *>(
                       widget.context()->functions()->glGetString(GL_RENDERER));
        qInfo() << "Device pixel ratio:" << widget.devicePixelRatioF();
        QVERIFY(widget.context()->format().version() >= qMakePair(3, 3));
        QCOMPARE(widget.context()->format().profile(), QSurfaceFormat::CoreProfile);
        QVERIFY(!widget.context()->isOpenGLES());
        widget.doneCurrent();
        QVERIFY2(widget.lastError().isEmpty(), qPrintable(widget.lastError()));
    }

    void createsFixedCanvasAndKeepsItAcrossImageAndWidgetChanges()
    {
        PreviewWidget widget;
        QSignalSpy errors(&widget, &PreviewWidget::renderingFailed);
        widget.resize(240, 200);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QCOMPARE(readCanvas(widget).size(), QSize(1920, 1080));
        QImage image(32, 16, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        widget.setSources({{"image", image, true}});
        QCOMPARE(readCanvas(widget).size(), QSize(1920, 1080));
        widget.resize(400, 100);
        image.fill(Qt::blue);
        widget.setSources({{"image", image, true}});
        QCOMPARE(readCanvas(widget).size(), QSize(1920, 1080));
        widget.setSources({});
        QCOMPARE(readCanvas(widget).size(), QSize(1920, 1080));
        QCOMPARE(errors.count(), 0);
    }

    void cleanupStopsWorkerAndReleasesOutput()
    {
        PreviewWidget widget;
        widget.resize(100, 100);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QVERIFY(!readCanvas(widget).isNull());
        QVERIFY(widget.compositor()->isRunning());
        widget.cleanup();
        QVERIFY(!widget.compositor()->isRunning());
        QVERIFY(!widget.compositor()->latestFrame());
        QVERIFY(!widget.m_initialized);
    }

    void drawsImageWithCorrectOrientationAndLetterboxing()
    {
        QImage image(1920, 960, QImage::Format_RGBA8888);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                image.setPixelColor(x, y,
                                    y < image.height() / 2
                                        ? (x < image.width() / 2 ? Qt::red : Qt::green)
                                        : (x < image.width() / 2 ? Qt::blue : Qt::white));
            }
        }
        PreviewWidget widget;
        QSignalSpy errors(&widget, &PreviewWidget::renderingFailed);
        widget.setSources({{"image", image, true}});
        widget.resize(240, 200);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        const QImage frame = grabPreview(widget);
        QVERIFY(!frame.isNull());
        QCOMPARE(sample(frame, .25, .35), QColor(Qt::red));
        QCOMPARE(sample(frame, .75, .35), QColor(Qt::green));
        QCOMPARE(sample(frame, .25, .65), QColor(Qt::blue));
        QCOMPARE(sample(frame, .75, .65), QColor(Qt::white));
        QCOMPARE(sample(frame, .5, .05), QColor(Qt::black));
        QCOMPARE(sample(frame, .5, .95), QColor(Qt::black));
        const QImage canvas = readCanvas(widget);
        QCOMPARE(canvas.size(), QSize(1920, 1080));
        QCOMPARE(sample(canvas, .25, .25), QColor(Qt::red));
        QCOMPARE(sample(canvas, .75, .25), QColor(Qt::green));
        QCOMPARE(sample(canvas, .25, .75), QColor(Qt::blue));
        QCOMPARE(sample(canvas, .75, .75), QColor(Qt::white));
        QCOMPARE(sample(canvas, .5, .02), QColor(Qt::black));
        QCOMPARE(sample(canvas, .5, .98), QColor(Qt::black));

        widget.resize(400, 100);
        const QImage wideFrame = grabPreview(widget);
        QCOMPARE(sample(wideFrame, .05, .5), QColor(Qt::black));
        QCOMPARE(sample(wideFrame, .95, .5), QColor(Qt::black));
        QCOMPARE(sample(wideFrame, .4, .25), QColor(Qt::red));
        QCOMPARE(readCanvas(widget), canvas);
        QCOMPARE(errors.count(), 0);
        widget.makeCurrent();
        GLint framebuffer = 0;
        GLint viewport[4] = {};
        widget.paintGL();
        widget.context()->functions()->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        widget.context()->functions()->glGetIntegerv(GL_VIEWPORT, viewport);
        QCOMPARE(GLuint(framebuffer), widget.defaultFramebufferObject());
        QCOMPARE(viewport[0], 0);
        QCOMPARE(viewport[1], 0);
        QCOMPARE(viewport[2], qRound(widget.width() * widget.devicePixelRatioF()));
        QCOMPARE(viewport[3], qRound(widget.height() * widget.devicePixelRatioF()));
        QCOMPARE(widget.context()->functions()->glGetError(), GLenum(GL_NO_ERROR));
        widget.doneCurrent();
    }

    void replacesBlendsAndClearsImage()
    {
        PreviewWidget widget;
        widget.resize(100, 100);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QImage image(16, 16, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        widget.setSources({{"image", image, true}});
        QCOMPARE(sample(grabPreview(widget), .5, .5), QColor(Qt::red));
        QCOMPARE(sample(readCanvas(widget), .5, .5), QColor(Qt::red));
        image.fill(QColor(0, 255, 0, 128));
        widget.setSources({{"image", image, true}});
        const QColor blended = sample(grabPreview(widget), .5, .5);
        QCOMPARE(blended.red(), 0);
        QVERIFY(qAbs(blended.green() - 128) <= 1);
        QCOMPARE(blended.blue(), 0);
        QCOMPARE(blended.alpha(), 255);
        const QColor canvasBlended = sample(readCanvas(widget), .5, .5);
        QCOMPARE(canvasBlended, blended);
        widget.setSources({});
        QCOMPARE(sample(grabPreview(widget), .5, .5), QColor(Qt::black));
        const QImage clearedCanvas = readCanvas(widget);
        QCOMPARE(sample(clearedCanvas, .5, .5), QColor(Qt::black));
        QCOMPARE(sample(clearedCanvas, .05, .05), QColor(Qt::black));
        QVERIFY(widget.lastError().isEmpty());
    }

    void surroundingColorDoesNotChangeCanvasOrResources()
    {
        PreviewWidget widget;
        widget.resize(240, 240);
        QImage image(80, 40, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        widget.setSources({{"image", image, true}});
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        const QImage before = grabPreview(widget);
        QCOMPARE(sample(before, .5, .05), QColor(Qt::black));
        const QImage canvas = readCanvas(widget);
        QCOMPARE(canvas.pixelColor(920, 540), QColor(Qt::red));
        QCOMPARE(canvas.pixelColor(919, 540), QColor(Qt::black));
        QCOMPARE(canvas.pixelColor(999, 540), QColor(Qt::red));
        QCOMPARE(canvas.pixelColor(1000, 540), QColor(Qt::black));
        QCOMPARE(canvas.pixelColor(960, 519), QColor(Qt::black));
        QCOMPARE(canvas.pixelColor(960, 520), QColor(Qt::red));
        QCOMPARE(canvas.pixelColor(960, 559), QColor(Qt::red));
        QCOMPARE(canvas.pixelColor(960, 560), QColor(Qt::black));
        const auto *programResource = widget.m_program.get();
        widget.setSurroundingColor(QColor("#D4D7DC"));
        const QImage after = grabPreview(widget);
        QCOMPARE(sample(after, .5, .05), QColor("#D4D7DC"));
        QCOMPARE(sample(after, .5, .5), QColor(Qt::red));
        QCOMPARE(sample(readCanvas(widget), .5, .02), QColor(Qt::black));
        QCOMPARE(readCanvas(widget), canvas);
        QCOMPARE(widget.m_program.get(), programResource);
        widget.setSurroundingColor(Qt::black);
        QCOMPARE(sample(grabPreview(widget), .5, .05), QColor(Qt::black));
    }

    void fitsPortraitImageIntoCanvasAndCanvasIntoWidget()
    {
        PreviewWidget widget;
        widget.resize(240, 240);
        QImage image(540, 1080, QImage::Format_RGBA8888);
        image.fill(Qt::yellow);
        widget.setSources({{"image", image, true}});
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        const QImage frame = grabPreview(widget);
        const QImage canvas = readCanvas(widget);
        QCOMPARE(sample(canvas, .5, .05), QColor(Qt::yellow));
        QCOMPARE(sample(canvas, .5, .95), QColor(Qt::yellow));
        QCOMPARE(sample(canvas, .3, .5), QColor(Qt::black));
        QCOMPARE(sample(canvas, .7, .5), QColor(Qt::black));
        QCOMPARE(sample(frame, .5, .05), QColor(Qt::black));
        QCOMPARE(sample(frame, .5, .95), QColor(Qt::black));
        QCOMPARE(sample(frame, .5, .25), QColor(Qt::yellow));
        QCOMPARE(sample(frame, .3, .5), QColor(Qt::black));
    }

    void clearsTargetsAndRestoresStateWhenImageUploadIsRejected()
    {
        PreviewWidget widget;
        widget.resize(100, 100);
        QImage image(16, 16, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        widget.setSources({{"image", image, true}});
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QCOMPARE(sample(grabPreview(widget), .5, .5), QColor(Qt::red));
        widget.makeCurrent();
        auto *functions = widget.context()->functions();
        GLint maxSize = 0;
        functions->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
        QImage oversized(maxSize + 1, 1, QImage::Format_RGBA8888);
        QVERIFY(!oversized.isNull());
        oversized.fill(Qt::white);
        widget.setSources({{"image", oversized, true}});
        QSignalSpy errors(&widget, &PreviewWidget::sourceFailed);
        widget.compositor()->captureImage();
        QCoreApplication::processEvents();
        widget.makeCurrent();
        widget.paintGL();
        QCOMPARE(errors.count(), 1);
        QVERIFY(widget.lastError().isEmpty());
        GLint framebuffer = 0;
        GLint viewport[4] = {};
        functions->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        functions->glGetIntegerv(GL_VIEWPORT, viewport);
        QCOMPARE(GLuint(framebuffer), widget.defaultFramebufferObject());
        QCOMPARE(viewport[0], 0);
        QCOMPARE(viewport[1], 0);
        QCOMPARE(viewport[2], qRound(widget.width() * widget.devicePixelRatioF()));
        QCOMPARE(viewport[3], qRound(widget.height() * widget.devicePixelRatioF()));
        QCOMPARE(functions->glGetError(), GLenum(GL_NO_ERROR));
        widget.doneCurrent();
        QCOMPARE(sample(readCanvas(widget), .5, .5), QColor(Qt::black));
        QCOMPARE(sample(grabPreview(widget), .5, .5), QColor(Qt::black));
        widget.setSources({{"image", image, true}});
        QCOMPARE(sample(grabPreview(widget), .5, .5), QColor(Qt::red));
        QVERIFY(widget.lastError().isEmpty());
    }

    void restoresImageAfterContextRecreation()
    {
        QWidget firstWindow;
        QWidget secondWindow;
        firstWindow.resize(500, 350);
        secondWindow.resize(500, 350);
        auto *widget = new PreviewWidget(&firstWindow);
        widget->resize(400, 250);
        QImage image(16, 16, QImage::Format_RGBA8888);
        image.fill(Qt::blue);
        QImage overlay(200, 100, QImage::Format_RGBA8888);
        overlay.fill(Qt::yellow);
        QImage background(1920, 1080, QImage::Format_RGBA8888);
        background.fill(Qt::blue);
        const SourceTransform transform{{960, 540}, {200, 100}, 30};
        widget->setSources(
            {{"overlay", overlay, true, transform}, {"background", background, true}});
        widget->setCurrentSourceId("overlay");
        firstWindow.show();
        QVERIFY(QTest::qWaitForWindowExposed(&firstWindow));
        QCOMPARE(sample(grabPreview(*widget), .5, .5), QColor(Qt::yellow));
        QCOMPARE(sample(readCanvas(*widget), .5, .5), QColor(Qt::yellow));
        bool canvasWasReleased = false;
        connect(widget->context(), &QOpenGLContext::aboutToBeDestroyed, this, [&] {
            canvasWasReleased = !widget->compositor()->isRunning();
        });
        QSignalSpy destroyed(widget->context(), &QOpenGLContext::aboutToBeDestroyed);
        widget->setParent(&secondWindow);
        secondWindow.show();
        widget->show();
        QVERIFY(QTest::qWaitForWindowExposed(&secondWindow));
        QCOMPARE(destroyed.count(), 1);
        QVERIFY(canvasWasReleased);
        QCOMPARE(sample(grabPreview(*widget), .5, .5), QColor(Qt::yellow));
        QCOMPARE(sample(readCanvas(*widget), .5, .5), QColor(Qt::yellow));
        QVERIFY(widget->m_sources[0].transform == transform);
        QCOMPARE(widget->m_currentSourceId, QString("overlay"));
        QCOMPARE(sample(readCanvas(*widget), .1, .5), QColor(Qt::blue));
        widget->makeCurrent();
        auto *functions = widget->context()->functions();
        QCOMPARE(functions->glCheckFramebufferStatus(GL_FRAMEBUFFER),
                 GLenum(GL_FRAMEBUFFER_COMPLETE));
        functions->glBindFramebuffer(GL_FRAMEBUFFER, widget->defaultFramebufferObject());
        QCOMPARE(functions->glGetError(), GLenum(GL_NO_ERROR));
        widget->doneCurrent();
        QVERIFY(widget->lastError().isEmpty());
    }

    void compositesSourcesAndReusesTextures()
    {
        PreviewWidget widget;
        widget.resize(480, 400);
        QImage background(1920, 1080, QImage::Format_RGBA8888);
        background.fill(Qt::blue);
        QImage foreground(400, 200, QImage::Format_RGBA8888);
        foreground.fill(QColor(255, 0, 0, 128));
        QList<PreviewSource> sources{{"top", foreground, true}, {"bottom", background, true}};
        widget.setSources(sources);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        const auto blended = sample(grabPreview(widget), .5, .5);
        QVERIFY(qAbs(blended.red() - 128) <= 1);
        QVERIFY(qAbs(blended.blue() - 127) <= 1);
        QCOMPARE(blended.alpha(), 255);
        const auto canvas = readCanvas(widget);
        QCOMPARE(canvas.pixelColor(600, 540), QColor(Qt::blue));
        QCOMPARE(canvas.pixelColor(960, 540), blended);
        sources[0].visible = false;
        widget.setSources(sources);
        QCOMPARE(sample(grabPreview(widget), .5, .5), QColor(Qt::blue));
        sources[0].visible = true;
        sources.swapItemsAt(0, 1);
        widget.setSources(sources);
        QCOMPARE(sample(grabPreview(widget), .5, .5), QColor(Qt::blue));
        widget.resize(600, 160);
        grabPreview(widget);
        sources.removeFirst();
        widget.setSources(sources);
        grabPreview(widget);
        widget.makeCurrent();
        QCOMPARE(widget.context()->functions()->glGetError(), GLenum(GL_NO_ERROR));
        widget.doneCurrent();
        QCOMPARE(readCanvas(widget).pixelColor(600, 540), QColor(Qt::black));
        sources[0].image = {};
        widget.setSources(sources);
        QCOMPARE(sample(grabPreview(widget), .5, .5), QColor(Qt::black));
    }

    void fitsOversizedSourcesAndSkipsFailedSource()
    {
        PreviewWidget widget;
        widget.resize(480, 400);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QImage tall(1200, 2400, QImage::Format_RGBA8888);
        tall.fill(Qt::yellow);
        widget.setSources({{"tall", tall, true}});
        grabPreview(widget);
        const auto canvas = readCanvas(widget);
        QCOMPARE(sample(canvas, .5, .05), QColor(Qt::yellow));
        QCOMPARE(sample(canvas, .3, .5), QColor(Qt::black));
        widget.makeCurrent();
        GLint maxSize = 0;
        widget.context()->functions()->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
        widget.doneCurrent();
        QImage invalid(maxSize + 1, 1, QImage::Format_RGBA8888);
        invalid.fill(Qt::white);
        QSignalSpy sourceErrors(&widget, &PreviewWidget::sourceFailed);
        QSignalSpy fatalErrors(&widget, &PreviewWidget::renderingFailed);
        widget.setSources({{"invalid", invalid, true}, {"tall", tall, true}});
        QCOMPARE(sample(grabPreview(widget), .5, .5), QColor(Qt::yellow));
        QCOMPARE(sourceErrors.size(), 1);
        QCOMPARE(fatalErrors.size(), 0);
        grabPreview(widget);
        QCOMPARE(sourceErrors.size(), 1);
        widget.setSources({});
        QCOMPARE(sample(grabPreview(widget), .5, .5), QColor(Qt::black));
    }

    void transformsRenderAndSelectionStaysOutOfCanvas()
    {
        QImage image(80, 40, QImage::Format_RGBA8888);
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 80; ++x)
                image.setPixelColor(x, y,
                                    y < 20 ? (x < 40 ? Qt::red : Qt::green)
                                           : (x < 40 ? Qt::blue : Qt::white));
        PreviewWidget widget;
        widget.resize(640, 480);
        SourceTransform transform{{500, 400}, {400, 200}, 90};
        widget.setSources({{"image", image, true, transform}});
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        grabPreview(widget);
        const auto verifyColors = [&] {
            const auto canvas = readCanvas(widget);
            const auto matrix = SourceGeometry::sourceToCanvas(transform);
            const QColor colors[]{Qt::red, Qt::green, Qt::blue, Qt::white};
            int index = 0;
            for (qreal y : {.25, .75}) {
                for (qreal x : {.25, .75}) {
                    const auto point = matrix.map(
                        QPointF(x * transform.size.width(), y * transform.size.height()));
                    QCOMPARE(canvas.pixelColor(qRound(point.x()), qRound(point.y())),
                             colors[index++]);
                }
            }
            QCOMPARE(canvas.pixelColor(0, 0), QColor(Qt::black));
        };
        verifyColors();
        const auto *program = widget.m_program.get();
        transform = {{900, 600}, {600, 300}, 37};
        widget.setSources({{"image", image, true, transform}});
        const auto unselected = grabPreview(widget);
        verifyColors();
        const auto canvas = readCanvas(widget);
        widget.setCurrentSourceId("image");
        const auto selected = grabPreview(widget);
        QVERIFY(selected != unselected);
        QCOMPARE(readCanvas(widget), canvas);
        QCOMPARE(widget.m_program.get(), program);
        widget.setSources({{"image", image, true, {{-50, 400}, {400, 200}, 0}}});
        grabPreview(widget);
        QCOMPARE(readCanvas(widget).pixelColor(10, 380), QColor(Qt::green));
        widget.setSources({{"image", image, true, {{-1000, 400}, {400, 200}, 0}}});
        grabPreview(widget);
        QCOMPARE(sample(readCanvas(widget), .5, .5), QColor(Qt::black));
        widget.setSources({{"image", image, true, transform}});
        widget.makeCurrent();
        widget.paintGL();
        GLint binding = 0, viewport[4]{};
        auto *gl = widget.context()->functions();
        gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &binding);
        gl->glGetIntegerv(GL_VIEWPORT, viewport);
        QCOMPARE(GLuint(binding), widget.defaultFramebufferObject());
        QCOMPARE(viewport[2], qRound(widget.width() * widget.devicePixelRatioF()));
        QCOMPARE(viewport[3], qRound(widget.height() * widget.devicePixelRatioF()));
        QCOMPARE(gl->glGetError(), GLenum(GL_NO_ERROR));
        widget.doneCurrent();
    }

    void mouseEditingUsesCanvasCoordinatesAndLocalAxes()
    {
        PreviewWidget widget;
        wireEditor(widget);
        QImage image(200, 100, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        SourceTransform start{{960, 540}, {600, 300}, 0};
        widget.resize(640, 480);
        widget.setSources({{"image", image, true, start}});
        widget.setCurrentSourceId("image");
        widget.show();
        QVERIFY(QTest::qWaitForWindowActive(&widget));
        for (const QSize &size : {QSize(640, 480), QSize(800, 280), QSize(300, 500)}) {
            widget.resize(size);
            widget.setSources({{"image", image, true, start}});
            grabPreview(widget);
            const auto display = SourceGeometry::canvasToWidget(widget.size());
            drag(widget, display.map(start.center), display.map(start.center + QPointF(120, -60)));
            const auto moved = widget.m_sources[0].transform;
            QVERIFY(closePoint(moved.center, start.center + QPointF(120, -60)));
            QVERIFY(moved.size == start.size);
            widget.setSources({{"image", image, true, start}});
            const auto point = display.map(start.center);
            mouse(widget, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
            mouse(widget, QEvent::MouseMove, point + QPointF(20, 10), Qt::NoButton, Qt::LeftButton);
            QVERIFY(widget.m_sources[0].transform != start);
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(&widget, &escape);
            QVERIFY(widget.m_sources[0].transform == start);
            mouse(widget, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
            const auto handles = widget.selectionHandles(widget.m_sources[0]);
            QVERIFY(qAbs(std::hypot((handles[8] - handles[1]).x(), (handles[8] - handles[1]).y()) -
                         24) < 1e-6);
            const auto insideHandle = handles[0] - QPointF(5.5, 5.5);
            mouse(widget, QEvent::MouseButtonPress, insideHandle, Qt::LeftButton, Qt::LeftButton);
            QVERIFY(widget.m_gesture == PreviewWidget::Gesture::Resize);
            mouse(widget, QEvent::MouseButtonRelease, insideHandle, Qt::LeftButton, Qt::NoButton);
            const auto outsideHandle = handles[0] - QPointF(7.5, 7.5);
            drag(widget, outsideHandle, outsideHandle);
            QVERIFY(widget.m_currentSourceId.isEmpty());
            mouse(widget, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
            mouse(widget, QEvent::MouseButtonRelease, point + QPointF(10, 0), Qt::LeftButton,
                  Qt::NoButton);
            QVERIFY(closePoint(widget.m_sources[0].transform.center,
                               display.inverted().map(point + QPointF(10, 0))));
            widget.setCurrentSourceId("image");
        }
        widget.resize(800, 500);
        start.rotation = 37;
        widget.setSources({{"image", image, true, start}});
        grabPreview(widget);
        QTransform rotation;
        rotation.rotate(37);
        auto handles = widget.selectionHandles(widget.m_sources[0]);
        const auto display = SourceGeometry::canvasToWidget(widget.size());
        const QPointF delta = rotation.map(QPointF(120, 60));
        drag(widget, handles[4], handles[4] + display.map(delta) - display.map(QPointF()));
        const auto resized = widget.m_sources[0].transform;
        QVERIFY(qAbs(resized.size.width() - 720) < 1e-6);
        QVERIFY(qAbs(resized.size.height() - 360) < 1e-6);
        QVERIFY(closePoint(SourceGeometry::sourceToCanvas(start).map(QPointF()),
                           SourceGeometry::sourceToCanvas(resized).map(QPointF())));
        widget.setSources({{"image", image, true, start}});
        handles = widget.selectionHandles(widget.m_sources[0]);
        const QPointF sideDelta = rotation.map(QPointF(80, 0));
        drag(widget, handles[3], handles[3] + display.map(sideDelta) - display.map(QPointF()));
        QVERIFY(qAbs(widget.m_sources[0].transform.size.width() - 680) < 1e-6);
        QVERIFY(qAbs(widget.m_sources[0].transform.size.height() - 300) < 1e-6);
        start.rotation = 0;
        widget.setSources({{"image", image, true, start}});
        handles = widget.selectionHandles(widget.m_sources[0]);
        const auto center = display.map(start.center);
        QTransform quarterTurn;
        quarterTurn.rotate(90);
        drag(widget, handles[8], center + quarterTurn.map(handles[8] - center));
        QVERIFY(qAbs(widget.m_sources[0].transform.rotation - 90) < 1e-6);
        QVERIFY(closePoint(widget.m_sources[0].transform.center, start.center));
    }

    void rotationHandleUsesHandCursors()
    {
        PreviewWidget widget;
        widget.resize(640, 480);
        wireEditor(widget);
        QImage image(200, 100, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        widget.setSources({{"image", image, true, {{960, 540}, {600, 300}, 0}}});
        widget.setCurrentSourceId("image");
        widget.show();
        QVERIFY(QTest::qWaitForWindowActive(&widget));
        grabPreview(widget);
        const auto handle = widget.selectionHandles(widget.m_sources[0])[8];
        mouse(widget, QEvent::MouseMove, handle, Qt::NoButton, Qt::NoButton);
        QCOMPARE(widget.cursor().shape(), Qt::OpenHandCursor);
        mouse(widget, QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton);
        QCOMPARE(widget.cursor().shape(), Qt::ClosedHandCursor);
        const QPointF away(10, 10);
        mouse(widget, QEvent::MouseMove, away, Qt::NoButton, Qt::LeftButton);
        QCOMPARE(widget.cursor().shape(), Qt::ClosedHandCursor);
        mouse(widget, QEvent::MouseButtonRelease, away, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(widget.cursor().shape(), Qt::ArrowCursor);
        const auto rotatedHandle = widget.selectionHandles(widget.m_sources[0])[8];
        mouse(widget, QEvent::MouseMove, rotatedHandle, Qt::NoButton, Qt::NoButton);
        QCOMPARE(widget.cursor().shape(), Qt::OpenHandCursor);
        mouse(widget, QEvent::MouseButtonPress, rotatedHandle, Qt::LeftButton, Qt::LeftButton);
        QCOMPARE(widget.cursor().shape(), Qt::ClosedHandCursor);
        mouse(widget, QEvent::MouseButtonRelease, rotatedHandle, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(widget.cursor().shape(), Qt::OpenHandCursor);
        mouse(widget, QEvent::MouseButtonPress, rotatedHandle, Qt::LeftButton, Qt::LeftButton);
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&widget, &escape);
        QVERIFY(widget.cursor().shape() != Qt::ClosedHandCursor);
        auto sources = widget.m_sources;
        sources[0].locked = true;
        widget.setSources(sources);
        mouse(widget, QEvent::MouseMove, rotatedHandle, Qt::NoButton, Qt::NoButton);
        QVERIFY(widget.cursor().shape() != Qt::OpenHandCursor);
    }

    void selectionUsesRotatedRectanglesAndSkipsLockedSources()
    {
        PreviewWidget widget;
        widget.resize(640, 480);
        wireEditor(widget);
        QImage transparent(100, 100, QImage::Format_RGBA8888);
        transparent.fill(Qt::transparent);
        QImage background(100, 100, QImage::Format_RGBA8888);
        background.fill(Qt::yellow);
        QList<PreviewSource> sources{{"top", transparent, true, {{960, 540}, {200, 400}, 45}, true},
                                     {"bottom", background, true, {{960, 540}, {500, 500}, 0}}};
        widget.setSources(sources);
        widget.show();
        QVERIFY(QTest::qWaitForWindowActive(&widget));
        grabPreview(widget);
        const auto display = SourceGeometry::canvasToWidget(widget.size());
        const auto center = display.map(QPointF(960, 540));
        drag(widget, center, center);
        QCOMPARE(widget.m_currentSourceId, QString("bottom"));
        sources[0].locked = false;
        widget.setSources(sources);
        drag(widget, center, center);
        QCOMPARE(widget.m_currentSourceId, QString("top"));
        const auto bounds = SourceGeometry::sourceToCanvas(sources[0].transform)
                                .mapRect(QRectF(QPointF(), sources[0].transform.size));
        const auto outsideRotatedRectangle = bounds.topLeft() + QPointF(5, 5);
        QVERIFY(!SourceGeometry::contains(sources[0].transform, outsideRotatedRectangle));
        drag(widget, display.map(outsideRotatedRectangle), display.map(outsideRotatedRectangle));
        QCOMPARE(widget.m_currentSourceId, QString("bottom"));
        sources[0].locked = true;
        widget.setSources(sources);
        widget.setCurrentSourceId("top");
        QCOMPARE(
            widget.handleAt(widget.m_sources[0], widget.selectionHandles(widget.m_sources[0])[0]),
            -1);
        drag(widget, center, center);
        QCOMPARE(widget.m_currentSourceId, QString("bottom"));
        sources[0].visible = false;
        widget.setSources(sources);
        widget.setCurrentSourceId("top");
        QVERIFY(!widget.selectedSource());
        drag(widget, QPointF(5, 5), QPointF(5, 5));
        QVERIFY(widget.m_currentSourceId.isEmpty());
    }

    void captureFramesDoNotInterruptEditing_data()
    {
        QTest::addColumn<int>("operation");
        QTest::addColumn<bool>("gpu");
        QTest::newRow("cpu-move") << 0 << false;
        QTest::newRow("cpu-resize") << 1 << false;
        QTest::newRow("cpu-rotate") << 2 << false;
        QTest::newRow("gpu-move") << 0 << true;
        QTest::newRow("gpu-resize") << 1 << true;
        QTest::newRow("gpu-rotate") << 2 << true;
    }

    void captureFramesDoNotInterruptEditing()
    {
        QFETCH(int, operation);
        QFETCH(bool, gpu);
        PreviewWidget widget;
        widget.resize(640, 480);
        wireEditor(widget);
        QImage image(200, 100, QImage::Format_RGBA8888);
        image.fill(Qt::green);
        auto resource = std::make_shared<GpuFrameResource>();
        if (gpu) {
#ifdef Q_OS_WIN
            QVERIFY(SUCCEEDED(D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr, 0, D3D11_SDK_VERSION, &resource->device, nullptr, nullptr)));
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = UINT(image.width());
            desc.Height = UINT(image.height());
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            D3D11_SUBRESOURCE_DATA pixels{image.constBits(), UINT(image.bytesPerLine()), 0};
            QVERIFY(
                SUCCEEDED(resource->device->CreateTexture2D(&desc, &pixels, &resource->texture)));
#else
            QSKIP("Capture frames require Windows D3D11");
#endif
        }
        const SourceTransform start{{960, 540}, {600, 300}, 0};
        quint64 sequence = 0;
        const auto publish = [&] {
            auto sources = widget.m_sources;
            if (sources.isEmpty())
                sources.append({"dynamic", {}, true, start});
            if (gpu) {
                auto frame = std::make_shared<CaptureFrame>();
                frame->size = image.size();
                frame->gpu = resource;
                frame->generation = 1;
                frame->sequence = ++sequence;
                sources[0].frame = frame;
            } else {
                // Camera frames are independent images with a fresh cache key.
                sources[0].image = image.copy();
            }
            widget.setSources(sources);
        };
        publish();
        QSignalSpy transferErrors(&widget, &PreviewWidget::gpuTransferFailed);
        widget.show();
        QVERIFY(QTest::qWaitForWindowActive(&widget));
        grabPreview(widget);
        if (!transferErrors.isEmpty()) {
            QVERIFY2(!qEnvironmentVariableIsSet("OPENLIVE_REQUIRE_GPU_INTEROP"),
                     "GPU interop required");
            QSKIP("GPU interop unavailable for capture editing test");
        }
        const auto display = SourceGeometry::canvasToWidget(widget.size());
        const auto handles = widget.selectionHandles(widget.m_sources[0]);
        const auto center = display.map(start.center);
        const auto from = operation == 0 ? center : handles[operation == 1 ? 4 : 8];
        if (operation != 0)
            widget.setCurrentSourceId("dynamic");
        // Receive a new frame before mouse input, without uploading it yet.
        const auto pixelsPresented = [&] {
            const auto &ready = widget.m_presentedFrame->sources[QStringLiteral("dynamic")];
            return gpu ? ready.sequence == sequence
                       : ready.imageKey == widget.m_sources[0].image.cacheKey();
        };
        publish();
        QVERIFY(!pixelsPresented());
        mouse(widget, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        QCOMPARE(widget.m_currentSourceId, QString("dynamic"));
        const auto gesture = operation == 0   ? PreviewWidget::Gesture::Move
                             : operation == 1 ? PreviewWidget::Gesture::Resize
                                              : PreviewWidget::Gesture::Rotate;
        QVERIFY(widget.m_gesture == gesture);
        QPointF to;
        for (int step = 1; step <= 3; ++step) {
            publish();
            QVERIFY(widget.m_gesture == gesture);
            if (operation == 2) {
                QTransform rotation;
                rotation.rotate(20 * step);
                to = center + rotation.map(from - center);
            } else {
                const QPointF delta =
                    operation == 0 ? QPointF(30 * step, 15 * step) : QPointF(40 * step, 20 * step);
                to = from + display.map(delta) - display.map(QPointF());
            }
            mouse(widget, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
            QVERIFY(widget.m_gesture == gesture);
            const auto transform = widget.m_sources[0].transform;
            if (operation == 0)
                QVERIFY(closePoint(transform.center, start.center + QPointF(30 * step, 15 * step)));
            else if (operation == 1) {
                QVERIFY(qAbs(transform.size.width() - (600 + 40 * step)) < 1e-6);
                QVERIFY(qAbs(transform.size.height() - (300 + 20 * step)) < 1e-6);
            } else
                QVERIFY(qAbs(transform.rotation - 20 * step) < 1e-6);
        }
        mouse(widget, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        QVERIFY(widget.m_gesture == PreviewWidget::Gesture::None);
        grabPreview(widget);
        QVERIFY(pixelsPresented());
    }

    void interruptsGesturesWhenEditingBecomesUnavailable()
    {
        PreviewWidget widget;
        widget.resize(640, 480);
        wireEditor(widget);
        QImage image(100, 100, QImage::Format_RGBA8888);
        image.fill(Qt::green);
        // Gestures and deactivation are injected below; foreground ownership
        // is not a prerequisite and Windows may retain focus in another app.
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QSignalSpy transforms(&widget, &PreviewWidget::transformRequested);
        for (int reason = 0; reason < 9; ++reason) {
            widget.show();
            QVERIFY(QTest::qWaitForWindowExposed(&widget));
            widget.setSources({{"image", image, true, {{960, 540}, {600, 300}, 0}}});
            widget.setCurrentSourceId("image");
            grabPreview(widget);
            const QPointF center =
                SourceGeometry::canvasToWidget(widget.size()).map(QPointF(960, 540));
            mouse(widget, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
            mouse(widget, QEvent::MouseMove, center + QPointF(10, 0), Qt::NoButton, Qt::LeftButton);
            QVERIFY(widget.m_gesture != PreviewWidget::Gesture::None);
            const auto before = widget.m_sources;
            auto changed = before;
            if (reason == 0) {
                changed[0].visible = false;
                widget.setSources(changed);
            }
            if (reason == 1) {
                changed[0].locked = true;
                widget.setSources(changed);
            }
            if (reason == 2)
                widget.setSources({});
            if (reason == 3)
                widget.setCurrentSourceId({});
            if (reason == 4)
                widget.resize(widget.width() + 10, widget.height());
            if (reason == 5) {
                QFocusEvent event(QEvent::FocusOut);
                QApplication::sendEvent(&widget, &event);
            }
            if (reason == 6)
                widget.hide();
            if (reason == 7) {
                QEvent event(QEvent::WindowDeactivate);
                QApplication::sendEvent(&widget, &event);
            }
            if (reason == 8) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
                QEvent event(QEvent::DevicePixelRatioChange);
#else
                QEvent event(QEvent::WindowDeactivate);
#endif
                QApplication::sendEvent(&widget, &event);
            }
            QVERIFY(widget.m_gesture == PreviewWidget::Gesture::None);
            const int count = transforms.count();
            mouse(widget, QEvent::MouseMove, center + QPointF(100, 0), Qt::NoButton,
                  Qt::LeftButton);
            QCOMPARE(transforms.count(), count);
            mouse(widget, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
        }
    }

    void destroysWithoutInitialization()
    {
        PreviewWidget widget;
        QImage image(16, 16, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        widget.setSources({{"image", image, true}});
        QVERIFY(!widget.isValid());
    }

private:
    static bool closePoint(const QPointF &a, const QPointF &b)
    {
        return std::hypot(a.x() - b.x(), a.y() - b.y()) < 1e-6;
    }

    static void wireEditor(PreviewWidget &widget)
    {
        connect(&widget, &PreviewWidget::selectionRequested, &widget,
                &PreviewWidget::setCurrentSourceId);
        connect(&widget, &PreviewWidget::transformRequested, &widget,
                [&widget](const QString &sourceId, const SourceTransform &transform) {
                    auto sources = widget.m_sources;
                    for (auto &source : sources)
                        if (source.id == sourceId && !source.locked)
                            source.transform = transform;
                    widget.setSources(sources);
                });
    }

    static void mouse(PreviewWidget &widget, QEvent::Type type, const QPointF &position,
                      Qt::MouseButton button, Qt::MouseButtons buttons)
    {
        QMouseEvent event(type, position, widget.mapToGlobal(position), button, buttons,
                          Qt::NoModifier);
        QApplication::sendEvent(&widget, &event);
    }

    static void drag(PreviewWidget &widget, const QPointF &from, const QPointF &to)
    {
        mouse(widget, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        mouse(widget, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
        mouse(widget, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    }

    static QImage readCanvas(PreviewWidget &widget)
    {
        const QImage image = widget.compositor()->captureImage();
        QCoreApplication::processEvents();
        return image;
    }

    static QImage grabPreview(PreviewWidget &widget)
    {
        widget.compositor()->captureImage();
        QCoreApplication::processEvents();
        return widget.grabFramebuffer();
    }

    static QColor sample(const QImage &image, double x, double y)
    {
        return image.pixelColor(int(image.width() * x), int(image.height() * y));
    }
};

int main(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    PreviewWidgetTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_previewwidget.moc"
