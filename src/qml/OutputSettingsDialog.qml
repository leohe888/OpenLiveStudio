import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "outputSettingsDialog"
    required property var outputSettings
    property string directory: ""
    property int quality: 1
    property string server: ""
    property string key: ""
    property string errorText: ""
    readonly property var colors: uiTheme.colors
    implicitWidth: 620
    implicitHeight: 410
    color: colors.background.panel
    signal closeRequested()
    signal browseDirectoryRequested()
    Component.onCompleted: {
        directory=outputSettings.recordingDirectory
        quality=outputSettings.quality
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
                    text: root.server
                    placeholderText: "rtmp://server/app"
                    onTextEdited: root.server=text
                }
                Label { text: qsTr("串流密钥"); color: root.colors.text.primary }
                SettingField {
                    objectName: "streamKeyInput"
                    text: root.key
                    echoMode: TextInput.Password
                    onTextEdited: root.key=text
                }
                Label {
                    text: qsTr("直播输出暂不可用，配置可在本次运行中保留。")
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
                Label { text: qsTr("录制质量"); color: root.colors.text.primary }
                ComboBox {
                    objectName: "recordingQualityInput"
                    Layout.fillWidth: true
                    implicitHeight: 34
                    enabled: !root.outputSettings.recordingLocked
                    model: [qsTr("标准"),qsTr("高质量"),qsTr("更高质量")]
                    currentIndex: root.quality
                    onActivated: root.quality=currentIndex
                    palette.text: root.colors.text.primary
                    palette.buttonText: root.colors.text.primary
                    palette.base: root.colors.background.input
                    palette.window: root.colors.background.panel
                    palette.highlight: root.colors.background.selected
                    palette.highlightedText: root.colors.text.primary
                    contentItem: Text {
                        text: parent.displayText
                        leftPadding: 8; rightPadding: 28
                        color: parent.enabled ? root.colors.text.primary : root.colors.text.disabled
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: root.colors.background.input
                        border.color: parent.activeFocus ? root.colors.border.focus : root.colors.border.default
                        radius: 2
                    }
                }
                Label { text: qsTr("MP4 · 1080p · 30 FPS"); color: root.colors.text.secondary }
                Label {
                    text: qsTr("录制期间，保存目录和质量保持不变。")
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
                    if (root.outputSettings.apply(root.directory,root.quality,root.server,root.key)) root.closeRequested()
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
