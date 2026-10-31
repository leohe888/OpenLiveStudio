import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"

Rectangle {
    id: root
    objectName: "outputSettingsDialog"
    required property var outputSettings
    property string directory: ""
    property int recordingBitrate: 12000
    property int streamingBitrate: 6000
    property string server: ""
    property string key: ""
    property string errorText: ""
    readonly property var colors: uiColors
    implicitWidth: 620
    implicitHeight: 460
    color: colors.background.panel
    signal closeRequested()
    signal browseDirectoryRequested()
    Component.onCompleted: {
        directory=outputSettings.recordingDirectory
        recordingBitrate=outputSettings.recordingBitrate
        streamingBitrate=outputSettings.streamingBitrate
        server=outputSettings.streamServer
        key=outputSettings.streamKey
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        StudioTabBar {
            id: tabs
            objectName: "outputSettingsTabs"
            Layout.fillWidth: true
            currentIndex: 1
            Repeater {
                model: [qsTr("直播推流"),qsTr("录屏")]
                delegate: StudioTabButton {
                    required property string modelData
                    text: modelData
                }
            }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex
            ColumnLayout {
                spacing: 8
                StudioLabel { text: qsTr("服务器地址") }
                StudioTextField {
                    implicitHeight: 34
                    maximumLength: 4096
                    Layout.fillWidth: true
                    objectName: "streamServerInput"
                    enabled: !root.outputSettings.streamingLocked
                    text: root.server
                    placeholderText: "rtmp://server/app"
                    onTextEdited: root.server=text
                }
                StudioLabel { text: qsTr("串流密钥") }
                StudioTextField {
                    implicitHeight: 34
                    maximumLength: 4096
                    Layout.fillWidth: true
                    objectName: "streamKeyInput"
                    enabled: !root.outputSettings.streamingLocked
                    text: root.key
                    echoMode: TextInput.Password
                    onTextEdited: root.key=text
                }
                StudioLabel { text: qsTr("视频码率（kbps，500–50000）") }
                StudioTextField {
                    implicitHeight: 34
                    maximumLength: 4096
                    Layout.fillWidth: true
                    objectName: "streamingBitrateInput"
                    enabled: !root.outputSettings.streamingLocked
                    text: String(root.streamingBitrate)
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 500; top: 50000 }
                    onTextEdited: root.streamingBitrate=Number(text)
                }
                StudioLabel {
                    text: qsTr("RTMP · H.264 / AAC · 1080p · 30 FPS；连接和重连期间设置锁定。")
                    color: root.colors.text.secondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Item { Layout.fillHeight: true }
            }
            ColumnLayout {
                spacing: 8
                StudioLabel { text: qsTr("保存目录") }
                RowLayout {
                    Layout.fillWidth: true
                    StudioTextField {
                        implicitHeight: 34
                        maximumLength: 4096
                        Layout.fillWidth: true
                        objectName: "recordingDirectoryInput"
                        text: root.directory
                        enabled: !root.outputSettings.recordingLocked
                        onTextEdited: root.directory=text
                    }
                    StudioButton {
                        implicitWidth: 88
                        objectName: "browseRecordingDirectoryButton"
                        text: qsTr("浏览…")
                        enabled: !root.outputSettings.recordingLocked
                        onClicked: root.browseDirectoryRequested()
                    }
                }
                StudioLabel { text: qsTr("视频码率（kbps，500–50000）") }
                StudioTextField {
                    implicitHeight: 34
                    maximumLength: 4096
                    Layout.fillWidth: true
                    objectName: "recordingBitrateInput"
                    enabled: !root.outputSettings.recordingLocked
                    text: String(root.recordingBitrate)
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 500; top: 50000 }
                    onTextEdited: root.recordingBitrate=Number(text)
                }
                StudioLabel { text: qsTr("MP4 · 1080p · 30 FPS"); color: root.colors.text.secondary }
                StudioLabel {
                    text: qsTr("录制期间，保存目录和码率保持不变。")
                    color: root.colors.text.secondary
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Item { Layout.fillHeight: true }
            }
        }
        StudioLabel {
            objectName: "outputSettingsError"
            text: root.errorText
            textFormat: Text.PlainText
            color: root.colors.status.error
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
            visible: text.length>0
        }
        RowLayout {
            Layout.fillWidth: true
            StudioLabel { text: qsTr("设置仅在本次运行中生效"); color: root.colors.text.secondary; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            StudioButton {
                implicitWidth: 88
                objectName: "applyOutputSettingsButton"
                text: qsTr("确定")
                onClicked: {
                    if (root.outputSettings.apply(root.directory,root.recordingBitrate,root.server,root.key,root.streamingBitrate)) root.closeRequested()
                    else root.errorText=root.outputSettings.lastError
                }
            }
            StudioButton {
                implicitWidth: 88
                objectName: "cancelOutputSettingsButton"
                text: qsTr("取消")
                onClicked: root.closeRequested()
            }
        }
    }
}
