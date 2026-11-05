#ifndef MEDIACLOCK_H
#define MEDIACLOCK_H
#include <QtGlobal>
#include <chrono>
namespace MediaClock {
inline qint64 nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
}
#endif
