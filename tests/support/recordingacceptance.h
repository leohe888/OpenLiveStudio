#include "../video/videosourcetesthelpers.h"
#ifndef RECORDINGACCEPTANCE_H
#define RECORDINGACCEPTANCE_H

#include "video/display/displaysourcecontroller.h"
#include "video/camera/camerasourcecontroller.h"
#include "video/videosourceaggregator.h"
#include "audio/audiosourcemanager.h"
#include "core/mediaclock.h"
#include "output/recordingcontroller.h"
#include "output/outputsettings.h"
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QScopeGuard>
#include <QUuid>
#include <atomic>
#include <numeric>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace RecordingAcceptance {
inline bool saveJson(const QString &path, const QJsonObject &data)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(data).toJson()) >= 0 &&
           file.commit();
}

inline QJsonObject collectProcessMetrics(qint64 elapsedMs)
{
    QJsonObject value{{"elapsedMs", elapsedMs}};
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX memory{};
    DWORD handles = 0;
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory)))
        value["privateBytes"] = qint64(memory.PrivateUsage);
    if (GetProcessHandleCount(GetCurrentProcess(), &handles))
        value["handles"] = int(handles);
#endif
    return value;
}

inline QByteArray runMediaTool(const QString &binary, const QStringList &arguments, QString *error)
{
    QProcess process;
    process.start(binary, arguments);
    if (!process.waitForStarted(5000)) {
        *error = process.errorString();
        return {};
    }
    QElapsedTimer timer;
    timer.start();
    while (!process.waitForFinished(500)) {
        if (timer.elapsed() > 180000) {
            process.kill();
            process.waitForFinished(5000);
            *error = QStringLiteral("Media inspection timed out");
            return {};
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode())
        *error = QString::fromUtf8(process.readAllStandardError());
    return process.readAllStandardOutput();
}

inline QVector<double> risingEdges(const QVector<bool> &active, double step)
{
    QVector<double> result;
    for (int i = 0; i < active.size(); ++i)
        if (active[i] && (!i || !active[i - 1])) {
            const double timestamp = i * step;
            if (result.isEmpty() || timestamp - result.back() > .5)
                result.append(timestamp);
        }
    return result;
}

inline void run(MainWindow &window)
{
    const int seconds = qEnvironmentVariableIntValue("OPENLIVE_RECORDING_SOAK_SECONDS");
    if (seconds <= 0)
        QSKIP("Set OPENLIVE_RECORDING_SOAK_SECONDS for explicit real-time recording acceptance");
    QVERIFY(seconds >= 10 && seconds <= 3600);
    auto *recorder = window.findChild<RecordingController *>();
    QVERIFY(recorder);
    auto *settings = window.findChild<OutputSettings *>();
    QVERIFY(settings);
    auto *audio = window.findChild<AudioSourceManager *>();
    QVERIFY(audio);
    auto *model = window.findChild<VideoSourceManager *>();
    QVERIFY(model);
    auto *pictures = window.findChild<ImageSourceController *>();
    QVERIFY(pictures);
    auto *display = window.findChild<DisplaySourceController *>();
    QVERIFY(display);
    auto *camera = window.findChild<CameraSourceController *>();
    QVERIFY(camera);
    auto *aggregator = window.findChild<VideoSourceAggregator *>();
    QVERIFY(aggregator);
    auto *preview = window.findChild<PreviewWidget *>("previewWidget");
    QVERIFY(preview);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(recorder->ready(), 10000);
    const auto directory =
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("acceptance-%1-%2")
                          .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"),
                               QUuid::createUuid().toString(QUuid::WithoutBraces).left(6)));
    QVERIFY(QDir().mkpath(directory));
    qInfo() << "Acceptance artifacts:" << directory;
    QVERIFY(settings->apply(directory, 12000, "", ""));
    display->refreshDisplayDevices();
    QVERIFY(!display->displayDevices().isEmpty());
    camera->refreshDevices();
    audio->refreshDevices();
    QTRY_VERIFY_WITH_TIMEOUT(!camera->refreshing() && !audio->refreshing(), 10000);
    QVERIFY2(!camera->devices().isEmpty(),
             "A Media Foundation camera is required for comprehensive acceptance");
    QString microphoneId, systemId;
    for (const auto &value : audio->devices()) {
        const auto &device = value;
        if (device.type == AudioSourceType::Microphone && microphoneId.isEmpty())
            microphoneId = device.id;
        if (device.type == AudioSourceType::SystemAudio && systemId.isEmpty())
            systemId = device.id;
    }
    QVERIFY(!microphoneId.isEmpty() && !systemId.isEmpty());
    const auto screen = display->addDisplay("Screen", display->displayDevices()[0].id, true);
    const auto cameraSourceId = camera->addCamera("Camera", CameraSourceConfig{camera->devices()[0].id});
    const auto microphoneSourceId = audio->addSource("Microphone", AudioSourceType::Microphone, microphoneId);
    QString system = audio->addSource("System", AudioSourceType::SystemAudio, systemId);
    QVERIFY(!screen.isEmpty() && !cameraSourceId.isEmpty() && !microphoneSourceId.isEmpty() && !system.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(model->sourceRuntime(screen).state(),
                              VideoSourceState::Capturing, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(model->sourceRuntime(cameraSourceId).state(), VideoSourceState::Capturing,
                              15000);
    QTRY_COMPARE_WITH_TIMEOUT(audio->source(microphoneSourceId)->state, QString("capturing"), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(audio->source(system)->state, QString("capturing"), 5000);
    QVERIFY(audio->setVolume(microphoneSourceId, .05));
    QVERIFY(audio->setVolume(system, .05));
    const auto picturePath = QDir(directory).filePath("source.png");
    QImage picture(128, 128, QImage::Format_RGBA8888);
    picture.fill(Qt::yellow);
    QVERIFY(picture.save(picturePath));
    QObject requester;
    pictures->addImage("Picture", picturePath, &requester);
    QTRY_COMPARE(model->sources().size(), 3);
    QString imageId = model->currentSourceId();
    auto queue = std::make_shared<AudioPacketQueue>();
    std::atomic_bool stopped{false};
    const qint64 epoch = MediaClock::nowNs();
    std::atomic<qint64> cueOnset{epoch};
    qint64 cuePeriod = -1;
    QList<RenderSourceSnapshot> latest;
    qint64 maximumUiGapMs = 0, lastHeartbeatNs = MediaClock::nowNs();
    const auto render = [&] {
        const auto now = MediaClock::nowNs();
        maximumUiGapMs = std::max(maximumUiGapMs, (now - lastHeartbeatNs) / 1000000);
        lastHeartbeatNs = now;
        auto inputs = audio->mixer()->inputs();
        if (std::none_of(inputs.cbegin(), inputs.cend(), [](const auto &input) {
                return input.id == "acceptance-sync";
            })) {
            inputs.append({"acceptance-sync", 1, false, queue});
            audio->mixer()->setInputs(inputs);
        }
        QImage marker(8, 8, QImage::Format_RGBA8888);
        // Publish both cues at the actual GUI update, rather than comparing a
        // scheduled sound against a flash delayed by GUI/device operations.
        const auto period = (now - epoch) / 1000000000LL;
        if (period != cuePeriod) {
            cuePeriod = period;
            cueOnset.store(now);
        }
        marker.fill(now - cueOnset.load() < 100000000LL ? Qt::white : Qt::black);
        auto sources = latest;
        sources.prepend({"acceptance-sync", marker, true, {{125, 125}, {250, 250}, 0}});
        preview->setSources(sources);
    };
    const auto connection = QObject::connect(aggregator, &VideoSourceAggregator::renderSourcesChanged,
                                             &window, [&](const auto &sources) {
                                                 latest = sources;
                                                 render();
                                             });
    QTimer heartbeat;
    heartbeat.setInterval(20);
    heartbeat.setTimerType(Qt::PreciseTimer);
    QObject::connect(&heartbeat, &QTimer::timeout, &window, render);
    auto producer = std::unique_ptr<QThread>(QThread::create([&] {
        qint64 position = 0;
        while (!stopped.load()) {
            const auto timestamp = epoch + AudioFormat::framesToNs(position);
            if (timestamp > MediaClock::nowNs()) {
                QThread::msleep(1);
                continue;
            }
            AudioBlock block;
            block.timestampNs = timestamp;
            block.samples.resize(960);
            for (int frame = 0; frame < 480; ++frame) {
                const auto index = position + frame;
                const auto sinceCueNs = epoch + AudioFormat::framesToNs(index) - cueOnset.load();
                const float value =
                    sinceCueNs >= 0 && sinceCueNs < 100000000LL
                        ? .25f * float(std::sin(2 * std::acos(-1.0) * 1500 * double(index) / 48000))
                        : 0;
                block.samples[frame * 2] = block.samples[frame * 2 + 1] = value;
            }
            queue->publish(std::move(block));
            position += 480;
        }
    }));
    const auto cleanup = qScopeGuard([&] {
        heartbeat.stop();
        QObject::disconnect(connection);
        stopped.store(true);
        producer->wait();
        qunsetenv("OPENLIVE_FORCE_AUDIO_DEVICE_FAILURE");
        recorder->stopRecording();
        QElapsedTimer stop;
        stop.start();
        while (recorder->busy() && stop.elapsed() < 10000) {
            QCoreApplication::processEvents();
            QThread::msleep(5);
        }
    });
    heartbeat.start();
    render();
    producer->start();
    QTRY_VERIFY_WITH_TIMEOUT(latest.size() >= 3, 5000);
    auto *recordButton = window.findChild<QPushButton *>("recordButton");
    QVERIFY(recordButton);
    recordButton->click();
    QTRY_COMPARE_WITH_TIMEOUT(recorder->state(), QString("recording"), 10000);
    window.showMinimized();
    QElapsedTimer elapsed;
    elapsed.start();
    QJsonArray samples, events;
    qint64 lastReport = -10000, pausedAt = -1, faultAt = -1;
    std::array<bool, 9> done{};
    const auto event = [&](const QString &name) {
        events.append(QJsonObject{{"elapsedMs", elapsed.elapsed()}, {"event", name}});
    };
    event("minimized");
    while (elapsed.elapsed() < seconds * 1000LL) {
        QTest::qWait(20);
        QVERIFY2(recorder->state() == "recording", qPrintable(recorder->lastError()));
        const qint64 now = elapsed.elapsed();
        const int percent = int(now * 100 / (seconds * 1000LL));
        if (percent >= 10 && !done[0]) {
            done[0] = true;
            QVERIFY(model->setSourceTransform(imageId, {{700, 500}, {350, 200}, 35}));
            const auto filter = model->addFilter(imageId);
            QVERIFY(!filter.isEmpty());
            QVERIFY(setFilterConfig(*model, imageId, filter, ColorCorrectionConfig{.1f, .8f, .3f}));
            event("transform-and-filter");
        }
        if (percent >= 20 && !done[1]) {
            done[1] = true;
            QVERIFY(model->setSourceVisible(cameraSourceId, false));
            QTRY_COMPARE_WITH_TIMEOUT(camera->activeWorkerCount(), 0, 5000);
            pausedAt = now;
            event("camera-hidden-and-released");
        }
        if (pausedAt >= 0 && now - pausedAt > std::min(1500, seconds * 20)) {
            pausedAt = -1;
            QVERIFY(model->setSourceVisible(cameraSourceId, true));
            QTRY_COMPARE_WITH_TIMEOUT(model->sourceRuntime(cameraSourceId).state(),
                                      VideoSourceState::Capturing, 10000);
            event("camera-restored");
        }
        if (percent >= 35 && !done[2]) {
            done[2] = true;
            QVERIFY(model->removeSource(imageId));
            pictures->addImage("Replacement", picturePath, &requester);
            QTRY_COMPARE(model->sources().size(), 3);
            imageId = model->currentSourceId();
            event("picture-replaced");
        }
        if (percent >= 45 && !done[3]) {
            done[3] = true;
            qputenv("OPENLIVE_FORCE_AUDIO_DEVICE_FAILURE", microphoneId.toUtf8());
            QTRY_COMPARE_WITH_TIMEOUT(audio->source(microphoneSourceId)->state, QString("error"), 5000);
            QCOMPARE(audio->source(system)->state, QString("capturing"));
            faultAt = now;
            event("microphone-fault");
        }
        if (faultAt >= 0 && now - faultAt > std::min(2000, seconds * 30)) {
            faultAt = -1;
            qunsetenv("OPENLIVE_FORCE_AUDIO_DEVICE_FAILURE");
            QTRY_COMPARE_WITH_TIMEOUT(audio->source(microphoneSourceId)->state, QString("capturing"), 5000);
            event("microphone-restored");
        }
        if (percent >= 65 && !done[4]) {
            done[4] = true;
            QVERIFY(audio->setMuted(microphoneSourceId, true));
            QVERIFY(audio->setVolume(system, .1));
            event("audio-mute-and-volume");
        }
        if (percent >= 75 && !done[5]) {
            done[5] = true;
            QVERIFY(audio->removeSource(system));
            QTRY_VERIFY_WITH_TIMEOUT(!audio->deviceInUse(systemId), 5000);
            system = audio->addSource("System restored", AudioSourceType::SystemAudio, systemId);
            QVERIFY(!system.isEmpty());
            QVERIFY(audio->setVolume(system, .05));
            QTRY_COMPARE_WITH_TIMEOUT(audio->source(system)->state, QString("capturing"), 5000);
            event("audio-source-replaced");
        }
        if (percent >= 85 && !done[6]) {
            done[6] = true;
            window.showNormal();
            window.resize(1024, 700);
            window.resize(1280, 800);
            event("window-restored-and-resized");
        }
        if (percent >= 90 && !done[7]) {
            done[7] = true;
            window.showMinimized();
            event("minimized-again");
        }
        if (percent >= 95 && !done[8]) {
            done[8] = true;
            QVERIFY(model->setSourceLocked(imageId, true));
            QVERIFY(!model->setSourceTransform(imageId, {{1, 1}, {10, 10}, 0}));
            event("locked-source-protected");
        }
        if (now - lastReport >= 10000) {
            lastReport = now;
            samples.append(collectProcessMetrics(now));
            saveJson(QDir(directory).filePath("progress.json"),
                     {{"phase", "recording"},
                      {"elapsedMs", now},
                      {"targetSeconds", seconds},
                      {"recordingState", recorder->state()},
                      {"file", recorder->filePath()},
                      {"maximumUiGapMs", maximumUiGapMs},
                      {"events", events},
                      {"resources", samples}});
        }
    }
    recordButton->click();
    QTRY_VERIFY_WITH_TIMEOUT(!recorder->busy(), 15000);
    QVERIFY2(recorder->lastError().isEmpty(), qPrintable(recorder->lastError()));
    QVERIFY(recorder->fileComplete());
    const auto status = recorder->status();
    const QString movie = recorder->filePath();
    heartbeat.stop();
    QObject::disconnect(connection);
    stopped.store(true);
    producer->wait();
    QVERIFY(model->removeSource(cameraSourceId));
    QVERIFY(model->removeSource(screen));
    QVERIFY(model->removeSource(imageId));
    QVERIFY(audio->removeSource(microphoneSourceId));
    QVERIFY(audio->removeSource(system));
    QTRY_COMPARE_WITH_TIMEOUT(camera->activeWorkerCount() + display->activeWorkerCount() +
                                  audio->activeWorkerCount(),
                              0, 10000);
    preview->setSources({});
    window.hide();
    QString error;
    const auto tools = QFileInfo(QStringLiteral(OPENLIVE_FFPROBE)).absolutePath();
    const auto ffmpeg = QDir(tools).filePath(QStringLiteral("ffmpeg.exe"));
    saveJson(QDir(directory).filePath("progress.json"),
             {{"phase", "decoding-video"}, {"file", movie}, {"elapsedMs", elapsed.elapsed()}});
    const auto info = QJsonDocument::fromJson(
                          runMediaTool(QStringLiteral(OPENLIVE_FFPROBE),
                                  {"-v", "error", "-show_streams", "-of", "json", movie}, &error))
                          .object();
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QJsonObject video, audioStream;
    for (const auto &value : info["streams"].toArray()) {
        const auto stream = value.toObject();
        if (stream["codec_type"] == "video")
            video = stream;
        else
            audioStream = stream;
    }
    const double duration = video["duration"].toString().toDouble();
    QVERIFY(duration >= seconds && duration < seconds + 2);
    QCOMPARE(video["width"].toInt(), 1920);
    QCOMPARE(video["height"].toInt(), 1080);
    QCOMPARE(video["avg_frame_rate"].toString(), QString("30/1"));
    QVERIFY(std::abs(duration - audioStream["duration"].toString().toDouble()) < .035);
    const auto pixels = runMediaTool(ffmpeg,
                                {"-v", "error", "-threads", "2", "-i", movie, "-map", "0:v:0",
                                 "-vf", "crop=8:8:60:60,scale=1:1,format=gray", "-fps_mode",
                                 "passthrough", "-f", "rawvideo", "-"},
                                &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVector<bool> flashes;
    flashes.reserve(pixels.size());
    for (unsigned char pixel : pixels)
        flashes.append(pixel > 150);
    const auto visual = risingEdges(flashes, 1.0 / 30);
    saveJson(QDir(directory).filePath("progress.json"),
             {{"phase", "decoding-audio"}, {"file", movie}, {"elapsedMs", elapsed.elapsed()}});
    const auto pcm = runMediaTool(ffmpeg,
                             {"-v", "error", "-threads", "2", "-i", movie, "-map", "0:a:0", "-ac",
                              "1", "-ar", "48000", "-f", "f32le", "-"},
                             &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const auto *data = reinterpret_cast<const float *>(pcm.constData());
    QVector<bool> beeps;
    std::array<double, 480> sine{}, cosine{};
    for (int i = 0; i < 480; ++i) {
        const double angle = 2 * std::acos(-1.0) * 1500 * i / 48000;
        sine[size_t(i)] = std::sin(angle);
        cosine[size_t(i)] = std::cos(angle);
    }
    for (qsizetype position = 0; position + 480 <= pcm.size() / 4; position += 480) {
        double real = 0, imaginary = 0;
        for (int i = 0; i < 480; ++i) {
            real += data[position + i] * cosine[size_t(i)];
            imaginary += data[position + i] * sine[size_t(i)];
        }
        beeps.append(2 * std::sqrt(real * real + imaginary * imaginary) / 480 > .15);
    }
    const auto auditory = risingEdges(beeps, .01);
    QVector<double> offsets;
    for (double timestamp : visual) {
        if (timestamp < 1 || timestamp > duration - 1)
            continue;
        double nearest = 1000;
        for (double sound : auditory)
            if (std::abs(sound - timestamp) < std::abs(nearest))
                nearest = sound - timestamp;
        offsets.append(nearest);
    }
    QVERIFY(offsets.size() >= seconds - 3);
    double maximumOffset = 0;
    for (double offset : offsets)
        maximumOffset = std::max(maximumOffset, std::abs(offset));
    const int count = std::min(30, int(offsets.size()));
    const double firstMean =
        std::accumulate(offsets.cbegin(), offsets.cbegin() + count, 0.0) / count;
    const double lastMean = std::accumulate(offsets.cend() - count, offsets.cend(), 0.0) / count;
    qint64 baseline = 0, maximumPrivate = 0;
    int baselineHandles = 0, maximumHandles = 0;
    for (const auto &sample : samples) {
        const auto value = sample.toObject();
        const auto bytes = qint64(value["privateBytes"].toDouble());
        const int handles = value["handles"].toInt();
        if (!baseline && value["elapsedMs"].toDouble() >= std::min(60000, seconds * 100)) {
            baseline = bytes;
            baselineHandles = handles;
        }
        if (baseline) {
            maximumPrivate = std::max(maximumPrivate, bytes);
            maximumHandles = std::max(maximumHandles, handles);
        }
    }
    QJsonObject summary{{"seconds", duration},
                        {"file", movie},
                        {"frames", status.videoFrames},
                        {"decodedFrames", pixels.size()},
                        {"maximumSyncErrorMs", maximumOffset * 1000},
                        {"firstMeanOffsetMs", firstMean * 1000},
                        {"lastMeanOffsetMs", lastMean * 1000},
                        {"driftMs", std::abs(lastMean - firstMean) * 1000},
                        {"pairedEvents", offsets.size()},
                        {"missingAudioFrames", status.missingAudioFrames},
                        {"repeatedVideoFrames", qint64(status.repeatedFrames)},
                        {"droppedVideoFrames", qint64(status.droppedFrames)},
                        {"maximumUiGapMs", maximumUiGapMs},
                        {"privateBytesBaseline", baseline},
                        {"privateBytesMaximum", maximumPrivate},
                        {"handlesBaseline", baselineHandles},
                        {"handlesMaximum", maximumHandles},
                        {"events", events},
                        {"resources", samples}};
    QVERIFY(saveJson(QDir(directory).filePath("summary.json"), summary));
    qInfo() << "Recording acceptance summary:"
            << QJsonDocument(summary).toJson(QJsonDocument::Compact);
    QCOMPARE(pixels.size(), status.videoFrames);
    QVERIFY2(maximumOffset <= .1, "Encoded flash/beep offset exceeded 100 ms");
    QVERIFY2(std::abs(lastMean - firstMean) < .05, "Encoded sync offset drifted by 50 ms");
    QVERIFY2(maximumUiGapMs < 250, "GUI heartbeat stalled for 250 ms");
    QVERIFY2(maximumPrivate - baseline < 192 * 1024 * 1024LL,
             "Private memory grew by over 192 MiB after warmup");
    QVERIFY2(maximumHandles - baselineHandles < 64, "Process handles grew by over 64 after warmup");
    QCOMPARE(status.missingAudioFrames, qint64(0));
    saveJson(QDir(directory).filePath("progress.json"),
             {{"phase", "passed"}, {"summary", summary}});
}
}
#endif
