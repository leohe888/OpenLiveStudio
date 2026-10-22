import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "controlsPanel"
    implicitWidth: 640
    implicitHeight: 64
    color: "#232323"
    signal exitRequested()

    component ControlButton: Button {
        implicitHeight: 32
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
        contentWidth: Math.max(availableWidth, root.implicitWidth)
        contentHeight: controls.implicitHeight + 16

        RowLayout {
            id: controls
            objectName: "controlActions"
            x: 8
            y: 8
            width: scroll.contentWidth - 16
            spacing: 8

            // Independent entries; output state will be supplied by C++ later.
            ControlButton {
                objectName: "recordButton"
                Layout.minimumWidth: 110
                text: qsTr("开始录制")
                icon.source: "qrc:/assets/icons/circle.svg"
            }
            ControlButton {
                objectName: "streamButton"
                Layout.minimumWidth: 110
                text: qsTr("开始直播")
                icon.source: "qrc:/assets/icons/radio.svg"
            }
            Item { Layout.fillWidth: true }
            Label {
                objectName: "recordDuration"
                text: "REC —"
                color: "#8c8c8c"
                Accessible.name: qsTr("录制时长未接入")
            }
            Label {
                objectName: "streamDuration"
                text: "LIVE —"
                color: "#8c8c8c"
                Accessible.name: qsTr("直播时长未接入")
            }
            Rectangle {
                implicitWidth: 1
                implicitHeight: 20
                color: "#444444"
            }
            ControlButton {
                objectName: "settingsButton"
                Layout.minimumWidth: 64
                text: qsTr("设置")
            }
            ControlButton {
                objectName: "exitButton"
                Layout.minimumWidth: 64
                text: qsTr("退出")
                enabled: true
                Accessible.name: text
                onClicked: root.exitRequested()
            }
        }
    }
}
