#include "audio/audioresampler.h"
#include "audio/audiobuffer.h"
extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

AudioResampler::~AudioResampler() { swr_free(&m_context); }
bool AudioResampler::configure(int rate, int channels, PcmFormat format, quint64 channelMask, QString *error)
{
    swr_free(&m_context);
    if (rate < 8000 || rate > 384000 || channels < 1 || channels > 32) {
        *error = QStringLiteral("不支持此音频采样率或声道数。"); return false;
    }
    AVSampleFormat sampleFormat = AV_SAMPLE_FMT_NONE;
    switch (format) {
    case PcmFormat::Unsigned8: sampleFormat = AV_SAMPLE_FMT_U8; break;
    case PcmFormat::Signed16: sampleFormat = AV_SAMPLE_FMT_S16; break;
    case PcmFormat::Signed24:
    case PcmFormat::Signed32: sampleFormat = AV_SAMPLE_FMT_S32; break;
    case PcmFormat::Float32: sampleFormat = AV_SAMPLE_FMT_FLT; break;
    case PcmFormat::Float64: sampleFormat = AV_SAMPLE_FMT_DBL; break;
    }
    AVChannelLayout input{};
    if (channelMask) av_channel_layout_from_mask(&input, channelMask);
    if (input.nb_channels != channels) { av_channel_layout_uninit(&input); av_channel_layout_default(&input, channels); }
    AVChannelLayout output = AV_CHANNEL_LAYOUT_STEREO;
    int result = swr_alloc_set_opts2(&m_context, &output, AV_SAMPLE_FMT_FLT, AudioFormat::rate,
                                   &input, sampleFormat, rate, 0, nullptr);
    av_channel_layout_uninit(&input);
    if (result >= 0) result = av_opt_set_int(m_context, "flags", SWR_FLAG_RESAMPLE, 0);
    if (result >= 0 && channels == 1) {
        const double matrix[]{1, 1}; // Mono feeds both channels at unity gain.
        result = swr_set_matrix(m_context, matrix, 1);
    }
    if (result >= 0) result = swr_init(m_context);
    if (result < 0) {
        *error = QStringLiteral("无法初始化音频格式转换（%1）。").arg(result);
        swr_free(&m_context); return false;
    }
    m_rate = rate; m_channels = channels; m_format = format;
    return true;
}

QVector<float> AudioResampler::convert(const unsigned char *data, int frames, QString *error)
{
    if (!m_context || !data || frames <= 0 || frames > m_rate) {
        *error = QStringLiteral("音频采样块无效或过大。"); return {};
    }
    QVector<qint32> expanded;
    if (m_format == PcmFormat::Signed24) {
        expanded.resize(frames * m_channels);
        for (int i = 0; i < expanded.size(); ++i) {
            const auto *bytes = data + i * 3;
            qint32 value = bytes[0] | (qint32(bytes[1]) << 8) | (qint32(bytes[2]) << 16);
            if (value & 0x800000) value -= 0x1000000;
            expanded[i] = value * 256;
        }
        data = reinterpret_cast<const unsigned char *>(expanded.constData());
    }
    const int capacity = swr_get_out_samples(m_context, frames);
    if (capacity < 0 || capacity > AudioFormat::rate * 2) {
        *error = QStringLiteral("音频重采样缓冲异常。"); return {};
    }
    QVector<float> output(capacity * 2);
    auto *destination = reinterpret_cast<unsigned char *>(output.data());
    const int count = swr_convert(m_context, &destination, capacity, &data, frames);
    if (count < 0) { *error = QStringLiteral("音频重采样失败（%1）。").arg(count); return {}; }
    output.resize(count * 2);
    return output;
}
bool AudioResampler::reset() { if (!m_context) return false; swr_close(m_context); return swr_init(m_context) >= 0; }
qint64 AudioResampler::delayFrames() const { return m_context ? swr_get_delay(m_context, m_rate) : 0; }
bool AudioResampler::compensate(int delta, int distance) { return m_context && swr_set_compensation(m_context, delta, distance) >= 0; }
