import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    readonly property var themeColors: uiTheme.colors
    objectName: "sceneRemovalDialogContent"
    implicitWidth: 420
    implicitHeight: 200
    color: root.themeColors.background.panel
    required property var sceneManager
    required property string sceneId
    required property string sceneName
    property string errorText: ""
    signal closeRequested()

    function removeScene() {
        if (sceneManager && sceneManager.removeScene(sceneId))
            closeRequested()
        else
            errorText = qsTr("无法删除场景：场景可能已不存在，或只剩最后一个场景。")
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
            objectName: "sceneRemovalMessage"
            Layout.fillWidth: true
            text: qsTr("确定删除场景“%1”吗？").arg(root.sceneName)
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: root.themeColors.text.primary
        }
        Label {
            objectName: "sceneRemovalError"
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
