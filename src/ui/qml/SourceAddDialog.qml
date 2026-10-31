import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"

Rectangle {
    id: root
    readonly property var themeColors: uiColors
    objectName: "sourceAddDialogContent"
    implicitWidth: 520
    implicitHeight: supportsDisplay || supportsCamera ? 430 : 300
    color: root.themeColors.background.panel
    required property var videoSourceModel
    property var imageSourceController: null
    property var displaySourceController: null
    property var cameraSourceController: null
    readonly property bool isCamera: sourceType.currentValue === "camera"
    readonly property bool supportsCamera: isCamera && cameraSourceController !== null
    property bool loading: false
    readonly property bool isImage: sourceType.currentValue === "image"
    readonly property bool isDisplay: sourceType.currentValue === "displayCapture"
    readonly property bool supportsDisplay: isDisplay && displaySourceController !== null
    property string initialName: qsTr("显示器采集")
    property string errorText: ""
    property bool nameEdited: false
    signal closeRequested()

    function submit() {
        if (supportsCamera) {
            const id = cameraSourceController.addCamera(nameInput.text, cameraSettings.deviceId, cameraSettings.selectedMode)
            if (id.length) closeRequested()
            else errorText = qsTr("请输入名称并选择设备；同一摄像头不能重复添加。")
            return
        }
        if (supportsDisplay) {
            const id = displaySourceController.addDisplay(nameInput.text, displaySettings.displayId, displaySettings.captureCursor)
            if (id.length) closeRequested()
            else errorText = qsTr("请输入来源名称并选择显示器。")
            return
        }
        if (!isImage || loading || !imageSourceController) return
        errorText = ""
        loading = true
        imageSourceController.addImage(nameInput.text, fileInput.text, root)
    }

    Connections {
        target: root.imageSourceController
        function onImageAdded(requester) {
            if (requester !== root) return
            root.loading = false
            root.closeRequested()
        }
        function onAddFailed(requester, message) {
            if (requester !== root) return
            root.loading = false
            root.errorText = message
        }
    }

    Component.onCompleted: {
        nameInput.forceActiveFocus()
        nameInput.selectAll()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        GridLayout {
            columns: 2
            columnSpacing: 10
            rowSpacing: 12
            Layout.fillWidth: true
            StudioLabel { text: qsTr("来源类型"); Layout.preferredWidth: 64 }
            StudioComboBox {
                id: sourceType
                objectName: "sourceTypeInput"
                Layout.fillWidth: true
                enabled: !root.loading
                textRole: "name"
                valueRole: "type"
                model: [
                    { name: qsTr("显示器采集"), type: "displayCapture" },
                    { name: qsTr("摄像头"), type: "camera" },
                    { name: qsTr("图片"), type: "image" }
                ]
                onActivated: {
                    root.errorText = ""
                    if (!root.nameEdited && root.videoSourceModel)
                        nameInput.text = root.videoSourceModel.suggestedSourceName(currentText)
                }
                Accessible.name: qsTr("来源类型")
            }
            StudioLabel { text: qsTr("名称") }
            StudioTextField {
                onAccepted: root.submit()
                id: nameInput
                objectName: "sourceNameInput"
                Layout.fillWidth: true
                enabled: !root.loading
                text: root.initialName
                Accessible.name: qsTr("来源名称")
                onTextEdited: { root.errorText = ""; root.nameEdited = true }
            }
            StudioLabel {
                visible: root.isImage
                text: qsTr("图片文件")
            }
            RowLayout {
                visible: root.isImage
                Layout.fillWidth: true
                StudioTextField {
                    onAccepted: root.submit()
                    onTextEdited: root.errorText = ""
                    id: fileInput
                    objectName: "sourceFileInput"
                    Layout.fillWidth: true
                    enabled: !root.loading
                    placeholderText: qsTr("本地图片路径")
                    Accessible.name: qsTr("图片文件路径")
                }
                StudioButton {
                    objectName: "browseSourceImageButton"
                    text: qsTr("浏览…")
                    enabled: !root.loading && root.imageSourceController !== null
                    onClicked: {
                        const path = root.imageSourceController.browseImage(fileInput.text)
                        if (path.length > 0) {
                            fileInput.text = path
                            root.errorText = ""
                        }
                    }
                }
            }
        }
        CameraCaptureSettings {
            id: cameraSettings
            objectName: "cameraSettings"
            Layout.fillWidth: true
            visible: root.supportsCamera
            cameraSourceController: root.cameraSourceController
        }
        DisplayCaptureSettings {
            id: displaySettings
            objectName: "displaySettings"
            Layout.fillWidth: true
            visible: root.supportsDisplay
            displaySourceController: root.displaySourceController
        }
        StudioLabel {
            objectName: "sourceAddError"
            Layout.fillWidth: true
            text: root.errorText
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: root.themeColors.status.error
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            StudioButton {
                objectName: "confirmSourceAddButton"
                text: root.loading ? qsTr("加载中…") : qsTr("确定")
                enabled: !root.loading && ((root.isImage && root.imageSourceController !== null)
                    || (root.supportsDisplay && displaySettings.displayId.length > 0)
                    || (root.supportsCamera && cameraSettings.deviceId.length > 0 && !cameraSettings.loadingModes))
                onClicked: root.submit()
            }
            StudioButton {
                objectName: "cancelSourceAddButton"
                text: qsTr("取消")
                onClicked: root.closeRequested()
            }
        }
    }
}
