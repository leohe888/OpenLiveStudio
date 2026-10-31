import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"

Rectangle {
    id: root
    objectName: "sourceRemovalDialogContent"
    implicitWidth: 460
    implicitHeight: 220
    color: uiColors.background.panel
    property string errorText: ""
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
            text: qsTr("确定移除来源“%1”吗？").arg(sourceName)
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
        }
        StudioLabel {
            Layout.fillWidth: true
            objectName: "sourceRemovalHint"
            text: removalHint
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: uiColors.text.secondary
        }
        StudioLabel {
            objectName: "sourceRemovalError"
            Layout.fillWidth: true
            text: root.errorText
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: uiColors.status.error
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
