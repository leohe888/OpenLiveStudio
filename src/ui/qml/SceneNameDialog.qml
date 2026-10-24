import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    readonly property var themeColors: uiTheme.colors
    objectName: "sceneNameDialogContent"
    implicitWidth: 420
    implicitHeight: 200
    color: root.themeColors.background.panel
    required property var state
    required property string initialName
    property string sceneId: ""
    property string errorText: ""
    signal closeRequested()

    function submit() {
        if (!state) {
            errorText = qsTr("场景状态不可用。")
            return
        }
        errorText = state.sceneNameError(nameInput.text, sceneId)
        if (errorText.length > 0)
            return
        const succeeded = sceneId.length > 0
            ? state.renameScene(sceneId, nameInput.text)
            : state.addScene(nameInput.text).length > 0
        if (succeeded)
            closeRequested()
        else
            errorText = qsTr("无法保存场景名称，请重试。")
    }

    component ActionButton: Button {
        implicitWidth: 88
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
        spacing: 10
        Label {
            text: qsTr("场景名称")
            color: root.themeColors.text.primary
        }
        TextField {
            id: nameInput
            objectName: "sceneNameInput"
            Layout.fillWidth: true
            text: root.initialName
            color: root.themeColors.text.primary
            selectedTextColor: root.themeColors.text.primary
            selectionColor: root.themeColors.background.selected
            Accessible.name: qsTr("场景名称")
            selectByMouse: true
            onTextEdited: root.errorText = ""
            onAccepted: root.submit()
            Component.onCompleted: {
                forceActiveFocus()
                selectAll()
            }
            background: Rectangle {
                color: root.themeColors.background.input
                border.color: nameInput.activeFocus ? root.themeColors.border.focus : root.themeColors.border.default
                radius: 2
            }
        }
        Label {
            objectName: "sceneNameError"
            Layout.fillWidth: true
            text: root.errorText
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.themeColors.status.error
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            ActionButton {
                objectName: "confirmSceneNameButton"
                text: qsTr("确定")
                onClicked: root.submit()
            }
            ActionButton {
                objectName: "cancelSceneNameButton"
                text: qsTr("取消")
                onClicked: root.closeRequested()
            }
        }
    }
}
