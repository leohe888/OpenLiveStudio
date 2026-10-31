import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"

Rectangle {
    id: root
    readonly property var displayController: displaySourceController
    objectName: "displayPropertiesDialog"
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
        target: videoSourceModel
        function onCaptureStatusChanged(id) {
            if (id === sourceId) root.status = videoSourceModel.displayProperties(id)
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        StudioLabel { text: qsTr("显示器") }
        DisplayCaptureSettings {
            id: settings
            objectName: "displaySettings"
            Layout.fillWidth: true
            displaySourceController: root.displayController
        }
        StudioLabel {
            objectName: "captureStateLabel"
            text: root.status.state === "capturing" ? qsTr("采集中")
                : root.status.state === "paused" ? qsTr("已暂停：来源当前隐藏")
                : root.status.state === "error" ? qsTr("采集失败") : qsTr("等待采集画面…")
            color: root.status.state === "error" ? root.colors.status.error : root.colors.text.secondary
        }
        StudioLabel {
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
            StudioButton {
                objectName: "applyDisplayPropertiesButton"
                text: qsTr("应用")
                enabled: settings.displayId.length > 0
                onClicked: displaySourceController.applyProperties(sourceId, settings.displayId, settings.captureCursor)
            }
            StudioButton {
                objectName: "retryDisplayButton"
                text: qsTr("重试")
                onClicked: displaySourceController.retry(sourceId)
            }
            StudioButton {
                objectName: "closeDisplayPropertiesButton"
                text: qsTr("关闭")
                onClicked: root.closeRequested()
            }
        }
    }
}
