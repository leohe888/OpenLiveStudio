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
    // The pointer is only valid until the next mutation; do not retain it across signals.
    const AudioSource *source(const QString &sourceId) const;

    int count() const
    {
        return int(m_entries.size());
    }

    QString currentSourceId() const
    {
        return m_selected;
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
        return m_master.left;
    }

    qreal masterPeakRight() const
    {
        return m_master.right;
    }

    bool masterClipped() const
    {
        return m_clipTicks > 0;
    }

    void refreshDevices();
    QString addSource(const QString &name, const QString &type, const QString &deviceId);
    bool removeSource(const QString &sourceId);
    bool renameSource(const QString &sourceId, const QString &name);
    bool setVolume(const QString &sourceId, qreal volume);
    bool setMuted(const QString &sourceId, bool muted);
    bool selectSource(const QString &sourceId);
    bool deviceInUse(const QString &deviceId) const;
    QString suggestedSourceName(const QString &base) const;
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

    int rowFor(const QString &sourceId) const;
    bool nameAvailable(const QString &name, const QString &except = {}) const;
    void synchronize();
    void poll();
    std::vector<Entry> m_entries;
    std::vector<std::unique_ptr<AudioCaptureWorker>> m_retired;
    std::vector<std::unique_ptr<QThread>> m_queries;
    AudioMixer m_mixer;
    QTimer m_timer;
    QString m_selected, m_catalogError;
    QList<AudioDevice> m_devices;
    bool m_refreshing = false;
    AudioPeak m_master;
    int m_clipTicks = 0;
};
#endif
