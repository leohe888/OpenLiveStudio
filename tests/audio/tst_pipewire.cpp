#include "audio/audiosourcemanager.h"
#include "output/mediaencoder.h"
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <cstring>

// An isolated daemon with generated audio: never enumerates or records host devices.
class PipeWireTest : public QObject
{
    Q_OBJECT
    QTemporaryDir directory;
    QProcess daemon, policy, playback;
    QMap<QByteArray, QByteArray> previousEnvironment;
    bool configured = false;

    static void stopProcess(QProcess &process)
    {
        if (process.state() == QProcess::NotRunning)
            return;
        process.terminate();
        if (!process.waitForFinished(2000)) {
            process.kill();
            process.waitForFinished(2000);
        }
    }

    QByteArray command(const QString &program, const QStringList &arguments)
    {
        QProcess process;
        process.start(program, arguments);
        if (!process.waitForFinished(3000) || process.exitCode())
            return {};
        return process.readAllStandardOutput();
    }

    bool setDefault(AudioSourceType type, const QString &name)
    {
        const auto key = type == AudioSourceType::Microphone ? "default.audio.source"
                                                           : "default.audio.sink";
        return !command("pw-metadata", {"-n", "default", "0", key,
            QString::fromUtf8(QJsonDocument(QJsonObject{{"name", name}})
                                  .toJson(QJsonDocument::Compact)), "Spa:String:JSON"}).isEmpty();
    }

    int nodeId(const QString &name)
    {
        for (const auto &value : QJsonDocument::fromJson(command("pw-dump", {})).array()) {
            const auto object = value.toObject();
            const auto props = object["info"].toObject()["props"].toObject();
            if (props["node.name"].toString() == name)
                return object["id"].toInt(-1);
        }
        return -1;
    }

    bool startServer()
    {
        daemon.start("pipewire", {"-c", directory.filePath("pipewire.conf")});
        if (!daemon.waitForStarted(2000))
            return false;
        const auto deadline = audioClockNs() + 3000000000LL;
        while (!QFile::exists(directory.filePath("openlive-test")) && audioClockNs() < deadline)
            QTest::qWait(20);
        if (!QFile::exists(directory.filePath("openlive-test"))) {
            qWarning().noquote() << daemon.readAllStandardError();
            return false;
        }
        policy.start("pipewire-media-session", QStringList{});
        if (!policy.waitForStarted(2000))
            return false;
        QTest::qWait(150);
        return setDefault(AudioSourceType::Microphone, "test-mic-a") &&
               setDefault(AudioSourceType::SystemAudio, "test-sink-a");
    }

    bool playTone(const QString &name)
    {
        stopProcess(playback);
        const auto id = nodeId(name);
        if (id < 0)
            return false;
        QFile wave(directory.filePath("tone.wav"));
        if (!wave.open(QIODevice::WriteOnly))
            return false;
        QDataStream output(&wave);
        output.setByteOrder(QDataStream::LittleEndian);
        output.setFloatingPointPrecision(QDataStream::SinglePrecision);
        constexpr quint32 frames = 48000 * 20, bytes = frames * 2 * 4;
        output.writeRawData("RIFF", 4);
        output << quint32(bytes + 36);
        output.writeRawData("WAVEfmt ", 8);
        output << quint32(16) << quint16(3) << quint16(2) << quint32(48000)
               << quint32(48000 * 8) << quint16(8) << quint16(32);
        output.writeRawData("data", 4);
        output << bytes;
        for (quint32 i = 0; i < frames; ++i) {
            const auto tone = float(std::sin(2 * std::acos(-1.) * 880 * i / 48000));
            output << .15f * tone << .3f * tone;
        }
        wave.close();
        playback.start("pw-cat", {"--playback", "--target", QString::number(id), wave.fileName()});
        return playback.waitForStarted(2000);
    }

    static float peak(const std::shared_ptr<AudioPacketQueue> &queue)
    {
        float result = 0;
        for (const auto &block : queue->takeAll().packets) {
            if (block.timestampNs < audioClockNs() - 1000000000LL ||
                block.timestampNs > audioClockNs() + 100000000LL)
                QTest::qFail("Captured timestamp is outside the media clock window", __FILE__, __LINE__);
            for (float sample : block.samples) {
                if (!std::isfinite(sample))
                    QTest::qFail("Captured sample is not finite", __FILE__, __LINE__);
                result = std::max(result, std::abs(sample));
            }
        }
        return result;
    }

