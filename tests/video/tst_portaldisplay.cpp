#include "video/display/displaysourcecontroller.h"
#include "video/videosourcemanager.h"
#include "video/display/waylanddisplaycapture.h"
#include <spa/buffer/buffer.h>
#include <spa/param/video/raw.h>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

struct MockStream { uint node; QVariantMap properties; };
Q_DECLARE_METATYPE(MockStream)
QDBusArgument &operator<<(QDBusArgument &argument, const MockStream &stream)
{
    argument.beginStructure();
    argument << stream.node << stream.properties;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, MockStream &stream)
{
    argument.beginStructure();
    argument >> stream.node >> stream.properties;
    argument.endStructure();
    return argument;
}

class MockPortal : public QDBusVirtualObject
{
public:
    bool holdStart = false, cancelSelection = true, closeSession = false;
    QStringList methods;
    QVariantMap selection;
    int sessionsClosed = 0, requestsClosed = 0;
    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &bus) override
    {
        if (message.member() == "Get") {
            bus.send(message.createReply({QVariant::fromValue(QDBusVariant(3u))}));
            return true;
        }
        if (message.member() == "Close") {
            if (message.interface() == "org.freedesktop.portal.Session")
                ++sessionsClosed;
            else
                ++requestsClosed;
            bus.send(message.createReply());
            return true;
        }
        if (message.member() == "OpenPipeWireRemote") {
            methods.append(message.member());
            bus.send(message.createErrorReply("org.freedesktop.portal.Error.Failed", "Test remote error"));
            return true;
        }
        const auto options = qdbus_cast<QVariantMap>(message.arguments().last());
        auto sender = message.service().mid(1);
        sender.replace('.', '_');
        const auto request = "/org/freedesktop/portal/desktop/request/" + sender + "/" +
                             options.value("handle_token").toString();
        methods.append(message.member());
        if (message.member() == "SelectSources")
            selection = options;
        bus.send(message.createReply({QVariant::fromValue(QDBusObjectPath(request))}));
        if (holdStart && message.member() == "Start")
            return true;
        QVariantMap result;
        uint code = 0;
        if (message.member() == "CreateSession")
            result.insert("session_handle", "/org/freedesktop/portal/desktop/session/" + sender +
                          "/" + options.value("session_handle_token").toString());
        if (message.member() == "Start") {
            if (closeSession) {
                const auto session = qvariant_cast<QDBusObjectPath>(message.arguments()[0]).path();
                auto signal = QDBusMessage::createSignal(session, "org.freedesktop.portal.Session", "Closed");
                signal.setArguments({QVariantMap{}});
                bus.send(signal);
                return true;
            }
            if (cancelSelection)
                code = 1;
            else
                result.insert("streams", QVariant::fromValue(QList<MockStream>{{42u, {}}}));
        }
        QTimer::singleShot(0, this, [=] {
            auto signal = QDBusMessage::createSignal(request, "org.freedesktop.portal.Request", "Response");
            signal.setArguments({code, result});
            bus.send(signal);
        });
        return true;
    }
};

class PortalDisplayTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qDBusRegisterMetaType<MockStream>();
        qDBusRegisterMetaType<QList<MockStream>>();
    }
    void sessionClosureAndRemoteError_data()
    {
        QTest::addColumn<bool>("closeSession");
        QTest::newRow("system-stops-sharing") << true;
        QTest::newRow("remote-open-fails") << false;
    }
    void sessionClosureAndRemoteError()
    {
        QFETCH(bool, closeSession);
        MockPortal portal;
        portal.cancelSelection = false;
        portal.closeSession = closeSession;
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService("org.freedesktop.portal.Desktop"));
        QVERIFY(bus.registerVirtualObject("/org/freedesktop/portal/desktop", &portal,
                                         QDBusConnection::SubPath));
        {
            VideoSourceManager manager;
            DisplaySourceController controller(manager);
            QObject requester;
            QSignalSpy failures(&controller, &DisplaySourceController::addFailed);
            controller.requestAdd("Screen", true, &requester);
            QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 5000);
            QVERIFY2(failures[0][1].toString().contains(closeSession ? "停止" : "无法打开"),
                     qPrintable(failures[0][1].toString()));
            QCOMPARE(portal.methods.contains("OpenPipeWireRemote"), !closeSession);
            QVERIFY(manager.sources().isEmpty());
            QTRY_COMPARE(controller.activeWorkerCount(), 0);
            QTRY_COMPARE(portal.sessionsClosed, 1);
        }
        bus.unregisterObject("/org/freedesktop/portal/desktop", QDBusConnection::UnregisterTree);
        bus.unregisterService("org.freedesktop.portal.Desktop");
    }
    void existingSourceFailure_data()
    {
        QTest::addColumn<bool>("removeOnFailure");
        QTest::newRow("remove-during-error-notification") << true;
        QTest::newRow("manual-retry-and-visibility") << false;
    }
    void existingSourceFailure()
    {
        QFETCH(bool, removeOnFailure);
        MockPortal portal;
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService("org.freedesktop.portal.Desktop"));
        QVERIFY(bus.registerVirtualObject("/org/freedesktop/portal/desktop", &portal,
                                         QDBusConnection::SubPath));
        {
            VideoSourceManager manager;
            DisplaySourceController controller(manager);
            QSignalSpy failures(&controller, &DisplaySourceController::sourceFailed);
            if (removeOnFailure)
                connect(&controller, &DisplaySourceController::sourceFailed, &manager,
                        [&](const QString &id, const QString &) { manager.removeSource(id); });
            QVERIFY(manager.addSource({"screen", "Screen", DisplaySourceConfig{}}));
            QTRY_COMPARE(failures.count(), 1);
            QTRY_COMPARE(controller.activeWorkerCount(), 0);
            if (removeOnFailure) {
                QVERIFY(manager.sources().isEmpty());
                QVERIFY(!controller.retry("screen"));
            } else {
                QCOMPARE(manager.sourceRuntime("screen").state(), VideoSourceState::Error);
                QTest::qWait(150);
                QCOMPARE(portal.methods.count("Start"), 1);
                QVERIFY(manager.setSourceVisible("screen", false));
                QVERIFY(controller.retry("screen"));
                QCOMPARE(manager.sourceRuntime("screen").state(), VideoSourceState::Paused);
                QCOMPARE(controller.activeWorkerCount(), 0);
                QVERIFY(manager.setSourceVisible("screen", true));
                QTRY_COMPARE(failures.count(), 2);
                QCOMPARE(portal.methods.count("Start"), 2);
                const auto transform = manager.sources()[0].transform();
                QVERIFY(manager.setSourceConfig("screen", DisplaySourceConfig{false}));
                QTRY_COMPARE(failures.count(), 3);
                QCOMPARE(portal.selection.value("cursor_mode").toUInt(), 1u);
                QCOMPARE(manager.sources()[0].transform(), transform);
                QVERIFY(manager.removeSource("screen"));
                QTRY_COMPARE(controller.activeWorkerCount(), 0);
            }
        }
        bus.unregisterObject("/org/freedesktop/portal/desktop", QDBusConnection::UnregisterTree);
        bus.unregisterService("org.freedesktop.portal.Desktop");
    }
    void copiesPaddedPixelsAndRejectsBadBounds()
    {
        QByteArray bytes = QByteArray::fromHex("0000010203000506070000000000090a0b000d0e0f");
        spa_chunk chunk{};
        chunk.offset = 1;
        chunk.stride = 12;
        chunk.size = 20;
        spa_data data{};
        data.data = bytes.data();
        data.maxsize = bytes.size();
        data.chunk = &chunk;
        spa_buffer buffer{};
        buffer.n_datas = 1;
        buffer.datas = &data;
        spa_video_info_raw format{};
        format.format = SPA_VIDEO_FORMAT_BGRx;
        format.size = {2, 2};
        QString error;
        const auto pixels = WaylandDisplayCapture::copyPixels(buffer, format, error);
        QVERIFY2(pixels, qPrintable(error));
        QCOMPARE(pixels->layout, PixelFrame::BGRA);
        QCOMPARE(pixels->planeSizes[0], QSize(2, 2));
        QCOMPARE(pixels->planes[0], QByteArray::fromHex("000102ff000506ff00090aff000d0eff"));
        chunk.stride = 7;
        QVERIFY(!WaylandDisplayCapture::copyPixels(buffer, format, error));
        QVERIFY(!error.isEmpty());
        chunk.stride = 12;
        chunk.offset = bytes.size();
        QVERIFY(!WaylandDisplayCapture::copyPixels(buffer, format, error));
        chunk.offset = 1;
        chunk.size = 19;
        QVERIFY(!WaylandDisplayCapture::copyPixels(buffer, format, error));
        chunk.size = 20;
        chunk.flags = SPA_CHUNK_FLAG_CORRUPTED;
        QVERIFY(!WaylandDisplayCapture::copyPixels(buffer, format, error));
        QVERIFY(error.isEmpty()); // A dropped corrupt buffer does not end the session.
    }
    void cancelledPickerDoesNotAddSource()
    {
        MockPortal portal;
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService("org.freedesktop.portal.Desktop"));
        QVERIFY(bus.registerVirtualObject("/org/freedesktop/portal/desktop", &portal,
                                         QDBusConnection::SubPath));
        {
            VideoSourceManager manager;
            DisplaySourceController controller(manager);
            QObject requester;
            QSignalSpy failures(&controller, &DisplaySourceController::addFailed);
            QSignalSpy additions(&controller, &DisplaySourceController::displayAdded);
            controller.requestAdd("Screen", true, &requester);
            QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 5000);
            QVERIFY(failures[0][1].toString().contains("取消"));
            QVERIFY(manager.sources().isEmpty());
            QCOMPARE(additions.count(), 0);
            QCOMPARE(portal.methods, QStringList({"CreateSession", "SelectSources", "Start"}));
            QCOMPARE(portal.selection.value("types").toUInt(), 1u);
            QCOMPARE(portal.selection.value("multiple").toBool(), false);
            QCOMPARE(portal.selection.value("cursor_mode").toUInt(), 2u);
            QTRY_COMPARE(controller.activeWorkerCount(), 0);
            QTRY_COMPARE(portal.sessionsClosed, 1);
        }
        bus.unregisterObject("/org/freedesktop/portal/desktop", QDBusConnection::UnregisterTree);
        bus.unregisterService("org.freedesktop.portal.Desktop");
    }
    void closingAddDialogCancelsPendingRequest()
    {
        MockPortal portal;
        portal.holdStart = true;
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService("org.freedesktop.portal.Desktop"));
        QVERIFY(bus.registerVirtualObject("/org/freedesktop/portal/desktop", &portal,
                                         QDBusConnection::SubPath));
        {
            VideoSourceManager manager;
            DisplaySourceController controller(manager);
            QObject requester;
            QSignalSpy failures(&controller, &DisplaySourceController::addFailed);
            controller.requestAdd("Screen", false, &requester);
            QTRY_VERIFY(portal.methods.contains("Start"));
            QCOMPARE(portal.selection.value("cursor_mode").toUInt(), 1u);
            controller.cancelAdd(&requester);
            QTRY_COMPARE(controller.activeWorkerCount(), 0);
            QTRY_COMPARE(portal.requestsClosed, 1);
            QTRY_COMPARE(portal.sessionsClosed, 1);
            QVERIFY(manager.sources().isEmpty());
            QCOMPARE(failures.count(), 0);
        }
        bus.unregisterObject("/org/freedesktop/portal/desktop", QDBusConnection::UnregisterTree);
        bus.unregisterService("org.freedesktop.portal.Desktop");
    }
};
QTEST_GUILESS_MAIN(PortalDisplayTest)
#include "tst_portaldisplay.moc"
