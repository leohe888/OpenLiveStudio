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
    required property var sourceListModel
    property string initialName: qsTr("显示器采集")
    property string errorText: ""
    property bool nameEdited: false
    signal closeRequested()

    function submit() {
        errorText = qsTr("该类型尚未接入添加功能。")
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
                text: root.initialName
                Accessible.name: qsTr("来源名称")
                onTextEdited: root.nameEdited = true
            }
        }
        Label {
            objectName: "sourceTypeUnavailableHint"
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
                text: qsTr("确定")
                enabled: false
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
