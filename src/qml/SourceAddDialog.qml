import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    readonly property var themeColors: uiTheme.colors
    objectName: "sourceAddDialogContent"
    implicitWidth: 520
    implicitHeight: supportsDisplay || supportsCamera ? 430 : 300
    color: root.themeColors.background.panel
    required property var sourceListModel
    property var imageSourceController: null
    property var displayCaptureController: null
    property var cameraCaptureController: null
    readonly property bool isCamera: sourceType.currentValue === "camera"
    readonly property bool supportsCamera: isCamera && cameraCaptureController !== null
    property bool loading: false
    readonly property bool isImage: sourceType.currentValue === "image"
    readonly property bool isDisplay: sourceType.currentValue === "displayCapture"
    readonly property bool supportsDisplay: isDisplay && displayCaptureController !== null
    property string initialName: qsTr("显示器采集")
    property string errorText: ""
    property bool nameEdited: false
    signal closeRequested()

    function submit() {
        if (supportsCamera) {
            const id = cameraCaptureController.addCamera(nameInput.text, cameraSettings.deviceId, cameraSettings.selectedMode)
            if (id.length) closeRequested()
            else errorText = qsTr("请输入名称并选择设备；同一摄像头不能重复添加。")
            return
        }
        if (supportsDisplay) {
            const id = displayCaptureController.addDisplay(nameInput.text, displaySettings.displayId, displaySettings.captureCursor)
            if (id.length) closeRequested()
            else errorText = qsTr("请输入来源名称并选择显示器。")
            return
        }
        if (!isImage) {
            errorText = qsTr("该类型尚未接入添加功能。")
            return
        }
        if (loading || !imageSourceController) return
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

    component FormInput: TextField {
        implicitHeight: 32
        color: root.themeColors.text.primary
        placeholderTextColor: root.themeColors.text.secondary
        selectedTextColor: root.themeColors.text.primary
        selectionColor: root.themeColors.background.selected
        selectByMouse: true
        onTextEdited: root.errorText = ""
        onAccepted: root.submit()
        background: Rectangle {
            color: root.themeColors.background.input
            border.color: parent.enabled && parent.activeFocus ? root.themeColors.border.focus : root.themeColors.border.default
            radius: 2
        }
    }
    component ActionButton: Button {
        implicitWidth: 76
        implicitHeight: 32
        hoverEnabled: true
        palette.buttonText: enabled ? root.themeColors.text.primary : root.themeColors.text.disabled
        Accessible.name: text
        background: Rectangle {
            color: !parent.enabled ? root.themeColors.button.background.disabled
                : (parent.down ? root.themeColors.button.background.pressed
                    : (parent.hovered ? root.themeColors.button.background.hovered : root.themeColors.button.background.normal))
            border.color: parent.enabled && parent.activeFocus ? root.themeColors.border.focus : root.themeColors.border.default
            radius: 2
        }
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
            Label { text: qsTr("来源类型"); color: root.themeColors.text.primary; Layout.preferredWidth: 64 }
            ComboBox {
                id: sourceType
                objectName: "sourceTypeInput"
                Layout.fillWidth: true
                enabled: !root.loading
                implicitHeight: 32
                textRole: "name"
                valueRole: "type"
                model: [
                    { name: qsTr("显示器采集"), type: "displayCapture" },
                    { name: qsTr("窗口采集"), type: "windowCapture" },
                    { name: qsTr("摄像头"), type: "camera" },
                    { name: qsTr("图片"), type: "image" },
                    { name: qsTr("文字"), type: "text" }
                ]
                onActivated: {
                    root.errorText = ""
                    if (!root.nameEdited && root.sourceListModel)
                        nameInput.text = root.sourceListModel.suggestedSourceName(currentText)
                }
                Accessible.name: qsTr("来源类型")
                hoverEnabled: true
                palette.buttonText: enabled ? root.themeColors.text.primary : root.themeColors.text.disabled
                palette.text: enabled ? root.themeColors.text.primary : root.themeColors.text.disabled
                palette.base: root.themeColors.background.input
                palette.window: root.themeColors.background.input
                palette.highlight: root.themeColors.background.selected
                palette.highlightedText: root.themeColors.text.primary
                palette.light: root.themeColors.background.selected
                palette.midlight: root.themeColors.background.hovered
                palette.mid: root.themeColors.border.default
                palette.dark: root.themeColors.border.focus
                indicator: Canvas {
                    readonly property color arrowColor: sourceType.enabled
                        ? root.themeColors.icon.primary : root.themeColors.icon.disabled
                    width: 10
                    height: 6
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    onArrowColorChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.fillStyle = arrowColor
                        ctx.beginPath()
                        ctx.moveTo(0, 0)
                        ctx.lineTo(width, 0)
                        ctx.lineTo(width / 2, height)
                        ctx.closePath()
                        ctx.fill()
                    }
                }
                background: Rectangle {
                    color: !sourceType.enabled ? root.themeColors.button.background.disabled
                        : (sourceType.down ? root.themeColors.button.background.pressed
                            : (sourceType.hovered ? root.themeColors.background.hovered : root.themeColors.background.input))
                    border.color: sourceType.enabled && sourceType.activeFocus
                        ? root.themeColors.border.focus : root.themeColors.border.default
                    radius: 2
                }
            }
            Label { text: qsTr("名称"); color: root.themeColors.text.primary }
            FormInput {
                id: nameInput
                objectName: "sourceNameInput"
                Layout.fillWidth: true
                enabled: !root.loading
                text: root.initialName
                Accessible.name: qsTr("来源名称")
                onTextEdited: root.nameEdited = true
            }
            Label {
                visible: root.isImage
                text: qsTr("图片文件")
                color: root.themeColors.text.primary
            }
            RowLayout {
                visible: root.isImage
                Layout.fillWidth: true
                FormInput {
                    id: fileInput
                    objectName: "sourceFileInput"
                    Layout.fillWidth: true
                    enabled: !root.loading
                    placeholderText: qsTr("本地图片路径")
                    Accessible.name: qsTr("图片文件路径")
                }
                ActionButton {
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
            cameraCaptureController: root.cameraCaptureController
        }
        DisplayCaptureSettings {
            id: displaySettings
            objectName: "displaySettings"
            Layout.fillWidth: true
            visible: root.supportsDisplay
            displayCaptureController: root.displayCaptureController
        }
        Label {
            objectName: "sourceTypeUnavailableHint"
            visible: !root.isImage && !root.supportsDisplay && !root.supportsCamera
            Layout.fillWidth: true
            text: qsTr("该类型尚未接入添加功能。")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.themeColors.text.secondary
        }
        Label {
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
            ActionButton {
                objectName: "confirmSourceAddButton"
                text: root.loading ? qsTr("加载中…") : qsTr("确定")
                enabled: !root.loading && ((root.isImage && root.imageSourceController !== null)
                    || (root.supportsDisplay && displaySettings.displayId.length > 0)
                    || (root.supportsCamera && cameraSettings.deviceId.length > 0 && !cameraSettings.loadingModes))
                onClicked: root.submit()
            }
            ActionButton {
                objectName: "cancelSourceAddButton"
                text: qsTr("取消")
                onClicked: root.closeRequested()
            }
        }
    }
}
