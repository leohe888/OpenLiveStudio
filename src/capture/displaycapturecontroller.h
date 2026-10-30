#ifndef DISPLAYCAPTURECONTROLLER_H
#define DISPLAYCAPTURECONTROLLER_H

#include "displaycaptureworker.h"
#include "preview/previewsource.h"
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <map>
#include <vector>

class VideoSourceModel;
class DisplayCaptureController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList displays READ displays NOTIFY displaysChanged)
public:
    explicit DisplayCaptureController(VideoSourceModel &model, QObject *parent = nullptr);
    ~DisplayCaptureController() override;
    QVariantList displays() const { return m_displays; }
    Q_INVOKABLE void refreshDisplays();
    Q_INVOKABLE QString addDisplay(const QString &name, const QString &displayId, bool cursor);
    Q_INVOKABLE bool applyProperties(const QString &sourceId, const QString &displayId, bool cursor);
    Q_INVOKABLE bool retry(const QString &sourceId);
    void failTransfer(const QString &id, quint64 generation, const QString &reason);
    void setImageSources(const QList<PreviewSource> &images);
    int activeWorkerCount() const;
signals:
    void displaysChanged();
    void sourcesChanged(const QList<PreviewSource> &sources);
    void sourceError(const QString &id, const QString &message);
private:
    struct Entry {
        QString displayId;
        bool cursor = true, failed = false;
        quint64 generation = 0;
        std::unique_ptr<DisplayCaptureWorker> worker;
        std::shared_ptr<const CaptureFrame> frame;
    };
    void synchronize();
    void poll();
    void publish();
    void stop(Entry &entry);
    VideoSourceModel &m_model;
    QVariantList m_displays;
    QList<PreviewSource> m_images;
    std::map<QString, Entry> m_entries;
    std::vector<std::unique_ptr<DisplayCaptureWorker>> m_retired;
    QTimer m_timer;
    quint64 m_generation = 0;
    bool m_busy = false;
    bool m_syncPending = false;
};
#endif
