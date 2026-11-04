#ifndef CAMERAMFINPUT_H
#define CAMERAMFINPUT_H

#include "video/camera/cameracaptureworker.h"

#ifdef Q_OS_WIN
#include <mfidl.h>
struct AVCodecParameters;
struct AVPacket;

// MF 只负责设备原生格式，不插入解码器或 RGB 转换器。
class MfCameraInput
{
public:
    explicit MfCameraInput(const CameraSourceConfig &config);
    ~MfCameraInput();
    const CameraMode &mode() const;
    const AVCodecParameters *parameters() const;

    struct Sample
    {
        std::shared_ptr<const PixelFrame> pixels;
        qint64 timestamp100ns = 0, arrivalNs = 0;
        bool inferred = false;
    };

    // 压缩样本填入 packet；未压缩样本保留原始像素布局。
    bool read(AVPacket *packet, Sample &sample, const std::atomic_bool &stop);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

QString cameraMfError(HRESULT hr);
std::shared_ptr<const PixelFrame> cameraMfPixels(IMFSample *sample, IMFMediaType *type,
                                                 bool &inferred);
// 输出参数对象必须为空；保留 H.264 初始化数据及 MF 色彩元数据。
void cameraMfParameters(IMFMediaType *type, AVCodecParameters *parameters);
void cameraMfPacket(IMFSample *sample, AVPacket *packet, qint64 timestamp100ns, bool intraFrame);
#endif
#endif
