#include "preview/previewwidget.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSignalSpy>
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

        widget.resize(400, 100);
        const QImage wideFrame = widget.grabFramebuffer();
        QCOMPARE(sample(wideFrame, .05, .5), QColor(Qt::black));
        QCOMPARE(sample(wideFrame, .95, .5), QColor(Qt::black));
        QCOMPARE(sample(wideFrame, .4, .25), QColor(Qt::red));
        QCOMPARE(errors.count(), 0);
        widget.makeCurrent();
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
        image.fill(QColor(0, 255, 0, 128));
        widget.setImage(image);
        const QColor blended = sample(widget.grabFramebuffer(), .5, .5);
        QCOMPARE(blended.red(), 0);
        QVERIFY(qAbs(blended.green() - 128) <= 1);
        QCOMPARE(blended.blue(), 0);
        QCOMPARE(blended.alpha(), 255);
        widget.setImage({});
        QCOMPARE(sample(widget.grabFramebuffer(), .5, .5), QColor(Qt::black));
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
        QSignalSpy destroyed(widget->context(), &QOpenGLContext::aboutToBeDestroyed);
        widget->setParent(&secondWindow);
        secondWindow.show();
        widget->show();
        QVERIFY(QTest::qWaitForWindowExposed(&secondWindow));
        QCOMPARE(destroyed.count(), 1);
        QCOMPARE(sample(widget->grabFramebuffer(), .5, .5), QColor(Qt::blue));
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
    static QColor sample(const QImage &image, double x, double y)
    {
        return image.pixelColor(int(image.width() * x), int(image.height() * y));
    }
};

QTEST_MAIN(PreviewWidgetTest)
#include "tst_previewwidget.moc"
