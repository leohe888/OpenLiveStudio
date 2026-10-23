#include "imageloader.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

class ImageLoaderTest : public QObject
{
    Q_OBJECT
private slots:
    void loadsDecodedDimensions_data();
    void loadsDecodedDimensions();
    void rejectsInvalidFiles_data();
    void rejectsInvalidFiles();
};

void ImageLoaderTest::loadsDecodedDimensions_data()
{
    QTest::addColumn<QByteArray>("format");
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<bool>("relativePath");
    QTest::newRow("png") << QByteArray("PNG") << QString("image.png") << false;
    QTest::newRow("bmp") << QByteArray("BMP") << QString("image.bmp") << false;
    QTest::newRow("content-detection") << QByteArray("PNG") << QString("image.bin") << false;
    QTest::newRow("relative-unicode-path") << QByteArray("PNG") << QString::fromUtf8("图像 文件.png") << true;
}

void ImageLoaderTest::loadsDecodedDimensions()
{
    QFETCH(QByteArray, format);
    QFETCH(QString, fileName);
    QFETCH(bool, relativePath);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(fileName);
    QImage image(19, 11, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    image.setPixelColor(3, 4, Qt::red);
    QVERIFY(image.save(path, format.constData()));
    const auto result = ImageLoader::load(relativePath ? QDir::current().relativeFilePath(path) : path);
    QVERIFY2(result.isValid(), qPrintable(result.error));
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.filePath, path);
    QVERIFY(QDir::isAbsolutePath(result.filePath));
    QCOMPARE(result.originalSize, image.size());
}

void ImageLoaderTest::rejectsInvalidFiles_data()
{
    QTest::addColumn<QString>("failure");
    for (const auto *name : {"empty-path", "missing-file", "directory", "empty-file",
                             "corrupt-file", "truncated-image"})
        QTest::newRow(name) << QString::fromLatin1(name);
}

void ImageLoaderTest::rejectsInvalidFiles()
{
    QFETCH(QString, failure);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString path = dir.filePath("image.png");
    if (failure == "empty-path") {
        path.clear();
    } else if (failure == "directory") {
        path = dir.path();
    } else if (failure != "missing-file") {
        if (failure == "truncated-image") {
            QImage image(19, 11, QImage::Format_ARGB32);
            image.fill(Qt::red);
            QVERIFY(image.save(path));
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        if (failure == "corrupt-file")
            QCOMPARE(file.write("not an image"), qint64(12));
        else if (failure == "truncated-image")
            QVERIFY(file.resize(40)); // PNG dimensions exist, but pixels cannot be decoded.
    }
    const auto result = ImageLoader::load(path);
    QVERIFY(!result.isValid());
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.filePath.isEmpty());
    QVERIFY(!result.originalSize.isValid());
}

QTEST_GUILESS_MAIN(ImageLoaderTest)
#include "tst_imageloader.moc"
