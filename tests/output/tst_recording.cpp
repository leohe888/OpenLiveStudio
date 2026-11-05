#include <QStandardPaths>
#include "output/mediaencoder.h"
#include "output/recordingcontroller.h"
#include "output/outputsettings.h"
#include <QDir>
#include "graphics/compositor.h"
#include "audio/audiomixer.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QScopeGuard>
#include <QSignalSpy>

class RecordingTest : public QObject
{
    Q_OBJECT
    static QJsonObject probe(const QString &path)
    {
        QProcess process;
        process.start(QStringLiteral(OPENLIVE_FFPROBE),
                      {"-v", "error", "-show_streams", "-show_format", "-of", "json", path});
        if (!process.waitForFinished(10000) || process.exitCode())
            return {};
        return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    }

    static void verifyFile(const QString &path, double minimumDurationSeconds)
    {
        const auto info = probe(path);
        QVERIFY(!info.isEmpty());
        const auto streams = info["streams"].toArray();
        QCOMPARE(streams.size(), 2);
        QJsonObject video, audio;
        for (const auto &value : streams) {
            const auto stream = value.toObject();
            if (stream["codec_type"] == "video")
                video = stream;
            else
                audio = stream;
        }
        QCOMPARE(video["codec_name"].toString(), QString("h264"));
        QCOMPARE(video["width"].toInt(), 1920);
        QCOMPARE(video["height"].toInt(), 1080);
        QCOMPARE(video["pix_fmt"].toString(), QString("yuv420p"));
        QCOMPARE(video["avg_frame_rate"].toString(), QString("30/1"));
        QCOMPARE(audio["codec_name"].toString(), QString("aac"));
        QCOMPARE(audio["sample_rate"].toString(), QString("48000"));
        QCOMPARE(audio["channels"].toInt(), 2);
        const double videoDurationSeconds = video["duration"].toString().toDouble(),
                     audioDurationSeconds = audio["duration"].toString().toDouble();
        QVERIFY(videoDurationSeconds >= minimumDurationSeconds);
        QVERIFY(std::abs(videoDurationSeconds - audioDurationSeconds) < .035);
        QVERIFY(std::abs(video["start_time"].toString().toDouble()) < .001);
        QVERIFY(std::abs(audio["start_time"].toString().toDouble()) < .025);
    }

private slots:

