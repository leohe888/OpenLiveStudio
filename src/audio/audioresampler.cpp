#include "audio/audioresampler.h"
#include "audio/audiobuffer.h"
extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

AudioResampler::~AudioResampler()
{
    swr_free(&m_context);
}

bool AudioResampler::configure(int inputSampleRate, int inputChannels, PcmFormat format,
                               quint64 channelMask, QString *error)
{
    swr_free(&m_context);
    if (inputSampleRate < 8000 || inputSampleRate > 384000 || inputChannels < 1 ||
        inputChannels > 32) {
        *error = QStringLiteral("不支持此音频采样率或声道数。");
        return false;
    }
    AVSampleFormat sampleFormat = AV_SAMPLE_FMT_NONE;
    switch (format) {
    case PcmFormat::Unsigned8:
        sampleFormat = AV_SAMPLE_FMT_U8;
        break;
    case PcmFormat::Signed16:
        sampleFormat = AV_SAMPLE_FMT_S16;
        break;
    case PcmFormat::Signed24:
    case PcmFormat::Signed32:
        sampleFormat = AV_SAMPLE_FMT_S32;
        break;
    case PcmFormat::Float32:
        sampleFormat = AV_SAMPLE_FMT_FLT;
        break;
    case PcmFormat::Float64:
        sampleFormat = AV_SAMPLE_FMT_DBL;
        break;
    }
    AVChannelLayout inputLayout{};
    if (channelMask)
        av_channel_layout_from_mask(&inputLayout, channelMask);
    if (inputLayout.nb_channels != inputChannels) {
        av_channel_layout_uninit(&inputLayout);
        av_channel_layout_default(&inputLayout, inputChannels);
    }
    AVChannelLayout outputLayout = AV_CHANNEL_LAYOUT_STEREO;
    int result =
        swr_alloc_set_opts2(&m_context, &outputLayout, AV_SAMPLE_FMT_FLT, AudioFormat::rate,
                            &inputLayout, sampleFormat, inputSampleRate, 0, nullptr);
    av_channel_layout_uninit(&inputLayout);
    if (result >= 0)
        result = av_opt_set_int(m_context, "flags", SWR_FLAG_RESAMPLE, 0);
    if (result >= 0 && inputChannels == 1) {
        const double matrix[]{1, 1}; // 单声道等幅复制到左右声道，避免默认混音矩阵降低音量。
        result = swr_set_matrix(m_context, matrix, 1);
    }
    if (result >= 0)
        result = swr_init(m_context);
    if (result < 0) {
        *error = QStringLiteral("无法初始化音频格式转换（%1）。").arg(result);
        swr_free(&m_context);
        return false;
    }
    m_inputSampleRate = inputSampleRate;
    m_inputChannels = inputChannels;
    m_inputFormat = format;
    return true;
}

QVector<float> AudioResampler::convert(const unsigned char *data, int inputFrameCount,
                                       QString *error)
{
    if (!m_context || !data || inputFrameCount <= 0 || inputFrameCount > m_inputSampleRate) {
        *error = QStringLiteral("音频采样块无效或过大。");
        return {};
    }
    QVector<qint32> expandedPcmSamples;
    if (m_inputFormat == PcmFormat::Signed24) {
        expandedPcmSamples.resize(inputFrameCount * m_inputChannels);
        for (int i = 0; i < expandedPcmSamples.size(); ++i) {
            const auto *bytes = data + i * 3;
            qint32 value = bytes[0] | (qint32(bytes[1]) << 8) | (qint32(bytes[2]) << 16);
            if (value & 0x800000)
                value -= 0x1000000;
            expandedPcmSamples[i] = value * 256;
        }
        data = reinterpret_cast<const unsigned char *>(expandedPcmSamples.constData());
    }
    const int outputFrameCapacity = swr_get_out_samples(m_context, inputFrameCount);
    if (outputFrameCapacity < 0 || outputFrameCapacity > AudioFormat::rate * 2) {
        *error = QStringLiteral("音频重采样缓冲异常。");
        return {};
    }
    QVector<float> outputSamples(outputFrameCapacity * 2);
    auto *destination = reinterpret_cast<unsigned char *>(outputSamples.data());
    const int outputFrameCount =
        swr_convert(m_context, &destination, outputFrameCapacity, &data, inputFrameCount);
    if (outputFrameCount < 0) {
        *error = QStringLiteral("音频重采样失败（%1）。").arg(outputFrameCount);
        return {};
    }
    outputSamples.resize(outputFrameCount * 2);
    return outputSamples;
}

bool AudioResampler::reset()
{
    if (!m_context)
        return false;
    swr_close(m_context);
    return swr_init(m_context) >= 0;
}

qint64 AudioResampler::delayFrames() const
{
    return m_context ? swr_get_delay(m_context, m_inputSampleRate) : 0;
}

bool AudioResampler::compensate(int sampleDelta, int compensationDistanceFrames)
{
    return m_context &&
           swr_set_compensation(m_context, sampleDelta, compensationDistanceFrames) >= 0;
}
