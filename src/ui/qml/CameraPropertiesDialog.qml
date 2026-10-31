import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"

Rectangle {
    id: root
    objectName: "cameraPropertiesDialog"
    required property var videoSourceModel
    required property var cameraSourceController
    required property string sourceId
    property var status: ({})
    property string errorText: ""
    readonly property var colors: uiColors
    implicitWidth: 600
    implicitHeight: 440
    color: colors.background.panel
    signal closeRequested()
    Component.onCompleted: {
        status = videoSourceModel.cameraProperties(sourceId)
        settings.selectedMode = {width: status.width, height: status.height,
            numerator: status.numerator, denominator: status.denominator}
        settings.deviceId = status.deviceId || ""
    }
    Connections {
        target: root.videoSourceModel
        function onCaptureStatusChanged(id) {
            if (id === root.sourceId) root.status = root.videoSourceModel.cameraProperties(id)
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        CameraCaptureSettings {
            id: settings
            objectName: "cameraSettings"
            Layout.fillWidth: true
            cameraSourceController: root.cameraSourceController
        }
        StudioLabel {
            objectName: "cameraStateLabel"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.status.state === "capturing"
                ? qsTr("采集中 · %1 × %2 · 协商帧率 %3 FPS").arg(root.status.actualWidth).arg(root.status.actualHeight)
                    .arg((root.status.actualFrameRateNumerator / root.status.actualFrameRateDenominator).toFixed(2))
                : root.status.state === "paused" ? qsTr("已暂停：来源当前隐藏")
                : root.status.state === "error" ? qsTr("采集失败") : qsTr("等待采集画面…")
            color: root.colors.text.secondary
        }
        StudioLabel {
            objectName: "cameraErrorLabel"
            Layout.fillWidth: true
            text: root.errorText || root.status.error || ""
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: root.colors.status.error
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            StudioButton {
                objectName: "confirmCameraPropertiesButton"
                text: qsTr("确定")
                enabled: settings.deviceId.length > 0 && !settings.loadingModes
                onClicked: {
                    if (root.cameraSourceController.applyProperties(root.sourceId, settings.deviceId, settings.selectedMode))
                        root.closeRequested()
                    else root.errorText = qsTr("无法应用配置：所选摄像头已被其他来源使用，或采集模式无效。")
                }
            }
            StudioButton {
                objectName: "retryCameraButton"
                text: qsTr("重试")
                enabled: root.status.state === "error"
                onClicked: root.cameraSourceController.retry(root.sourceId)
            }
            StudioButton {
                objectName: "cancelCameraPropertiesButton"
                text: qsTr("取消")
                onClicked: root.closeRequested()
            }
        }
    }
}
