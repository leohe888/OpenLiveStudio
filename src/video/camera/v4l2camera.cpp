#include "video/camera/v4l2camera.h"
#include "util/mediaclock.h"
#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstring>
#include <climits>
#include <utility>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

namespace {
int deviceIoctl(int fd, unsigned long request, void *arg)
{
    int result;
    do { result = ::ioctl(fd, request, arg); } while (result < 0 && errno == EINTR);
    return result;
}
void checkIoctl(int fd, unsigned long request, void *arg, const QString &operation)
{
    if (deviceIoctl(fd, request, arg) < 0)
        throw operation + QStringLiteral(": %1").arg(QString::fromLocal8Bit(strerror(errno)));
}
int openDevice(const QString &path)
{
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        throw QStringLiteral("无法打开摄像头 %1：%2").arg(path, QString::fromLocal8Bit(strerror(errno)));
    return fd;
}
bool captureDevice(int fd, v4l2_capability &cap)
{
    if (deviceIoctl(fd, VIDIOC_QUERYCAP, &cap) < 0) return false;
    const auto flags = cap.capabilities & V4L2_CAP_DEVICE_CAPS ? cap.device_caps : cap.capabilities;
    return (flags & V4L2_CAP_VIDEO_CAPTURE) && (flags & V4L2_CAP_STREAMING);
}
struct Format { quint32 fourcc; const char *name; AVPixelFormat pixel; };
constexpr Format formats[] = {
    {V4L2_PIX_FMT_MJPEG, "mjpeg", AV_PIX_FMT_NONE},
    {V4L2_PIX_FMT_H264, "h264", AV_PIX_FMT_NONE},
    {V4L2_PIX_FMT_YUYV, "yuyv422", AV_PIX_FMT_YUYV422},
    {V4L2_PIX_FMT_UYVY, "uyvy422", AV_PIX_FMT_UYVY422},
    {V4L2_PIX_FMT_NV12, "nv12", AV_PIX_FMT_NV12},
    {V4L2_PIX_FMT_YUV420, "yuv420p", AV_PIX_FMT_YUV420P},
    {V4L2_PIX_FMT_YVU420, "yv12", AV_PIX_FMT_YUV420P},
    {V4L2_PIX_FMT_RGB24, "rgb24", AV_PIX_FMT_RGB24},
    {V4L2_PIX_FMT_BGR24, "bgr24", AV_PIX_FMT_BGR24},
};
const Format *formatInfo(quint32 code)
{
    for (const auto &f : formats) if (f.fourcc == code) return &f;
    return nullptr;
}
quint32 formatCode(const QString &name)
{
    for (const auto &f : formats) if (name == QLatin1String(f.name)) return f.fourcc;
    return 0;
}
QString stablePath(const QString &node)
{
    // 无序列号的 USB 摄像头只按连接位置恢复，避免同型号 by-id 别名重新指向另一端口。
    for (const auto &alias : QDir("/dev/v4l/by-path").entryInfoList(QDir::Files | QDir::System))
        if (alias.canonicalFilePath() == node) return alias.absoluteFilePath();
    return node;
}
QList<CameraMode> enumerateModes(int fd)
{
    QList<CameraMode> modes;
    v4l2_fmtdesc f{};
    f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (; deviceIoctl(fd, VIDIOC_ENUM_FMT, &f) == 0; ++f.index) {
        const auto *info = formatInfo(f.pixelformat);
        if (!info) continue; // 不展示当前解码/渲染路径无法处理的格式。
        v4l2_frmsizeenum size{};
        size.pixel_format = f.pixelformat;
        for (; deviceIoctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) == 0; ++size.index) {
            // 首版面向 USB UVC 的离散模式，不展开 ISP 的连续模式空间。
            if (size.type != V4L2_FRMSIZE_TYPE_DISCRETE) break;
            v4l2_frmivalenum interval{};
            interval.pixel_format = f.pixelformat;
            interval.width = size.discrete.width;
            interval.height = size.discrete.height;
            for (; deviceIoctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) == 0; ++interval.index) {
                if (interval.type != V4L2_FRMIVAL_TYPE_DISCRETE) break;
                if (!interval.discrete.numerator || !interval.discrete.denominator) continue;
                CameraMode mode{int(interval.width), int(interval.height),
                                int(interval.discrete.denominator), int(interval.discrete.numerator),
                                QString::fromLatin1(info->name)};
                mode.name = QStringLiteral("%1 · %2 × %3 · %4 fps")
                    .arg(mode.subtype).arg(mode.width).arg(mode.height)
                    .arg(double(mode.frameRateNumerator) / mode.frameRateDenominator, 0, 'g', 5);
                modes.append(mode);
            }
        }
    }
    return modes;
}
}

