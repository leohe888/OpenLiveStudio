#include "audio/audiosourcemanager.h"
#include "output/mediaencoder.h"
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <cstring>

// An isolated daemon with generated audio: never enumerates or records host devices.
class PulseAudioTest : public QObject
{
    Q_OBJECT
    QTemporaryDir directory;
    QProcess daemon, playback, micPlaybackA, micPlaybackB;
    bool lastCommandSucceeded = false;
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
        lastCommandSucceeded = process.waitForFinished(3000) && process.exitCode() == 0;
        if (!lastCommandSucceeded)
            qWarning().noquote() << program << arguments << process.readAllStandardError();
        return lastCommandSucceeded ? process.readAllStandardOutput() : QByteArray{};
    }

    bool setDefault(AudioSourceType type, const QString &name)
    {
        command("pactl", {type == AudioSourceType::Microphone ? "set-default-source"
                                                            : "set-default-sink", name});
        return lastCommandSucceeded;
    }

    int moduleId(const QString &name)
    {
        for (const auto &line : command("pactl", {"list", "short", "modules"}).split('\n')) {
            const auto fields = line.split('\t');
            if (fields.size() < 3)
                continue;
            const auto args = fields[2].split(' ');
            if (args.contains("source_name=" + name.toUtf8()) ||
                args.contains("sink_name=" + name.toUtf8()))
                return fields[0].toInt();
        }
        return -1;
    }

    bool startServer()
    {
        daemon.start("pulseaudio", {"-n", "--daemonize=no", "--use-pid-file=no",
            "--exit-idle-time=-1", "--log-target=stderr", "--file=" + directory.filePath("pulse.pa")});
        if (!daemon.waitForStarted(2000))
            return false;
        const auto deadline = audioClockNs() + 3000000000LL;
        while (!QFile::exists(directory.filePath("native")) && audioClockNs() < deadline)
            QTest::qWait(20);
        if (!QFile::exists(directory.filePath("native"))) {
            qWarning().noquote() << daemon.readAllStandardError();
            return false;
        }
        const bool ready = setDefault(AudioSourceType::Microphone, "test-mic-a") &&
                           setDefault(AudioSourceType::SystemAudio, "test-sink-a");
        if (!ready) {
            daemon.waitForReadyRead(200);
            qWarning().noquote() << daemon.readAllStandardError();
        }
        if (!ready)
            return false;
        micPlaybackA.start("paplay", {"--device=test-mic-carrier-a", directory.filePath("tone.wav")});
        micPlaybackB.start("paplay", {"--device=test-mic-carrier-b", directory.filePath("tone.wav")});
        return micPlaybackA.waitForStarted(2000) && micPlaybackB.waitForStarted(2000);
    }

    bool makeToneFile()
    {
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
        return output.status() == QDataStream::Ok;
    }

    bool playTone(const QString &name)
    {
        stopProcess(playback);
        playback.start("paplay", {"--device=" + name, directory.filePath("tone.wav")});
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
        for (const char *program : {"pulseaudio", "pactl", "paplay"})
            if (QStandardPaths::findExecutable(program).isEmpty())
                QSKIP("Isolated integration requires PulseAudio and pulseaudio-utils");
        QVERIFY(directory.isValid());
        QFile config(directory.filePath("pulse.pa"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write(("load-module module-native-protocol-unix auth-anonymous=1 socket=" +
                      directory.filePath("native") + "\n").toUtf8());
        config.write(R"(
load-module module-null-sink sink_name=test-mic-carrier-a rate=44100 channels=2
load-module module-null-sink sink_name=test-mic-carrier-b rate=48000 channels=2
load-module module-remap-source source_name=test-mic-a master=test-mic-carrier-a.monitor
load-module module-remap-source source_name=test-mic-b master=test-mic-carrier-b.monitor
load-module module-null-sink sink_name=test-sink-a rate=48000 channels=2
load-module module-null-sink sink_name=test-sink-b rate=48000 channels=2
)");
        config.close();
        QVERIFY(makeToneFile());
        const QMap<QByteArray, QByteArray> environment{
            {"XDG_RUNTIME_DIR", directory.path().toUtf8()},
            {"PULSE_RUNTIME_PATH", directory.path().toUtf8()},
            {"PULSE_STATE_PATH", directory.path().toUtf8()},
            {"PULSE_SERVER", "unix:" + directory.filePath("native").toUtf8()}};
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
        QCOMPARE(devices.size(), 8);
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
        AudioCaptureWorker system("pulse:system:test-sink-a", AudioSourceType::SystemAudio,
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
        const auto fixedId = QString("pulse:%1:%2a").arg(toString(type), prefix);
        const auto fixed = manager.addSource("Fixed", type, fixedId);
        const auto follow = manager.addSource("Default", type, defaultAudioDeviceId(type));
        QVERIFY(!fixed.isEmpty() && !follow.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(fixed)->state, QString("capturing"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(follow)->state, QString("capturing"), 5000);
        QVERIFY(manager.setVolume(fixed, .4));
        QVERIFY(manager.setMuted(fixed, true));
        QVERIFY(setDefault(type, prefix + "b"));
        QTest::qWait(300);
        const auto id = moduleId(prefix + "a");
        QVERIFY(id >= 0);
        command("pactl", {"unload-module", QString::number(id)});
        QVERIFY(lastCommandSucceeded);
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(fixed)->state, QString("error"), 5000);
        QTest::qWait(300);
        QCOMPARE(manager.source(follow)->state, QString("capturing"));
        const auto before = manager.mixer()->latestBlock()->sequence;
        QTest::qWait(150);
        QVERIFY(manager.mixer()->latestBlock()->sequence > before);
        const auto module = type == AudioSourceType::Microphone ? "module-remap-source"
                                                              : "module-null-sink";
        const auto argument = type == AudioSourceType::Microphone ? "source_name=" : "sink_name=";
        QStringList arguments{"load-module", module, argument + prefix + "a"};
        if (type == AudioSourceType::Microphone)
            arguments.append("master=test-mic-carrier-a.monitor");
        command("pactl", arguments);
        QVERIFY(lastCommandSucceeded);
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
        stopProcess(micPlaybackA);
        stopProcess(micPlaybackB);
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
        qInfo() << "Captured microphone/system mixed energy:" << energy[0] << energy[1];
        QVERIFY(energy[0] > 1 && energy[1] > 1);
        QVERIFY(hasDecodedAudio(recorded));
        QVERIFY(hasDecodedAudio(received));
    }

    void emptyCatalogDoesNotOfferPhantomDefaults()
    {
        stopProcess(playback);
        for (const auto &name : {"test-mic-a", "test-mic-b", "test-sink-a", "test-sink-b",
                                  "test-mic-carrier-a", "test-mic-carrier-b"}) {
            const auto module = moduleId(name);
            QVERIFY(module >= 0);
            command("pactl", {"unload-module", QString::number(module)});
            QVERIFY(lastCommandSucceeded);
        }
        QString error;
        QVERIFY(audioDevices(&error).isEmpty());
        QVERIFY(error.contains("未发现"));
    }

    void cleanupTestCase()
    {
        stopProcess(playback);
        stopProcess(micPlaybackA);
        stopProcess(micPlaybackB);
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

QTEST_GUILESS_MAIN(PulseAudioTest)
#include "tst_pulseaudio.moc"
