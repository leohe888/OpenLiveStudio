import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "controlsPanel"
    implicitWidth: 286
    implicitHeight: 174
    color: "#232323"

    component ControlButton: Button {
        implicitHeight: 32
        Layout.maximumHeight: 32
        enabled: false
        display: AbstractButton.TextBesideIcon
        icon.width: 16
        icon.height: 16
        icon.color: enabled ? "#d6d6d6" : "#666666"
        palette.buttonText: enabled ? "#d6d6d6" : "#666666"
        Accessible.name: text + qsTr("（未接入）")
        background: Rectangle {
            color: parent.enabled && parent.down ? "#383838" : "#2b2b2b"
            border.color: "#3a3a3a"
            radius: 2
        }
    }

    ScrollView {
        id: scroll
        objectName: "controlsScrollView"
        anchors.fill: parent
        clip: true
        // Keep viewport sizing independent of the layout it sizes.
        contentWidth: Math.max(root.width, 180)
        contentHeight: Math.max(root.height, 130)

        ColumnLayout {
            id: controls
            objectName: "controlActions"
            x: 8
            y: 8
            width: scroll.contentWidth - 16
            height: scroll.contentHeight - 16
            spacing: 6

            // Independent entries; output state will be supplied by C++ later.
            ControlButton {
                objectName: "recordButton"
                Layout.minimumWidth: 110
                Layout.fillWidth: true
                Layout.minimumHeight: 32
                text: qsTr("开始录制")
                icon.source: "qrc:/assets/icons/circle.svg"
            }
            ControlButton {
                objectName: "streamButton"
                Layout.minimumWidth: 110
                Layout.fillWidth: true
                Layout.minimumHeight: 32
                text: qsTr("开始直播")
                icon.source: "qrc:/assets/icons/radio.svg"
            }
            ControlButton {
                objectName: "settingsButton"
                Layout.minimumWidth: 110
                Layout.fillWidth: true
                Layout.minimumHeight: 32
                text: qsTr("设置")
            }
            Item {
                objectName: "controlsSpacer"
                Layout.fillHeight: true
            }
        }
    }
}