    bool hasDecodedAudio(const QString &path)
    {
        const auto executable = QFileInfo(QStringLiteral(OPENLIVE_FFPROBE))
                                    .absolutePath() + "/ffmpeg";
        const auto bytes = command(executable, {"-v", "error", "-threads", "1", "-i", path,
            "-vn", "-f", "f32le", "-acodec", "pcm_f32le", "-"});
        if (bytes.size() < 48000 * 2 * int(sizeof(float)))
            return false;
        double energy = 0;
        for (qsizetype i = 0; i + 4 <= bytes.size(); i += 4) {
            float sample;
            memcpy(&sample, bytes.constData() + i, 4);
            if (!std::isfinite(sample))
                return false;
            energy += double(sample) * sample;
        }
        return energy / (bytes.size() / 4) > .00001;
    }

private slots:
    void initTestCase()
    {
        for (const char *program : {"pipewire", "pipewire-media-session", "pw-metadata",
                                    "pw-cli", "pw-dump", "pw-cat"})
            if (QStandardPaths::findExecutable(program).isEmpty())
                QSKIP("Isolated integration requires PipeWire tools and pipewire-media-session");
        QVERIFY(directory.isValid());
        QFile config(directory.filePath("pipewire.conf"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        // Pause idle generated sources immediately: the 0.3.48 live test source
        // otherwise exhausts its buffers between capture clients and stops its timer.
        config.write(R"(
context.properties = {
 core.daemon = true
 core.name = openlive-test
 support.dbus = false
 default.clock.rate = 48000
 default.clock.quantum = 480
 default.clock.min-quantum = 480
 default.clock.max-quantum = 480
}
context.spa-libs = {
 audio.convert.* = audioconvert/libspa-audioconvert
 support.* = support/libspa-support
 audiotestsrc = audiotestsrc/libspa-audiotestsrc
}
context.modules = [
 { name = libpipewire-module-protocol-native }
 { name = libpipewire-module-metadata }
 { name = libpipewire-module-spa-node-factory }
 { name = libpipewire-module-client-node }
 { name = libpipewire-module-adapter }
 { name = libpipewire-module-link-factory }
 { name = libpipewire-module-access }
]
context.objects = [
 { factory = metadata args = { metadata.name = default } }
 { factory = spa-node-factory args = { factory.name = support.node.driver node.name = Dummy-Driver priority.driver = 20000 } }
 { factory = adapter args = { factory.name = audiotestsrc node.name = test-mic-a node.description = MicA media.class = Audio/Source node.pause-on-idle = true node.suspend-on-idle = true audio.position = [ FL FR ] } }
 { factory = adapter args = { factory.name = audiotestsrc node.name = test-mic-b node.description = MicB media.class = Audio/Source node.pause-on-idle = true node.suspend-on-idle = true audio.position = [ FL FR ] } }
 { factory = adapter args = { factory.name = support.null-audio-sink node.name = test-sink-a node.description = SinkA media.class = Audio/Sink audio.position = [ FL FR ] } }
 { factory = adapter args = { factory.name = support.null-audio-sink node.name = test-sink-b node.description = SinkB media.class = Audio/Sink audio.position = [ FL FR ] } }
]
)");
        config.close();
        QVERIFY(QDir().mkpath(directory.filePath("pipewire/media-session.d")));
        QFile session(directory.filePath("pipewire/media-session.d/media-session.conf"));
        QVERIFY(session.open(QIODevice::WriteOnly));
        session.write(R"(
context.properties = { support.dbus = false }
context.modules = [
 { name = libpipewire-module-protocol-native }
 { name = libpipewire-module-client-node }
 { name = libpipewire-module-adapter }
 { name = libpipewire-module-session-manager }
 { name = libpipewire-module-metadata }
]
session.modules = { default = [ flatpak portal suspend-node policy-node ] }
)");
        session.close();
        const QMap<QByteArray, QByteArray> environment{
            {"XDG_RUNTIME_DIR", directory.path().toUtf8()},
            {"PIPEWIRE_RUNTIME_DIR", directory.path().toUtf8()},
            {"PIPEWIRE_REMOTE", "openlive-test"},
            {"XDG_CONFIG_HOME", directory.path().toUtf8()},
            {"XDG_STATE_HOME", directory.path().toUtf8()}};
        for (auto it = environment.cbegin(); it != environment.cend(); ++it) {
            previousEnvironment.insert(it.key(), qgetenv(it.key()));
            qputenv(it.key(), it.value());
        }
        configured = true;
        QVERIFY(startServer());
    }

    void catalogAndRealSamples()
    {
        QString error;
        const auto devices = audioDevices(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(devices.size(), 6);
        QCOMPARE(devices[0].id, defaultAudioDeviceId(AudioSourceType::Microphone));
        QCOMPARE(devices[1].id, defaultAudioDeviceId(AudioSourceType::SystemAudio));
        auto micQueue = std::make_shared<AudioPacketQueue>();
        AudioCaptureWorker microphone(defaultAudioDeviceId(AudioSourceType::Microphone),
                                      AudioSourceType::Microphone, micQueue);
        microphone.start();
        QTRY_VERIFY2_WITH_TIMEOUT(microphone.status().state == "capturing", qPrintable(microphone.status().error), 5000);
        float micPeak = 0;
        QTRY_VERIFY_WITH_TIMEOUT((micPeak = std::max(micPeak, peak(micQueue))) > .01f, 3000);

        auto systemQueue = std::make_shared<AudioPacketQueue>();
        AudioCaptureWorker system("pipewire:system:test-sink-a", AudioSourceType::SystemAudio,
                                  systemQueue);
        system.start();
        QTRY_COMPARE_WITH_TIMEOUT(system.status().state, QString("capturing"), 5000);
        QTest::qWait(100);
        QVERIFY(peak(systemQueue) == 0); // Microphone tone must not leak into sink capture.
        QVERIFY(playTone("test-sink-b"));
        QTest::qWait(150);
        QCOMPARE(peak(systemQueue), 0.f); // An unselected output must not leak into this source.
        QVERIFY(playTone("test-sink-a"));
        float systemPeak = 0;
        QTRY_VERIFY2_WITH_TIMEOUT((systemPeak = std::max(systemPeak, peak(systemQueue))) > .1f,
            playback.readAllStandardError().constData(), 5000);
        microphone.stop();
        system.stop();
        QVERIFY(microphone.wait(1000));
        QVERIFY(system.wait(1000));
        QVERIFY(micQueue->takeAll().packets.empty());
        QVERIFY(systemQueue->takeAll().packets.empty());
    }

    void defaultsAndFixedDeviceRecovery_data()
    {
        QTest::addColumn<AudioSourceType>("type");
        QTest::addColumn<QString>("prefix");
        QTest::newRow("microphone") << AudioSourceType::Microphone << QString("test-mic-");
        QTest::newRow("system") << AudioSourceType::SystemAudio << QString("test-sink-");
    }

    void defaultsAndFixedDeviceRecovery()
    {
        QFETCH(AudioSourceType, type);
        QFETCH(QString, prefix);
        QVERIFY(setDefault(type, prefix + "a"));
        AudioSourceManager manager;
        const auto fixedId = QString("pipewire:%1:%2a").arg(toString(type), prefix);
        const auto fixed = manager.addSource("Fixed", type, fixedId);
        const auto follow = manager.addSource("Default", type, defaultAudioDeviceId(type));
        QVERIFY(!fixed.isEmpty() && !follow.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(fixed)->state, QString("capturing"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(follow)->state, QString("capturing"), 5000);
        QVERIFY(manager.setVolume(fixed, .4));
        QVERIFY(manager.setMuted(fixed, true));
        QVERIFY(setDefault(type, prefix + "b"));
        QTest::qWait(300);
        const auto id = nodeId(prefix + "a");
        QVERIFY(id >= 0);
        command("pw-cli", {"destroy", QString::number(id)});
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(fixed)->state, QString("error"), 5000);
        QTest::qWait(300);
        QCOMPARE(manager.source(follow)->state, QString("capturing"));
        const auto before = manager.mixer()->latestBlock()->sequence;
        QTest::qWait(150);
        QVERIFY(manager.mixer()->latestBlock()->sequence > before);
        const auto factory = type == AudioSourceType::Microphone ? "audiotestsrc"
                                                               : "support.null-audio-sink";
        const auto mediaClass = type == AudioSourceType::Microphone ? "Audio/Source" : "Audio/Sink";
        command("pw-cli", {"create-node", "adapter", QString(
            "{ factory.name = %1 node.name = %2a media.class = %3 audio.position = [ FL FR ] object.linger = true %4 }")
            .arg(factory, prefix, mediaClass, type == AudioSourceType::Microphone
                ? "node.pause-on-idle = true node.suspend-on-idle = true" : "")});
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(fixed)->state, QString("capturing"), 5000);
        QCOMPARE(manager.source(fixed)->deviceId, fixedId);
        QCOMPARE(manager.source(fixed)->volume, .4f);
        QVERIFY(manager.source(fixed)->muted);
        QVERIFY(setDefault(type, prefix + "a"));
        QVERIFY(manager.removeSource(fixed));
        QVERIFY(manager.removeSource(follow));
        QTRY_COMPARE_WITH_TIMEOUT(manager.activeWorkerCount(), 0, 1000);
    }

    void daemonRestartRecovers()
    {
        AudioSourceManager manager;
        const auto id = manager.addSource("Mic", AudioSourceType::Microphone,
                                         defaultAudioDeviceId(AudioSourceType::Microphone));
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(id)->state, QString("capturing"), 5000);
        stopProcess(playback);
        stopProcess(policy);
        stopProcess(daemon);
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(id)->state, QString("error"), 5000);
        QVERIFY(startServer());
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(id)->state, QString("capturing"), 5000);
    }

    void capturedMixReachesMp4AndRtmp()
    {
        AudioSourceManager manager;
        const auto mic = manager.addSource("Mic", AudioSourceType::Microphone,
                                           defaultAudioDeviceId(AudioSourceType::Microphone));
        const auto system = manager.addSource("System", AudioSourceType::SystemAudio,
                                              defaultAudioDeviceId(AudioSourceType::SystemAudio));
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(mic)->state, QString("capturing"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(system)->state, QString("capturing"), 5000);
        QVERIFY(playTone("test-sink-a"));
        QTRY_VERIFY2_WITH_TIMEOUT(manager.source(system)->peak.left > .01f,
            playback.readAllStandardError().constData(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(manager.source(mic)->peak.left > .01f, 5000);
        QVERIFY(manager.setVolume(mic, .5));
        QVERIFY(manager.setVolume(system, .5));
        QVERIFY(manager.setMuted(system, true));

        QTcpServer port;
        QVERIFY(port.listen(QHostAddress::LocalHost));
        const auto endpoint = QString("rtmp://127.0.0.1:%1/live/audio").arg(port.serverPort());
        port.close();
        const auto received = directory.filePath("received.flv");
        const auto recorded = directory.filePath("captured.mp4");
        QProcess receiver;
        receiver.start(QFileInfo(QStringLiteral(OPENLIVE_FFPROBE)).absolutePath() + "/ffmpeg",
            {"-v", "error", "-listen", "1", "-i", endpoint, "-c", "copy", "-f", "flv", received});
        QVERIFY(receiver.waitForStarted(2000));
        const auto cleanup = qScopeGuard([&] { stopProcess(receiver); stopProcess(playback); });
        QTest::qWait(300);
        auto recordingTap = manager.mixer()->subscribeOutput();
        auto streamingTap = manager.mixer()->subscribeOutput();
        QVector<float> recordingSamples, streamingSamples;
        QImage image(1920, 1080, QImage::Format_RGBA8888);
        image.fill(Qt::black);
        double energy[2]{};
        try {
            MediaEncoder recording, streaming;
            recording.open(recorded, 1000);
            streaming.openStream(endpoint, 1000, [] { return false; });
            for (int frame = 0; frame < 60; ++frame) {
                if (frame == 30) {
                    QVERIFY(manager.setMuted(mic, true));
                    QVERIFY(manager.setMuted(system, false));
                }
                const auto deadline = audioClockNs() + 2000000000LL;
                while ((recordingSamples.size() < 3200 || streamingSamples.size() < 3200) &&
                       audioClockNs() < deadline) {
                    for (const auto &block : recordingTap->takeAll())
                        recordingSamples += block->samples;
                    for (const auto &block : streamingTap->takeAll())
                        streamingSamples += block->samples;
                    QTest::qWait(5);
                }
                QVERIFY(recordingSamples.size() >= 3200 && streamingSamples.size() >= 3200);
                const auto samples = recordingSamples.first(3200);
                for (float sample : samples)
                    energy[frame / 30] += double(sample) * sample;
                recording.encodeVideoFrame(image, frame);
                streaming.encodeVideoFrame(image, frame);
                recording.encodeAudioSamples(samples);
                streaming.encodeAudioSamples(streamingSamples.first(3200));
                recordingSamples.remove(0, 3200);
                streamingSamples.remove(0, 3200);
            }
            recording.finish();
            streaming.finish();
            QVERIFY(recording.completed());
        } catch (const QString &error) {
            QFAIL(qPrintable(error));
        } catch (const StreamFailure &error) {
            QFAIL(qPrintable(error.message));
        }
        QVERIFY(receiver.waitForFinished(5000));
        QCOMPARE(recordingTap->dropped(), quint64(0));
        QCOMPARE(streamingTap->dropped(), quint64(0));
        QVERIFY(energy[0] > 1 && energy[1] > 1);
        QVERIFY(hasDecodedAudio(recorded));
        QVERIFY(hasDecodedAudio(received));
    }

    void cleanupTestCase()
    {
        stopProcess(playback);
        stopProcess(policy);
        stopProcess(daemon);
        if (configured) {
            for (auto it = previousEnvironment.cbegin(); it != previousEnvironment.cend(); ++it) {
                if (it.value().isNull())
                    qunsetenv(it.key());
                else
                    qputenv(it.key(), it.value());
            }
        }
    }
};

QTEST_GUILESS_MAIN(PipeWireTest)
#include "tst_pipewire.moc"
