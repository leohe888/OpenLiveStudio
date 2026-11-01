#ifndef AUDIOMIXER_H
#define AUDIOMIXER_H

#include "audio/audiobuffer.h"
#include <QThread>
#include "core/outputbus.h"
#include <condition_variable>
#include <map>

// Deterministic sample-timeline mixer, also used by synthetic audio tests.
class AudioMixerCore
{
public:
    MixedAudioBlock mix(const QList<AudioInput> &inputs, qint64 timestampNs,
                        QHash<QString, AudioPeak> *inputPeaks = nullptr);

    void clear()
    {
        m_tracks.clear();
    }

private:
    struct Track
    {
        std::shared_ptr<AudioPacketQueue> queue;
        quint64 generation = 0;
        std::deque<AudioBlock> pending;
    };

    std::map<QString, Track> m_tracks;
};

class AudioMixer : public QThread
{
    Q_OBJECT
public:
    explicit AudioMixer(QObject *parent = nullptr);
    ~AudioMixer() override;
    void stop();
    void setInputs(const QList<AudioInput> &inputs);
    QList<AudioInput> inputs() const;
    std::shared_ptr<const MixedAudioBlock> latestBlock() const;

    struct Levels
    {
        QHash<QString, AudioPeak> sources;
        AudioPeak master;
        bool clipped = false;
    };

    Levels takeLevels();
    // Independent bounded subscriptions; legacy diagnostic tap below.
    std::shared_ptr<OutputTap<std::shared_ptr<const MixedAudioBlock>>> subscribeOutput();
    void setOutputEnabled(bool enabled);
    std::deque<std::shared_ptr<const MixedAudioBlock>> takeOutput();
    quint64 droppedOutputBlocks() const;
    quint64 discontinuities() const;

protected:
    void run() override;

private:
    OutputBus<std::shared_ptr<const MixedAudioBlock>> m_bus;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false, m_outputEnabled = false;
    QList<AudioInput> m_inputs;
    std::shared_ptr<const MixedAudioBlock> m_latest;
    Levels m_levels;
    std::deque<std::shared_ptr<const MixedAudioBlock>> m_output;
    quint64 m_dropped = 0, m_discontinuities = 0;
};

#endif