QList<CameraDevice> cameraDevices(QString *error)
{
    if (error) error->clear();
    QList<CameraDevice> result;
    for (const auto &node : QDir("/dev").entryInfoList({"video*"}, QDir::System | QDir::Files)) {
        const int fd = ::open(QFile::encodeName(node.absoluteFilePath()).constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            if (error && (errno == EACCES || errno == EPERM))
                *error = QStringLiteral("没有摄像头设备访问权限：%1").arg(node.absoluteFilePath());
            continue;
        }
        v4l2_capability cap{};
        if (captureDevice(fd, cap))
            result.append({stablePath(node.canonicalFilePath()),
                           QString::fromLocal8Bit(reinterpret_cast<const char *>(cap.card))});
        ::close(fd);
    }
    return result;
}
QList<CameraMode> cameraModes(const QString &deviceId, QString *error)
{
    if (error) error->clear();
    try {
        const int fd = openDevice(deviceId);
        const auto cleanup = qScopeGuard([&] { ::close(fd); });
        v4l2_capability cap{};
        if (!captureDevice(fd, cap)) throw QStringLiteral("设备不支持单平面 V4L2 视频采集。");
        return enumerateModes(fd);
    } catch (const QString &message) {
        if (error) *error = message;
        return {};
    }
}

CameraMode preferredCameraMode(const QList<CameraMode> &modes, const CameraSource &source)
{
    CameraMode selected;
    double best = 1e100;
    for (const auto &m : modes) {
        if (!m.supported || !m.isValid()) continue;
        if (!source.pixelFormat().isEmpty() && m.subtype != source.pixelFormat())
            continue;
        if (!source.resolution().isEmpty() && QSize(m.width, m.height) != source.resolution())
            continue;
        if (source.frameRateNumerator() > 0 &&
            qint64(m.frameRateNumerator) * source.frameRateDenominator() !=
                qint64(source.frameRateNumerator()) * m.frameRateDenominator)
            continue;
        const double fps = double(m.frameRateNumerator) / m.frameRateDenominator;
        const double score = 8 * std::abs(std::log((double(m.width) / m.height) / (16.0 / 9))) +
            4 * std::abs(std::log(fps / 30)) +
            std::abs(std::log(double(m.width) * m.height / (1920.0 * 1080))) +
            (m.subtype == "h264" ? 0.1 : 0);
        if (score < best) { best = score; selected = m; }
    }
    return selected;
}

