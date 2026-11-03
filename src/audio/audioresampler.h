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
    bool configure(int inputSampleRate, int inputChannels, PcmFormat format, quint64 channelMask,
                   QString *error);
    QVector<float> convert(const unsigned char *data, int inputFrameCount, QString *error);
    bool reset();
    qint64 delayFrames() const;
    bool compensate(int sampleDelta, int compensationDistanceFrames);

private:
    SwrContext *m_context = nullptr;
    int m_inputSampleRate = 0, m_inputChannels = 0;
    PcmFormat m_inputFormat = PcmFormat::Float32;
};
#endif
