import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "scenesPanel"
    implicitWidth: 238
    implicitHeight: 260
    color: "#1d1d1d"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: sceneList
                objectName: "sceneList"
                anchors.fill: parent
                clip: true
                model: null
                currentIndex: -1
            }

            Label {
                objectName: "emptyScenesLabel"
                anchors.centerIn: parent
                text: qsTr("暂无场景")
                color: "#8c8c8c"
                visible: sceneList.count === 0
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: "#2e2e2e"
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 32
            color: "#232323"

            Row {
                objectName: "sceneActions"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Repeater {
                    model: [
                        { name: "addSceneButton", symbol: "+", label: qsTr("添加场景") },
                        { name: "removeSceneButton", symbol: "−", label: qsTr("移除场景") },
                        { name: "moveSceneUpButton", symbol: "↑", label: qsTr("场景上移") },
                        { name: "moveSceneDownButton", symbol: "↓", label: qsTr("场景下移") }
                    ]

                    delegate: ToolButton {
                        required property var modelData
                        objectName: modelData.name
                        width: 30
                        height: 30
                        text: modelData.symbol
                        enabled: false
                        Accessible.name: modelData.label
                        ToolTip.text: modelData.label

                        contentItem: Text {
                            text: parent.text
                            color: "#666666"
                            font.pixelSize: 18
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Item {}
                    }
                }
            }
        }
    }
}