    void temporarySettingsValidateLockAndGenerateUniquePaths()
    {
        QTemporaryDir directory;
        OutputSettings settings;
        const auto defaults = settings.recordingDirectory();
        QCOMPARE(settings.recordingBitrateKbps(), 12000);
        QVERIFY(settings.streamKey().isEmpty());
        const auto selected = directory.filePath("new/output");
        QVERIFY(settings.apply(selected, 16000, "rtmp://example.test/live", "unit-test-key"));
        QVERIFY(!QFileInfo::exists(selected));
        QVERIFY(!settings.apply(selected, 0, "", ""));
        QVERIFY(!settings.apply(selected, 16000, "https://example.test/live", ""));
        QCOMPARE(settings.recordingBitrateKbps(), 16000);
        settings.setRecordingLocked(true);
        QVERIFY(!settings.apply(directory.path(), 12000, "", ""));
        QVERIFY(settings.apply(selected, 16000, "rtmp://example.test/app", "new-unit-key"));
        const auto first = settings.nextRecordingPath();
        QVERIFY(!first.isEmpty());
        QVERIFY(QFileInfo(selected).isDir());
        QVERIFY(!QFileInfo::exists(first));
        QFile file(first);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        file.write("keep");
        file.close();
        const auto next = settings.nextRecordingPath();
        QVERIFY(next != first);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("keep"));
        OutputSettings fresh;
        QCOMPARE(fresh.recordingDirectory(), defaults);
        QCOMPARE(fresh.recordingBitrateKbps(), 12000);
        QVERIFY(fresh.streamKey().isEmpty());
    }

    void encoderUnicodeNoOverwriteAndTracks()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QString::fromUtf8("测试录制.mp4"));
        try {
            MediaEncoder encoder;
            encoder.open(path);
            QImage image(1920, 1080, QImage::Format_RGBA8888);
            image.fill(Qt::red);
            for (int i = 0; i < 6; ++i) {
                encoder.encodeVideoFrame(image, i);
                encoder.encodeAudioSamples(QVector<float>(3200, 0));
            }
            encoder.finish();
            QVERIFY(encoder.completed());
        } catch (const QString &error) {
            QFAIL(qPrintable(error));
        }
        verifyFile(path, .19);
        const auto size = QFileInfo(path).size();
        bool failed = false;
        try {
            MediaEncoder duplicate;
            duplicate.open(path);
        } catch (const QString &) {
            failed = true;
        }
        QVERIFY(failed);
        QCOMPARE(QFileInfo(path).size(), size);
    }

    void nativeReadbackKeepsRedAndBlueChannels()
    {
        QTemporaryDir directory;
        for (auto format : {QImage::Format_RGBA8888, QImage::Format_ARGB32_Premultiplied}) {
            const auto path = directory.filePath(QString::number(int(format)) + ".mp4");
            try {
                MediaEncoder encoder;
                encoder.open(path);
                QImage image(1920, 1080, format);
                image.fill(QColor(240, 30, 10));
                for (int i = 0; i < 3; ++i) {
                    encoder.encodeVideoFrame(image, i);
                    encoder.encodeAudioSamples(QVector<float>(3200, 0));
                }
                encoder.finish();
            } catch (const QString &error) {
                QFAIL(qPrintable(error));
            }
            QProcess decoder;
            decoder.start(QStandardPaths::findExecutable("ffmpeg",
                {QFileInfo(QStringLiteral(OPENLIVE_FFPROBE)).absolutePath()}),
                          {"-v", "error", "-threads", "1", "-filter_threads", "1", "-i", path,
                           "-frames:v", "1", "-vf", "scale=1:1", "-pix_fmt", "rgb24", "-f",
                           "rawvideo", "pipe:1"});
            QVERIFY(decoder.waitForFinished(10000));
            QCOMPARE(decoder.exitCode(), 0);
            const auto rgb = decoder.readAllStandardOutput();
            QCOMPARE(rgb.size(), 3);
            QVERIFY(static_cast<unsigned char>(rgb[0]) > 180);
            QVERIFY(static_cast<unsigned char>(rgb[1]) < 80);
            QVERIFY(static_cast<unsigned char>(rgb[2]) < 80);
        }
    }

    void recordsCompositionWithSilentAudioAndRestarts()
    {
        Compositor compositor;
        AudioMixer mixer;
        mixer.start();
        QImage image(32, 16, QImage::Format_RGBA8888);
        image.fill(Qt::blue);
        compositor.setSources({{"image", image, true, {{960, 540}, {1920, 1080}, 0}}});
        QVERIFY(compositor.startRendering());
        QVERIFY(!compositor.captureImage().isNull());
        QTemporaryDir directory;
        RecordingController recording(compositor, mixer);
        const auto path = directory.filePath("session.mp4");
        QVERIFY(recording.startRecording(path));
        QVERIFY(!recording.startRecording(directory.filePath("duplicate.mp4")));
        QTRY_COMPARE_WITH_TIMEOUT(recording.state(), RecordingState::Recording, 5000);
        QTest::qWait(600);
        image.fill(Qt::green);
        compositor.setSources({{"image", image, true, {{960, 540}, {1920, 1080}, 15}}});
        QTest::qWait(100);
        recording.stopRecording();
        recording.stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(), 10000);
        QVERIFY2(recording.lastError().isEmpty(), qPrintable(recording.lastError()));
        QVERIFY(recording.fileComplete());
        qInfo() << "recorded frames / missing audio samples:" << recording.status().videoFrames
                << recording.status().missingAudioFrames;
        QVERIFY(recording.status().missingAudioFrames < 480);
        verifyFile(path, .65);
        QVERIFY(compositor.isRunning());
        QVERIFY(mixer.isRunning());
        QVERIFY(mixer.takeOutput().empty());
        QVERIFY(!recording.startRecording(directory.filePath("invalid.mp4"), 0));
        QTest::qWait(150);
        QCOMPARE(recording.state(), RecordingState::Error);
        QVERIFY(!recording.lastError().isEmpty());
        QVERIFY(recording.startRecording(directory.filePath("again.mp4")));
        QTRY_COMPARE_WITH_TIMEOUT(recording.state(), RecordingState::Recording, 5000);
        QTest::qWait(200);
        recording.stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(), 10000);
        QVERIFY2(recording.lastError().isEmpty(), qPrintable(recording.lastError()));
        verifyFile(recording.filePath(), .19);
        compositor.stopRendering();
        mixer.stop();
        QVERIFY(mixer.wait(1000));
    }

    void errorsAndCancelDoNotOverwriteOrLeaveBusyState()
    {
        Compositor compositor;
        AudioMixer mixer;
        mixer.start();
        QVERIFY(compositor.startRendering());
        QVERIFY(!compositor.captureImage().isNull());
        RecordingController recording(compositor, mixer);
        QTemporaryDir directory;
        const auto path = directory.filePath("existing.mp4");
        QFile sentinel(path);
        QVERIFY(sentinel.open(QIODevice::WriteOnly));
        sentinel.write("preserve");
        sentinel.close();
        QVERIFY(recording.startRecording(path));
        QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(), 5000);
        QCOMPARE(recording.state(), RecordingState::Error);
        QVERIFY(!recording.lastError().isEmpty());
        QVERIFY(sentinel.open(QIODevice::ReadOnly));
        QCOMPARE(sentinel.readAll(), QByteArray("preserve"));
        sentinel.close();
        QVERIFY(recording.startRecording(directory.filePath("cancel.mp4")));
        recording.stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(), 5000);
        compositor.stopRendering();
        mixer.stop();
        mixer.wait();
    }

    void temporarySchedulingPauseRecoversWithoutSheddingStreaming()
    {
        Compositor compositor;
        AudioMixer mixer;
        mixer.start();
        QVERIFY(compositor.startRendering());
        QVERIFY(!compositor.captureImage().isNull());
        const auto cleanup = qScopeGuard([&] {
            compositor.stopRendering();
            mixer.stop();
            mixer.wait();
        });
        RecordingController recording(compositor, mixer);
        QSignalSpy overloaded(&recording, &RecordingController::overloaded);
        QTemporaryDir directory;
        qputenv("OPENLIVE_RECORDING_PAUSE_ONCE_MS", "550");
        const auto restore = qScopeGuard([] {
            qunsetenv("OPENLIVE_RECORDING_PAUSE_ONCE_MS");
        });
        QVERIFY(recording.startRecording(directory.filePath("pause.mp4")));
        QTRY_COMPARE_WITH_TIMEOUT(recording.state(), RecordingState::Recording, 5000);
        QTest::qWait(2000);
        QCOMPARE(recording.state(), RecordingState::Recording);
        QCOMPARE(overloaded.count(), 0);
        QVERIFY(recording.status().lagMs < 400);
        recording.stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(), 5000);
        QVERIFY(recording.fileComplete());
        verifyFile(recording.filePath(), 1.9);
    }

    void overloadStopsWithPlayableFileAndCanRecover()
    {
        Compositor compositor;
        AudioMixer mixer;
        mixer.start();
        QVERIFY(compositor.startRendering());
        QVERIFY(!compositor.captureImage().isNull());
        RecordingController recording(compositor, mixer);
        QTemporaryDir directory;
        qputenv("OPENLIVE_RECORDING_ENCODER_DELAY_MS", "100");
        const auto restore = qScopeGuard([] {
            qunsetenv("OPENLIVE_RECORDING_ENCODER_DELAY_MS");
        });
        QVERIFY(recording.startRecording(directory.filePath("overload.mp4")));
        QTRY_COMPARE_WITH_TIMEOUT(recording.state(), RecordingState::Error, 10000);
        QVERIFY(!recording.lastError().isEmpty());
        QVERIFY(recording.fileComplete());
        verifyFile(recording.filePath(), .1);
        qunsetenv("OPENLIVE_RECORDING_ENCODER_DELAY_MS");
        QVERIFY(recording.startRecording(directory.filePath("recovered.mp4")));
        QTRY_COMPARE_WITH_TIMEOUT(recording.state(), RecordingState::Recording, 5000);
        QTest::qWait(250);
        recording.stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(), 5000);
        QVERIFY2(recording.lastError().isEmpty(), qPrintable(recording.lastError()));
        QVERIFY(recording.fileComplete());
        verifyFile(recording.filePath(), .2);
    }
};

int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setRenderableType(QSurfaceFormat::OpenGL);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    RecordingTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_recording.moc"
