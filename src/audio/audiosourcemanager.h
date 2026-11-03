#ifndef AUDIOSOURCEMANAGER_H
#define AUDIOSOURCEMANAGER_H
#include "audio/audiocaptureworker.h"
#include "audio/audiomixer.h"
#include "audio/audiosource.h"
#include <QObject>
#include <QTimer>
#include <vector>

class AudioSourceManager : public QObject
{
    Q_OBJECT
public:
    explicit AudioSourceManager(QObject *parent = nullptr);
    ~AudioSourceManager() override;
    QList<AudioSource> sources() const;
    // 指针仅在下一次修改前有效；信号回调可能触发修改，不可跨回调持有。
    const AudioSource *source(const QString &sourceId) const;

    int count() const
    {
        return int(m_entries.size());
    }

    QString currentSourceId() const
    {
        return m_currentSourceId;
    }

    QList<AudioDevice> devices() const
    {
        return m_devices;
    }

    bool refreshing() const
    {
        return m_refreshing;
    }

    QString catalogError() const
    {
        return m_catalogError;
    }

    qreal masterPeakLeft() const
    {
        return m_masterPeak.left;
    }

    qreal masterPeakRight() const
    {
        return m_masterPeak.right;
    }

    bool masterClipped() const
    {
        return m_clippingHoldTicks > 0;
    }

    void refreshDevices();
    QString addSource(const QString &name, AudioSourceType type, const QString &deviceId);
    bool removeSource(const QString &sourceId);
    bool renameSource(const QString &sourceId, const QString &name);
    bool setVolume(const QString &sourceId, qreal volume);
    bool setMuted(const QString &sourceId, bool muted);
    bool selectSource(const QString &sourceId);
    bool deviceInUse(const QString &deviceId) const;
    QString suggestedSourceName(const QString &baseName) const;
    int activeWorkerCount() const;

    AudioMixer *mixer()
    {
        return &m_mixer;
    }

signals:
    void sourcesChanged();
    void sourceChanged(const QString &sourceId);
    void sourceLevelsChanged(const QString &sourceId);
    void currentSourceIdChanged();
    void devicesChanged();
    void masterLevelsChanged();
    void sourceFailed(const QString &sourceId, const QString &message);

private:
    struct Entry
    {
        AudioSource source;
        std::shared_ptr<AudioPacketQueue> queue;
        std::unique_ptr<AudioCaptureWorker> worker;
    };

    int sourceRow(const QString &sourceId) const;
    bool isSourceNameAvailable(const QString &name, const QString &excludedSourceId = {}) const;
    void updateMixerInputs();
    void refreshSourceStatusAndLevels();
    std::vector<Entry> m_entries;
    std::vector<std::unique_ptr<AudioCaptureWorker>> m_retiringWorkers;
    std::vector<std::unique_ptr<QThread>> m_deviceQueryThreads;
    AudioMixer m_mixer;
    QTimer m_statusPollTimer;
    QString m_currentSourceId, m_catalogError;
    QList<AudioDevice> m_devices;
    bool m_refreshing = false;
    AudioPeak m_masterPeak;
    int m_clippingHoldTicks = 0;
};
#endif
