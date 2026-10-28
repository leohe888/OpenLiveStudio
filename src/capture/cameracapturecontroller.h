#ifndef CAMERACAPTURECONTROLLER_H
#define CAMERACAPTURECONTROLLER_H
#include "cameracaptureworker.h"
#include "preview/previewsource.h"
#include <QObject>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <map>
#include <vector>
class SourceListModel;
class CameraCaptureController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY devicesChanged)
    Q_PROPERTY(QString catalogError READ catalogError NOTIFY devicesChanged)
public:
    explicit CameraCaptureController(SourceListModel &model, QObject *parent = nullptr);
    ~CameraCaptureController() override;
    QVariantList devices() const { return m_devices; }
    bool refreshing() const { return m_refreshing; }
    QString catalogError() const { return m_catalogError; }
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void requestModes(const QString &deviceId);
    Q_INVOKABLE QVariantList modes(const QString &deviceId) const { return m_modes.value(deviceId); }
    Q_INVOKABLE QString addCamera(const QString &name, const QString &deviceId, const QVariantMap &mode);
    Q_INVOKABLE bool applyProperties(const QString &sourceId, const QString &deviceId, const QVariantMap &mode);
    Q_INVOKABLE bool retry(const QString &sourceId);
    void setOtherSources(const QList<PreviewSource> &sources);
    int activeWorkerCount() const;
signals:
    void devicesChanged();
    void modesChanged(const QString &deviceId, const QString &error);
    void sourcesChanged(const QList<PreviewSource> &sources);
    void sourceError(const QString &id, const QString &message);
private:
    struct Entry {
        CameraConfig config;
        bool failed = false;
        quint64 generation = 0;
        std::unique_ptr<CameraCaptureWorker> worker;
        std::shared_ptr<const CameraFrame> frame;
    };
    void synchronize();
    void poll();
    void publish();
    void stop(Entry &entry);
    SourceListModel &m_model;
    QVariantList m_devices;
    QHash<QString, QVariantList> m_modes;
    QSet<QString> m_querying;
    QString m_catalogError;
    bool m_refreshing = false, m_busy = false, m_pending = false;
    quint64 m_generation = 0;
    QTimer m_timer;
    QList<PreviewSource> m_others;
    std::map<QString, Entry> m_entries;
    std::vector<std::unique_ptr<CameraCaptureWorker>> m_retired;
    std::vector<std::unique_ptr<QThread>> m_queries;
};
#endif
