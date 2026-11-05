#ifndef V4L2CAMERA_H
#define V4L2CAMERA_H
#include "video/camera/cameracaptureworker.h"
#include <linux/videodev2.h>
#include <vector>

struct AVPacket;
struct AVFrame;

// 单平面 V4L2 streaming 输入；所有方法与映射缓冲仅在采集线程使用。
class V4l2Camera
{
public:
    explicit V4l2Camera(const CameraSource &source);
    ~V4l2Camera();
    V4l2Camera(const V4l2Camera &) = delete;
    V4l2Camera &operator=(const V4l2Camera &) = delete;
    void start();
    bool read(AVPacket *packet, AVFrame *raw, qint64 &timestampNs,
              const std::atomic_bool &stop);
    const CameraMode &mode() const { return m_mode; }
private:
    struct Buffer { void *data; size_t length; };
    int m_fd = -1;
    bool m_streaming = false;
    CameraMode m_mode;
    v4l2_pix_format m_format{};
    std::vector<Buffer> m_buffers;
    void close();
};

// 返回最接近 1080p30 的支持模式，同时考虑画幅比例而非只比较像素数。
CameraMode preferredCameraMode(const QList<CameraMode> &modes, const CameraSource &source);
// 单平面原始缓冲的行布局和长度检查，供采集和测试共用。
void cameraRawFrame(AVFrame *frame, const v4l2_pix_format &format, unsigned char *data, size_t size);
#endif
