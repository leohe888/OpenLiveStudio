#include "audio/audiosourcemanager.h"
#include "audio/audioresampler.h"
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTest>
#include <limits>

class AudioTest : public QObject
{
    Q_OBJECT
private:
    static AudioBlock constant(qint64 timestamp, float left, float right, int frames = 480)
    {
        AudioBlock block;
        block.timestampNs = timestamp;
        block.samples.resize(frames * 2);
        for (int i = 0; i < frames; ++i) {
            block.samples[i * 2] = left;
            block.samples[i * 2 + 1] = right;
        }
        return block;
    }

private slots:

    void mixerCatchesUpAfterShortSchedulingPause()
    {
        qputenv("OPENLIVE_AUDIO_MIXER_PAUSE_ONCE_MS", "80");
        const auto restore = qScopeGuard([] {
            qunsetenv("OPENLIVE_AUDIO_MIXER_PAUSE_ONCE_MS");
        });
        AudioMixer mixer;
        auto tap = mixer.subscribeOutput();
        mixer.start();
        const auto cleanup = qScopeGuard([&] {
            mixer.stop();
            mixer.wait();
        });
        QTest::qWait(650);
        const auto blocks = tap->take();
        QVERIFY(blocks.size() > 50);
        for (size_t i = 1; i < blocks.size(); ++i)
            QCOMPARE(blocks[i]->timestampNs - blocks[i - 1]->timestampNs, qint64(10000000));
        QCOMPARE(mixer.discontinuities(), quint64(0));
        QCOMPARE(tap->dropped(), quint64(0));
    }

    void emptyManagerAndSourceManagement()
    {
        AudioSourceManager manager;
        QCOMPARE(manager.count(), 0);
        QTRY_VERIFY(manager.mixer()->latestBlock());
        const auto silent = manager.mixer()->latestBlock();
        QVERIFY(std::all_of(silent->samples.cbegin(), silent->samples.cend(), [](float value) {
            return value == 0;
        }));
        QVERIFY(manager.addSource("", "microphone", "missing-a").isEmpty());
        QVERIFY(manager.addSource("Bad", "camera", "missing-a").isEmpty());
        const auto first = manager.addSource("Mic", "microphone", "missing-a");
        QVERIFY(!first.isEmpty());
        QCOMPARE(manager.currentSourceId(), first);
        QVERIFY(manager.addSource("Other", "microphone", "missing-a").isEmpty());
        QVERIFY(manager.addSource("Other", "microphone", "MISSING-A").isEmpty());
        QVERIFY(manager.addSource("mic", "system", "missing-b").isEmpty());
        const auto second = manager.addSource("System", "system", "missing-b");
        QVERIFY(!second.isEmpty());
        QCOMPARE(manager.count(), 2);
        QVERIFY(manager.setVolume(first, .25));
        QVERIFY(manager.setMuted(first, true));
        QVERIFY(!manager.setVolume(first, -1));
        QVERIFY(!manager.setVolume(first, std::numeric_limits<double>::quiet_NaN()));
        QCOMPARE(manager.source(first)->volume, .25);
        QCOMPARE(manager.source(first)->muted, true);
        QVERIFY(manager.renameSource(first, "  Renamed  "));
        QCOMPARE(manager.source(first)->name, QString("Renamed"));
        QVERIFY(!manager.renameSource(first, "System"));
        QVERIFY(manager.selectSource(first));
        QVERIFY(!manager.selectSource("missing"));
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(first)->state, QString("error"), 3000);
        const auto sequence = manager.mixer()->latestBlock()->sequence;
        QTest::qWait(1200); // A failed endpoint retries without stopping the mixer.
        QVERIFY(manager.mixer()->latestBlock()->sequence > sequence + 30);
        QCOMPARE(manager.count(), 2);
        QVERIFY(manager.removeSource(first));
        QVERIFY(manager.removeSource(second));
        QCOMPARE(manager.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(manager.activeWorkerCount(), 0, 2000);
        QTRY_VERIFY(!manager.deviceInUse("missing-a"));
    }

    void removalFromSourceNotificationsIsSafe()
    {
        AudioSourceManager manager;
        connect(&manager, &AudioSourceManager::sourceFailed, &manager,
                [&](const QString &sourceId, const QString &) {
                    manager.removeSource(sourceId);
                });
        QVERIFY(!manager.addSource("Missing", "microphone", "missing-notification").isEmpty());
        QTRY_COMPARE(manager.count(), 0);
        QTRY_COMPARE(manager.activeWorkerCount(), 0);
        connect(&manager, &AudioSourceManager::sourcesChanged, &manager, [&] {
            const auto sources = manager.sources();
            if (!sources.isEmpty())
                manager.removeSource(sources.first().id);
        });
        manager.addSource("Removed during insertion", "system", "missing-insertion");
        QCOMPARE(manager.count(), 0);
        QTRY_COMPARE(manager.activeWorkerCount(), 0);
    }

