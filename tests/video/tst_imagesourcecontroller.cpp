#include "video/image/imagesourcecontroller.h"
#include "video/videosourcemodel.h"
#include <QBuffer>
#include <QFile>
#include <QFileDialog>
#include <QTimer>
#include <QApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>

class ImageSourceControllerTest : public QObject
{
    Q_OBJECT
private slots:
    void loadsAddsAndSynchronizes()
    {
        QTemporaryDir dir;
        QImage image(64, 32, QImage::Format_RGBA8888);
        image.fill(QColor(255, 0, 0, 128));
        const QString path = dir.filePath("image.png");
        QVERIFY(image.save(path));
        VideoSourceModel model;
        ImageSourceController controller(model);
        QList<PreviewSource> snapshot;
        int updates = 0;
        connect(&controller, &ImageSourceController::sourcesChanged, this,
            [&](const QList<PreviewSource> &sources) { snapshot = sources; ++updates; });
        QSignalSpy added(&controller, &ImageSourceController::imageAdded);
        QObject owner;
        controller.addImage(" First ", path, &owner);
        controller.addImage("Duplicate submission", path, &owner);
        QTRY_COMPARE(added.count(), 1);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(snapshot.size(), 1);
        QCOMPARE(snapshot[0].image.pixelColor(0, 0), QColor(255, 0, 0, 128));
        QCOMPARE(model.index(0, 0).data(VideoSourceModel::NameRole).toString(), QString("First"));
        QCOMPARE(model.index(0, 0).data(VideoSourceModel::ImageLoadStateRole).toString(), QString("ready"));
        QVERIFY(!model.index(0, 0).data(VideoSourceModel::CaptureStateRole).isValid());
        QCOMPARE(model.index(0, 0).data(VideoSourceModel::PixelSizeRole).toSize(), image.size());
        QVERIFY(QFileInfo(model.index(0, 0).data(VideoSourceModel::FilePathRole).toString()).isAbsolute());
        const QString first = snapshot[0].id;
        const auto key = snapshot[0].image.cacheKey();
        const auto filter = model.addColorCorrection(first);
        QVERIFY(model.setFilterParameters(first, filter, .3, .7, 1.2));
        QCOMPARE(snapshot[0].filters.size(), 1);
        QCOMPARE(snapshot[0].filters[0].id, filter);
        QCOMPARE(snapshot[0].filters[0].brightness, .3f);
        QCOMPARE(snapshot[0].image.cacheKey(), key);
        controller.addImage("Second", path, &owner);
        QTRY_COMPARE(added.count(), 2);
        QCOMPARE(snapshot.size(), 2);
        const QString second = snapshot[0].id;
        QVERIFY(first != second);
        QCOMPARE(snapshot[1].image.cacheKey(), key);
        QVERIFY(model.setSourceVisible(second, false));
        QVERIFY(!snapshot[0].visible);
        const int beforeLock = updates;
        QVERIFY(model.setSourceLocked(second, true));
        model.selectSource(first);
        QCOMPARE(updates, beforeLock + 1);
        QVERIFY(snapshot[0].locked);
        const auto beforeTransform = snapshot[1].image.cacheKey();
        SourceTransform transform{{800, 500}, {100, 80}, 30};
        QVERIFY(model.setSourceTransform(first, transform));
        QVERIFY(snapshot[1].transform == transform);
        QCOMPARE(snapshot[1].image.cacheKey(), beforeTransform);
        QVERIFY(model.moveSource(second, 1));
        QCOMPARE(snapshot[0].id, first);
        QCOMPARE(snapshot[0].image.cacheKey(), key);
        QVERIFY(model.removeSource(first));
        QCOMPARE(snapshot.size(), 1);
        QCOMPARE(snapshot[0].id, second);
        QVERIFY(model.removeSource(second));
        QVERIFY(snapshot.isEmpty());
        QVERIFY(QFile::exists(path));
    }

