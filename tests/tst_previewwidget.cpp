#include "preview/previewwidget.h"

#include <QApplication>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QSignalSpy>
#include <QSurfaceFormat>
#include <QTest>

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
        widget.setImage(image);
        QVERIFY(!widget.grabFramebuffer().isNull());
        widget.resize(400, 100);
        image.fill(Qt::blue);
        widget.setImage(image);
        QVERIFY(!widget.grabFramebuffer().isNull());
        widget.setImage({});
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
        QImage image(80, 40, QImage::Format_RGBA8888);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                image.setPixelColor(x, y, y < 20 ? (x < 40 ? Qt::red : Qt::green)
                                                 : (x < 40 ? Qt::blue : Qt::white));
            }
        }
        PreviewWidget widget;
        QSignalSpy errors(&widget, &PreviewWidget::renderingError);
        widget.setImage(image);
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
        widget.setImage(image);
        QCOMPARE(sample(widget.grabFramebuffer(), .5, .5), QColor(Qt::red));
        QCOMPARE(sample(readCanvas(widget), .5, .5), QColor(Qt::red));
        image.fill(QColor(0, 255, 0, 128));
        widget.setImage(image);
        const QColor blended = sample(widget.grabFramebuffer(), .5, .5);
        QCOMPARE(blended.red(), 0);
        QVERIFY(qAbs(blended.green() - 128) <= 1);
        QCOMPARE(blended.blue(), 0);
        QCOMPARE(blended.alpha(), 255);
        const QColor canvasBlended = sample(readCanvas(widget), .5, .5);
        QCOMPARE(canvasBlended, blended);
        widget.setImage({});
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
        widget.setImage(image);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        const QImage before = widget.grabFramebuffer();
        QCOMPARE(sample(before, .5, .05), QColor(Qt::black));
        const QImage canvas = readCanvas(widget);
        const auto *canvasResource = widget.m_canvasFbo.get();
        const auto *imageResource = widget.m_texture.get();
        const auto *programResource = widget.m_program.get();
        widget.setSurroundingColor(QColor("#D4D7DC"));
        const QImage after = widget.grabFramebuffer();
        QCOMPARE(sample(after, .5, .05), QColor("#D4D7DC"));
        QCOMPARE(sample(after, .5, .5), QColor(Qt::red));
        QCOMPARE(sample(readCanvas(widget), .5, .02), QColor(Qt::black));
        QCOMPARE(readCanvas(widget), canvas);
        QCOMPARE(widget.m_canvasFbo.get(), canvasResource);
        QCOMPARE(widget.m_texture.get(), imageResource);
        QCOMPARE(widget.m_program.get(), programResource);
        widget.setSurroundingColor(Qt::black);
        QCOMPARE(sample(widget.grabFramebuffer(), .5, .05), QColor(Qt::black));
    }

    void fitsPortraitImageIntoCanvasAndCanvasIntoWidget()
    {
        PreviewWidget widget;
        widget.resize(240, 240);
        QImage image(40, 80, QImage::Format_RGBA8888);
        image.fill(Qt::yellow);
        widget.setImage(image);
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
        widget.setImage(image);
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
        widget.setImage(oversized);
        QSignalSpy errors(&widget, &PreviewWidget::renderingError);
        QTest::ignoreMessage(QtWarningMsg, "预览图片尺寸超过显卡纹理限制。");
        widget.paintGL();
        QCOMPARE(errors.count(), 1);
        QVERIFY(!widget.lastError().isEmpty());
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
        widget.setImage(image);
        QCOMPARE(sample(widget.grabFramebuffer(), .5, .5), QColor(Qt::red));
        QVERIFY(widget.lastError().isEmpty());
    }

    void restoresImageAfterContextRecreation()
    {
        QWidget firstWindow;
        QWidget secondWindow;
        firstWindow.resize(160, 160);
        secondWindow.resize(160, 160);
        auto *widget = new PreviewWidget(&firstWindow);
        widget->resize(100, 100);
        QImage image(16, 16, QImage::Format_RGBA8888);
        image.fill(Qt::blue);
        widget->setImage(image);
        firstWindow.show();
        QVERIFY(QTest::qWaitForWindowExposed(&firstWindow));
        QCOMPARE(sample(widget->grabFramebuffer(), .5, .5), QColor(Qt::blue));
        QCOMPARE(sample(readCanvas(*widget), .5, .5), QColor(Qt::blue));
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
        QCOMPARE(sample(widget->grabFramebuffer(), .5, .5), QColor(Qt::blue));
        QCOMPARE(sample(readCanvas(*widget), .5, .5), QColor(Qt::blue));
        QVERIFY(widget->m_canvasFbo);
        QVERIFY(widget->m_canvasFbo->isValid());
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

    void destroysWithoutInitialization()
    {
        PreviewWidget widget;
        QImage image(16, 16, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        widget.setImage(image);
        QVERIFY(!widget.isValid());
    }

private:
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
