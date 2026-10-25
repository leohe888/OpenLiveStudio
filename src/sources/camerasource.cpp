#include "camerasource.h"

#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QVideoSink>

namespace {
QString deviceToken(const QCameraDevice &device)
{
    return QString::fromLatin1(device.id().toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
}

CameraSource::CameraSource(QObject *parent) : QObject(parent)
{
    m_startTimeout.setSingleShot(true);
    m_startTimeout.setInterval(10000);
    connect(&m_startTimeout, &QTimer::timeout, this, [this] {
        if (m_state == State::Starting)
            fail(tr("摄像头启动超时，未收到有效视频帧。"));
    });
    connect(&m_devices, &QMediaDevices::videoInputsChanged, this, [this] {
        if (m_capture) {
            bool found = false;
            for (const auto &device : availableDevices())
                found |= device.id == m_deviceId;
            if (!found)
                fail(tr("摄像头已断开。"));
        }
        emit devicesChanged();
    });
}

CameraSource::~CameraSource()
{
    releaseCapture();
}

QList<CameraSource::DeviceInfo> CameraSource::availableDevices()
{
    QList<DeviceInfo> result;
    for (const auto &device : QMediaDevices::videoInputs())
        result.append({deviceToken(device), device.description(), device.isDefault()});
    return result;
}

bool CameraSource::start(const QString &deviceId)
{
    if ((m_state == State::Starting || m_state == State::Capturing) && m_deviceId == deviceId)
        return true;
    stop();
    QCameraDevice selected;
    for (const auto &device : QMediaDevices::videoInputs()) {
        if (deviceToken(device) == deviceId) {
            selected = device;
            break;
        }
    }
    if (deviceId.isEmpty() || selected.isNull()) {
        fail(deviceId.isEmpty() ? tr("请选择摄像头。") : tr("摄像头设备不存在或已断开。"));
        return false;
    }

    m_capture = new QObject(this);
    m_camera = new QCamera(selected, m_capture);
    m_session = new QMediaCaptureSession(m_capture);
    m_sink = new QVideoSink(m_capture);
    m_session->setCamera(m_camera);
    m_session->setVideoSink(m_sink);
    const quint64 generation = m_generation;
    connect(m_camera, &QCamera::errorOccurred, this,
            [this, generation](QCamera::Error error, const QString &message) {
        if (generation == m_generation && error != QCamera::NoError)
            fail(message.isEmpty() ? tr("摄像头采集失败。") : message);
    });
    connect(m_camera, &QCamera::activeChanged, this, [this, generation](bool active) {
        if (generation == m_generation && !active && m_state == State::Capturing)
            fail(tr("摄像头采集意外停止。"));
    });
    connect(m_sink, &QVideoSink::videoFrameChanged, this,
            [this, generation](const QVideoFrame &frame) {
        if (generation != m_generation || !frame.isValid() || frame.size().isEmpty())
            return;
        m_startTimeout.stop();
        const bool changed = m_state != State::Capturing || m_frameSize != frame.size();
        m_state = State::Capturing;
        m_frameSize = frame.size();
        if (changed)
            emit statusChanged();
        if (generation == m_generation)
            emit frameReady(frame);
    });
    if (m_camera->error() != QCamera::NoError) {
        fail(m_camera->errorString());
        return false;
    }
    m_startTimeout.start();
    setStatus(State::Starting, {}, deviceId);
    if (generation != m_generation)
        return false;
    m_camera->start();
    return generation == m_generation;
}

void CameraSource::stop()
{
    releaseCapture();
    setStatus(State::Stopped);
}

void CameraSource::releaseCapture()
{
    ++m_generation;
    m_startTimeout.stop();
    if (!m_capture)
        return;
    m_camera->disconnect(this);
    m_sink->disconnect(this);
    m_session->setCamera(nullptr);
    m_session->setVideoSink(nullptr);
    m_camera->stop();
    // Errors may arrive from inside a camera event; defer QObject destruction.
    m_capture->deleteLater();
    m_capture = nullptr;
    m_camera = nullptr;
    m_session = nullptr;
    m_sink = nullptr;
}

void CameraSource::fail(const QString &error)
{
    const QString deviceId = m_deviceId;
    releaseCapture();
    setStatus(State::Error, error.isEmpty() ? tr("摄像头采集失败。") : error, deviceId);
}

void CameraSource::setStatus(State state, const QString &error, const QString &deviceId)
{
    const bool changed = m_state != state || m_errorString != error
        || m_deviceId != deviceId || m_frameSize.isValid();
    m_state = state;
    m_errorString = error;
    m_deviceId = deviceId;
    m_frameSize = {};
    if (changed)
        emit statusChanged();
}
