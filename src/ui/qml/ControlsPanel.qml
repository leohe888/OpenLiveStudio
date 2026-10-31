import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"

Rectangle {
    id: root
    readonly property var themeColors: uiColors
    objectName: "controlsPanel"
    implicitWidth: 358
    implicitHeight: 48
    color: themeColors.background.panel
    readonly property var recorder: typeof recordingController !== "undefined" ? recordingController : null
    readonly property var settings: typeof outputSettings !== "undefined" ? outputSettings : null
    readonly property var streamer: typeof streamingController !== "undefined" ? streamingController : null
    signal streamingToggleRequested()
    signal recordingToggleRequested()
    signal settingsDialogRequested()

    StudioScrollView {
        id: scroll
        objectName: "controlsScrollView"
        anchors.fill: parent
        clip: true
        // Keep viewport sizing independent of the layout it sizes.
        contentWidth: Math.max(root.width, controls.implicitWidth + 16)
        contentHeight: Math.max(root.height, 48)
        ScrollBar.horizontal: StudioScrollBar {
            objectName: "controlsHorizontalScrollBar"
            parent: scroll
            x: scroll.leftPadding
            y: scroll.height - height
            width: scroll.availableWidth
            active: scroll.ScrollBar.vertical.active
        }
        ScrollBar.vertical: StudioScrollBar {
            objectName: "controlsVerticalScrollBar"
            parent: scroll
            x: scroll.mirrored ? 0 : scroll.width - width
            y: scroll.topPadding
            height: scroll.availableHeight
            active: scroll.ScrollBar.horizontal.active
        }

        RowLayout {
            id: controls
            objectName: "controlActions"
            x: 8
            y: 8
            width: implicitWidth
            height: 32
            spacing: 6

            StudioButton {
                Layout.maximumHeight: 32
                Accessible.name: enabled ? text : text + qsTr("（不可用）")
                objectName: "recordButton"
                Layout.minimumWidth: 110
                Layout.minimumHeight: 32
                enabled: root.recorder !== null && root.recorder.state !== "stopping" && (root.recorder.busy || root.recorder.ready)
                text: !root.recorder ? qsTr("开始录制") : root.recorder.state === "recording" ? qsTr("停止录制")
                    : root.recorder.state === "starting" ? qsTr("取消启动")
                    : root.recorder.state === "stopping" ? qsTr("正在停止…") : qsTr("开始录制")
                icon.source: "qrc:/assets/icons/circle.svg"
                onClicked: root.recordingToggleRequested()
            }
            StudioButton {
                Layout.maximumHeight: 32
                Accessible.name: enabled ? text : text + qsTr("（不可用）")
                objectName: "streamButton"
                Layout.minimumWidth: 110
                Layout.minimumHeight: 32
                enabled: root.streamer !== null && root.streamer.state !== "stopping" && (root.streamer.busy || root.streamer.ready)
                text: !root.streamer ? qsTr("开始直播") : root.streamer.state === "live" ? qsTr("停止直播")
                    : root.streamer.state === "connecting" ? qsTr("取消连接")
                    : root.streamer.state === "reconnecting" ? qsTr("停止重连")
                    : root.streamer.state === "stopping" ? qsTr("正在停止…") : qsTr("开始直播")
                onClicked: root.streamingToggleRequested()
                icon.source: "qrc:/assets/icons/radio.svg"
            }
            StudioButton {
                Layout.maximumHeight: 32
                Accessible.name: enabled ? text : text + qsTr("（不可用）")
                objectName: "settingsButton"
                Layout.minimumWidth: 110
                Layout.minimumHeight: 32
                text: qsTr("设置")
                enabled: root.settings !== null
                onClicked: root.settingsDialogRequested()
            }
        }
    }
}
