#ifndef DISPLAYCAPTUREWORKER_H
#define DISPLAYCAPTUREWORKER_H

#include "graphics/captureframe.h"
#include <atomic>
#include <QThread>
#include <QString>

// 在界面线程管理生命周期；共享会话与采集在 run() 线程创建和释放。
// stop() 请求停止并清除待取帧，析构等待退出；takeLatest()/lastError() 可跨线程读取。
// 返回帧的共享所有权独立于 worker，消费者只读。
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
