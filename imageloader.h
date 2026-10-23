#ifndef IMAGELOADER_H
#define IMAGELOADER_H

#include <QSize>
#include <QString>

class ImageLoader
{
public:
    struct Result {
        QString filePath;
        QSize originalSize;
        QString error;
        bool isValid() const { return error.isEmpty() && originalSize.isValid(); }
    };

    // Synchronously decode a local file to validate it and obtain its dimensions.
    // Pixels are discarded; success returns an absolute path, failure only an error.
    static Result load(const QString &filePath);
};

#endif // IMAGELOADER_H
