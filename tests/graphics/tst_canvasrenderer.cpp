#include "graphics/canvasrenderer.h"
#include "graphics/compositor.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLTexture>
#include <QSignalSpy>
#include <QTest>

class CanvasRendererTest : public QObject
{
    Q_OBJECT
private slots:

    void initTestCase()
    {
        m_context.setFormat(QSurfaceFormat::defaultFormat());
        if (!m_context.create())
            QSKIP("Desktop OpenGL unavailable");
        m_surface.setFormat(m_context.format());
        m_surface.create();
        if (!m_context.makeCurrent(&m_surface))
            QSKIP("Offscreen OpenGL unavailable");
        qInfo() << "Compositor test renderer:"
                << reinterpret_cast<const char *>(m_context.functions()->glGetString(GL_RENDERER));
    }

    void init()
    {
        QVERIFY(m_context.makeCurrent(&m_surface));
    }

    void fixedCanvasAndInitializationPreserveBindings()
    {
        auto *gl = m_context.functions();
        QOpenGLFramebufferObject readTarget(16, 16), drawTarget(16, 16);
        QVERIFY(readTarget.isValid() && drawTarget.isValid());
        gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, readTarget.handle());
        gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawTarget.handle());
        GLuint sentinel = 0;
        gl->glGenTextures(1, &sentinel);
        gl->glBindTexture(GL_TEXTURE_2D, sentinel);
        CanvasRenderer renderer;
        QVERIFY2(renderer.initialize(), qPrintable(renderer.lastError()));
        GLint binding = 0;
        gl->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &binding);
        QCOMPARE(GLuint(binding), readTarget.handle());
        gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &binding);
        QCOMPARE(GLuint(binding), drawTarget.handle());
        gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
        QCOMPARE(GLuint(binding), sentinel);
        auto *canvas = renderer.canvas();
        QCOMPARE(canvas->size(), QSize(1920, 1080));
        QCOMPARE(canvas->format().internalTextureFormat(), GLenum(GL_RGBA8));
        QCOMPARE(canvas->format().samples(), 0);
        QCOMPARE(canvas->attachment(), QOpenGLFramebufferObject::NoAttachment);
        QVERIFY(renderer.render({}));
        QCOMPARE(renderer.canvas(), canvas);
        QCOMPARE(canvas->toImage().pixelColor(960, 540), QColor(Qt::black));
        gl->glBindTexture(GL_TEXTURE_2D, canvas->texture());
        for (GLenum parameter :
             {GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T}) {
            GLint value = 0;
            gl->glGetTexParameteriv(GL_TEXTURE_2D, parameter, &value);
            QCOMPARE(value, parameter == GL_TEXTURE_MIN_FILTER || parameter == GL_TEXTURE_MAG_FILTER
                                ? GLint(GL_LINEAR)
                                : GLint(GL_CLAMP_TO_EDGE));
        }
        const GLuint texture = canvas->texture(), fbo = canvas->handle();
        renderer.cleanup();
        QVERIFY(!renderer.canvas());
        QVERIFY(!gl->glIsTexture(texture));
        QVERIFY(!gl->glIsFramebuffer(fbo));
        gl->glBindTexture(GL_TEXTURE_2D, 0);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gl->glDeleteTextures(1, &sentinel);
        QCOMPARE(gl->glGetError(), GLenum(GL_NO_ERROR));
    }

    void textureReuseFilterAlphaAndRemoval()
    {
        CanvasRenderer renderer;
        QVERIFY(renderer.initialize());
        QImage image(32, 16, QImage::Format_RGBA8888);
        image.fill(QColor(128, 0, 0, 128));
        PreviewSource source{"image", image, true, {{960, 540}, {1920, 1080}, 0}};
        QVERIFY(renderer.render({source}));
        const GLuint texture = renderer.m_textures.at(source.id).texture->textureId();
        const auto *canvas = renderer.canvas();
        SourceFilter first{"first"}, second{"second"};
        first.brightness = .1f;
        second.contrast = .5f;
        source.filters = {first, second};
        QVERIFY(renderer.render({source}));
        QCOMPARE(renderer.m_textures.at(source.id).texture->textureId(), texture);
        QCOMPARE(renderer.m_filterFbos[0]->size(), image.size());
        QCOMPARE(renderer.m_filterFbos[1]->size(), image.size());
        QVERIFY(qAbs(renderer.m_filterFbos[1]->toImage().pixelColor(8, 4).alpha() - 128) <= 1);
        source.visible = false;
        QVERIFY(renderer.render({source}));
        QCOMPARE(renderer.canvas(), canvas);
        QCOMPARE(renderer.m_textures.at(source.id).texture->textureId(), texture);
        QVERIFY(renderer.render({}));
        QVERIFY(renderer.m_textures.empty());
        QVERIFY(!m_context.functions()->glIsTexture(texture));
        renderer.cleanup();
        QVERIFY(!renderer.m_filterFbos[0] && !renderer.m_filterFbos[1]);
        QCOMPARE(m_context.functions()->glGetError(), GLenum(GL_NO_ERROR));
    }

    void independentWorkerRendersWithoutGuiEventsAndRestarts()
    {
        Compositor compositor;
        QSignalSpy errors(&compositor, &Compositor::renderingFailed);
        compositor.setSources({solid(Qt::red)});
        QVERIFY(compositor.startRendering(&m_context));
        QCOMPARE(compositor.captureImage().pixelColor(960, 540), QColor(Qt::red));
        // Bursty diagnostic reads must not postpone normal output deadlines.
        for (int i = 0; i < 12; ++i)
            QVERIFY(!compositor.captureImage().isNull());
        auto frame = compositor.latestFrame();
        QVERIFY(frame);
        const quint64 sequence = frame->sequence;
        const qint64 timestamp = frame->timestamp;
        frame.reset();
        QThread::msleep(180); // Deliberately do not dispatch GUI events.
        frame = compositor.latestFrame();
        QVERIFY(frame && frame->sequence >= sequence + 3);
        QVERIFY(frame->timestamp > timestamp);
        frame.reset();
        compositor.setSources({solid(Qt::blue)});
        QCOMPARE(compositor.captureImage().pixelColor(960, 540), QColor(Qt::blue));
        compositor.stopRendering();
        QVERIFY(!compositor.isRunning());
        QVERIFY(!compositor.latestFrame());
        QVERIFY(compositor.captureImage().isNull());
        QVERIFY(compositor.startRendering(&m_context));
        QCOMPARE(compositor.captureImage().pixelColor(960, 540), QColor(Qt::blue));
        compositor.stopRendering();
        QCoreApplication::processEvents();
        QCOMPARE(errors.count(), 0);
    }

    void outputLeasesAreImmutableAndBounded()
    {
        Compositor compositor;
        QVERIFY(compositor.startRendering(&m_context));
        std::array<std::shared_ptr<const CompositionFrame>, 3> held;
        const QColor colors[]{Qt::red, Qt::green, Qt::blue};
        for (int i = 0; i < 3; ++i) {
            compositor.setSources({solid(colors[i])});
            QCOMPARE(compositor.captureImage().pixelColor(960, 540), colors[i]);
            held[i] = compositor.latestFrame();
            QVERIFY(held[i]);
            for (int j = 0; j < i; ++j)
                QVERIFY(held[i]->texture != held[j]->texture);
        }
        compositor.setSources({solid(Qt::yellow)});
        QCOMPARE(compositor.captureImage().pixelColor(960, 540), QColor(Qt::yellow));
        QThread::msleep(100);
        QCOMPARE(compositor.latestFrame()->sequence, held[2]->sequence);
        // Attaching a shared texture to this context's FBO verifies that neither
        // hidden-preview output nor a slow consumer can observe a slot overwrite.
        QVERIFY(m_context.makeCurrent(&m_surface));
        {
            QOpenGLFramebufferObject target(1, 1);
            auto *gl = m_context.functions();
            for (int i = 0; i < 3; ++i) {
                QVERIFY(target.bind());
                gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                           held[i]->texture, 0);
                QCOMPARE(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER),
                         GLenum(GL_FRAMEBUFFER_COMPLETE));
                unsigned char pixel[4]{};
                gl->glReadPixels(960, 540, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
                QCOMPARE(QColor(pixel[0], pixel[1], pixel[2]), colors[i]);
            }
            gl->glFinish();
        }
        held[0].reset();
        compositor.captureImage();
        QVERIFY(compositor.latestFrame()->sequence > held[2]->sequence);
        for (auto &frame : held)
            frame.reset();
        const GLuint lastTexture = compositor.latestFrame()->texture;
        compositor.stopRendering();
        QVERIFY(!m_context.functions()->glIsTexture(lastTexture));
    }

private:
    static PreviewSource solid(const QColor &color)
    {
        QImage image(16, 16, QImage::Format_RGBA8888);
        image.fill(color);
        return {"image", image, true, {{960, 540}, {1920, 1080}, 0}};
    }

    QOpenGLContext m_context;
    QOffscreenSurface m_surface;
};

int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    CanvasRendererTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_canvasrenderer.moc"
