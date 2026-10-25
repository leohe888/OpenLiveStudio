import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    readonly property var themeColors: uiTheme.colors
    objectName: "aboutDialogContent"
    implicitWidth: 420
    implicitHeight: 240
    color: root.themeColors.background.panel
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
            color: root.themeColors.text.primary
        }
        Label {
            objectName: "aboutVersion"
            Layout.fillWidth: true
            text: qsTr("版本：%1").arg(root.versionText)
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.themeColors.text.secondary
        }
        Label {
            objectName: "aboutQtVersion"
            Layout.fillWidth: true
            text: "Qt " + root.qtVersion
            color: root.themeColors.text.secondary
        }
        Item { Layout.fillHeight: true }
        Button {
            objectName: "closeAboutButton"
            Layout.alignment: Qt.AlignRight
            implicitWidth: 88
            implicitHeight: 32
            text: qsTr("关闭")
            hoverEnabled: true
            palette.buttonText: enabled ? root.themeColors.text.primary : root.themeColors.text.disabled
            Accessible.name: text
            onClicked: root.closeRequested()
            background: Rectangle {
                color: !parent.enabled ? root.themeColors.button.background.disabled
                    : (parent.down ? root.themeColors.button.background.pressed
                        : (parent.hovered ? root.themeColors.button.background.hovered : root.themeColors.button.background.normal))
                border.color: parent.enabled && parent.activeFocus ? root.themeColors.border.focus : root.themeColors.border.default
                radius: 2
            }
        }
    }
}
