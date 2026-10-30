#ifndef GPUINTEROP_H
#define GPUINTEROP_H

#include "graphics/captureframe.h"
#include <QOpenGLFunctions>
#include <memory>

// Create, copy, and destroy only with the owning GL context current.
class GpuInterop {
public:
    GpuInterop();
    ~GpuInterop();
    bool copy(const CaptureFrame &frame, GLuint destination, QString *error);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif
