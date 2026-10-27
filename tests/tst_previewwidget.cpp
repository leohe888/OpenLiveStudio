#include "preview/previewwidget.h"

#include <QApplication>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
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
    void initTestCase()
    {
        PreviewWidget widget;
        widget.resize(240, 200);
        widget.show();
        if (!QTest::qWaitForWindowExposed(&widget) || !widget.isValid())
            QSKIP("Desktop OpenGL context unavailable; GPU rendering was not verified.");
        widget.makeCurrent();
        qInfo() << "OpenGL renderer:" << reinterpret_cast<const char *>(
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
        QSignalSpy errors(&widget, &PreviewWidget::renderingError);
        widget.resize(240, 200);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QVERIFY(widget.m_initialized);
        QVERIFY(widget.m_canvasFbo);
        QVERIFY(widget.m_canvasFbo->isValid());
        QCOMPARE(widget.m_canvasFbo->size(), QSize(1920, 1080));
        QCOMPARE(widget.m_canvasFbo->format().textureTarget(), GLenum(GL_TEXTURE_2D));
        QCOMPARE(widget.m_canvasFbo->format().internalTextureFormat(), GLenum(GL_RGBA8));
        QCOMPARE(widget.m_canvasFbo->attachment(), QOpenGLFramebufferObject::NoAttachment);
        QCOMPARE(widget.m_canvasFbo->format().samples(), 0);
        const auto *canvas = widget.m_canvasFbo.get();
        const GLuint framebuffer = canvas->handle();
        const GLuint texture = canvas->texture();
        QVERIFY(texture != 0);

        widget.makeCurrent();
        auto *functions = widget.context()->functions();
        QVERIFY(functions->glIsTexture(texture));
        QVERIFY(widget.m_canvasFbo->bind());
        QCOMPARE(functions->glCheckFramebufferStatus(GL_FRAMEBUFFER), GLenum(GL_FRAMEBUFFER_COMPLETE));
        functions->glBindFramebuffer(GL_FRAMEBUFFER, widget.defaultFramebufferObject());
        QCOMPARE(functions->glGetError(), GLenum(GL_NO_ERROR));
        widget.doneCurrent();

        QImage image(32, 16, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        widget.setSources({{"image", image, true}});
        QVERIFY(!widget.grabFramebuffer().isNull());
        widget.resize(400, 100);
        image.fill(Qt::blue);
        widget.setSources({{"image", image, true}});
        QVERIFY(!widget.grabFramebuffer().isNull());
        widget.setSources({});
        QVERIFY(!widget.grabFramebuffer().isNull());
        QCOMPARE(widget.m_canvasFbo.get(), canvas);
        QCOMPARE(widget.m_canvasFbo->handle(), framebuffer);
        QCOMPARE(widget.m_canvasFbo->texture(), texture);
        QCOMPARE(widget.m_canvasFbo->size(), QSize(1920, 1080));
        QCOMPARE(errors.count(), 0);
    }

    void preservesFramebufferBindingsDuringInitialization()
    {
        PreviewWidget widget;
        widget.resize(100, 100);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.cleanup();
        widget.makeCurrent();
        auto *functions = widget.context()->functions();
        {
            QOpenGLFramebufferObject readTarget(16, 16);
            QOpenGLFramebufferObject drawTarget(16, 16);
            QVERIFY(readTarget.isValid());
            QVERIFY(drawTarget.isValid());
            functions->glBindFramebuffer(GL_READ_FRAMEBUFFER, readTarget.handle());
            functions->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawTarget.handle());
            GLuint sentinelTexture = 0;
            functions->glGenTextures(1, &sentinelTexture);
            functions->glBindTexture(GL_TEXTURE_2D, sentinelTexture);

            widget.initializeGL();
            QVERIFY2(widget.m_initialized, qPrintable(widget.lastError()));
            GLint readBinding = 0;
            GLint drawBinding = 0;
            functions->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readBinding);
            functions->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawBinding);
            QCOMPARE(GLuint(readBinding), readTarget.handle());
            QCOMPARE(GLuint(drawBinding), drawTarget.handle());
            GLint textureBinding = 0;
            functions->glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureBinding);
            QCOMPARE(GLuint(textureBinding), sentinelTexture);
            functions->glBindTexture(GL_TEXTURE_2D, widget.m_canvasFbo->texture());
            for (GLenum parameter : {GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER,
                                     GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T}) {
                GLint value = 0;
                functions->glGetTexParameteriv(GL_TEXTURE_2D, parameter, &value);
                QCOMPARE(value, parameter == GL_TEXTURE_MIN_FILTER || parameter == GL_TEXTURE_MAG_FILTER
                                   ? GLint(GL_LINEAR) : GLint(GL_CLAMP_TO_EDGE));
            }
            functions->glBindTexture(GL_TEXTURE_2D, 0);
            functions->glDeleteTextures(1, &sentinelTexture);
            functions->glBindFramebuffer(GL_FRAMEBUFFER, widget.defaultFramebufferObject());
        }
        QCOMPARE(functions->glGetError(), GLenum(GL_NO_ERROR));
        widget.doneCurrent();
    }

    void cleanupReleasesCanvasResources()
    {
        PreviewWidget widget;
        widget.resize(100, 100);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        QVERIFY(widget.m_canvasFbo);
        const GLuint framebuffer = widget.m_canvasFbo->handle();
        const GLuint texture = widget.m_canvasFbo->texture();
        widget.cleanup();
        QVERIFY(!widget.m_canvasFbo);
        QVERIFY(!widget.m_initialized);
        widget.makeCurrent();
        auto *functions = widget.context()->functions();
        QVERIFY(!functions->glIsFramebuffer(framebuffer));
        QVERIFY(!functions->glIsTexture(texture));
        QCOMPARE(functions->glGetError(), GLenum(GL_NO_ERROR));
        widget.doneCurrent();
    }

    void drawsImageWithCorrectOrientationAndLetterboxing()
    {
        QImage image(1920, 960, QImage::Format_RGBA8888);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                image.setPixelColor(x, y, y < image.height() / 2 ? (x < image.width() / 2 ? Qt::red : Qt::green)
                                                 : (x < image.width() / 2 ? Qt::blue : Qt::white));
            }
        }
        PreviewWidget widget;
        QSignalSpy errors(&widget, &PreviewWidget::renderingError);
        widget.setSources({{"image", image, true}});
        widget.resize(240, 200);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        const QImage frame = widget.grabFramebuffer();
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
        const QImage wideFrame = widget.grabFramebuffer();
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
        QCOMPARE(sample(widget.grabFramebuffer(), .5, .5), QColor(Qt::red));
        QCOMPARE(sample(readCanvas(widget), .5, .5), QColor(Qt::red));
        image.fill(QColor(0, 255, 0, 128));
        widget.setSources({{"image", image, true}});
        const QColor blended = sample(widget.grabFramebuffer(), .5, .5);
        QCOMPARE(blended.red(), 0);
        QVERIFY(qAbs(blended.green() - 128) <= 1);
        QCOMPARE(blended.blue(), 0);
        QCOMPARE(blended.alpha(), 255);
        const QColor canvasBlended = sample(readCanvas(widget), .5, .5);
        QCOMPARE(canvasBlended, blended);
        widget.setSources({});
        QCOMPARE(sample(widget.grabFramebuffer(), .5, .5), QColor(Qt::black));
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
        const QImage before = widget.grabFramebuffer();
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
        const auto *canvasResource = widget.m_canvasFbo.get();
        const auto *imageResource = widget.m_textures.at(QStringLiteral("image")).texture.get();
        const auto *programResource = widget.m_program.get();
        widget.setSurroundingColor(QColor("#D4D7DC"));
        const QImage after = widget.grabFramebuffer();
        QCOMPARE(sample(after, .5, .05), QColor("#D4D7DC"));
        QCOMPARE(sample(after, .5, .5), QColor(Qt::red));
        QCOMPARE(sample(readCanvas(widget), .5, .02), QColor(Qt::black));
        QCOMPARE(readCanvas(widget), canvas);
        QCOMPARE(widget.m_canvasFbo.get(), canvasResource);
        QCOMPARE(widget.m_textures.at(QStringLiteral("image")).texture.get(), imageResource);
        QCOMPARE(widget.m_program.get(), programResource);
        widget.setSurroundingColor(Qt::black);
        QCOMPARE(sample(widget.grabFramebuffer(), .5, .05), QColor(Qt::black));
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
        const QImage frame = widget.grabFramebuffer();
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
        QCOMPARE(sample(widget.grabFramebuffer(), .5, .5), QColor(Qt::red));
        widget.makeCurrent();
        auto *functions = widget.context()->functions();
        GLint maxSize = 0;
        functions->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
        QImage oversized(maxSize + 1, 1, QImage::Format_RGBA8888);
        QVERIFY(!oversized.isNull());
        oversized.fill(Qt::white);
        widget.setSources({{"image", oversized, true}});
        QSignalSpy errors(&widget, &PreviewWidget::sourceError);
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
        QCOMPARE(sample(widget.grabFramebuffer(), .5, .5), QColor(Qt::black));
        widget.setSources({{"image", image, true}});
        QCOMPARE(sample(widget.grabFramebuffer(), .5, .5), QColor(Qt::red));
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
        widget->setSources({{"overlay", overlay, true, transform}, {"background", background, true}});
        widget->setSelectedSourceId("overlay");
        firstWindow.show();
        QVERIFY(QTest::qWaitForWindowExposed(&firstWindow));
        QCOMPARE(sample(widget->grabFramebuffer(), .5, .5), QColor(Qt::yellow));
        QCOMPARE(sample(readCanvas(*widget), .5, .5), QColor(Qt::yellow));
        QVERIFY(widget->m_canvasFbo);
        QVERIFY(widget->m_canvasFbo->isValid());
        bool canvasWasReleased = false;
        connect(widget->context(), &QOpenGLContext::aboutToBeDestroyed, this, [&] {
            canvasWasReleased = !widget->m_canvasFbo;
        });
        QSignalSpy destroyed(widget->context(), &QOpenGLContext::aboutToBeDestroyed);
        widget->setParent(&secondWindow);
        secondWindow.show();
        widget->show();
        QVERIFY(QTest::qWaitForWindowExposed(&secondWindow));
        QCOMPARE(destroyed.count(), 1);
        QVERIFY(canvasWasReleased);
        QCOMPARE(sample(widget->grabFramebuffer(), .5, .5), QColor(Qt::yellow));
        QCOMPARE(sample(readCanvas(*widget), .5, .5), QColor(Qt::yellow));
        QVERIFY(widget->m_canvasFbo);
        QVERIFY(widget->m_canvasFbo->isValid());
        QCOMPARE(widget->m_textures.size(), size_t(2));
        QVERIFY(widget->m_sources[0].transform == transform);
        QCOMPARE(widget->m_selectedSourceId, QString("overlay"));
        QCOMPARE(sample(readCanvas(*widget), .1, .5), QColor(Qt::blue));
        QCOMPARE(widget->m_canvasFbo->size(), QSize(1920, 1080));
        widget->makeCurrent();
        QVERIFY(widget->m_canvasFbo->bind());
        auto *functions = widget->context()->functions();
        QCOMPARE(functions->glCheckFramebufferStatus(GL_FRAMEBUFFER), GLenum(GL_FRAMEBUFFER_COMPLETE));
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
        const auto blended = sample(widget.grabFramebuffer(), .5, .5);
        QVERIFY(qAbs(blended.red() - 128) <= 1);
        QVERIFY(qAbs(blended.blue() - 127) <= 1);
        QCOMPARE(blended.alpha(), 255);
        const auto canvas = readCanvas(widget);
        QCOMPARE(canvas.pixelColor(600, 540), QColor(Qt::blue));
        QCOMPARE(canvas.pixelColor(960, 540), blended);
        const GLuint top = widget.m_textures.at("top").texture->textureId();
        const GLuint bottom = widget.m_textures.at("bottom").texture->textureId();
        const GLuint fbo = widget.m_canvasFbo->handle();
        sources[0].visible = false;
        widget.setSources(sources);
        QCOMPARE(sample(widget.grabFramebuffer(), .5, .5), QColor(Qt::blue));
        sources[0].visible = true;
        sources.swapItemsAt(0, 1);
        widget.setSources(sources);
        QCOMPARE(sample(widget.grabFramebuffer(), .5, .5), QColor(Qt::blue));
        QCOMPARE(widget.m_textures.at("top").texture->textureId(), top);
        QCOMPARE(widget.m_textures.at("bottom").texture->textureId(), bottom);
        widget.resize(600, 160);
        widget.grabFramebuffer();
        QCOMPARE(widget.m_canvasFbo->handle(), fbo);
        QCOMPARE(widget.m_textures.at("top").texture->textureId(), top);
        sources.removeFirst();
        widget.setSources(sources);
        widget.grabFramebuffer();
        QCOMPARE(widget.m_textures.size(), size_t(1));
        widget.makeCurrent();
        QVERIFY(!widget.context()->functions()->glIsTexture(bottom));
        QCOMPARE(widget.context()->functions()->glGetError(), GLenum(GL_NO_ERROR));
        widget.doneCurrent();
        QCOMPARE(readCanvas(widget).pixelColor(600, 540), QColor(Qt::black));
        sources[0].image = {};
        widget.setSources(sources);
        QCOMPARE(sample(widget.grabFramebuffer(), .5, .5), QColor(Qt::black));
        QVERIFY(widget.m_textures.empty());
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
        widget.grabFramebuffer();
        const auto canvas = readCanvas(widget);
        QCOMPARE(sample(canvas, .5, .05), QColor(Qt::yellow));
        QCOMPARE(sample(canvas, .3, .5), QColor(Qt::black));
        widget.makeCurrent();
        GLint maxSize = 0;
        widget.context()->functions()->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
        widget.doneCurrent();
        QImage invalid(maxSize + 1, 1, QImage::Format_RGBA8888);
        invalid.fill(Qt::white);
        QSignalSpy sourceErrors(&widget, &PreviewWidget::sourceError);
        QSignalSpy fatalErrors(&widget, &PreviewWidget::renderingError);
        widget.setSources({{"invalid", invalid, true}, {"tall", tall, true}});
        QCOMPARE(sample(widget.grabFramebuffer(), .5, .5), QColor(Qt::yellow));
        QCOMPARE(sourceErrors.size(), 1);
        QCOMPARE(fatalErrors.size(), 0);
        widget.grabFramebuffer();
        QCOMPARE(sourceErrors.size(), 1);
        widget.setSources({});
        QCOMPARE(sample(widget.grabFramebuffer(), .5, .5), QColor(Qt::black));
    }

    void transformsRenderAndSelectionStaysOutOfCanvas()
    {
        QImage image(80, 40, QImage::Format_RGBA8888);
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 80; ++x)
                image.setPixelColor(x, y, y < 20 ? (x < 40 ? Qt::red : Qt::green)
                                                               : (x < 40 ? Qt::blue : Qt::white));
        PreviewWidget widget;
        widget.resize(640, 480);
        SourceTransform transform{{500, 400}, {400, 200}, 90};
        widget.setSources({{"image", image, true, transform}});
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.grabFramebuffer();
        const auto verifyColors = [&] {
            const auto canvas = readCanvas(widget);
            const auto matrix = SourceGeometry::sourceToCanvas(transform);
            const QColor colors[]{Qt::red, Qt::green, Qt::blue, Qt::white};
            int index = 0;
            for (qreal y : {.25, .75}) {
                for (qreal x : {.25, .75}) {
                    const auto point = matrix.map(QPointF(x * transform.size.width(), y * transform.size.height()));
                    QCOMPARE(canvas.pixelColor(qRound(point.x()), qRound(point.y())), colors[index++]);
                }
            }
            QCOMPARE(canvas.pixelColor(0, 0), QColor(Qt::black));
        };
        verifyColors();
        const auto texture = widget.m_textures.at("image").texture->textureId();
        const auto fbo = widget.m_canvasFbo->handle();
        const auto *program = widget.m_program.get();
        transform = {{900, 600}, {600, 300}, 37};
        widget.setSources({{"image", image, true, transform}});
        const auto unselected = widget.grabFramebuffer();
        verifyColors();
        const auto canvas = readCanvas(widget);
        widget.setSelectedSourceId("image");
        const auto selected = widget.grabFramebuffer();
        QVERIFY(selected != unselected);
        QCOMPARE(readCanvas(widget), canvas);
        QCOMPARE(widget.m_textures.at("image").texture->textureId(), texture);
        QCOMPARE(widget.m_canvasFbo->handle(), fbo);
        QCOMPARE(widget.m_program.get(), program);
        widget.setSources({{"image", image, true, {{-50, 400}, {400, 200}, 0}}});
        widget.grabFramebuffer();
        QCOMPARE(readCanvas(widget).pixelColor(10, 380), QColor(Qt::green));
        widget.setSources({{"image", image, true, {{-1000, 400}, {400, 200}, 0}}});
        widget.grabFramebuffer();
        QCOMPARE(sample(readCanvas(widget), .5, .5), QColor(Qt::black));
        QCOMPARE(widget.m_textures.at("image").texture->textureId(), texture);
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
        widget.setSelectedSourceId("image");
        widget.show();
        QVERIFY(QTest::qWaitForWindowActive(&widget));
        for (const QSize &size : {QSize(640, 480), QSize(800, 280), QSize(300, 500)}) {
            widget.resize(size);
            widget.setSources({{"image", image, true, start}});
            widget.grabFramebuffer();
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
            QVERIFY(qAbs(std::hypot((handles[8] - handles[1]).x(), (handles[8] - handles[1]).y()) - 24) < 1e-6);
            const auto insideHandle = handles[0] - QPointF(5.5, 5.5);
            mouse(widget, QEvent::MouseButtonPress, insideHandle, Qt::LeftButton, Qt::LeftButton);
            QVERIFY(widget.m_gesture == PreviewWidget::Gesture::Resize);
            mouse(widget, QEvent::MouseButtonRelease, insideHandle, Qt::LeftButton, Qt::NoButton);
            const auto outsideHandle = handles[0] - QPointF(7.5, 7.5);
            drag(widget, outsideHandle, outsideHandle);
            QVERIFY(widget.m_selectedSourceId.isEmpty());
            mouse(widget, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
            mouse(widget, QEvent::MouseButtonRelease, point + QPointF(10, 0), Qt::LeftButton, Qt::NoButton);
            QVERIFY(closePoint(widget.m_sources[0].transform.center,
                display.inverted().map(point + QPointF(10, 0))));
            widget.setSelectedSourceId("image");
        }
        widget.resize(800, 500);
        start.rotation = 37;
        widget.setSources({{"image", image, true, start}});
        widget.grabFramebuffer();
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
        widget.grabFramebuffer();
        const auto display = SourceGeometry::canvasToWidget(widget.size());
        const auto center = display.map(QPointF(960, 540));
        drag(widget, center, center);
        QCOMPARE(widget.m_selectedSourceId, QString("bottom"));
        sources[0].locked = false;
        widget.setSources(sources);
        drag(widget, center, center);
        QCOMPARE(widget.m_selectedSourceId, QString("top"));
        const auto bounds = SourceGeometry::sourceToCanvas(sources[0].transform).mapRect(
            QRectF(QPointF(), sources[0].transform.size));
        const auto outsideRotatedRectangle = bounds.topLeft() + QPointF(5, 5);
        QVERIFY(!SourceGeometry::contains(sources[0].transform, outsideRotatedRectangle));
        drag(widget, display.map(outsideRotatedRectangle), display.map(outsideRotatedRectangle));
        QCOMPARE(widget.m_selectedSourceId, QString("bottom"));
        sources[0].locked = true;
        widget.setSources(sources);
        widget.setSelectedSourceId("top");
        QCOMPARE(widget.handleAt(widget.m_sources[0], widget.selectionHandles(widget.m_sources[0])[0]), -1);
        drag(widget, center, center);
        QCOMPARE(widget.m_selectedSourceId, QString("bottom"));
        sources[0].visible = false;
        widget.setSources(sources);
        widget.setSelectedSourceId("top");
        QVERIFY(!widget.selectedSource());
        drag(widget, QPointF(5, 5), QPointF(5, 5));
        QVERIFY(widget.m_selectedSourceId.isEmpty());
    }

    void interruptsGesturesWhenEditingBecomesUnavailable()
    {
        PreviewWidget widget;
        widget.resize(640, 480);
        wireEditor(widget);
        QImage image(100, 100, QImage::Format_RGBA8888);
        image.fill(Qt::green);
        widget.show();
        QVERIFY(QTest::qWaitForWindowActive(&widget));
        QSignalSpy transforms(&widget, &PreviewWidget::transformRequested);
        for (int reason = 0; reason < 9; ++reason) {
            widget.show();
            QVERIFY(QTest::qWaitForWindowActive(&widget));
            widget.setSources({{"image", image, true, {{960, 540}, {600, 300}, 0}}});
            widget.setSelectedSourceId("image");
            widget.grabFramebuffer();
            const QPointF center = SourceGeometry::canvasToWidget(widget.size()).map(QPointF(960, 540));
            mouse(widget, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
            mouse(widget, QEvent::MouseMove, center + QPointF(10, 0), Qt::NoButton, Qt::LeftButton);
            QVERIFY(widget.m_gesture != PreviewWidget::Gesture::None);
            const auto before = widget.m_sources;
            auto changed = before;
            if (reason == 0) { changed[0].visible = false; widget.setSources(changed); }
            if (reason == 1) { changed[0].locked = true; widget.setSources(changed); }
            if (reason == 2) widget.setSources({});
            if (reason == 3) widget.setSelectedSourceId({});
            if (reason == 4) widget.resize(widget.width() + 10, widget.height());
            if (reason == 5) {
                QFocusEvent event(QEvent::FocusOut);
                QApplication::sendEvent(&widget, &event);
            }
            if (reason == 6) widget.hide();
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
            mouse(widget, QEvent::MouseMove, center + QPointF(100, 0), Qt::NoButton, Qt::LeftButton);
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
    { return std::hypot(a.x() - b.x(), a.y() - b.y()) < 1e-6; }

    static void wireEditor(PreviewWidget &widget)
    {
        connect(&widget, &PreviewWidget::selectionRequested, &widget, &PreviewWidget::setSelectedSourceId);
        connect(&widget, &PreviewWidget::transformRequested, &widget,
            [&widget](const QString &id, const SourceTransform &transform) {
                auto sources = widget.m_sources;
                for (auto &source : sources)
                    if (source.id == id && !source.locked) source.transform = transform;
                widget.setSources(sources);
            });
    }

    static void mouse(PreviewWidget &widget, QEvent::Type type, const QPointF &position,
                      Qt::MouseButton button, Qt::MouseButtons buttons)
    {
        QMouseEvent event(type, position, widget.mapToGlobal(position), button, buttons, Qt::NoModifier);
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
        widget.makeCurrent();
        const QImage result = widget.m_canvasFbo->toImage();
        widget.context()->functions()->glBindFramebuffer(GL_FRAMEBUFFER, widget.defaultFramebufferObject());
        widget.doneCurrent();
        return result;
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
