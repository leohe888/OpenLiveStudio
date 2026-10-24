import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "sourceRemovalDialogContent"
    implicitWidth: 460
    implicitHeight: 220
    color: "#1d1d1d"
    required property var state
    required property string sceneId
    required property string sourceId
    required property string sourceName
    property string errorText: ""
    signal closeRequested()

    function removeSource() {
        if (state && state.removeSource(sceneId, sourceId))
            closeRequested()
        else
            errorText = qsTr("无法移除来源：目标场景或来源可能已不存在。")
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
            objectName: "sourceRemovalMessage"
            Layout.fillWidth: true
            text: qsTr("确定移除来源“%1”吗？").arg(root.sourceName)
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: "#d6d6d6"
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("仅从此场景移除，不会删除磁盘上的文件。")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: "#8c8c8c"
        }
        Label {
            objectName: "sourceRemovalError"
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
                objectName: "confirmSourceRemovalButton"
                text: qsTr("移除")
                onClicked: root.removeSource()
            }
            ActionButton {
                objectName: "cancelSourceRemovalButton"
                text: qsTr("取消")
                Component.onCompleted: forceActiveFocus()
                onClicked: root.closeRequested()
            }
        }
    }
}
