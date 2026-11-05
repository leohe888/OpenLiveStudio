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
    static AudioBlock constant(qint64 timestampNs, float left, float right, int frames = 480)
    {
        AudioBlock block;
        block.timestampNs = timestampNs;
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
        const auto blocks = tap->takeAll();
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
        QVERIFY(manager.addSource("", AudioSourceType::Microphone, "missing-a").isEmpty());
        const auto first = manager.addSource("Mic", AudioSourceType::Microphone, "missing-a");
        QVERIFY(!first.isEmpty());
        QCOMPARE(manager.currentSourceId(), first);
        QVERIFY(manager.addSource("Other", AudioSourceType::Microphone, "missing-a").isEmpty());
        QVERIFY(manager.addSource("Other", AudioSourceType::Microphone, "MISSING-A").isEmpty());
        QVERIFY(manager.addSource("mic", AudioSourceType::SystemAudio, "missing-b").isEmpty());
        const auto second = manager.addSource("System", AudioSourceType::SystemAudio, "missing-b");
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
        QTest::qWait(1200); // 验证设备故障后的重试不会停止混音。
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
        QVERIFY(!manager.addSource("Missing", AudioSourceType::Microphone, "missing-notification")
                     .isEmpty());
        QTRY_COMPARE(manager.count(), 0);
        QTRY_COMPARE(manager.activeWorkerCount(), 0);
        connect(&manager, &AudioSourceManager::sourcesChanged, &manager, [&] {
            const auto sources = manager.sources();
            if (!sources.isEmpty())
                manager.removeSource(sources.first().id);
        });
        manager.addSource("Removed during insertion", AudioSourceType::SystemAudio,
                          "missing-insertion");
        QCOMPARE(manager.count(), 0);
        QTRY_COMPARE(manager.activeWorkerCount(), 0);
    }

    void typedSnapshotsAndChangeNotifications()
    {
        AudioSourceManager manager;
        const auto first =
            manager.addSource("First", AudioSourceType::Microphone, "missing-snapshot-first");
        const auto second =
            manager.addSource("Second", AudioSourceType::SystemAudio, "missing-snapshot-second");
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
                    replacement = manager.addSource("Replacement", AudioSourceType::SystemAudio,
                                                    "missing-replacement");
                });
        const auto original =
            manager.addSource("Original", AudioSourceType::Microphone, "missing-original");
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
        const auto id = manager.addSource("Meter", AudioSourceType::Microphone, "missing-meter");
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
        const qint64 timestampNs = 1000000000;
        auto firstInputQueue = std::make_shared<AudioPacketQueue>(), secondInputQueue = std::make_shared<AudioPacketQueue>();
        firstInputQueue->publish(constant(timestampNs, .8f, -.8f, 960));
        secondInputQueue->publish(constant(timestampNs, .7f, .9f, 960));
        AudioMixerCore mixer;
        QHash<QString, AudioPeak> peaks;
        auto output = mixer.mix({{"a", 1, false, firstInputQueue}, {"b", 1, false, secondInputQueue}}, timestampNs, &peaks);
        QCOMPARE(output.samples[0], 1.f);
        QVERIFY(std::abs(output.samples[1] - .1f) < .00001f);
        QVERIFY(output.clipped);
        QVERIFY(std::abs(output.peak.left - 1.5f) < .00001f);
        QCOMPARE(peaks["a"].left, .8f);
        output = mixer.mix({{"b", .5f, false, secondInputQueue}, {"a", 1, true, firstInputQueue}}, timestampNs + 10000000, &peaks);
        QVERIFY(!output.clipped);
        QVERIFY(std::abs(output.samples[0] - .35f) < .00001f);
        QVERIFY(std::abs(output.samples[1] - .45f) < .00001f);
        QCOMPARE(peaks["a"].right, .8f); // 静音只影响混音输出，输入电平仍应可见。
        output = mixer.mix({}, timestampNs + 20000000);
        QVERIFY(std::all_of(output.samples.cbegin(), output.samples.cend(), [](float value) {
            return value == 0;
        }));
    }

    void timestampsMissingSamplesAndReconnect()
    {
        const qint64 timestampNs = 1000000000;
        auto queue = std::make_shared<AudioPacketQueue>();
        queue->publish(constant(timestampNs + 5000000, .5f, -.5f));
        AudioMixerCore mixer;
        const QList<AudioInput> inputs{{"source", 1, false, queue}};
        auto first = mixer.mix(inputs, timestampNs);
        QCOMPARE(first.samples[0], 0.f);
        QCOMPARE(first.samples[480], .5f);
        auto second = mixer.mix(inputs, timestampNs + 10000000);
        QCOMPARE(second.samples[0], .5f);
        QCOMPARE(second.samples[480], 0.f);
        queue->publish(constant(timestampNs + 20000000, .8f, .8f, 960));
        QCOMPARE(mixer.mix(inputs, timestampNs + 20000000).samples[0], .8f);
        queue->clear(); // 断开设备时必须丢弃旧会话残留，避免重连后播放旧声音。
        QCOMPARE(mixer.mix(inputs, timestampNs + 30000000).samples[0], 0.f);
        queue->publish(constant(timestampNs + 40000000, .2f, .3f));
        QCOMPARE(mixer.mix(inputs, timestampNs + 40000000).samples[0], .2f);
        queue->publish(constant(timestampNs - 2000000000LL, 1, 1));
        queue->publish(constant(timestampNs + 2000000000LL, 1, 1));
        QCOMPARE(mixer.mix(inputs, timestampNs + 50000000).samples[0], 0.f);
        queue->publish(constant(timestampNs + 60000000, std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity()));
        QCOMPARE(mixer.mix(inputs, timestampNs + 60000000).samples[0], 0.f);
    }

    void boundedQueuesAndIndependentSilentOutput()
    {
        AudioPacketQueue queue;
        for (int i = 0; i < 1000; ++i)
            queue.publish(constant(i * 10000000LL, 1, 1));
        auto batch = queue.takeAll();
        QCOMPARE(batch.packets.size(), size_t(50));
        QCOMPARE(queue.droppedFrames(), quint64(950 * 480));
        AudioMixer mixer;
        mixer.setOutputEnabled(true);
        mixer.start();
        QThread::msleep(1250); // 故意不处理界面事件，也不消费输出，验证后台混音和缓冲上限。
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
        const int bytesPerSample[]{1, 2, 3, 4, 4, 8};
        QByteArray data(rate / 100 * bytesPerSample[format], 0);
        for (int sample = 0; sample < rate / 100; ++sample) {
            auto *out = data.data() + sample * bytesPerSample[format];
            if (format == 0)
                *out = char(160); // 构造满幅四分之一的测试信号。
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

    void capturesHardwareMicrophoneAndSystemAudio()
    {
        if (!qEnvironmentVariableIsSet("OPENLIVE_AUDIO_HARDWARE"))
            QSKIP("Requires explicit PipeWire hardware validation");
        QString error;
        const auto devices = audioDevices(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!devices.isEmpty());
        qInfo() << "Active audio endpoints:" << devices.size();
        bool microphone = false, system = false;
        for (const auto &value : devices) {
            const auto &info = value;
            const auto type = info.type;
            if ((type == AudioSourceType::Microphone && microphone) ||
                (type == AudioSourceType::SystemAudio && system))
                continue;
            auto queue = std::make_shared<AudioPacketQueue>();
            AudioCaptureWorker worker(info.id, type, queue);
            worker.start();
            QTRY_COMPARE_WITH_TIMEOUT(worker.status().state, QString("capturing"), 5000);
            int frames = 0;
            for (int i = 0; i < 40; ++i) {
                QTest::qWait(25);
                for (const auto &packet : queue->takeAll().packets) {
                    frames += packet.frames();
                    QVERIFY(packet.timestampNs > audioClockNs() - 2000000000LL);
                    QVERIFY(packet.timestampNs < audioClockNs() + 100000000LL);
                    for (float sample : packet.samples)
                        QVERIFY(std::isfinite(sample));
                }
                QVERIFY2(worker.status().state == "capturing", qPrintable(worker.status().error));
            }
            if (type == AudioSourceType::Microphone) {
                QVERIFY(frames > 24000);
                microphone = true;
            } else
                system = true; // 系统空闲时回环采集可以不返回数据，不能据此判定失败。
            qInfo() << toString(type) << "normalized frames:" << frames;
            worker.stop();
            QVERIFY(worker.wait(2000));
        }
        QVERIFY(microphone && system);
    }

    void hardwareFaultRecoveryAndIndependentSources()
    {
        if (!qEnvironmentVariableIsSet("OPENLIVE_AUDIO_HARDWARE"))
            QSKIP("Requires explicit PipeWire hardware validation");
        AudioSourceManager manager;
        manager.refreshDevices();
        QTRY_VERIFY(!manager.refreshing());
        QString microphone, system;
        for (const auto &value : manager.devices()) {
            const auto &device = value;
            if (device.type == AudioSourceType::Microphone && microphone.isEmpty())
                microphone = device.id;
            if (device.type == AudioSourceType::SystemAudio && system.isEmpty())
                system = device.id;
        }
        QVERIFY(!microphone.isEmpty() && !system.isEmpty());
        const auto mic = manager.addSource("Mic", AudioSourceType::Microphone, microphone);
        const auto desktop = manager.addSource("Desktop", AudioSourceType::SystemAudio, system);
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
