import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

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
    readonly property var colors: uiTheme.colors
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
    component SettingField: TextField {
        Layout.fillWidth: true
        implicitHeight: 34
        selectByMouse: true
        maximumLength: 4096
        color: enabled ? root.colors.text.primary : root.colors.text.disabled
        selectionColor: root.colors.background.selected
        selectedTextColor: root.colors.text.primary
        placeholderTextColor: root.colors.text.secondary
        background: Rectangle {
            color: root.colors.background.input
            border.color: parent.activeFocus ? root.colors.border.focus : root.colors.border.default
            radius: 2
        }
    }
    component ActionButton: Button {
        implicitWidth: 88
        implicitHeight: 32
        palette.buttonText: enabled ? root.colors.text.primary : root.colors.text.disabled
        background: Rectangle {
            color: !parent.enabled ? root.colors.button.background.disabled : parent.down ? root.colors.button.background.pressed
                : parent.hovered ? root.colors.button.background.hovered : root.colors.button.background.normal
            border.color: parent.activeFocus ? root.colors.border.focus : root.colors.border.default
            radius: 2
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        TabBar {
            id: tabs
            objectName: "outputSettingsTabs"
            Layout.fillWidth: true
            currentIndex: 1
            Repeater {
                model: [qsTr("直播推流"),qsTr("录屏")]
                delegate: TabButton {
                    required property string modelData
                    text: modelData
                    contentItem: Text {
                        text: parent.text
                        color: root.colors.text.primary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.checked ? root.colors.background.selected : root.colors.background.header
                        border.color: parent.activeFocus ? root.colors.border.focus : root.colors.border.default
                        radius: 2
                    }
                }
            }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex
            ColumnLayout {
                spacing: 8
                Label { text: qsTr("服务器地址"); color: root.colors.text.primary }
                SettingField {
                    objectName: "streamServerInput"
                    enabled: !root.outputSettings.streamingLocked
                    text: root.server
                    placeholderText: "rtmp://server/app"
                    onTextEdited: root.server=text
                }
                Label { text: qsTr("串流密钥"); color: root.colors.text.primary }
                SettingField {
                    objectName: "streamKeyInput"
                    enabled: !root.outputSettings.streamingLocked
                    text: root.key
                    echoMode: TextInput.Password
                    onTextEdited: root.key=text
                }
                Label { text: qsTr("视频码率（kbps，500–50000）"); color: root.colors.text.primary }
                SettingField {
                    objectName: "streamingBitrateInput"
                    enabled: !root.outputSettings.streamingLocked
                    text: String(root.streamingBitrate)
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 500; top: 50000 }
                    onTextEdited: root.streamingBitrate=Number(text)
                }
                Label {
                    text: qsTr("RTMP · H.264 / AAC · 1080p · 30 FPS；连接和重连期间设置锁定。")
                    color: root.colors.text.secondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Item { Layout.fillHeight: true }
            }
            ColumnLayout {
                spacing: 8
                Label { text: qsTr("保存目录"); color: root.colors.text.primary }
                RowLayout {
                    Layout.fillWidth: true
                    SettingField {
                        objectName: "recordingDirectoryInput"
                        text: root.directory
                        enabled: !root.outputSettings.recordingLocked
                        onTextEdited: root.directory=text
                    }
                    ActionButton {
                        objectName: "browseRecordingDirectoryButton"
                        text: qsTr("浏览…")
                        enabled: !root.outputSettings.recordingLocked
                        onClicked: root.browseDirectoryRequested()
                    }
                }
                Label { text: qsTr("视频码率（kbps，500–50000）"); color: root.colors.text.primary }
                SettingField {
                    objectName: "recordingBitrateInput"
                    enabled: !root.outputSettings.recordingLocked
                    text: String(root.recordingBitrate)
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 500; top: 50000 }
                    onTextEdited: root.recordingBitrate=Number(text)
                }
                Label { text: qsTr("MP4 · 1080p · 30 FPS"); color: root.colors.text.secondary }
                Label {
                    text: qsTr("录制期间，保存目录和码率保持不变。")
                    color: root.colors.text.secondary
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Item { Layout.fillHeight: true }
            }
        }
        Label {
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
            Label { text: qsTr("设置仅在本次运行中生效"); color: root.colors.text.secondary; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            ActionButton {
                objectName: "applyOutputSettingsButton"
                text: qsTr("确定")
                onClicked: {
                    if (root.outputSettings.apply(root.directory,root.recordingBitrate,root.server,root.key,root.streamingBitrate)) root.closeRequested()
                    else root.errorText=root.outputSettings.lastError
                }
            }
            ActionButton {
                objectName: "cancelOutputSettingsButton"
                text: qsTr("取消")
                onClicked: root.closeRequested()
            }
        }
    }
}
