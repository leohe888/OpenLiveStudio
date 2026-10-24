import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "sceneRemovalDialogContent"
    implicitWidth: 420
    implicitHeight: 200
    color: "#1d1d1d"
    required property var state
    required property string sceneId
    required property string sceneName
    property string errorText: ""
    signal closeRequested()

    function removeScene() {
        if (state && state.removeScene(sceneId))
            closeRequested()
        else
            errorText = qsTr("无法删除场景：场景可能已不存在，或只剩最后一个场景。")
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
            objectName: "sceneRemovalMessage"
            Layout.fillWidth: true
            text: qsTr("确定删除场景“%1”吗？").arg(root.sceneName)
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: "#d6d6d6"
        }
        Label {
            objectName: "sceneRemovalError"
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
                objectName: "confirmSceneRemovalButton"
                text: qsTr("删除")
                onClicked: root.removeScene()
            }
            ActionButton {
                objectName: "cancelSceneRemovalButton"
                text: qsTr("取消")
                Component.onCompleted: forceActiveFocus()
                onClicked: root.closeRequested()
            }
        }
    }
}
