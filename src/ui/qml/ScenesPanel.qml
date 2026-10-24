import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    readonly property var themeColors: uiTheme.colors
    objectName: "scenesPanel"
    implicitWidth: 238
    implicitHeight: 260
    color: themeColors.background.panel
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
                        color: sceneScrollBar.pressed ? root.themeColors.scrollbar.thumb.pressed
                            : (sceneScrollBar.hovered ? root.themeColors.scrollbar.thumb.hovered
                                : root.themeColors.scrollbar.thumb.normal)
                    }
                    background: Rectangle { color: root.themeColors.background.header }
                }

                delegate: Rectangle {
                    required property string sceneId
                    required property string sceneName
                    readonly property bool selected: root.state !== null
                        && root.state.currentSceneId === sceneId
                    objectName: "sceneRow_" + sceneId
                    width: sceneList.width - (sceneScrollBar.visible ? sceneScrollBar.width : 0)
                    height: 32
                    color: selected ? root.themeColors.background.selected
                        : (rowHover.hovered ? root.themeColors.background.hovered : "transparent")
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
                        color: root.themeColors.text.primary
                    }
                    HoverHandler {
                        id: rowHover
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
                color: root.themeColors.text.secondary
                visible: sceneList.count === 0
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: root.themeColors.border.subtle
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 32
            color: root.themeColors.background.header

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
                        id: sceneButton
                        hoverEnabled: true
                        required property var modelData
                        objectName: modelData.name
                        width: 30
                        height: 30
                        text: modelData.label
                        display: AbstractButton.IconOnly
                        icon.source: modelData.icon
                        icon.width: 16
                        icon.height: 16
                        icon.color: enabled ? root.themeColors.icon.primary : root.themeColors.icon.disabled
                        palette.toolTipBase: root.themeColors.background.header
                        palette.toolTipText: root.themeColors.text.primary
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

                        background: Rectangle {
                            radius: 2
                            color: !sceneButton.enabled ? "transparent"
                                : (sceneButton.down ? root.themeColors.button.background.pressed
                                    : (sceneButton.hovered ? root.themeColors.button.background.hovered : "transparent"))
                            border.color: sceneButton.enabled && sceneButton.activeFocus
                                ? root.themeColors.border.focus : "transparent"
                        }
                    }
                }
            }
        }
    }
}
