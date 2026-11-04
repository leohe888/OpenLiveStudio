#ifndef MEDIAENCODER_H
#define MEDIAENCODER_H
#include <QFile>
#include <QImage>
#include <QVector>
#include <cstdint>
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

// 每个实例仅供一个工作线程的一次输出会话使用。
// 本地错误抛出 QString；网络错误抛出已脱敏的 StreamFailure，并标明能否重试。
// 文件以独占新建方式打开，调用方负责选择未占用的路径。
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
    void skipTo(qint64 nextFrame); // 仅直播允许跳过积压；跳转后仍须保持音视频 PTS 单调递增。
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
    static std::int64_t seek(void *opaque, std::int64_t offset, int whence);
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
