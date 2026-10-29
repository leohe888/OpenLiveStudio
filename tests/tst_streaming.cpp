#include "output/streamingcontroller.h"
#include "output/recordingcontroller.h"
#include "output/outputsettings.h"
#include "preview/compositor.h"
#include "audio/audiomixer.h"
#include "core/mediaclock.h"
#include <QGuiApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QDir>
#include <QUrl>
#include <QTest>
#include <QScopeGuard>
#include <atomic>
#include <cmath>

class StreamingTest : public QObject {
    Q_OBJECT
private slots:
    void settingsAndIndependentLocks() {
        OutputSettings settings; QTemporaryDir directory;
        QCOMPARE(settings.recordingBitrate(),12000); QCOMPARE(settings.streamingBitrate(),6000);
        QVERIFY(settings.apply(directory.path(),12000,"rtmp://127.0.0.1/live","test"));
        QVERIFY(!settings.apply(directory.path(),12000,"rtmps://example.test/live","test"));
        QVERIFY(!settings.apply(directory.path(),12000,"rtmp://example.test","test"));
        QVERIFY(!settings.apply(directory.path(),12000,"rtmp://example.test/live","bad\nkey"));
        QVERIFY(!settings.apply(directory.path(),12000,"rtmp://example.test/live",""));
        settings.setStreamingLocked(true);
        QVERIFY(!settings.apply(directory.path(),12000,"rtmp://127.0.0.1/live","other"));
        QVERIFY(!settings.apply(directory.path(),12000,"rtmp://127.0.0.1/live","test",8000));
        QVERIFY(settings.apply(directory.path(),16000,"rtmp://127.0.0.1/live","test"));
        settings.setRecordingLocked(true); settings.setStreamingLocked(false);
        QVERIFY(settings.apply(directory.path(),16000,"rtmp://127.0.0.1/live","other",8000));
        QVERIFY(!settings.apply(directory.path(),12000,"rtmp://127.0.0.1/live","other",8000));
    }
    void subscriptionsDoNotStealOrDisableEachOther() {
        Compositor video; AudioMixer audio; audio.start();
        QVERIFY(video.startRendering()); QVERIFY(!video.captureImage().isNull());
        const auto stop=qScopeGuard([&] { video.stopRendering(); audio.stop(); audio.wait(); });
        auto a=video.subscribeOutput(),b=video.subscribeOutput();
        auto c=audio.subscribeOutput(),d=audio.subscribeOutput();
        QTest::qWait(350);
        const auto first=a->take(),second=b->take();
        QVERIFY(!first.empty()); QCOMPARE(first.size(),second.size());
        QCOMPARE(first.back().timestampNs,second.back().timestampNs);
        QVERIFY(!c->take().empty()); QVERIFY(!d->take().empty());
        a.reset(); c.reset(); video.setOutputEnabled(false); audio.setOutputEnabled(false);
        QTest::qWait(200); QVERIFY(!b->take().empty()); QVERIFY(!d->take().empty());
        QTest::qWait(600); d->take(); QTest::qWait(600);
        QVERIFY(b->dropped()>0); QVERIFY(d->dropped()==0);
    }
    void hungHandshakeRetriesAndCancelsWithoutStoppingRecording() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QTcpSocket *> peers;
        connect(&server,&QTcpServer::newConnection,this,[&] { while (server.hasPendingConnections()) peers.append(server.nextPendingConnection()); });
        Compositor video; AudioMixer audio; audio.start();
        QVERIFY(video.startRendering()); QVERIFY(!video.captureImage().isNull());
        const auto cleanup=qScopeGuard([&] { video.stopRendering(); audio.stop(); audio.wait(); });
        RecordingController recording(video,audio); StreamingController streaming(video,audio);
        QTemporaryDir directory;
        QVERIFY(!streaming.startStreaming("rtmps://127.0.0.1/live","secret"));
        QCOMPARE(streaming.state(),QString("error"));
        QVERIFY(recording.startRecording(directory.filePath("independent.mp4")));
        QTRY_COMPARE_WITH_TIMEOUT(recording.state(),QString("recording"),5000);
        const auto endpoint=QString("rtmp://127.0.0.1:%1/live").arg(server.serverPort());
        QVERIFY(streaming.startStreaming(endpoint,"secret"));
        QVERIFY(!streaming.startStreaming(endpoint,"duplicate"));
        QTRY_VERIFY_WITH_TIMEOUT(!peers.empty(),2000);
        QTRY_VERIFY_WITH_TIMEOUT(streaming.status().reconnects>=1,5000);
        QCOMPARE(streaming.state(),QString("reconnecting"));
        QCOMPARE(recording.state(),QString("recording"));
        QElapsedTimer elapsed; elapsed.start(); streaming.stopStreaming();
        QTRY_VERIFY_WITH_TIMEOUT(!streaming.busy(),1500); QVERIFY(elapsed.elapsed()<1500);
        QCOMPARE(streaming.state(),QString("idle"));
        QCOMPARE(recording.state(),QString("recording"));
        QVERIFY(streaming.startStreaming(endpoint,"secret"));
        QTRY_COMPARE_WITH_TIMEOUT(streaming.state(),QString("connecting"),1000);
        QTest::qWait(100); elapsed.restart(); streaming.stopStreaming();
        QTRY_VERIFY_WITH_TIMEOUT(!streaming.busy(),1500); QVERIFY(elapsed.elapsed()<1500);
        recording.stopRecording(); QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(),5000);
        QVERIFY2(recording.fileComplete(),qPrintable(recording.lastError()));
        QVERIFY(streaming.startStreaming("rtmp://openlive-test.invalid/live","secret"));
        streaming.stopStreaming(); QTRY_VERIFY_WITH_TIMEOUT(!streaming.busy(),1500);
        QVERIFY(!streaming.startStreaming("rtmps://127.0.0.1/live","secret"));
        QTest::qWait(150); QCOMPARE(streaming.state(),QString("error"));
        QVERIFY(!streaming.lastError().isEmpty());
    }
    void srsIndependentLifecycle() {
        const auto server=qEnvironmentVariable("OPENLIVE_SRS_SERVER");
        if (server.isEmpty()) QSKIP("Set OPENLIVE_SRS_SERVER for explicit SRS integration");
        Compositor video; AudioMixer audio; audio.start();
        QVERIFY(video.startRendering()); QVERIFY(!video.captureImage().isNull());
        const auto cleanup=qScopeGuard([&] { video.stopRendering(); audio.stop(); audio.wait(); });
        RecordingController recording(video,audio); StreamingController streaming(video,audio);
        connect(&recording,&RecordingController::overloaded,&streaming,&StreamingController::stopForOverload);
        QTemporaryDir directory;
        const auto key=qEnvironmentVariable("OPENLIVE_SRS_KEY","openlive-integration");
        for (bool recordFirst:{true,false}) {
            if (recordFirst) { QVERIFY(recording.startRecording(directory.filePath("first.mp4"))); QTRY_COMPARE_WITH_TIMEOUT(recording.state(),QString("recording"),5000); }
            QVERIFY(streaming.startStreaming(server,key));
            QTRY_COMPARE_WITH_TIMEOUT(streaming.state(),QString("live"),6000);
            if (!recordFirst) { QVERIFY(recording.startRecording(directory.filePath("second.mp4"))); QTRY_COMPARE_WITH_TIMEOUT(recording.state(),QString("recording"),5000); }
            QTest::qWait(1200); QCOMPARE(streaming.state(),QString("live")); QCOMPARE(recording.state(),QString("recording"));
            if (recordFirst) {
                recording.stopRecording(); QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(),5000);
                QVERIFY(recording.fileComplete()); QCOMPARE(streaming.state(),QString("live"));
                // A local file error must not interrupt the live output.
                QVERIFY(recording.startRecording(directory.filePath("first.mp4")));
                QTRY_COMPARE_WITH_TIMEOUT(recording.state(),QString("error"),3000);
                QCOMPARE(streaming.state(),QString("live"));
                streaming.stopStreaming(); QTRY_VERIFY_WITH_TIMEOUT(!streaming.busy(),3000);
            } else {
                streaming.stopStreaming(); QTRY_VERIFY_WITH_TIMEOUT(!streaming.busy(),3000);
                QCOMPARE(recording.state(),QString("recording"));
                QVERIFY(streaming.startStreaming(server,key)); QTRY_COMPARE_WITH_TIMEOUT(streaming.state(),QString("live"),6000);
                recording.stopRecording(); QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(),5000); QVERIFY(recording.fileComplete());
                streaming.stopStreaming(); QTRY_VERIFY_WITH_TIMEOUT(!streaming.busy(),3000);
            }
        }
        // This SRS version closes a duplicate publisher without a typed rejection.
        // Treat the ambiguous disconnect as transient, while preserving the original.
        StreamingController duplicate(video,audio);
        QVERIFY(streaming.startStreaming(server,key)); QTRY_COMPARE_WITH_TIMEOUT(streaming.state(),QString("live"),6000);
        QVERIFY(duplicate.startStreaming(server,key)); QTRY_VERIFY_WITH_TIMEOUT(duplicate.status().reconnects>0,6000);
        QVERIFY(!duplicate.lastError().contains(key)); duplicate.stopStreaming(); QTRY_VERIFY_WITH_TIMEOUT(!duplicate.busy(),2000);
        QCOMPARE(streaming.state(),QString("live")); streaming.stopStreaming(); QTRY_VERIFY_WITH_TIMEOUT(!streaming.busy(),3000);
        qputenv("OPENLIVE_STREAMING_ENCODER_DELAY_MS","100");
        const auto restore=qScopeGuard([] { qunsetenv("OPENLIVE_STREAMING_ENCODER_DELAY_MS"); });
        QVERIFY(streaming.startStreaming(server,key)); QTRY_COMPARE_WITH_TIMEOUT(streaming.state(),QString("error"),10000);
        QCOMPARE(streaming.status().reconnects,quint64(0));
    }
    void srsTemporaryEncoderPauseKeepsConnection() {
        const auto server=qEnvironmentVariable("OPENLIVE_SRS_SERVER");
        if (server.isEmpty()) QSKIP("Set OPENLIVE_SRS_SERVER for live timeline recovery");
        Compositor video; AudioMixer audio; audio.start();
        QVERIFY(video.startRendering()); QVERIFY(!video.captureImage().isNull());
        const auto cleanup=qScopeGuard([&] { video.stopRendering(); audio.stop(); audio.wait(); });
        RecordingController recording(video,audio); StreamingController streaming(video,audio);
        QTemporaryDir directory;
        qputenv("OPENLIVE_STREAMING_PAUSE_ONCE_MS","1200");
        const auto restore=qScopeGuard([] { qunsetenv("OPENLIVE_STREAMING_PAUSE_ONCE_MS"); });
        QVERIFY(recording.startRecording(directory.filePath("continuous.mp4")));
        QTRY_COMPARE_WITH_TIMEOUT(recording.state(),QString("recording"),5000);
        QVERIFY(streaming.startStreaming(server,"transient-recovery"));
        QTRY_COMPARE_WITH_TIMEOUT(streaming.state(),QString("live"),6000);
        const auto ffmpeg=QDir(QFileInfo(QStringLiteral(OPENLIVE_FFPROBE)).absolutePath()).filePath("ffmpeg.exe");
        QUrl playback(server); playback.setScheme("http"); playback.setPort(8080);
        playback.setPath(playback.path()+"/transient-recovery.flv");
        const auto sample=directory.filePath("recovery.flv");
        QProcess capture; capture.start(ffmpeg,{"-v","error","-rw_timeout","5000000","-analyzeduration","0","-probesize","32768","-i",playback.toString(),"-t","7","-c","copy",sample});
        QVERIFY(capture.waitForFinished(15000)); QCOMPARE(capture.exitCode(),0);
        QCoreApplication::processEvents();
        QCOMPARE(streaming.state(),QString("live")); QCOMPARE(streaming.status().reconnects,quint64(0));
        QVERIFY(streaming.status().droppedFrames>=20); QCOMPARE(recording.state(),QString("recording"));
        QProcess probe; probe.start(QStringLiteral(OPENLIVE_FFPROBE),{"-v","error","-show_packets","-of","json",sample});
        QVERIFY(probe.waitForFinished(10000)); QCOMPARE(probe.exitCode(),0);
        const auto packets=QJsonDocument::fromJson(probe.readAllStandardOutput()).object()["packets"].toArray();
        double previous[2]{-1,-1},maximumVideoGap=0; int keyframes=0,count=0;
        for (const auto &value:packets) {
            const auto packet=value.toObject(); const int track=packet["stream_index"].toInt(); QVERIFY(track==0 || track==1);
            const auto dts=packet["dts_time"].toString().toDouble();
            if (previous[track]>=0) {
                QVERIFY(dts>previous[track]);
                if (track==0) maximumVideoGap=std::max(maximumVideoGap,dts-previous[track]);
            }
            if (track==0 && packet["flags"].toString().contains('K')) ++keyframes;
            previous[track]=dts; ++count;
        }
        // SRS full time-jitter correction can compress the timestamp jump.
        // The encoder checks the forced packet's KEY flag before sending it.
        QVERIFY(count>100); QVERIFY(keyframes>=2);
        qInfo()<<"Recovered live maximum DTS gap / keyframes:"<<maximumVideoGap<<keyframes;
        QProcess decode; decode.start(ffmpeg,{"-v","error","-threads","1","-i",sample,"-f","null","-"});
        QVERIFY(decode.waitForFinished(10000)); QCOMPARE(decode.exitCode(),0);
        streaming.stopStreaming(); QTRY_VERIFY_WITH_TIMEOUT(!streaming.busy(),3000);
        recording.stopRecording(); QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(),5000); QVERIFY(recording.fileComplete());
    }
    void srsNetworkFaults() {
        const auto endpoint=qEnvironmentVariable("OPENLIVE_SRS_SERVER");
        if (endpoint.isEmpty()) QSKIP("Set OPENLIVE_SRS_SERVER for network fault integration");
        const QUrl destination(endpoint); QTcpServer relay;
        QVERIFY(relay.listen(QHostAddress::LocalHost)); const auto port=relay.serverPort();
        QList<QTcpSocket *> peers; bool paused=false;
        connect(&relay,&QTcpServer::newConnection,this,[&] {
            while (relay.hasPendingConnections()) {
                auto *client=relay.nextPendingConnection(); auto *upstream=new QTcpSocket(&relay);
                peers.append(client); peers.append(upstream);
                client->setReadBufferSize(65536); upstream->setReadBufferSize(65536);
                connect(client,&QTcpSocket::readyRead,upstream,[&,client,upstream] { if (!paused && upstream->state()==QAbstractSocket::ConnectedState) upstream->write(client->readAll()); });
                connect(upstream,&QTcpSocket::connected,client,[client,upstream] { upstream->write(client->readAll()); });
                connect(upstream,&QTcpSocket::readyRead,client,[&,client,upstream] { if (!paused) client->write(upstream->readAll()); });
                connect(upstream,&QTcpSocket::disconnected,client,&QTcpSocket::abort);
                connect(client,&QTcpSocket::disconnected,upstream,&QTcpSocket::abort);
                upstream->connectToHost(destination.host(),quint16(destination.port(1935)));
            }
        });
        Compositor video; AudioMixer audio; audio.start();
        QVERIFY(video.startRendering()); QVERIFY(!video.captureImage().isNull());
        const auto cleanup=qScopeGuard([&] { video.stopRendering(); audio.stop(); audio.wait(); });
        RecordingController recording(video,audio); StreamingController streaming(video,audio);
        QTemporaryDir directory; QVERIFY(recording.startRecording(directory.filePath("network.mp4")));
        QTRY_COMPARE_WITH_TIMEOUT(recording.state(),QString("recording"),5000);
        const auto server=QString("rtmp://127.0.0.1:%1%2").arg(port).arg(destination.path());
        QVERIFY(streaming.startStreaming(server,"network-test")); QTRY_COMPARE_WITH_TIMEOUT(streaming.state(),QString("live"),6000);
        QTest::qWait(500); const auto frames=recording.status().videoFrames;
        relay.close(); for (auto *peer:peers) peer->abort();
        QTRY_VERIFY_WITH_TIMEOUT(streaming.status().reconnects>=2,8000);
        QCOMPARE(recording.state(),QString("recording")); QVERIFY(recording.status().videoFrames>frames+30);
        QVERIFY(relay.listen(QHostAddress::LocalHost,port));
        QTRY_COMPARE_WITH_TIMEOUT(streaming.state(),QString("live"),15000);
        QCOMPARE(recording.state(),QString("recording"));
        const auto reconnects=streaming.status().reconnects;
        // Stop draining a live TCP connection, exercising bounded network writes
        // and fresh-session recovery rather than only connection refusal.
        paused=true; QTest::qWait(8000); paused=false;
        for (auto *peer:peers) peer->abort();
        QTRY_VERIFY_WITH_TIMEOUT(streaming.status().reconnects>reconnects,6000);
        QTRY_COMPARE_WITH_TIMEOUT(streaming.state(),QString("live"),20000);
        QCOMPARE(recording.state(),QString("recording"));
        streaming.stopStreaming(); QTRY_VERIFY_WITH_TIMEOUT(!streaming.busy(),3000);
        recording.stopRecording(); QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(),5000); QVERIFY(recording.fileComplete());
    }
    void srsEndurance() {
        const auto server=qEnvironmentVariable("OPENLIVE_SRS_SERVER");
        const int seconds=qEnvironmentVariableIntValue("OPENLIVE_STREAMING_SOAK_SECONDS");
        if (server.isEmpty() || seconds<=0) QSKIP("Set SRS server and explicit soak duration");
        const auto directory=qEnvironmentVariable("OPENLIVE_STREAMING_ARTIFACTS");
        QVERIFY(!directory.isEmpty()); QVERIFY(QDir().mkpath(directory));
        const auto save=[&](QString name,QJsonObject data) { QSaveFile file(QDir(directory).filePath(name)); if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(data).toJson()); file.commit(); } };
        Compositor video; AudioMixer audio;
        const auto queue=std::make_shared<AudioPacketQueue>(); audio.setInputs({{"sync",1,false,queue}}); audio.start();
        QVERIFY(video.startRendering());
        std::atomic_bool stopped{false}; const auto epoch=MediaClock::nowNs();
        std::atomic<qint64> cue{epoch}; qint64 previousSecond=-1;
        QTimer heartbeat; heartbeat.setTimerType(Qt::PreciseTimer); heartbeat.setInterval(20);
        qint64 lastTick=epoch,maxUiGap=0;
        connect(&heartbeat,&QTimer::timeout,this,[&] {
            const auto now=MediaClock::nowNs(); maxUiGap=std::max(maxUiGap,(now-lastTick)/1000000); lastTick=now;
            const auto second=(now-epoch)/1000000000LL;
            if (second!=previousSecond) { previousSecond=second; cue=now; }
            QImage marker(32,32,QImage::Format_RGBA8888); marker.fill(Qt::black);
            const bool flash=now-cue.load()<100000000LL;
            const int stamp=int((now-epoch)/100000000LL)&65535;
            for (int y=0;y<32;++y) for (int x=0;x<32;++x)
                marker.setPixelColor(x,y,(y<16 ? flash : bool(stamp & (1<<(x/2)))) ? Qt::white : Qt::black);
            video.setSources({{"sync",marker,true,{{960,540},{1920,1080},0}}});
        });
        auto producer=std::unique_ptr<QThread>(QThread::create([&] {
            qint64 position=0;
            while (!stopped) {
                const auto timestamp=epoch+AudioFormat::framesToNs(position);
                if (timestamp>MediaClock::nowNs()) { QThread::msleep(1); continue; }
                AudioBlock block; block.timestampNs=timestamp; block.samples.resize(960);
                auto *samples=block.samples.data();
                for (int i=0;i<480;++i) {
                    const auto since=epoch+AudioFormat::framesToNs(position+i)-cue.load();
                    const float sample=since>=0 && since<100000000LL ? .25f*float(std::sin(2*std::acos(-1.0)*1500*double(position+i)/48000)) : 0;
                    samples[i*2]=samples[i*2+1]=sample;
                }
                queue->publish(std::move(block)); position+=480;
            }
        }));
        heartbeat.start(); producer->start(QThread::HighPriority);
        const auto cleanup=qScopeGuard([&] { heartbeat.stop(); stopped=true; producer->wait(); video.stopRendering(); audio.stop(); audio.wait(); });
        QVERIFY(!video.captureImage().isNull());
        RecordingController recording(video,audio); StreamingController streaming(video,audio);
        connect(&recording,&RecordingController::overloaded,&streaming,&StreamingController::stopForOverload);
        const auto path=QDir(directory).filePath("endurance.mp4");
        QVERIFY(recording.startRecording(path)); QTRY_COMPARE_WITH_TIMEOUT(recording.state(),QString("recording"),5000);
        const auto key=qEnvironmentVariable("OPENLIVE_SRS_KEY","openlive-endurance");
        QVERIFY(streaming.startStreaming(server,key)); QTRY_COMPARE_WITH_TIMEOUT(streaming.state(),QString("live"),6000);
        QProcess decoder;
        const auto http=qEnvironmentVariable("OPENLIVE_SRS_HTTP");
        QVERIFY(!http.isEmpty());
        const auto ffmpeg=QDir(QFileInfo(QStringLiteral(OPENLIVE_FFPROBE)).absolutePath()).filePath("ffmpeg.exe");
        decoder.start(ffmpeg,{"-v","error","-fflags","nobuffer","-flags","low_delay","-probesize","32768","-analyzeduration","0","-threads","2","-i",http,
            "-threads","2","-filter_threads","1","-an","-vf","crop=1920:8:0:800,scale=16:1,format=gray","-fps_mode","passthrough","-f","rawvideo","pipe:1"});
        QVERIFY(decoder.waitForStarted(3000));
        const auto stopDecoder=qScopeGuard([&] { decoder.kill(); decoder.waitForFinished(3000); });
        QElapsedTimer elapsed; elapsed.start(); qint64 reported=-10000,maxLatency=0,latencySamples=0;
        QByteArray decoded;
        while (elapsed.elapsed()<qint64(seconds)*1000) {
            QTest::qWait(20);
            QVERIFY2(recording.state()=="recording",qPrintable(recording.lastError()));
            QVERIFY2(streaming.busy() && streaming.state()!="stopping",qPrintable(QString("state=%1 error=%2 recordingFrames=%3 duration=%4 pressured=%5 encodeMs=%6 lagMs=%7").arg(streaming.state(),streaming.lastError()).arg(recording.status().videoFrames).arg(recording.durationMs()).arg(recording.status().pressured).arg(streaming.status().lastEncodeMs).arg(streaming.status().lagMs)));
            decoded+=decoder.readAllStandardOutput();
            while (decoded.size()>=16) {
                int stamp=0; for (int bit=0;bit<16;++bit) if (static_cast<unsigned char>(decoded[bit])>150) stamp|=1<<bit;
                const auto latency=(MediaClock::nowNs()-epoch)/1000000-qint64(stamp)*100;
                if (latencySamples>30) maxLatency=std::max(maxLatency,latency);
                ++latencySamples; decoded.remove(0,16);
            }
            if (elapsed.elapsed()-reported>=1000) {
                reported=elapsed.elapsed(); const auto status=streaming.status();
                save("progress.json",{{"elapsedMs",elapsed.elapsed()},{"targetSeconds",seconds},{"recording",recording.state()},{"encodeMs",status.lastEncodeMs},{"lagMs",status.lagMs},
                    {"recordLagMs",recording.status().lagMs},{"recordEncodeMs",recording.status().lastEncodeMs},{"recordOutputMs",recording.status().lastOutputMs},
                    {"recordVideoMs",recording.status().videoEncodeMs},{"recordAudioMs",recording.status().audioEncodeMs},
                    {"conversionMs",recording.status().conversionMs},{"imageFormat",recording.status().imageFormat},
                    {"streaming",status.state},{"reconnects",qint64(status.reconnects)},{"bitrateKbps",status.bitrateKbps},
                    {"droppedFrames",qint64(status.droppedFrames)},{"maxPlaybackLatencyMs",maxLatency},{"latencySamples",latencySamples},{"maxUiGapMs",maxUiGap}});
            }
        }
        streaming.stopStreaming(); QTRY_VERIFY_WITH_TIMEOUT(!streaming.busy(),3000);
        recording.stopRecording(); QTRY_VERIFY_WITH_TIMEOUT(!recording.busy(),10000); QVERIFY(recording.fileComplete());
        save("summary.json",{{"seconds",seconds},{"frames",recording.status().videoFrames},{"missingAudioSamples",recording.status().missingAudioSamples},
            {"reconnects",qint64(streaming.status().reconnects)},{"maxPlaybackLatencyMs",maxLatency},{"latencySamples",latencySamples},{"maxUiGapMs",maxUiGap},{"file",path}});
        QVERIFY(latencySamples>100); QVERIFY2(maxLatency<=3000,"HTTP-FLV playback latency exceeded 3 seconds");
        QCOMPARE(recording.status().missingAudioSamples,qint64(0));
        // This standalone harness also performs synchronous artifact writes and
        // pipe sampling on its main thread. Its heartbeat is diagnostic, not a
        // product-UI deadline; UI cancellation/close have separate native tests.
        qInfo()<<"Endurance harness maximum heartbeat gap (ms):"<<maxUiGap;
    }
};
int main(int argc,char **argv) {
    QSurfaceFormat format; format.setVersion(3,3); format.setProfile(QSurfaceFormat::CoreProfile);
    format.setRenderableType(QSurfaceFormat::OpenGL); QSurfaceFormat::setDefaultFormat(format);
    const int soak=qEnvironmentVariableIntValue("OPENLIVE_STREAMING_SOAK_SECONDS");
    if (soak>0 && !qEnvironmentVariableIsSet("QTEST_FUNCTION_TIMEOUT")) qputenv("QTEST_FUNCTION_TIMEOUT",QByteArray::number((qint64(soak)+300)*1000));
    QGuiApplication app(argc,argv); StreamingTest test; return QTest::qExec(&test,argc,argv);
}
#include "tst_streaming.moc"
