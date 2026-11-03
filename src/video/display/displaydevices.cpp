#include "video/display/displaydevices.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <vector>

QList<DisplayDevice> displayDevices()
{
    UINT32 pathCount = 0, modeCount = 0;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG result = ERROR_INSUFFICIENT_BUFFER;
    for (int attempt = 0; attempt < 3 && result == ERROR_INSUFFICIENT_BUFFER; ++attempt) {
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) !=
            ERROR_SUCCESS)
            return {};
        paths.resize(pathCount);
        modes.resize(modeCount);
        result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
                                    modes.data(), nullptr);
    }
    if (result != ERROR_SUCCESS)
        return {};

    struct Monitors
    {
        QList<std::pair<QString, HMONITOR>> entries;
    } monitors;

    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info))
                reinterpret_cast<Monitors *>(data)->entries.append(
                    {QString::fromWCharArray(info.szDevice), monitor});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));
    QList<DisplayDevice> targets;
    for (UINT32 i = 0; i < pathCount; ++i) {
        const auto &path = paths[i];
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source),
                         path.sourceInfo.adapterId, path.sourceInfo.id};
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target),
                         path.targetInfo.adapterId, path.targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
            DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS)
            continue;
        const QString id = QString::fromWCharArray(target.monitorDevicePath);
        if (id.isEmpty())
            continue; // 列表下标会随设备变化，不能作为显示器的稳定标识。
        for (const auto &entry : monitors.entries) {
            if (entry.first != QString::fromWCharArray(source.viewGdiDeviceName))
                continue;
            MONITORINFO info{sizeof(info)};
            if (!GetMonitorInfoW(entry.second, &info))
                continue;
            const QSize size(info.rcMonitor.right - info.rcMonitor.left,
                             info.rcMonitor.bottom - info.rcMonitor.top);
            QString name = QString::fromWCharArray(target.monitorFriendlyDeviceName);
            if (name.isEmpty())
                name = entry.first;
            DisplayDevice display{id,
                                  QStringLiteral("%1 · %2×%3%4")
                                      .arg(name)
                                      .arg(size.width())
                                      .arg(size.height())
                                      .arg(info.dwFlags & MONITORINFOF_PRIMARY
                                               ? QStringLiteral(" · 主显示器")
                                               : QString()),
                                  size, reinterpret_cast<quintptr>(entry.second)};
            if (info.dwFlags & MONITORINFOF_PRIMARY)
                targets.prepend(display);
            else
                targets.append(display);
            break;
        }
    }
    return targets;
}
#else
QList<DisplayDevice> displayDevices()
{
    return {};
}
#endif
