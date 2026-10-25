#include "imageprobe.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>

ImageProbe::Result ImageProbe::load(const QString &filePath)
{
    const QFileInfo file(filePath);
    if (filePath.isEmpty() || !file.isFile() || !file.isReadable())
        return {{}, {}, QCoreApplication::translate("ImageProbe", "请选择存在且可读取的图像文件。")};

    QImageReader reader(file.absoluteFilePath());
    const QImage image = reader.read();
    if (image.isNull()) {
        return {{}, {}, QCoreApplication::translate("ImageProbe", "无法读取图像：%1")
                            .arg(reader.errorString())};
    }
    return {file.absoluteFilePath(), image.size(), {}};
}
