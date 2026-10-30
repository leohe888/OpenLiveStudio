import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "displayPropertiesDialog"
    required property var videoSourceModel
    required property var displaySourceController
    required property string sourceId
    property var status: ({})
    readonly property var colors: uiColors
    implicitWidth: 560
    implicitHeight: 330
    color: colors.background.panel
    signal closeRequested()
    Component.onCompleted: {
        status = videoSourceModel.displayProperties(sourceId)
        settings.displayId = status.displayId || ""
        settings.captureCursor = status.captureCursor === true
    }
    Connections {
        target: root.videoSourceModel
        function onCaptureStatusChanged(id) {
            if (id === root.sourceId) root.status = root.videoSourceModel.displayProperties(id)
        }
    }
    component ActionButton: Button {
        palette.button: root.colors.button.background.normal
        palette.buttonText: enabled ? root.colors.text.primary : root.colors.text.disabled
        palette.mid: root.colors.border.default
        palette.light: root.colors.background.hovered
        palette.dark: root.colors.border.focus
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        Label { text: qsTr("显示器"); color: root.colors.text.primary }
        DisplayCaptureSettings {
            id: settings
            objectName: "displaySettings"
            Layout.fillWidth: true
            displaySourceController: root.displaySourceController
        }
        Label {
            objectName: "captureStateLabel"
            text: root.status.state === "capturing" ? qsTr("采集中")
                : root.status.state === "paused" ? qsTr("已暂停：来源当前隐藏")
                : root.status.state === "error" ? qsTr("采集失败") : qsTr("等待采集画面…")
            color: root.status.state === "error" ? root.colors.status.error : root.colors.text.secondary
        }
        Label {
            objectName: "captureErrorLabel"
            Layout.fillWidth: true
            text: root.status.error || ""
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: root.colors.status.error
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            ActionButton {
                objectName: "applyDisplayPropertiesButton"
                text: qsTr("应用")
                enabled: settings.displayId.length > 0
                onClicked: root.displaySourceController.applyProperties(root.sourceId, settings.displayId, settings.captureCursor)
            }
            ActionButton {
                objectName: "retryDisplayButton"
                text: qsTr("重试")
                onClicked: root.displaySourceController.retry(root.sourceId)
            }
            ActionButton {
                objectName: "closeDisplayPropertiesButton"
                text: qsTr("关闭")
                onClicked: root.closeRequested()
            }
        }
    }
}
