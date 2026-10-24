import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    readonly property var themeColors: uiTheme.colors
    objectName: "sourceRemovalDialogContent"
    implicitWidth: 460
    implicitHeight: 220
    color: root.themeColors.background.panel
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
            objectName: "sourceRemovalMessage"
            Layout.fillWidth: true
            text: qsTr("确定移除来源“%1”吗？").arg(root.sourceName)
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: root.themeColors.text.primary
        }
        Label {
            Layout.fillWidth: true
            objectName: "sourceRemovalHint"
            text: qsTr("仅从此场景移除，不会删除磁盘上的文件。")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.themeColors.text.secondary
        }
        Label {
            objectName: "sourceRemovalError"
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
