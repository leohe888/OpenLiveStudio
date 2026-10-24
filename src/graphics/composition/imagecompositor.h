#ifndef IMAGECOMPOSITOR_H
#define IMAGECOMPOSITOR_H

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>
#include <memory>

class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;

// No GUI models or file access: callers supply an immutable pixel snapshot.
// All methods and destruction must run on the thread that calls initialize().
class ImageCompositor
{
public:
    struct Layer { QImage image; bool visible = true; };

    ImageCompositor();
    ~ImageCompositor();
    ImageCompositor(const ImageCompositor &) = delete;
    ImageCompositor &operator=(const ImageCompositor &) = delete;

    // The caller creates/destroys the surface on the GUI thread and keeps it
    // alive until release(). The context is created on the calling thread.
    bool initialize(QOffscreenSurface *surface, QString *error = nullptr);
    // Topmost layer first; each image fits the canvas, centered without cropping.
    // A failed render invalidates texture(). Null hidden images are allowed.
    bool render(const QList<Layer> &layers, const QSize &canvas = QSize(1920, 1080),
                QString *error = nullptr);
    // Borrowed GL name, valid until next render/release; no cross-context sync yet.
    unsigned int texture() const;
    // Diagnostic/test readback only. Production preview must use shared textures.
    QImage readback(QString *error = nullptr);
    void release();

private:
    bool makeCurrent(QString *error);
    QOffscreenSurface *m_surface = nullptr;
    std::unique_ptr<QOpenGLContext> m_context;
    std::unique_ptr<QOpenGLFramebufferObject> m_framebuffer;
    bool m_hasFrame = false;
};

#endif // IMAGECOMPOSITOR_H
