import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    objectName: "transitionsPanel"
    implicitWidth: 238
    implicitHeight: 124
    color: "#1d1d1d"

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        spacing: 8

        ComboBox {
            objectName: "transitionEffect"
            Layout.fillWidth: true
            implicitHeight: 28
            model: [qsTr("淡入淡出"), qsTr("剪切"), qsTr("滑动"), qsTr("擦除")]
            currentIndex: -1
            displayText: qsTr("未接入")
            enabled: false
            Accessible.name: qsTr("转场效果（未接入）")
            palette.buttonText: "#666666"
            background: Rectangle {
                color: "#232323"
                border.color: "#3a3a3a"
                radius: 2
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: qsTr("时长")
                color: "#8c8c8c"
            }
            SpinBox {
                objectName: "transitionDuration"
                Layout.fillWidth: true
                Layout.minimumWidth: 80
                implicitHeight: 28
                from: 0
                to: 20000
                stepSize: 50
                value: 300 // Prototype default only; no transition state is connected.
                enabled: false
                Accessible.name: qsTr("转场时长，毫秒（未接入）")
                palette.text: "#666666"
                palette.buttonText: "#666666"
                palette.button: "#232323"
                background: Rectangle {
                    color: "#232323"
                    border.color: "#3a3a3a"
                    radius: 2
                }
            }
            Label {
                text: "ms"
                color: "#8c8c8c"
            }
        }

        Button {
            objectName: "executeTransitionButton"
            Layout.fillWidth: true
            implicitHeight: 30
            text: qsTr("转场")
            display: AbstractButton.TextBesideIcon
            icon.source: "qrc:/assets/icons/arrow-right.svg"
            icon.width: 16
            icon.height: 16
            icon.color: enabled ? "#d6d6d6" : "#666666"
            palette.buttonText: enabled ? "#d6d6d6" : "#666666"
            enabled: false
            Accessible.name: qsTr("执行转场（未接入）")
            background: Rectangle {
                color: "#232323"
                border.color: "#3a3a3a"
                radius: 2
            }
        }
    }
}
