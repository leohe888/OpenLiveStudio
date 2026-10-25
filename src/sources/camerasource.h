#ifndef CAMERASOURCE_H
#define CAMERASOURCE_H

#include <QMediaDevices>
#include <QObject>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVideoFrame>

class QCamera;
class QMediaCaptureSession;
class QVideoSink;

class CameraSource : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY statusChanged)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY statusChanged)

public:
    enum class State {
        Stopped,
        Starting,
        Capturing,
        Error
    };
    Q_ENUM(State)

    struct DeviceInfo {
        // Opaque, lossless token for QCameraDevice::id(); never a list index.
        QString id;
        QString description;
        bool isDefault = false;
    };

    explicit CameraSource(QObject *parent = nullptr);
    ~CameraSource() override;
    static QList<DeviceInfo> availableDevices();
    // True means the request was accepted. Capturing requires a valid first frame.
    bool start(const QString &deviceId);
    void stop();
    State state() const { return m_state; }
    QString errorString() const { return m_errorString; }
    QString deviceId() const { return m_deviceId; }
    QSize frameSize() const { return m_frameSize; }

signals:
    void statusChanged();
    void devicesChanged();
    void frameReady(const QVideoFrame &frame);

private:
    void releaseCapture();
    void fail(const QString &error);
    void setStatus(State state, const QString &error = {}, const QString &deviceId = {});
    QMediaDevices m_devices;
    QTimer m_startTimeout;
    QObject *m_capture = nullptr;
    QCamera *m_camera = nullptr;
    QMediaCaptureSession *m_session = nullptr;
    QVideoSink *m_sink = nullptr;
    quint64 m_generation = 0;
    State m_state = State::Stopped;
    QString m_errorString;
    QString m_deviceId;
    QSize m_frameSize;
};

#endif // CAMERASOURCE_H
