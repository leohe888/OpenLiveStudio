#include "graphics/captureframe.h"
#include "graphics/rendersourcesnapshot.h"
#include <QTest>
#include <thread>

class CaptureFrameTest : public QObject
{
    Q_OBJECT
private slots:
    void snapshotRequiresPixels()
    {
        RenderSourceSnapshot source;
        auto frame = std::make_shared<CaptureFrame>();
        source.frame = frame;
        QVERIFY(!source.hasFrame());
        frame->pixels = std::make_shared<PixelFrame>();
        QVERIFY(source.hasFrame());
    }

    void latestSlotNeverQueuesFrames()
    {
        LatestCaptureFrameSlot slot;
        std::weak_ptr<const CaptureFrame> previous;
        for (quint64 i = 1; i <= 10000; ++i) {
            auto frame = std::make_shared<CaptureFrame>();
            frame->sequence = i;
            slot.publish(frame);
            QVERIFY(previous.expired());
            previous = frame;
        }
        auto latest = slot.takeLatest();
        QCOMPARE(latest->sequence, quint64(10000));
        QVERIFY(!slot.takeLatest());
        latest.reset();
        QVERIFY(previous.expired());
        std::thread producer([&] {
            for (quint64 i = 1; i <= 10000; ++i) {
                auto frame = std::make_shared<CaptureFrame>();
                frame->sequence = i;
                slot.publish(frame);
            }
        });
        quint64 last = 0;
        for (int i = 0; i < 10000; ++i) {
            if (auto frame = slot.takeLatest()) {
                QVERIFY(frame->sequence > last);
                last = frame->sequence;
            }
        }
        producer.join();
        if (auto frame = slot.takeLatest()) {
            QVERIFY(frame->sequence > last);
            last = frame->sequence;
        }
        QCOMPARE(last, quint64(10000));
    }

};

QTEST_GUILESS_MAIN(CaptureFrameTest)
#include "tst_captureframe.moc"
