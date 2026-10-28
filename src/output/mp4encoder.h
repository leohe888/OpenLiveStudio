#ifndef MP4ENCODER_H
#define MP4ENCODER_H
#include <QFile>
#include <QImage>
#include <QVector>
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVStream;
struct AVIOContext;
struct SwsContext;

// Worker-thread-only FFmpeg encoder. Failures throw a user-facing QString.
// Never overwrites a path; the owning session selects a unique filename.
class Mp4Encoder {
public:
    Mp4Encoder() = default;
    ~Mp4Encoder();
    void open(const QString &path, int crf = 20);
    void video(const QImage &image, qint64 frameIndex);
    void audio(const QVector<float> &samples);
    void finish();
    bool completed() const { return m_completed; }
private:
    void encode(AVCodecContext *codec, AVStream *stream, AVFrame *frame);
    void encodeAudio(int count);
    static int write(void *opaque, const unsigned char *data, int size);
    static qint64 seek(void *opaque, qint64 offset, int whence);
    QFile m_file;
    AVFormatContext *m_format = nullptr;
    AVIOContext *m_io = nullptr;
    AVCodecContext *m_video = nullptr, *m_audio = nullptr;
    AVFrame *m_videoFrame = nullptr, *m_audioFrame = nullptr;
    AVStream *m_videoStream = nullptr, *m_audioStream = nullptr;
    SwsContext *m_scale = nullptr;
    QVector<float> m_pending;
    qint64 m_audioSamples = 0, m_videoFrames = 0;
    bool m_created = false, m_completed = false;
};
#endif
