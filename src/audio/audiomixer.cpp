#include "audiomixer.h"
#include "core/mediaclock.h"
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
    for (auto it = m_tracks.begin(); it != m_tracks.end();) {
        if (std::none_of(inputs.cbegin(), inputs.cend(), [&](const auto &input) { return input.id == it->first; }))
            it = m_tracks.erase(it);
        else ++it;
    }
    for (const auto &input : inputs) {
        if (!input.queue) continue;
        auto batch = input.queue->take();
        auto &track = m_tracks[input.id];
        if (track.queue != input.queue || track.generation != batch.generation) track.pending.clear();
        track.queue = input.queue;
        track.generation = batch.generation;
        for (auto &packet : batch.packets) {
            // Reject wildly future timestamps and discard stale audio, never
            // extend the timeline or allocate according to a device timestamp.
            if (packet.timestampNs > timestampNs + 500000000LL
                || packet.timestampNs + AudioFormat::framesToNs(packet.frames()) <= timestampNs) continue;
            track.pending.push_back(std::move(packet));
        }
        while (track.pending.size() > 100) track.pending.pop_front();
        QVector<float> samples(AudioFormat::blockFrames * 2, 0);
        for (auto it = track.pending.begin(); it != track.pending.end();) {
            const qint64 offset = AudioFormat::nsToFrames(it->timestampNs - timestampNs);
            const int begin = int(std::max<qint64>(0, offset));
            const int end = int(std::min<qint64>(AudioFormat::blockFrames, offset + it->frames()));
            for (int frame = begin; frame < end; ++frame) {
                const int source = int(frame - offset) * 2;
                samples[frame * 2] = it->samples[source];
                samples[frame * 2 + 1] = it->samples[source + 1];
            }
            if (offset + it->frames() <= AudioFormat::blockFrames) it = track.pending.erase(it);
            else ++it;
        }
        AudioPeak peak;
        const float gain = !input.muted && std::isfinite(input.gain) ? std::clamp(input.gain, 0.f, 1.f) : 0;
        for (int frame = 0; frame < AudioFormat::blockFrames; ++frame) {
            const float left = std::isfinite(samples[frame * 2]) ? samples[frame * 2] : 0;
            const float right = std::isfinite(samples[frame * 2 + 1]) ? samples[frame * 2 + 1] : 0;
            peak.left = std::max(peak.left, std::abs(left));
            peak.right = std::max(peak.right, std::abs(right));
            output.samples[frame * 2] += left * gain;
            output.samples[frame * 2 + 1] += right * gain;
        }
        if (inputPeaks) (*inputPeaks)[input.id].add(peak);
    }
    for (int frame = 0; frame < AudioFormat::blockFrames; ++frame) {
        output.peak.left = std::max(output.peak.left, std::abs(output.samples[frame * 2]));
        output.peak.right = std::max(output.peak.right, std::abs(output.samples[frame * 2 + 1]));
    }
    output.clipped = output.peak.left > 1 || output.peak.right > 1;
    for (auto &sample : output.samples) sample = std::clamp(sample, -1.f, 1.f);
    return output;
}

AudioMixer::AudioMixer(QObject *parent) : QThread(parent) { setObjectName(QStringLiteral("AudioMixer")); }
AudioMixer::~AudioMixer() { stop(); wait(); }
void AudioMixer::stop() { { std::lock_guard<std::mutex> lock(m_mutex); m_stop = true; } m_wake.notify_all(); }
void AudioMixer::setInputs(const QList<AudioInput> &inputs) { std::lock_guard<std::mutex> lock(m_mutex); m_inputs = inputs; }
QList<AudioInput> AudioMixer::inputs() const { std::lock_guard<std::mutex> lock(m_mutex); return m_inputs; }
std::shared_ptr<const MixedAudioBlock> AudioMixer::latestBlock() const { std::lock_guard<std::mutex> lock(m_mutex); return m_latest; }
AudioMixer::Levels AudioMixer::takeLevels() { std::lock_guard<std::mutex> lock(m_mutex); return std::exchange(m_levels, {}); }
void AudioMixer::setOutputEnabled(bool enabled) { std::lock_guard<std::mutex> lock(m_mutex); m_outputEnabled = enabled; m_output.clear(); m_dropped = 0; }
std::deque<std::shared_ptr<const MixedAudioBlock>> AudioMixer::takeOutput() { std::lock_guard<std::mutex> lock(m_mutex); return std::exchange(m_output, {}); }
quint64 AudioMixer::droppedOutputBlocks() const { std::lock_guard<std::mutex> lock(m_mutex); return m_dropped; }
quint64 AudioMixer::discontinuities() const { std::lock_guard<std::mutex> lock(m_mutex); return m_discontinuities; }

void AudioMixer::run()
{
    AudioMixerCore core;
    using Clock = std::chrono::steady_clock;
    const auto period = std::chrono::milliseconds(10);
    auto deadline = Clock::now();
    const qint64 origin = audioClockNs() - 60000000LL; // 60 ms input jitter allowance.
    qint64 samplePosition = 0;
    quint64 sequence = 0;
    while (true) {
        QList<AudioInput> inputs;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait_until(lock, deadline, [this] { return m_stop; });
            if (m_stop) break;
            inputs = m_inputs;
        }
        if (Clock::now() > deadline + period * 4) {
            const auto skipped = (Clock::now() - deadline) / period;
            samplePosition += skipped * AudioFormat::blockFrames;
            deadline += period * skipped;
            core.clear();
            std::lock_guard<std::mutex> lock(m_mutex); ++m_discontinuities;
        }
        Levels levels;
        auto block = std::make_shared<MixedAudioBlock>(core.mix(inputs, origin + AudioFormat::framesToNs(samplePosition), &levels.sources));
        block->sequence = ++sequence;
        levels.master = block->peak; levels.clipped = block->clipped;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_latest = block;
            for (auto it = levels.sources.cbegin(); it != levels.sources.cend(); ++it) m_levels.sources[it.key()].add(it.value());
            m_levels.master.add(levels.master); m_levels.clipped |= levels.clipped;
            if (m_outputEnabled) {
                m_output.push_back(std::move(block));
                if (m_output.size() > 100) { m_output.pop_front(); ++m_dropped; }
            }
        }
        samplePosition += AudioFormat::blockFrames;
        deadline += period;
    }
}
