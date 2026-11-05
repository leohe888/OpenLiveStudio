#ifndef AUDIOMIXER_H
#define AUDIOMIXER_H

#include "audio/audiobuffer.h"
#include <QThread>
#include "util/outputbus.h"
#include <condition_variable>
#include <map>

// 仅按传入的采样时间线混音，不依赖设备或线程，便于用合成音频验证对齐行为。
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

// 生命周期由界面线程管理；stop() 请求退出，析构等待混音线程结束。
// 输入、快照、电平与订阅接口均受锁保护；返回数据按值或共享所有权传递。
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
    // 每个输出独立订阅有界队列；下方的单队列接口仅供诊断和测试使用。
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
    std::deque<std::shared_ptr<const MixedAudioBlock>> m_diagnosticOutputBlocks;
    quint64 m_droppedDiagnosticBlocks = 0, m_discontinuities = 0;
};

#endif