void cameraRawFrame(AVFrame *frame, const v4l2_pix_format &f, unsigned char *data, size_t size)
{
    const auto *info = formatInfo(f.pixelformat);
    if (!info || info->pixel == AV_PIX_FMT_NONE || !data || !f.width || !f.height ||
        f.width > 16384 || f.height > 16384)
        throw QStringLiteral("不支持的摄像头像素布局。");
    frame->format = info->pixel;
    frame->width = int(f.width);
    frame->height = int(f.height);
    const int row = av_image_get_linesize(info->pixel, frame->width, 0);
    const size_t stride = f.bytesperline ? f.bytesperline : row;
    if (row <= 0 || stride < size_t(row) || stride > INT_MAX)
        throw QStringLiteral("摄像头行步长无效。");
    size_t offset = 0;
    const int planes = f.pixelformat == V4L2_PIX_FMT_NV12 ? 2 :
        (info->pixel == AV_PIX_FMT_YUV420P ? 3 : 1);
    for (int p = 0; p < planes; ++p) {
        const size_t pitch = p && planes == 3 ? (stride + 1) / 2 : stride;
        const size_t height = p ? (f.height + 1) / 2 : f.height;
        const size_t rowBytes = av_image_get_linesize(info->pixel, frame->width, p);
        if (pitch < rowBytes || offset > size ||
            (height - 1) * pitch + rowBytes > size - offset)
            throw QStringLiteral("摄像头原始帧缓冲不足。");
        frame->data[p] = data + offset;
        frame->linesize[p] = int(pitch);
        offset += pitch * height;
    }
    if (f.pixelformat == V4L2_PIX_FMT_YVU420) {
        std::swap(frame->data[1], frame->data[2]);
        std::swap(frame->linesize[1], frame->linesize[2]);
    }
    if (f.colorspace == V4L2_COLORSPACE_BT2020 || f.xfer_func == V4L2_XFER_FUNC_SMPTE2084)
        throw QStringLiteral("暂不支持摄像头 HDR 或 BT.2020 色彩。");
    frame->color_range = f.quantization == V4L2_QUANTIZATION_FULL_RANGE ? AVCOL_RANGE_JPEG :
        f.quantization == V4L2_QUANTIZATION_LIM_RANGE ? AVCOL_RANGE_MPEG : AVCOL_RANGE_UNSPECIFIED;
    frame->colorspace = f.ycbcr_enc == V4L2_YCBCR_ENC_709 ? AVCOL_SPC_BT709 :
        f.ycbcr_enc == V4L2_YCBCR_ENC_601 ? AVCOL_SPC_SMPTE170M : AVCOL_SPC_UNSPECIFIED;
}

