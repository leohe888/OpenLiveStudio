import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "sceneNameDialogContent"
    implicitWidth: 420
    implicitHeight: 200
    color: "#1d1d1d"
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
        palette.buttonText: "#d6d6d6"
        Accessible.name: text
        background: Rectangle {
            color: parent.down ? "#383838" : (parent.hovered ? "#333333" : "#2b2b2b")
            border.color: parent.activeFocus ? "#6b8db5" : "#3a3a3a"
            radius: 2
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10
        Label {
            text: qsTr("场景名称")
            color: "#d6d6d6"
        }
        TextField {
            id: nameInput
            objectName: "sceneNameInput"
            Layout.fillWidth: true
            text: root.initialName
            color: "#d6d6d6"
            selectedTextColor: "#ffffff"
            selectionColor: "#31445b"
            Accessible.name: qsTr("场景名称")
            selectByMouse: true
            onTextEdited: root.errorText = ""
            onAccepted: root.submit()
            Component.onCompleted: {
                forceActiveFocus()
                selectAll()
            }
            background: Rectangle {
                color: "#232323"
                border.color: nameInput.activeFocus ? "#6b8db5" : "#3a3a3a"
                radius: 2
            }
        }
        Label {
            objectName: "sceneNameError"
            Layout.fillWidth: true
            text: root.errorText
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: "#ee9b9b"
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
