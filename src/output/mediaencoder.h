#ifndef MEDIAENCODER_H
#define MEDIAENCODER_H
#include <QFile>
#include <QImage>
#include <QVector>
#include <functional>

struct StreamFailure
{
    QString message;
    bool retryable = false;
};
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVStream;
struct AVIOContext;
struct SwsContext;

// Worker-thread-only, one-shot FFmpeg encoder. Local failures throw QString;
// network failures throw redacted StreamFailure with a retry classification.
// Never overwrites a path; the owning session selects a unique filename.
class MediaEncoder
{
public:
    MediaEncoder() = default;
    MediaEncoder(const MediaEncoder &) = delete;
    MediaEncoder &operator=(const MediaEncoder &) = delete;
    ~MediaEncoder();
    void open(const QString &path, int bitrateKbps = 12000);
    void openStream(const QString &url, int bitrateKbps, std::function<bool()> cancelled,
                    const QString &tcUrl = {});
    qint64 bytesSent() const;

    qint64 outputTimeNs() const
    {
        return m_outputTimeNs;
    }

    qint64 lastConversionNs() const
    {
        return m_lastConversionNs;
    }

    void encodeVideoFrame(const QImage &image, qint64 frameIndex);
    void encodeAudioSamples(const QVector<float> &samples);
    void skipTo(qint64 nextFrame); // Live-only timeline jump, preserving monotonic PTS.
    void finish();

    bool completed() const
    {
        return m_completed;
    }

private:
    void initialize(int bitrateKbps);
    void networkCheck(int result, const char *operation);
    static int interrupted(void *opaque);
    void resetNetworkDeadline();
    void encode(AVCodecContext *codec, AVStream *stream, AVFrame *frame);
    void encodeAudio(int count);
    static int write(void *opaque, const unsigned char *data, int size);
    static qint64 seek(void *opaque, qint64 offset, int whence);
    QFile m_file;
    AVFormatContext *m_format = nullptr;
    AVIOContext *m_io = nullptr;
    AVCodecContext *m_videoCodecContext = nullptr, *m_audioCodecContext = nullptr;
    AVFrame *m_videoFrame = nullptr, *m_audioFrame = nullptr;
    AVStream *m_videoStream = nullptr, *m_audioStream = nullptr;
    SwsContext *m_pixelConversionContext = nullptr;
    QVector<float> m_pendingAudioSamples;
    qint64 m_encodedAudioFrames = 0, m_videoFrames = 0;
    bool m_created = false, m_completed = false;
    bool m_network = false;
    bool m_forceKeyframe = false;
    bool m_expectKeyframe = false;
    qint64 m_deadlineNs = 0;
    qint64 m_outputTimeNs = 0;
    qint64 m_lastConversionNs = 0;
    std::function<bool()> m_cancelled;
};
#endif
