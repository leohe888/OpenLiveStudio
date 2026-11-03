#include "audio/audiomixer.h"
#include "util/mediaclock.h"
#include <chrono>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

qint64 audioClockNs()
{
    return MediaClock::nowNs();
}

MixedAudioBlock AudioMixerCore::mix(const QList<AudioInput> &inputs, qint64 timestampNs,
                                    QHash<QString, AudioPeak> *inputPeaks)
{
    MixedAudioBlock output;
    output.timestampNs = timestampNs;
    output.samples.fill(0, AudioFormat::blockFrames * 2);
    auto *mixedSamples = output.samples.data();
    for (auto it = m_tracks.begin(); it != m_tracks.end();) {
        if (std::none_of(inputs.cbegin(), inputs.cend(), [&](const auto &input) {
                return input.id == it->first;
            }))
            it = m_tracks.erase(it);
        else
            ++it;
    }
    for (const auto &input : inputs) {
        if (!input.queue)
            continue;
        auto batch = input.queue->takeAll();
        auto &track = m_tracks[input.id];
        if (track.queue != input.queue || track.generation != batch.generation)
            track.pending.clear();
        track.queue = input.queue;
        track.generation = batch.generation;
        for (auto &packet : batch.packets) {
            // 设备时间戳可能异常，不能据此扩展时间线或分配缓冲；过远的未来数据和过期数据都丢弃。
            if (packet.timestampNs > timestampNs + 500000000LL ||
                packet.timestampNs + AudioFormat::framesToNs(packet.frames()) <= timestampNs)
                continue;
            track.pending.push_back(std::move(packet));
        }
        while (track.pending.size() > 100)
            track.pending.pop_front();
        QVector<float> samples(AudioFormat::blockFrames * 2, 0);
        auto *inputSamples = samples.data();
        for (auto it = track.pending.begin(); it != track.pending.end();) {
            const qint64 frameOffset = AudioFormat::nsToFrames(it->timestampNs - timestampNs);
            const int begin = int(std::max<qint64>(0, frameOffset));
            const int end =
                int(std::min<qint64>(AudioFormat::blockFrames, frameOffset + it->frames()));
            if (end > begin)
                std::copy_n(it->samples.constData() + (begin - frameOffset) * 2, (end - begin) * 2,
                            inputSamples + begin * 2);
            if (frameOffset + it->frames() <= AudioFormat::blockFrames)
                it = track.pending.erase(it);
            else
                ++it;
        }
        AudioPeak peak;
        const float gain =
            !input.muted && std::isfinite(input.gain) ? std::clamp(input.gain, 0.f, 1.f) : 0;
        for (int frame = 0; frame < AudioFormat::blockFrames; ++frame) {
            const float left = std::isfinite(inputSamples[frame * 2]) ? inputSamples[frame * 2] : 0;
            const float right =
                std::isfinite(inputSamples[frame * 2 + 1]) ? inputSamples[frame * 2 + 1] : 0;
            peak.left = std::max(peak.left, std::abs(left));
            peak.right = std::max(peak.right, std::abs(right));
            mixedSamples[frame * 2] += left * gain;
            mixedSamples[frame * 2 + 1] += right * gain;
        }
        if (inputPeaks)
            (*inputPeaks)[input.id].add(peak);
    }
    for (int frame = 0; frame < AudioFormat::blockFrames; ++frame) {
        output.peak.left = std::max(output.peak.left, std::abs(mixedSamples[frame * 2]));
        output.peak.right = std::max(output.peak.right, std::abs(mixedSamples[frame * 2 + 1]));
    }
    output.clipped = output.peak.left > 1 || output.peak.right > 1;
    for (auto &sample : output.samples)
        sample = std::clamp(sample, -1.f, 1.f);
    return output;
}

AudioMixer::AudioMixer(QObject *parent) : QThread(parent)
{
    setObjectName(QStringLiteral("AudioMixer"));
}

AudioMixer::~AudioMixer()
{
    stop();
    wait();
}

void AudioMixer::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
}

void AudioMixer::setInputs(const QList<AudioInput> &inputs)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_inputs = inputs;
}

QList<AudioInput> AudioMixer::inputs() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_inputs;
}

std::shared_ptr<const MixedAudioBlock> AudioMixer::latestBlock() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latest;
}

AudioMixer::Levels AudioMixer::takeLevels()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::exchange(m_levels, {});
}

std::shared_ptr<OutputTap<std::shared_ptr<const MixedAudioBlock>>> AudioMixer::subscribeOutput()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bus.subscribe(100);
}

void AudioMixer::setOutputEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_outputEnabled = enabled;
    m_diagnosticOutputBlocks.clear();
    m_droppedDiagnosticBlocks = 0;
}

std::deque<std::shared_ptr<const MixedAudioBlock>> AudioMixer::takeOutput()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::exchange(m_diagnosticOutputBlocks, {});
}

quint64 AudioMixer::droppedOutputBlocks() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_droppedDiagnosticBlocks;
}

quint64 AudioMixer::discontinuities() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_discontinuities;
}

void AudioMixer::run()
{
    setPriority(QThread::HighPriority);
    AudioMixerCore core;
    using Clock = std::chrono::steady_clock;
    const auto period = std::chrono::milliseconds(10);
    auto deadline = Clock::now();
    const qint64 originNs = audioClockNs() - 60000000LL; // 为不同设备的采集抖动预留 60 ms 对齐窗口。
    qint64 audioFramePosition = 0;
    quint64 sequence = 0;
    const int pauseOnce =
        std::clamp(qEnvironmentVariableIntValue("OPENLIVE_AUDIO_MIXER_PAUSE_ONCE_MS"), 0, 1200);
    while (true) {
        QList<AudioInput> inputs;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait_until(lock, deadline, [this] {
                return m_stop;
            });
            if (m_stop)
                break;
            inputs = m_inputs;
        }
        // 短暂调度延迟仍在输出同步窗口内时保留采样并追赶，避免把短暂停顿变成静音缺口。
        if (Clock::now() > deadline + period * 10) {
            const auto skippedBlocks = (Clock::now() - deadline) / period;
            audioFramePosition += skippedBlocks * AudioFormat::blockFrames;
            deadline += period * skippedBlocks;
            core.clear();
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_discontinuities;
        }
        Levels levels;
        auto block = std::make_shared<MixedAudioBlock>(core.mix(
            inputs, originNs + AudioFormat::framesToNs(audioFramePosition), &levels.sources));
        block->sequence = ++sequence;
        levels.master = block->peak;
        levels.clipped = block->clipped;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_latest = block;
            for (auto it = levels.sources.cbegin(); it != levels.sources.cend(); ++it)
                m_levels.sources[it.key()].add(it.value());
            m_levels.master.add(levels.master);
            m_levels.clipped |= levels.clipped;
            m_bus.publish(block);
            if (m_outputEnabled) {
                m_diagnosticOutputBlocks.push_back(std::move(block));
                if (m_diagnosticOutputBlocks.size() > 100) {
                    m_diagnosticOutputBlocks.pop_front();
                    ++m_droppedDiagnosticBlocks;
                }
            }
        }
        audioFramePosition += AudioFormat::blockFrames;
        deadline += period;
        if (pauseOnce && sequence == 20)
            QThread::msleep(unsigned(pauseOnce));
    }
}
