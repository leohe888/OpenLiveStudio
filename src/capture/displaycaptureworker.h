#ifndef DISPLAYCAPTUREWORKER_H
#define DISPLAYCAPTUREWORKER_H

#include "captureframe.h"
#include <QThread>
#include <QString>
#include <condition_variable>

class DisplayCaptureWorker : public QThread {
public:
    DisplayCaptureWorker(QString displayId, bool cursor, quint64 generation);
    ~DisplayCaptureWorker() override;
    void stop() { m_stop.store(true); m_wait.notify_one(); m_frames.clear(); }
    void useCpu() { m_cpu.store(true); m_frames.clear(); }
    std::shared_ptr<const CaptureFrame> takeLatest() { return m_frames.take(); }
    QString error() const;
    quint64 generation() const { return m_generation; }
protected:
    void run() override;
private:
    QString m_displayId;
    bool m_cursor;
    quint64 m_generation;
    std::atomic_bool m_stop{false}, m_cpu{false};
    LatestCaptureFrame m_frames;
    mutable std::mutex m_errorMutex;
    QString m_error;
    std::mutex m_waitMutex;
    std::condition_variable m_wait;
};

#endif
