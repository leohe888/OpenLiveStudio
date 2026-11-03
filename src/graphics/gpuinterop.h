#ifndef GPUINTEROP_H
#define GPUINTEROP_H

#include "graphics/captureframe.h"
#include <QOpenGLFunctions>
#include <memory>

// 创建、复制和析构都要求所属 GL 上下文处于当前状态。
class GpuInterop
{
public:
    GpuInterop();
    ~GpuInterop();
    bool copy(const CaptureFrame &frame, GLuint destination, QString *error);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif
