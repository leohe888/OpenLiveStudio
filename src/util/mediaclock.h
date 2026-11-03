#ifndef MEDIACLOCK_H
#define MEDIACLOCK_H
#include <QtGlobal>
#include <chrono>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace MediaClock {
inline qint64 nowNs()
{
#ifdef Q_OS_WIN
    static const qint64 frequency = [] {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        return value.QuadPart;
    }();
    LARGE_INTEGER value;
    QueryPerformanceCounter(&value);
    return value.QuadPart / frequency * 1000000000LL +
           value.QuadPart % frequency * 1000000000LL / frequency;
#else
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
#endif
}
}
#endif
