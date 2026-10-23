import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "sourceAddDialogContent"
    implicitWidth: 520
    implicitHeight: 300
    color: "#1d1d1d"
    required property var state
    required property string sceneId
    property string initialName: qsTr("图像")
    property string filePath: ""
    property string errorText: ""
    signal closeRequested()

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
        color: "#d6d6d6"
        placeholderTextColor: "#8c8c8c"
        selectedTextColor: "#ffffff"
        selectionColor: "#31445b"
        selectByMouse: true
        onTextEdited: root.errorText = ""
        onAccepted: root.submit()
        background: Rectangle {
            color: "#232323"
            border.color: parent.activeFocus ? "#6b8db5" : "#3a3a3a"
            radius: 2
        }
    }
    component ActionButton: Button {
        implicitWidth: 76
        implicitHeight: 32
        palette.buttonText: enabled ? "#d6d6d6" : "#666666"
        Accessible.name: text
        background: Rectangle {
            color: parent.down ? "#383838" : (parent.hovered ? "#333333" : "#2b2b2b")
            border.color: parent.activeFocus ? "#6b8db5" : "#3a3a3a"
            radius: 2
        }
    }

    // Only the image form exists today. Other source types can supply their own form.
    Component {
        id: imageConfiguration
        RowLayout {
            spacing: 10
            Label { text: qsTr("图像文件"); color: "#d6d6d6"; Layout.preferredWidth: 64 }
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
                enabled: false
                ToolTip.text: qsTr("文件选择尚未接入，请手动输入路径。")
                ToolTip.visible: hovered
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
            Label { text: qsTr("来源类型"); color: "#d6d6d6"; Layout.preferredWidth: 64 }
            ComboBox {
                objectName: "sourceTypeInput"
                Layout.fillWidth: true
                implicitHeight: 32
                model: [qsTr("图像")]
                Accessible.name: qsTr("来源类型")
                palette.buttonText: "#d6d6d6"
                palette.text: "#d6d6d6"
                palette.base: "#232323"
                palette.window: "#232323"
                indicator: Canvas {
                    width: 10
                    height: 6
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.fillStyle = "#d6d6d6"
                        ctx.beginPath()
                        ctx.moveTo(0, 0)
                        ctx.lineTo(width, 0)
                        ctx.lineTo(width / 2, height)
                        ctx.closePath()
                        ctx.fill()
                    }
                }
                background: Rectangle { color: "#232323"; border.color: "#3a3a3a"; radius: 2 }
            }
            Label { text: qsTr("名称"); color: "#d6d6d6" }
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
            color: "#ee9b9b"
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
