#ifndef DISPLAYCAPTUREWORKER_H
#define DISPLAYCAPTUREWORKER_H

#include "graphics/captureframe.h"
#include <atomic>
#include <QThread>
#include <QString>

class DisplayCaptureWorker : public QThread
{
public:
    DisplayCaptureWorker(bool captureCursor, quint64 generation);
    ~DisplayCaptureWorker() override;

    void stop()
    {
        m_stop.store(true);
        m_latestFrame.clear();
    }

    std::shared_ptr<const CaptureFrame> takeLatest()
    {
        return m_latestFrame.takeLatest();
    }

    QString lastError() const;

    quint64 generation() const
    {
        return m_generation;
    }

protected:
    void run() override;

private:
    bool m_captureCursor;
    quint64 m_generation;
    std::atomic_bool m_stop{false};
    LatestCaptureFrameSlot m_latestFrame;
    mutable std::mutex m_errorMutex;
    QString m_error;
};

#endif
