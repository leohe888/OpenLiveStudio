import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    readonly property var themeColors: uiTheme.colors
    objectName: "sourceAddDialogContent"
    implicitWidth: 520
    implicitHeight: 300
    color: root.themeColors.background.panel
    required property var sceneManager
    required property string sceneId
    property string initialName: qsTr("图像")
    property string filePath: ""
    property string errorText: ""
    property bool nameEdited: false
    readonly property bool imageSelected: sourceType.currentValue === "image"
    readonly property bool cameraSelected: sourceType.currentValue === "camera"
    readonly property var cameraDevices: sceneManager ? sceneManager.cameraDevices : []
    property string cameraDeviceId: ""
    property bool cameraSelectionLost: false
    readonly property bool cameraDeviceAvailable: cameraDevices.some(device => device.id === cameraDeviceId)
    onCameraDevicesChanged: syncCameraSelection()
    Component.onCompleted: syncCameraSelection()
    signal closeRequested()
    signal browseImageFileRequested()

    function syncCameraSelection() {
        if (cameraDeviceId.length > 0) {
            if (cameraDevices.some(device => device.id === cameraDeviceId))
                return
            cameraDeviceId = ""
            cameraSelectionLost = true
            if (cameraSelected)
                errorText = qsTr("所选摄像头已断开，请重新选择。")
        }
        if (!cameraSelectionLost && cameraDevices.length > 0) {
            const preferred = cameraDevices.find(device => device.isDefault) || cameraDevices[0]
            cameraDeviceId = preferred.id
        }
    }

    function submit() {
        if (!imageSelected && !cameraSelected) {
            errorText = qsTr("该类型尚未接入添加功能。")
            return
        }
        if (!sceneManager) {
            errorText = qsTr("来源状态不可用。")
            return
        }
        if (cameraSelected) {
            errorText = sceneManager.cameraSourceError(sceneId, nameInput.text, cameraDeviceId)
            if (errorText.length > 0)
                return
            if (sceneManager.addCameraSource(sceneId, nameInput.text, cameraDeviceId).length > 0)
                closeRequested()
            else
                errorText = qsTr("无法添加摄像头来源，请重试。")
            return
        }
        errorText = sceneManager.imageSourceError(sceneId, nameInput.text, filePath)
        if (errorText.length > 0)
            return
        if (sceneManager.addImageSource(sceneId, nameInput.text, filePath).length > 0)
            closeRequested()
        else
            errorText = qsTr("无法添加图像来源，请重试。")
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

    component FormComboBox: ComboBox {
        id: control
        implicitHeight: 32
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
            readonly property color arrowColor: control.enabled
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
            color: !control.enabled ? root.themeColors.button.background.disabled
                : (control.down ? root.themeColors.button.background.pressed
                    : (control.hovered ? root.themeColors.background.hovered : root.themeColors.background.input))
            border.color: control.enabled && control.activeFocus
                ? root.themeColors.border.focus : root.themeColors.border.default
            radius: 2
        }
    }

    // Each supported source type supplies its own configuration form.
    Component {
        id: imageConfiguration
        RowLayout {
            spacing: 10
            Label { text: qsTr("图像文件"); color: root.themeColors.text.primary; Layout.preferredWidth: 64 }
            FormInput {
                objectName: "sourceFileInput"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.filePath
                placeholderText: qsTr("输入本地图像文件路径")
                Accessible.name: qsTr("图像文件")
                onTextEdited: {
                    root.filePath = text
                    root.errorText = ""
                }
            }
            ActionButton {
                objectName: "browseSourceFileButton"
                text: qsTr("浏览")
                onClicked: root.browseImageFileRequested()
            }
        }
    }

    Component {
        id: cameraConfiguration
        RowLayout {
            spacing: 10
            Label { text: qsTr("摄像头"); color: root.themeColors.text.primary; Layout.preferredWidth: 64 }
            FormComboBox {
                objectName: "cameraDeviceInput"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                model: root.cameraDevices
                textRole: "description"
                valueRole: "id"
                enabled: root.cameraDevices.length > 0
                Accessible.name: qsTr("摄像头设备")
                currentIndex: root.cameraDevices.findIndex(device => device.id === root.cameraDeviceId)
                onActivated: {
                    root.cameraDeviceId = currentValue
                    root.cameraSelectionLost = false
                    root.errorText = ""
                }
            }
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
            FormComboBox {
                id: sourceType
                objectName: "sourceTypeInput"
                Layout.fillWidth: true
                implicitHeight: 32
                textRole: "name"
                valueRole: "type"
                model: [
                    { name: qsTr("图像"), type: "image" },
                    { name: qsTr("显示器采集"), type: "displayCapture" },
                    { name: qsTr("窗口采集"), type: "windowCapture" },
                    { name: qsTr("摄像头"), type: "camera" },
                    { name: qsTr("文字"), type: "text" }
                ]
                onActivated: {
                    root.errorText = ""
                    if (!root.nameEdited && root.sceneManager)
                        nameInput.text = root.sceneManager.suggestedSourceName(root.sceneId, currentText)
                }
                Accessible.name: qsTr("来源类型")

            }
            Label { text: qsTr("名称"); color: root.themeColors.text.primary }
            FormInput {
                id: nameInput
                objectName: "sourceNameInput"
                Layout.fillWidth: true
                text: root.initialName
                Accessible.name: qsTr("来源名称")
                onTextEdited: root.nameEdited = true
            }
        }
        Loader {
            objectName: "sourceConfigurationLoader"
            Layout.fillWidth: true
            active: root.imageSelected || root.cameraSelected
            visible: active
            sourceComponent: root.cameraSelected ? cameraConfiguration : imageConfiguration
            onLoaded: {
                nameInput.forceActiveFocus()
                nameInput.selectAll()
            }
        }
        Label {
            objectName: "sourceTypeUnavailableHint"
            Layout.fillWidth: true
            visible: !root.imageSelected && !root.cameraSelected
            text: qsTr("该类型尚未接入添加功能。")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.themeColors.text.secondary
        }
        Label {
            objectName: "cameraConfigurationHint"
            Layout.fillWidth: true
            visible: root.cameraSelected
            text: root.cameraDevices.length === 0 ? qsTr("未检测到摄像头。")
                : (!root.cameraDeviceAvailable ? qsTr("请选择摄像头。")
                    : qsTr("仅保存来源配置，暂不启动采集。"))
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
                text: qsTr("确定")
                enabled: root.imageSelected || (root.cameraSelected && root.cameraDeviceAvailable)
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