    void rejectsInvalidInputAndDiscardsCancelledResults()
    {
        QTemporaryDir dir;
        const QString corrupt = dir.filePath("bad.png");
        QFile file(corrupt);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not an image");
        file.close();
        VideoSourceModel model;
        ImageSourceController controller(model);
        QSignalSpy errors(&controller, &ImageSourceController::addFailed);
        QSignalSpy added(&controller, &ImageSourceController::imageAdded);
        QObject owner;
        controller.addImage(" ", corrupt, &owner);
        controller.addImage("Remote", "https://example.com/image.png", &owner);
        QCOMPARE(errors.count(), 2);
        controller.addImage("Missing", dir.filePath("missing.png"), &owner);
        QTRY_COMPARE(errors.count(), 3);
        controller.addImage("Corrupt", corrupt, &owner);
        QTRY_COMPARE(errors.count(), 4);
        QImage image(64, 32, QImage::Format_RGBA8888);
        image.fill(Qt::blue);
        const QString path = dir.filePath("valid.png");
        QVERIFY(image.save(path));
        controller.addImage("Cancelled", path, &owner);
        controller.cancelAdd(&owner);
        auto *temporaryOwner = new QObject;
        controller.addImage("Destroyed", path, temporaryOwner);
        delete temporaryOwner;
        // A subsequent completed request proves earlier queued completions were processed.
        controller.addImage("Kept", path, &owner);
        QTRY_COMPARE(added.count(), 1);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.index(0, 0).data(VideoSourceModel::NameRole).toString(), QString("Kept"));
        QCOMPARE(errors.count(), 4);
    }

    void appliesExifOrientation()
    {
        QTemporaryDir dir;
        QImage image(40, 20, QImage::Format_RGB888);
        image.fill(Qt::red);
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "JPEG"));
        // EXIF orientation 6: rotate 90 degrees clockwise.
        const QByteArray exif = QByteArray::fromHex(
            "ffe1002245786966000049492a0008000000010012010300010000000600000000000000");
        jpeg.insert(2, exif);
        const QString path = dir.filePath("rotated.jpg");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(jpeg), jpeg.size());
        file.close();
        VideoSourceModel model;
        ImageSourceController controller(model);
        QObject owner;
        controller.addImage("Rotated", path, &owner);
        QTRY_COMPARE(model.rowCount(), 1);
        QCOMPARE(model.index(0, 0).data(VideoSourceModel::PixelSizeRole).toSize(), QSize(20, 40));
    }

    void readsOnlyFirstAnimationFrame()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("animated.gif");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        // Two frames: red, then blue.
        const auto gif = QByteArray::fromBase64(
            "R0lGODlhAgABAIEAAP8AAAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQAAQAAACwAAAAAAgABAAAIBQABAAgIACH5BAEBAAEALAAAAAACAAEAgQAA/wAAAAAAAAAAAAgFAAEACAgAOw==");
        QCOMPARE(file.write(gif), gif.size());
        file.close();
        VideoSourceModel model;
        ImageSourceController controller(model);
        QList<PreviewSource> snapshot;
        connect(&controller, &ImageSourceController::sourcesChanged, this,
            [&](const QList<PreviewSource> &sources) { snapshot = sources; });
        QObject owner;
        controller.addImage("Animation", path, &owner);
        QTRY_COMPARE(model.rowCount(), 1);
        QCOMPARE(snapshot[0].image.pixelColor(0, 0), QColor(Qt::red));
    }

    void browseCancellationReturnsEmptyPath()
    {
        VideoSourceModel model;
        ImageSourceController controller(model);
        const bool previous = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        bool found = false;
        QTimer::singleShot(0, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *dialog = qobject_cast<QFileDialog *>(widget)) {
                    found = true;
                    dialog->reject();
                }
            }
        });
        const QString result = controller.browseImage(QString());
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous);
        QVERIFY(found);
        QVERIFY(result.isEmpty());
        QCOMPARE(model.rowCount(), 0);
    }

    void loadsExistingEntriesAndReportsFailuresOnce()
    {
        QTemporaryDir dir;
        QImage image(16, 32, QImage::Format_RGBA8888);
        image.fill(Qt::green);
        const QString path = dir.filePath("valid.png");
        QVERIFY(image.save(path));
        VideoSourceModel model({{"valid", "Valid", ImageSourceDetails{ImageConfig{path}}},
                               {"bad", "Bad", ImageSourceDetails{ImageConfig{dir.filePath("missing.png")}}}});
        ImageSourceController controller(model);
        QSignalSpy errors(&controller, &ImageSourceController::sourceError);
        QStringList loadStates;
        connect(&model, &VideoSourceModel::dataChanged, this,
            [&](const QModelIndex &index, const QModelIndex &, const QList<int> &roles) {
                if (roles.contains(VideoSourceModel::ImageLoadStateRole))
                    loadStates.append(index.data(VideoSourceModel::ImageLoadStateRole).toString());
            });
        QList<PreviewSource> snapshot;
        connect(&controller, &ImageSourceController::sourcesChanged, this,
            [&](const QList<PreviewSource> &sources) { snapshot = sources; });
        QTRY_COMPARE(errors.count(), 1);
        QTRY_VERIFY(snapshot.size() == 2 && !snapshot[0].image.isNull());
        QVERIFY(snapshot[1].image.isNull());
        QCOMPARE(model.index(0, 0).data(VideoSourceModel::PixelSizeRole).toSize(), image.size());
        QCOMPARE(model.index(0, 0).data(VideoSourceModel::ImageLoadStateRole).toString(), QString("ready"));
        QCOMPARE(model.index(1, 0).data(VideoSourceModel::ImageLoadStateRole).toString(), QString("error"));
        QVERIFY(!model.index(1, 0).data(VideoSourceModel::ImageLoadErrorRole).toString().isEmpty());
        QVERIFY(loadStates.contains("loading"));
        QVERIFY(loadStates.contains("ready"));
        QVERIFY(loadStates.contains("error"));
        model.setSourceVisible("bad", false);
        model.setSourceVisible("bad", true);
        QCOMPARE(errors.count(), 1);
        model.removeSource("bad");
        QCOMPARE(snapshot.size(), 1);
    }
};

QTEST_MAIN(ImageSourceControllerTest)
#include "tst_imagesourcecontroller.moc"
