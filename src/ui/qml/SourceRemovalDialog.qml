import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"

Rectangle {
    id: root
    readonly property var themeColors: uiColors
    objectName: "sourceRemovalDialogContent"
    implicitWidth: 460
    implicitHeight: 220
    color: root.themeColors.background.panel
    required property var videoSourceModel
    required property string sourceId
    required property string sourceName
    property string errorText: ""
    property string removalHint: qsTr("仅从来源列表移除，不会删除磁盘上的文件。")
    signal closeRequested()

    function removeSource() {
        if (videoSourceModel && videoSourceModel.removeSource(sourceId))
            closeRequested()
        else
            errorText = qsTr("无法移除来源：目标来源可能已不存在。")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10
        StudioLabel {
            objectName: "sourceRemovalMessage"
            Layout.fillWidth: true
            text: qsTr("确定移除来源“%1”吗？").arg(root.sourceName)
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
        }
        StudioLabel {
            Layout.fillWidth: true
            objectName: "sourceRemovalHint"
            text: root.removalHint
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.themeColors.text.secondary
        }
        StudioLabel {
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
            StudioButton {
                implicitWidth: 88
                objectName: "confirmSourceRemovalButton"
                text: qsTr("移除")
                onClicked: root.removeSource()
            }
            StudioButton {
                implicitWidth: 88
                objectName: "cancelSourceRemovalButton"
                text: qsTr("取消")
                Component.onCompleted: forceActiveFocus()
                onClicked: root.closeRequested()
            }
        }
    }
}
