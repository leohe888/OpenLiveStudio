#ifndef DISPLAYCAPTUREWORKER_H
#define DISPLAYCAPTUREWORKER_H

#include "graphics/captureframe.h"
#include <QThread>
#include <QString>
#include <condition_variable>

class DisplayCaptureWorker : public QThread
{
public:
    DisplayCaptureWorker(QString displayId, bool captureCursor, quint64 generation);
    ~DisplayCaptureWorker() override;

    void stop()
    {
        m_stop.store(true);
        m_stopCondition.notify_one();
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
    QString m_displayId;
    bool m_captureCursor;
    quint64 m_generation;
    std::atomic_bool m_stop{false};
    LatestCaptureFrameSlot m_latestFrame;
    mutable std::mutex m_errorMutex;
    QString m_error;
    std::mutex m_stopMutex;
    std::condition_variable m_stopCondition;
};

#endif
