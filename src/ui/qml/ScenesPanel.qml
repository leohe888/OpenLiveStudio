import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "scenesPanel"
    implicitWidth: 238
    implicitHeight: 260
    color: "#1d1d1d"
    readonly property var state: typeof studioState !== "undefined" ? studioState : null
    signal sceneNameDialogRequested(string sceneId, string initialName)
    signal sceneRemovalDialogRequested(string sceneId)

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
                model: root.state ? root.state.sceneModel : null
                currentIndex: -1
                keyNavigationEnabled: false

                ScrollBar.vertical: ScrollBar {
                    id: sceneScrollBar
                    objectName: "sceneScrollBar"
                    policy: sceneList.contentHeight > sceneList.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    implicitWidth: 12
                    padding: 2
                    minimumSize: Math.min(1, 24 / Math.max(1, height))
                    hoverEnabled: true
                    z: 1
                    contentItem: Rectangle {
                        implicitWidth: 8
                        implicitHeight: 24
                        radius: 4
                        color: sceneScrollBar.pressed ? "#9a9a9a"
                            : (sceneScrollBar.hovered ? "#808080" : "#606060")
                    }
                    background: Rectangle { color: "#232323" }
                }

                delegate: Rectangle {
                    required property string sceneId
                    required property string sceneName
                    readonly property bool selected: root.state !== null
                        && root.state.currentSceneId === sceneId
                    objectName: "sceneRow_" + sceneId
                    width: sceneList.width - (sceneScrollBar.visible ? sceneScrollBar.width : 0)
                    height: 32
                    color: selected ? "#31445b" : "transparent"
                    Accessible.role: Accessible.ListItem
                    Accessible.name: sceneName
                    Accessible.selected: selected

                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: Text.AlignVCenter
                        text: sceneName
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: "#d6d6d6"
                    }
                    TapHandler {
                        onTapped: root.state.selectScene(sceneId)
                        onDoubleTapped: root.sceneNameDialogRequested(sceneId, sceneName)
                    }
                }
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
                        { name: "addSceneButton", icon: "qrc:/assets/icons/plus.svg", label: qsTr("添加场景") },
                        { name: "removeSceneButton", icon: "qrc:/assets/icons/minus.svg", label: qsTr("移除场景") },
                        { name: "moveSceneUpButton", icon: "qrc:/assets/icons/arrow-up.svg", label: qsTr("场景上移") },
                        { name: "moveSceneDownButton", icon: "qrc:/assets/icons/arrow-down.svg", label: qsTr("场景下移") }
                    ]

                    delegate: ToolButton {
                        required property var modelData
                        objectName: modelData.name
                        width: 30
                        height: 30
                        text: modelData.label
                        display: AbstractButton.IconOnly
                        icon.source: modelData.icon
                        icon.width: 16
                        icon.height: 16
                        icon.color: enabled ? "#d6d6d6" : "#666666"
                        enabled: {
                            if (!root.state) return false
                            switch (modelData.name) {
                            case "addSceneButton": return true
                            case "removeSceneButton": return root.state.canRemoveCurrentScene
                            case "moveSceneUpButton": return root.state.canMoveCurrentSceneUp
                            case "moveSceneDownButton": return root.state.canMoveCurrentSceneDown
                            default: return false
                            }
                        }
                        onClicked: {
                            switch (modelData.name) {
                            case "addSceneButton": root.sceneNameDialogRequested("", ""); break
                            case "removeSceneButton": root.sceneRemovalDialogRequested(root.state.currentSceneId); break
                            case "moveSceneUpButton": root.state.moveScene(root.state.currentSceneId, -1); break
                            case "moveSceneDownButton": root.state.moveScene(root.state.currentSceneId, 1); break
                            }
                        }
                        Accessible.name: modelData.label
                        ToolTip.text: modelData.label

                        background: Item {}
                    }
                }
            }
        }
    }
}
