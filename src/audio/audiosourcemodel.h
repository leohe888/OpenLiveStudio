#ifndef AUDIOSOURCEMODEL_H
#define AUDIOSOURCEMODEL_H
#include "audiocaptureworker.h"
#include "audiomixer.h"
#include <QAbstractListModel>
#include <QTimer>
#include <vector>

class AudioSourceModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString currentSourceId READ currentSourceId NOTIFY currentSourceIdChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY devicesChanged)
    Q_PROPERTY(QString catalogError READ catalogError NOTIFY devicesChanged)
    Q_PROPERTY(qreal masterPeakLeft READ masterPeakLeft NOTIFY masterLevelsChanged)
    Q_PROPERTY(qreal masterPeakRight READ masterPeakRight NOTIFY masterLevelsChanged)
    Q_PROPERTY(bool masterClipped READ masterClipped NOTIFY masterLevelsChanged)
public:
    enum Role { IdRole = Qt::UserRole + 1, NameRole, TypeRole, DeviceIdRole, DeviceNameRole,
                VolumeRole, MutedRole, StateRole, ErrorRole, PeakLeftRole, PeakRightRole };
    explicit AudioSourceModel(QObject *parent = nullptr);
    ~AudioSourceModel() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return rowCount(); }
    QString currentSourceId() const { return m_selected; }
    QVariantList devices() const { return m_devices; }
    bool refreshing() const { return m_refreshing; }
    QString catalogError() const { return m_catalogError; }
    qreal masterPeakLeft() const { return m_master.left; }
    qreal masterPeakRight() const { return m_master.right; }
    bool masterClipped() const { return m_clipTicks > 0; }
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE QString addSource(const QString &name, const QString &type, const QString &deviceId);
    Q_INVOKABLE bool removeSource(const QString &id);
    Q_INVOKABLE bool renameSource(const QString &id, const QString &name);
    Q_INVOKABLE bool setVolume(const QString &id, qreal volume);
    Q_INVOKABLE bool setMuted(const QString &id, bool muted);
    Q_INVOKABLE bool selectSource(const QString &id);
    Q_INVOKABLE bool deviceInUse(const QString &deviceId) const;
    Q_INVOKABLE QString suggestedSourceName(const QString &base) const;
    Q_INVOKABLE QVariantMap source(const QString &id) const;
    int activeWorkerCount() const;
    AudioMixer *mixer() { return &m_mixer; }
signals:
    void countChanged();
    void currentSourceIdChanged();
    void devicesChanged();
    void masterLevelsChanged();
    void sourceError(const QString &id, const QString &message);
private:
    struct Entry {
        QString id, name, type, deviceId, deviceName;
        float volume = 1;
        bool muted = false;
        AudioPeak peak;
        AudioCaptureWorker::Status status;
        std::shared_ptr<AudioPacketQueue> queue;
        std::unique_ptr<AudioCaptureWorker> worker;
    };
    int rowFor(const QString &id) const;
    bool nameAvailable(const QString &name, const QString &except = {}) const;
    void synchronize();
    void poll();
    std::vector<Entry> m_entries;
    std::vector<std::unique_ptr<AudioCaptureWorker>> m_retired;
    std::vector<std::unique_ptr<QThread>> m_queries;
    AudioMixer m_mixer;
    QTimer m_timer;
    QString m_selected, m_catalogError;
    QVariantList m_devices;
    bool m_refreshing = false;
    AudioPeak m_master;
    int m_clipTicks = 0;
};
#endif
