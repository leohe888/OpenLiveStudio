#include "video/display/displaycaptureworker.h"
#include "video/display/waylanddisplaycapture.h"

DisplayCaptureWorker::DisplayCaptureWorker(bool captureCursor, quint64 generation)
    : m_captureCursor(captureCursor), m_generation(generation)
{
}

DisplayCaptureWorker::~DisplayCaptureWorker()
{
    stop();
    wait(); // 正常移除来源时，控制器等线程结束后才析构；应用退出时才在此等待剩余线程。
}

QString DisplayCaptureWorker::lastError() const
{
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_error;
}

void DisplayCaptureWorker::run()
{
    WaylandDisplayCapture capture;
    const auto error = capture.run(m_captureCursor, m_generation, m_stop, m_latestFrame);
    if (!m_stop.load() && !error.isEmpty()) {
        m_latestFrame.clear();
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_error = error;
    }
}
