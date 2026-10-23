import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "aboutDialogContent"
    implicitWidth: 420
    implicitHeight: 240
    color: "#1d1d1d"
    required property string productName
    required property string versionText
    required property string qtVersion
    signal closeRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        Label {
            objectName: "aboutProductName"
            Layout.fillWidth: true
            text: root.productName
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.pixelSize: 22
            color: "#d6d6d6"
        }
        Label {
            objectName: "aboutVersion"
            Layout.fillWidth: true
            text: qsTr("版本：%1").arg(root.versionText)
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: "#b9b9b9"
        }
        Label {
            objectName: "aboutQtVersion"
            Layout.fillWidth: true
            text: "Qt " + root.qtVersion
            color: "#b9b9b9"
        }
        Item { Layout.fillHeight: true }
        Button {
            objectName: "closeAboutButton"
            Layout.alignment: Qt.AlignRight
            implicitWidth: 88
            implicitHeight: 32
            text: qsTr("关闭")
            palette.buttonText: "#d6d6d6"
            Accessible.name: text
            onClicked: root.closeRequested()
            background: Rectangle {
                color: parent.down ? "#383838" : (parent.hovered ? "#333333" : "#2b2b2b")
                border.color: parent.activeFocus ? "#6b8db5" : "#3a3a3a"
                radius: 2
            }
        }
    }
}
