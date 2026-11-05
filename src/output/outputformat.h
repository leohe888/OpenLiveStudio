#ifndef OUTPUTFORMAT_H
#define OUTPUTFORMAT_H

#include "audio/audiobuffer.h"
#include "graphics/videoformat.h"

namespace OutputFormat {
static_assert(AudioFormat::rate % VideoFormat::framesPerSecond == 0);
inline constexpr int audioFramesPerVideoFrame = AudioFormat::rate / VideoFormat::framesPerSecond;
}

#endif
