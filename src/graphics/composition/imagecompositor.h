#ifndef IMAGECOMPOSITOR_H
#define IMAGECOMPOSITOR_H

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>
#include <memory>

class QOffscreenSurface;
class QOpenGLContext;

// Input pixels are immutable snapshots, independent of GUI models or files.
// initialize/render/texture/readback/release/destruction belong to the producer
// thread. Only acquireFrame/returnFrame may run on a consumer thread.
class ImageCompositor
{
public:
    struct Layer { QImage image; bool visible = true; };
    struct Frame {
        unsigned int texture = 0;
        QSize size;
        quint64 token = 0;
        // Premultiplied RGBA preview, with the same size/orientation as texture.
        // Both textures belong to this single lease and must be returned together.
        unsigned int previewTexture = 0;
    };
    static constexpr int BufferCount = 3;

    ImageCompositor();
    ~ImageCompositor();
    ImageCompositor(const ImageCompositor &) = delete;
    ImageCompositor &operator=(const ImageCompositor &) = delete;

    // Surface creation/destruction belongs to the GUI thread. It and the optional
    // sharing context must outlive this session. Sharing requires GL sync support.
    bool initialize(QOffscreenSurface *surface, QString *error = nullptr,
                    QOpenGLContext *shareContext = nullptr);
    // Topmost first, fit/center without cropping. False with an empty error means
    // all buffers are in use: skip production, without waiting or allocating more.
    // Invalid input invalidates texture()/readback(), not already acquired frames.
    bool render(const QList<Layer> &layers, const QSize &canvas = QSize(1920, 1080),
                QString *error = nullptr);
    // Producer diagnostics only; borrowed until next render/release.
    unsigned int texture() const;
    QImage readback(QString *error = nullptr);

    // A compatible sharing context must be CURRENT on this thread. Acquires the
    // latest unconsumed frame and queues a GPU wait (never a CPU completion wait).
    // Bind either texture in the consumer AFTER acquiring it. Zero token with empty
    // error means no new frame. One consumer per frame.
    Frame acquireFrame(QString *error = nullptr);
    // Call after all reads have been submitted, with the SAME consumer context
    // current. Inserts and flushes a completion fence; no further use of this token.
    // Copies of Frame do not extend ownership; stale/double returns are rejected.
    bool returnFrame(const Frame &frame, QString *error = nullptr);
    // Stop consumption and return every acquired frame first. Outstanding leases
    // cause a recoverable failure; destruction with outstanding leases is an error.
    bool release(QString *error = nullptr);

private:
    bool makeCurrent(QString *error);
    class Private;
    std::unique_ptr<Private> d;
};

#endif // IMAGECOMPOSITOR_H
