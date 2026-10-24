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
    required property var state
    required property string sceneId
    property string initialName: qsTr("图像")
    property string filePath: ""
    property string errorText: ""
    signal closeRequested()
    signal browseImageFileRequested()

    function submit() {
        if (!state) {
            errorText = qsTr("来源状态不可用。")
            return
        }
        errorText = state.imageSourceError(sceneId, nameInput.text, filePath)
        if (errorText.length > 0)
            return
        if (state.addImageSource(sceneId, nameInput.text, filePath).length > 0)
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

    // Only the image form exists today. Other source types can supply their own form.
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
                implicitHeight: 32
                model: [qsTr("图像")]
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
                text: root.initialName
                Accessible.name: qsTr("来源名称")
            }
        }
        Loader {
            objectName: "sourceConfigurationLoader"
            Layout.fillWidth: true
            sourceComponent: imageConfiguration
            onLoaded: {
                nameInput.forceActiveFocus()
                nameInput.selectAll()
            }
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
