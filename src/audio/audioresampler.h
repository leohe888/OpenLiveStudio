#ifndef AUDIORESAMPLER_H
#define AUDIORESAMPLER_H
#include <QString>
#include <QVector>
struct SwrContext;

enum class PcmFormat {
    Unsigned8,
    Signed16,
    Signed24,
    Signed32,
    Float32,
    Float64
};

class AudioResampler
{
public:
    ~AudioResampler();
    bool configure(int rate, int channels, PcmFormat format, quint64 channelMask, QString *error);
    QVector<float> convert(const unsigned char *data, int frames, QString *error);
    bool reset();
    qint64 delayFrames() const;
    bool compensate(int delta, int distance);

private:
    SwrContext *m_context = nullptr;
    int m_rate = 0, m_channels = 0;
    PcmFormat m_format = PcmFormat::Float32;
};
#endif
