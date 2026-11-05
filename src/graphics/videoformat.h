#ifndef VIDEOFORMAT_H
#define VIDEOFORMAT_H

#include <QSize>
#include <QtGlobal>

// The single canvas, compositor and encoder use the same fixed output format.
namespace VideoFormat {
inline constexpr int width = 1920;
inline constexpr int height = 1080;
inline constexpr QSize size{width, height};
inline constexpr int framesPerSecond = 30;
inline constexpr qint64 frameDurationNs = 1'000'000'000LL / framesPerSecond;
}

#endif
