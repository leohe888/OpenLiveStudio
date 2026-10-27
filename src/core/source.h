#ifndef SOURCE_H
#define SOURCE_H

#include <QColor>
#include <QFont>
#include <QSize>
#include <QString>
#include <variant>

enum class SourceType {
    DisplayCapture,
    WindowCapture,
    Camera,
    Image,
    Text
};

struct DisplayCaptureConfig {
    QString displayId;
    bool captureCursor = true;
};
struct WindowCaptureConfig {
    QString executablePath;
    QString windowClass;
    QString windowTitle;
    bool captureCursor = true;
};
struct CameraConfig {
    QString deviceId;
    // Invalid size and zero frame rate mean no explicit preference.
    QSize requestedResolution;
    int requestedFrameRate = 0;
};
struct ImageConfig {
    QString filePath;
};
struct TextConfig {
    QString text;
    QFont font;
    QColor color = Qt::white;
};
using SourceConfig = std::variant<
    DisplayCaptureConfig,
    WindowCaptureConfig,
    CameraConfig,
    ImageConfig,
    TextConfig
>;

struct SourceRuntimeInfo {
    // Invalid until loading, capture, or rendering supplies the actual size.
    QSize originalSize;
};
struct SourceData {
    QString id;
    QString name;
    SourceConfig config = DisplayCaptureConfig{};
    SourceRuntimeInfo runtime;
    bool visible = true;
    bool locked = false;

    SourceType type() const;
};

#endif // SOURCE_H
