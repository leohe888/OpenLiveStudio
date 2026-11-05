#ifndef WAYLANDDISPLAYCAPTURE_H
#define WAYLANDDISPLAYCAPTURE_H

#include "graphics/captureframe.h"
#include <atomic>
#include <QDBusConnection>
#include <QEventLoop>
#include <QVariantMap>

struct spa_buffer;
struct spa_video_info_raw;

// Lives entirely on the capture thread, including D-Bus responses and PipeWire callbacks.
class WaylandDisplayCapture : public QObject
{
    Q_OBJECT
public:
    WaylandDisplayCapture();
    ~WaylandDisplayCapture() override;
    QString run(bool cursor, quint64 generation, std::atomic_bool &stop,
                LatestCaptureFrameSlot &frames);
    static uint cursorModes();
    static std::shared_ptr<const PixelFrame> copyPixels(const spa_buffer &buffer,
        const spa_video_info_raw &format, QString &error);

private slots:
    void response(uint code, const QVariantMap &results);
    void closed();

private:
    QVariantMap request(const QString &method, const QVariantList &arguments,
                        QVariantMap options);
    QString m_connectionName;
    QDBusConnection m_bus;
    QString m_session, m_request, m_error;
    QEventLoop m_loop;
    QVariantMap m_results;
    bool m_responded = false, m_closed = false;
};
#endif
