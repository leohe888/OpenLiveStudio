#include "output/mp4encoder.h"
#include "output/recordingcontroller.h"
#include "output/outputsettings.h"
#include <QDir>
#include "preview/compositor.h"
#include "audio/audiomixer.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

class RecordingTest : public QObject {
    Q_OBJECT
    static QJsonObject probe(const QString &path)
    {
        QProcess process;
        process.start(QStringLiteral(OPENLIVE_FFPROBE),{"-v","error","-show_streams","-show_format","-of","json",path});
        if (!process.waitForFinished(10000) || process.exitCode()) return {};
        return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    }
    static void verifyFile(const QString &path,double minimum)
    {
        const auto info=probe(path); QVERIFY(!info.isEmpty());
        const auto streams=info["streams"].toArray(); QCOMPARE(streams.size(),2);
        QJsonObject video,audio;
        for (const auto &value:streams) {
            const auto stream=value.toObject();
            if (stream["codec_type"]=="video") video=stream; else audio=stream;
        }
        QCOMPARE(video["codec_name"].toString(),QString("h264"));
        QCOMPARE(video["width"].toInt(),1920); QCOMPARE(video["height"].toInt(),1080);
        QCOMPARE(video["pix_fmt"].toString(),QString("yuv420p"));
        QCOMPARE(video["avg_frame_rate"].toString(),QString("30/1"));
        QCOMPARE(audio["codec_name"].toString(),QString("aac"));
        QCOMPARE(audio["sample_rate"].toString(),QString("48000"));
        QCOMPARE(audio["channels"].toInt(),2);
        const double vd=video["duration"].toString().toDouble(), ad=audio["duration"].toString().toDouble();
        QVERIFY(vd>=minimum); QVERIFY(std::abs(vd-ad)<.035);
        QVERIFY(std::abs(video["start_time"].toString().toDouble())<.001);
        QVERIFY(std::abs(audio["start_time"].toString().toDouble())<.025);
    }
private slots:
    void temporarySettingsValidateLockAndGenerateUniquePaths()
    {
        QTemporaryDir directory;
        OutputSettings settings;
        const auto defaults=settings.recordingDirectory();
        QCOMPARE(settings.quality(),1); QVERIFY(settings.streamKey().isEmpty());
        const auto selected=directory.filePath("new/output");
        QVERIFY(settings.apply(selected,2,"rtmps://example.test/live","unit-test-key"));
        QVERIFY(!QFileInfo::exists(selected));
        QVERIFY(!settings.apply(selected,3,"",""));
        QVERIFY(!settings.apply(selected,2,"https://example.test/live",""));
        QCOMPARE(settings.quality(),2);
        settings.setRecordingLocked(true);
        QVERIFY(!settings.apply(directory.path(),1,"",""));
        QVERIFY(settings.apply(selected,2,"rtmp://example.test/app","new-unit-key"));
        const auto first=settings.nextRecordingPath(); QVERIFY(!first.isEmpty());
        QVERIFY(QFileInfo(selected).isDir()); QVERIFY(!QFileInfo::exists(first));
        QFile file(first); QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly)); file.write("keep"); file.close();
        const auto next=settings.nextRecordingPath(); QVERIFY(next!=first);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),QByteArray("keep"));
        OutputSettings fresh;
        QCOMPARE(fresh.recordingDirectory(),defaults); QCOMPARE(fresh.quality(),1); QVERIFY(fresh.streamKey().isEmpty());
    }
    void encoderUnicodeNoOverwriteAndTracks()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path=directory.filePath(QString::fromUtf8("测试录制.mp4"));
        try {
            Mp4Encoder encoder; encoder.open(path);
            QImage image(1920,1080,QImage::Format_RGBA8888); image.fill(Qt::red);
            for (int i=0;i<6;++i) { encoder.video(image,i); encoder.audio(QVector<float>(3200,0)); }
            encoder.finish(); QVERIFY(encoder.completed());
        } catch (const QString &error) { QFAIL(qPrintable(error)); }
        verifyFile(path,.19);
        const auto size=QFileInfo(path).size();
        bool failed=false;
        try { Mp4Encoder duplicate; duplicate.open(path); } catch (const QString &) { failed=true; }
        QVERIFY(failed); QCOMPARE(QFileInfo(path).size(),size);
    }
    void recordsCompositionWithSilentAudioAndRestarts()
    {
        Compositor compositor;
        AudioMixer mixer; mixer.start();
        QImage image(32,16,QImage::Format_RGBA8888); image.fill(Qt::blue);
        compositor.setSources({{"image",image,true,{{960,540},{1920,1080},0}}});
        QVERIFY(compositor.startRendering());
        QVERIFY(!compositor.captureImage().isNull());
        QTemporaryDir directory;
        RecordingController recording(compositor,mixer);
        const auto path=directory.filePath("session.mp4");
        QVERIFY(recording.startRecording(path));
        QVERIFY(!recording.startRecording(directory.filePath("duplicate.mp4")));
        QTRY_COMPARE_WITH_TIMEOUT(recording.state(),QString("recording"),5000);
        QTest::qWait(600);
        image.fill(Qt::green); compositor.setSources({{"image",image,true,{{960,540},{1920,1080},15}}});
        QTest::qWait(100);
        recording.stopRecording(); recording.stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(),10000);
        QVERIFY2(recording.lastError().isEmpty(),qPrintable(recording.lastError()));
        QVERIFY(recording.fileComplete());
        qInfo()<<"recorded frames / missing audio samples:"<<recording.status().videoFrames<<recording.status().missingAudioSamples;
        QVERIFY(recording.status().missingAudioSamples<480);
        verifyFile(path,.65);
        QVERIFY(compositor.isRunning()); QVERIFY(mixer.isRunning());
        QVERIFY(mixer.takeOutput().empty());
        QVERIFY(recording.startRecording(directory.filePath("again.mp4")));
        QTRY_COMPARE_WITH_TIMEOUT(recording.state(),QString("recording"),5000);
        QTest::qWait(200); recording.stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(),10000);
        QVERIFY2(recording.lastError().isEmpty(),qPrintable(recording.lastError()));
        verifyFile(recording.filePath(),.19);
        compositor.stopRendering(); mixer.stop(); QVERIFY(mixer.wait(1000));
    }
    void errorsAndCancelDoNotOverwriteOrLeaveBusyState()
    {
        Compositor compositor; AudioMixer mixer; mixer.start();
        QVERIFY(compositor.startRendering()); QVERIFY(!compositor.captureImage().isNull());
        RecordingController recording(compositor,mixer);
        QTemporaryDir directory;
        const auto path=directory.filePath("existing.mp4");
        QFile sentinel(path); QVERIFY(sentinel.open(QIODevice::WriteOnly)); sentinel.write("preserve"); sentinel.close();
        QVERIFY(recording.startRecording(path));
        QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(),5000);
        QCOMPARE(recording.state(),QString("error")); QVERIFY(!recording.lastError().isEmpty());
        QVERIFY(sentinel.open(QIODevice::ReadOnly)); QCOMPARE(sentinel.readAll(),QByteArray("preserve")); sentinel.close();
        QVERIFY(recording.startRecording(directory.filePath("cancel.mp4"))); recording.stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(),5000);
        compositor.stopRendering(); mixer.stop(); mixer.wait();
    }
};
int main(int argc,char **argv)
{
    QSurfaceFormat format; format.setVersion(3,3); format.setProfile(QSurfaceFormat::CoreProfile);
    format.setRenderableType(QSurfaceFormat::OpenGL); QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc,argv); RecordingTest test; return QTest::qExec(&test,argc,argv);
}
#include "tst_recording.moc"