    void typedSnapshotsAndChangeNotifications()
    {
        AudioSourceManager manager;
        const auto first = manager.addSource("First", "microphone", "missing-snapshot-first");
        const auto second = manager.addSource("Second", "system", "missing-snapshot-second");
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        const auto snapshot = manager.sources();
        QCOMPARE(snapshot.size(), 2);
        QVERIFY(!manager.source("missing"));
        QSignalSpy structure(&manager, &AudioSourceManager::sourcesChanged);
        QSignalSpy changed(&manager, &AudioSourceManager::sourceChanged);
        QVERIFY(manager.setVolume(first, .5));
        QVERIFY(manager.setVolume(first, .5));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.last().at(0).toString(), first);
        QVERIFY(manager.setMuted(first, true));
        QVERIFY(manager.setMuted(first, true));
        QCOMPARE(changed.count(), 2);
        QVERIFY(manager.renameSource(first, "Renamed"));
        QVERIFY(manager.renameSource(first, "Renamed"));
        QCOMPARE(changed.count(), 3);
        QCOMPARE(snapshot.first().name, QString("First"));
        QCOMPARE(snapshot.first().volume, 1.f);
        QVERIFY(!snapshot.first().muted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(first)->state, QString("error"), 3000);
        QCOMPARE(structure.count(), 0);
        QVERIFY(manager.removeSource(second));
        QCOMPARE(manager.currentSourceId(), first);
        QCOMPARE(snapshot.size(), 2);
        QVERIFY(!manager.source(second));
    }

    void removalDuringStatusNotificationIsSafe()
    {
        AudioSourceManager manager;
        bool replaced = false;
        QString replacement;
        connect(&manager, &AudioSourceManager::sourceChanged, &manager,
                [&](const QString &sourceId) {
            const auto *source = manager.source(sourceId);
            if (replaced || !source || source->state != "error")
                return;
            replaced = true;
            QVERIFY(manager.removeSource(sourceId));
            replacement = manager.addSource("Replacement", "system", "missing-replacement");
        });
        const auto original = manager.addSource("Original", "microphone", "missing-original");
        QVERIFY(!original.isEmpty());
        QTRY_VERIFY(replaced);
        QVERIFY(!replacement.isEmpty());
        QVERIFY(!manager.source(original));
        QVERIFY(manager.source(replacement));
        QCOMPARE(manager.count(), 1);
    }

    void sourceLevelsUseTypedDataAndKeepSourcesIntact()
    {
        AudioSourceManager manager;
        const auto id = manager.addSource("Meter", "microphone", "missing-meter");
        QVERIFY(!id.isEmpty());
        auto queue = std::make_shared<AudioPacketQueue>();
        queue->publish(constant(audioClockNs(), .5f, .75f, AudioFormat::rate / 5));
        manager.mixer()->setInputs({{id, 1, false, queue}});
        QSignalSpy levels(&manager, &AudioSourceManager::sourceLevelsChanged);
        QSignalSpy structure(&manager, &AudioSourceManager::sourcesChanged);
        QTRY_VERIFY(manager.source(id)->peak.left > 0);
        QVERIFY(manager.source(id)->peak.right > 0);
        QVERIFY(!levels.isEmpty());
        QCOMPARE(levels.last().at(0).toString(), id);
        QCOMPARE(structure.count(), 0);
        QCOMPARE(manager.count(), 1);
    }

    void mixingGainMuteClippingAndInputMeters()
    {
        const qint64 timestamp = 1000000000;
        auto a = std::make_shared<AudioPacketQueue>(), b = std::make_shared<AudioPacketQueue>();
        a->publish(constant(timestamp, .8f, -.8f, 960));
        b->publish(constant(timestamp, .7f, .9f, 960));
        AudioMixerCore mixer;
        QHash<QString, AudioPeak> peaks;
        auto output = mixer.mix({{"a", 1, false, a}, {"b", 1, false, b}}, timestamp, &peaks);
        QCOMPARE(output.samples[0], 1.f);
        QVERIFY(std::abs(output.samples[1] - .1f) < .00001f);
        QVERIFY(output.clipped);
        QVERIFY(std::abs(output.peak.left - 1.5f) < .00001f);
        QCOMPARE(peaks["a"].left, .8f);
        output = mixer.mix({{"b", .5f, false, b}, {"a", 1, true, a}}, timestamp + 10000000, &peaks);
        QVERIFY(!output.clipped);
        QVERIFY(std::abs(output.samples[0] - .35f) < .00001f);
        QVERIFY(std::abs(output.samples[1] - .45f) < .00001f);
        QCOMPARE(peaks["a"].right, .8f); // Muted input still meters its signal.
        output = mixer.mix({}, timestamp + 20000000);
        QVERIFY(std::all_of(output.samples.cbegin(), output.samples.cend(), [](float value) {
            return value == 0;
        }));
    }

    void timestampsMissingSamplesAndReconnect()
    {
        const qint64 timestamp = 1000000000;
        auto queue = std::make_shared<AudioPacketQueue>();
        queue->publish(constant(timestamp + 5000000, .5f, -.5f));
        AudioMixerCore mixer;
        const QList<AudioInput> inputs{{"source", 1, false, queue}};
        auto first = mixer.mix(inputs, timestamp);
        QCOMPARE(first.samples[0], 0.f);
        QCOMPARE(first.samples[480], .5f);
        auto second = mixer.mix(inputs, timestamp + 10000000);
        QCOMPARE(second.samples[0], .5f);
        QCOMPARE(second.samples[480], 0.f);
        queue->publish(constant(timestamp + 20000000, .8f, .8f, 960));
        QCOMPARE(mixer.mix(inputs, timestamp + 20000000).samples[0], .8f);
        queue->clear(); // Device disconnect discards the remainder of its old session.
        QCOMPARE(mixer.mix(inputs, timestamp + 30000000).samples[0], 0.f);
        queue->publish(constant(timestamp + 40000000, .2f, .3f));
        QCOMPARE(mixer.mix(inputs, timestamp + 40000000).samples[0], .2f);
        queue->publish(constant(timestamp - 2000000000LL, 1, 1));
        queue->publish(constant(timestamp + 2000000000LL, 1, 1));
        QCOMPARE(mixer.mix(inputs, timestamp + 50000000).samples[0], 0.f);
        queue->publish(constant(timestamp + 60000000, std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity()));
        QCOMPARE(mixer.mix(inputs, timestamp + 60000000).samples[0], 0.f);
    }

    void boundedQueuesAndIndependentSilentOutput()
    {
        AudioPacketQueue queue;
        for (int i = 0; i < 1000; ++i)
            queue.publish(constant(i * 10000000LL, 1, 1));
        auto batch = queue.take();
        QCOMPARE(batch.packets.size(), size_t(50));
        QCOMPARE(queue.droppedFrames(), quint64(950 * 480));
        AudioMixer mixer;
        mixer.setOutputEnabled(true);
        mixer.start();
        QThread::msleep(1250); // No GUI event processing and no output consumer.
        const auto blocks = mixer.takeOutput();
        QCOMPARE(blocks.size(), size_t(100));
        QVERIFY(mixer.droppedOutputBlocks() > 0);
        for (size_t i = 1; i < blocks.size(); ++i) {
            QCOMPARE(blocks[i]->timestampNs - blocks[i - 1]->timestampNs, qint64(10000000));
            QCOMPARE(blocks[i]->frames(), 480);
        }
        mixer.setOutputEnabled(false);
        QVERIFY(mixer.takeOutput().empty());
        mixer.stop();
        QVERIFY(mixer.wait(1000));
    }

    void resampling_data()
    {
        QTest::addColumn<int>("rate");
        QTest::addColumn<int>("format");
        for (int rate : {44100, 48000, 96000})
            for (int format = 0; format < 6; ++format)
                QTest::newRow(qPrintable(QString("%1-%2").arg(rate).arg(format))) << rate << format;
    }

    void resampling()
    {
        QFETCH(int, rate);
        QFETCH(int, format);
        const int bytes[]{1, 2, 3, 4, 4, 8};
        QByteArray data(rate / 100 * bytes[format], 0);
        for (int sample = 0; sample < rate / 100; ++sample) {
            auto *out = data.data() + sample * bytes[format];
            if (format == 0)
                *out = char(160); // +0.25 full scale
            if (format == 1) {
                qint16 value = 8192;
                memcpy(out, &value, 2);
            }
            if (format == 2) {
                out[0] = 0;
                out[1] = 0;
                out[2] = 32;
            }
            if (format == 3) {
                qint32 value = 536870912;
                memcpy(out, &value, 4);
            }
            if (format == 4) {
                float value = .25f;
                memcpy(out, &value, 4);
            }
            if (format == 5) {
                double value = .25;
                memcpy(out, &value, 8);
            }
        }
        AudioResampler resampler;
        QString error;
        QVERIFY2(resampler.configure(rate, 1, PcmFormat(format), 0, &error), qPrintable(error));
        QVector<float> result;
        for (int i = 0; i < 100; ++i)
            result += resampler.convert(reinterpret_cast<const unsigned char *>(data.constData()),
                                        rate / 100, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(result.size() / 2 >= 47900 && result.size() / 2 <= 48000);
        for (int i = 200; i < result.size(); ++i)
            QVERIFY(std::abs(result[i] - .25f) < .001f);
        QVERIFY(resampler.compensate(10, 48000));
        QVERIFY(resampler.reset());
    }

    void hardware()
    {
        if (!qEnvironmentVariableIsSet("OPENLIVE_AUDIO_HARDWARE"))
            QSKIP("Requires explicit WASAPI hardware validation");
        QString error;
        const auto devices = audioDevices(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!devices.isEmpty());
        qInfo() << "Active audio endpoints:" << devices.size();
        bool microphone = false, system = false;
        for (const auto &value : devices) {
            const auto &info = value;
            const auto type = info.type;
            if ((type == "microphone" && microphone) || (type == "system" && system))
                continue;
            auto queue = std::make_shared<AudioPacketQueue>();
            AudioCaptureWorker worker(info.id, type, queue);
            worker.start();
            QTRY_COMPARE_WITH_TIMEOUT(worker.status().state, QString("capturing"), 5000);
            int frames = 0;
            for (int i = 0; i < 40; ++i) {
                QTest::qWait(25);
                for (const auto &packet : queue->take().packets) {
                    frames += packet.frames();
                    QVERIFY(packet.timestampNs > audioClockNs() - 2000000000LL);
                    QVERIFY(packet.timestampNs < audioClockNs() + 100000000LL);
                    for (float sample : packet.samples)
                        QVERIFY(std::isfinite(sample));
                }
                QVERIFY2(worker.status().state == "capturing", qPrintable(worker.status().error));
            }
            if (type == "microphone") {
                QVERIFY(frames > 24000);
                microphone = true;
            } else
                system = true; // Idle loopback may legally return no packets.
            qInfo() << type << "normalized frames:" << frames;
            worker.stop();
            QVERIFY(worker.wait(2000));
        }
        QVERIFY(microphone && system);
    }

    void hardwareFaultRecoveryAndIndependentSources()
    {
        if (!qEnvironmentVariableIsSet("OPENLIVE_AUDIO_HARDWARE"))
            QSKIP("Requires explicit WASAPI hardware validation");
        AudioSourceManager manager;
        manager.refreshDevices();
        QTRY_VERIFY(!manager.refreshing());
        QString microphone, system;
        for (const auto &value : manager.devices()) {
            const auto &device = value;
            if (device.type == "microphone" && microphone.isEmpty())
                microphone = device.id;
            if (device.type == "system" && system.isEmpty())
                system = device.id;
        }
        QVERIFY(!microphone.isEmpty() && !system.isEmpty());
        const auto mic = manager.addSource("Mic", "microphone", microphone);
        const auto desktop = manager.addSource("Desktop", "system", system);
        QVERIFY(!mic.isEmpty() && !desktop.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(mic)->state, QString("capturing"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(desktop)->state, QString("capturing"), 5000);
        QVERIFY(manager.setVolume(mic, .4));
        QVERIFY(manager.setMuted(mic, true));
        qputenv("OPENLIVE_FORCE_AUDIO_DEVICE_FAILURE", microphone.toUtf8());
        const auto restore = qScopeGuard([] {
            qunsetenv("OPENLIVE_FORCE_AUDIO_DEVICE_FAILURE");
        });
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(mic)->state, QString("error"), 5000);
        QCOMPARE(manager.source(desktop)->state, QString("capturing"));
        const auto sequence = manager.mixer()->latestBlock()->sequence;
        QTest::qWait(150);
        QVERIFY(manager.mixer()->latestBlock()->sequence > sequence);
        QCOMPARE(manager.source(mic)->deviceId, microphone);
        qunsetenv("OPENLIVE_FORCE_AUDIO_DEVICE_FAILURE");
        QTRY_COMPARE_WITH_TIMEOUT(manager.source(mic)->state, QString("capturing"), 5000);
        QVERIFY(std::abs(manager.source(mic)->volume - .4) < .00001);
        QVERIFY(manager.source(mic)->muted);
        QCOMPARE(manager.source(mic)->deviceId, microphone);
        QVERIFY(manager.removeSource(mic));
        QVERIFY(manager.removeSource(desktop));
        QTRY_COMPARE_WITH_TIMEOUT(manager.activeWorkerCount(), 0, 2000);
    }
};

QTEST_GUILESS_MAIN(AudioTest)
#include "tst_audio.moc"