V4l2Camera::V4l2Camera(const CameraSource &source)
{
    try {
        m_fd = openDevice(source.deviceId());
        v4l2_capability cap{};
        if (!captureDevice(m_fd, cap)) throw QStringLiteral("设备不支持单平面 V4L2 streaming。");
        m_mode = preferredCameraMode(enumerateModes(m_fd), source);
        if (!m_mode.isValid()) throw QStringLiteral("摄像头不支持请求的格式、分辨率或帧率。");
        v4l2_format format{};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = m_mode.width;
        format.fmt.pix.height = m_mode.height;
        format.fmt.pix.pixelformat = formatCode(m_mode.subtype);
        format.fmt.pix.field = V4L2_FIELD_NONE;
        checkIoctl(m_fd, VIDIOC_S_FMT, &format, "设置摄像头格式失败");
        m_format = format.fmt.pix;
        if (m_format.width != unsigned(m_mode.width) || m_format.height != unsigned(m_mode.height) ||
            m_format.pixelformat != formatCode(m_mode.subtype) ||
            (m_format.field != V4L2_FIELD_NONE && m_format.field != V4L2_FIELD_ANY))
            throw QStringLiteral("驱动更改了请求的摄像头模式或返回隔行视频。");
        v4l2_streamparm rate{};
        rate.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        rate.parm.capture.timeperframe = {unsigned(m_mode.frameRateDenominator), unsigned(m_mode.frameRateNumerator)};
        checkIoctl(m_fd, VIDIOC_S_PARM, &rate, "设置摄像头帧率失败");
        const auto actual = rate.parm.capture.timeperframe;
        if (!actual.numerator || !actual.denominator ||
            qint64(actual.denominator) * m_mode.frameRateDenominator !=
            qint64(m_mode.frameRateNumerator) * actual.numerator)
            throw QStringLiteral("驱动更改了请求的摄像头帧率。");
        v4l2_requestbuffers request{};
        request.count = 4;
        request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        request.memory = V4L2_MEMORY_MMAP;
        checkIoctl(m_fd, VIDIOC_REQBUFS, &request, "申请摄像头缓冲失败");
        if (request.count < 2 || request.count > 32) throw QStringLiteral("摄像头缓冲数量无效。");
        for (unsigned i = 0; i < request.count; ++i) {
            v4l2_buffer buffer{};
            buffer.type = request.type;
            buffer.memory = request.memory;
            buffer.index = i;
            checkIoctl(m_fd, VIDIOC_QUERYBUF, &buffer, "查询摄像头缓冲失败");
            void *data = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, buffer.m.offset);
            if (data == MAP_FAILED) throw QStringLiteral("映射摄像头缓冲失败。");
            m_buffers.push_back({data, buffer.length});
            checkIoctl(m_fd, VIDIOC_QBUF, &buffer, "排队摄像头缓冲失败");
        }
    } catch (...) { close(); throw; }
}
V4l2Camera::~V4l2Camera() { close(); }
void V4l2Camera::close()
{
    if (m_streaming) {
        auto type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        deviceIoctl(m_fd, VIDIOC_STREAMOFF, &type);
        m_streaming = false;
    }
    for (const auto &b : m_buffers) munmap(b.data, b.length);
    m_buffers.clear();
    if (m_fd >= 0) ::close(std::exchange(m_fd, -1));
}
void V4l2Camera::start()
{
    auto type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    checkIoctl(m_fd, VIDIOC_STREAMON, &type, "启动摄像头失败");
    m_streaming = true;
}
bool V4l2Camera::read(AVPacket *packet, AVFrame *raw, qint64 &timestampNs, const std::atomic_bool &stop)
{
    const auto deadline = MediaClock::nowNs() + 3000000000LL;
    while (!stop.load()) {
        if (MediaClock::nowNs() >= deadline) throw QStringLiteral("摄像头超过三秒没有有效帧。");
        pollfd event{m_fd, POLLIN, 0};
        const int ready = poll(&event, 1, 100);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0 || (event.revents & (POLLERR | POLLHUP | POLLNVAL)))
            throw QStringLiteral("摄像头已断开或采集失败。");
        if (!ready) continue;
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (deviceIoctl(m_fd, VIDIOC_DQBUF, &b) < 0) {
            if (errno == EAGAIN) continue;
            throw QStringLiteral("读取摄像头帧失败。");
        }
        if (b.index >= m_buffers.size()) throw QStringLiteral("摄像头返回无效缓冲索引。");
        const auto &buffer = m_buffers[b.index];
        auto requeue = qScopeGuard([&] { deviceIoctl(m_fd, VIDIOC_QBUF, &b); });
        if ((b.flags & V4L2_BUF_FLAG_ERROR) || !b.bytesused) continue;
        if (b.bytesused > buffer.length) throw QStringLiteral("摄像头帧超出映射缓冲。");
        // 接收时刻使用全项目统一单调时钟，不混用驱动 timeval 的时钟域。
        timestampNs = MediaClock::nowNs();
        if (m_mode.subtype == "mjpeg" || m_mode.subtype == "h264") {
            if (b.bytesused > INT_MAX || av_new_packet(packet, int(b.bytesused)) < 0)
                throw QStringLiteral("无法分配摄像头压缩帧。");
            memcpy(packet->data, buffer.data, b.bytesused);
            packet->pts = timestampNs;
            packet->dts = AV_NOPTS_VALUE;
        } else {
            AVFrame view{};
            cameraRawFrame(&view, m_format, static_cast<unsigned char *>(buffer.data), b.bytesused);
            raw->format = view.format; raw->width = view.width; raw->height = view.height;
            if (av_frame_get_buffer(raw, 32) < 0) throw QStringLiteral("无法分配摄像头原始帧。");
            av_image_copy(raw->data, raw->linesize, const_cast<const uint8_t **>(view.data),
                          view.linesize, AVPixelFormat(view.format), view.width, view.height);
            raw->color_range = view.color_range; raw->colorspace = view.colorspace;
        }
        checkIoctl(m_fd, VIDIOC_QBUF, &b, "归还摄像头缓冲失败");
        requeue.dismiss();
        return true;
    }
    return false;
}
