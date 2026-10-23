import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "sourcesPanel"
    implicitWidth: 238
    implicitHeight: 320
    color: "#1d1d1d"
    readonly property var state: typeof studioState !== "undefined" ? studioState : null
    signal sourceAddDialogRequested(string sceneId)
    signal sourceRemovalDialogRequested(string sceneId, string sourceId)

    // Source operations remain unavailable until their business steps.
    component SourceButton: ToolButton {
        width: 30
        height: 30
        display: AbstractButton.IconOnly
        icon.width: 16
        icon.height: 16
        icon.color: enabled ? "#d6d6d6" : "#666666"
        enabled: false
        Accessible.name: text
        ToolTip.text: text
        background: Item {}
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: sourceList
                objectName: "sourceList"
                anchors.fill: parent
                clip: true
                model: root.state ? root.state.sourceModel : null
                currentIndex: -1
                keyNavigationEnabled: false

                ScrollBar.vertical: ScrollBar {
                    id: sourceScrollBar
                    objectName: "sourceScrollBar"
                    policy: sourceList.contentHeight > sourceList.height
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
                        color: sourceScrollBar.pressed ? "#9a9a9a"
                            : (sourceScrollBar.hovered ? "#808080" : "#606060")
                    }
                    background: Rectangle { color: "#232323" }
                }

                delegate: Rectangle {
                    required property string sourceId
                    required property string sourceName
                    readonly property bool selected: root.state !== null
                        && root.state.currentSourceId === sourceId
                    objectName: "sourceRow_" + sourceId
                    width: sourceList.width - (sourceScrollBar.visible ? sourceScrollBar.width : 0)
                    height: 32
                    color: selected ? "#31445b" : "transparent"
                    Accessible.role: Accessible.ListItem
                    Accessible.name: sourceName
                    Accessible.selected: selected

                    TapHandler {
                        onTapped: root.state.selectSource(sourceId)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        spacing: 2

                        Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: sourceName
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: "#d6d6d6"
                        }
                        SourceButton {
                            objectName: "sourceVisibilityButton"
                            text: qsTr("来源显隐")
                            icon.source: "qrc:/assets/icons/eye.svg"
                        }
                        SourceButton {
                            objectName: "sourceLockButton"
                            text: qsTr("来源锁定")
                            icon.source: "qrc:/assets/icons/lock.svg"
                        }
                    }
                }
            }

            Label {
                objectName: "emptySourcesLabel"
                anchors.centerIn: parent
                text: qsTr("暂无来源")
                color: "#8c8c8c"
                visible: sourceList.count === 0
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

            RowLayout {
                objectName: "sourceActions"
                anchors.fill: parent
                spacing: 2

                Repeater {
                    model: [
                        { name: "addSourceButton", icon: "plus", label: qsTr("添加来源") },
                        { name: "removeSourceButton", icon: "minus", label: qsTr("移除来源") },
                        { name: "moveSourceUpButton", icon: "arrow-up", label: qsTr("来源上移") },
                        { name: "moveSourceDownButton", icon: "arrow-down", label: qsTr("来源下移") }
                    ]
                    delegate: SourceButton {
                        required property var modelData
                        objectName: modelData.name
                        text: modelData.label
                        icon.source: "qrc:/assets/icons/" + modelData.icon + ".svg"
                        Layout.preferredWidth: 30
                        enabled: root.state !== null && root.state.currentSceneId.length > 0
                            && (modelData.name === "addSourceButton"
                                || (modelData.name === "removeSourceButton" && root.state.currentSourceId.length > 0))
                        onClicked: {
                            if (modelData.name === "addSourceButton")
                                root.sourceAddDialogRequested(root.state.currentSceneId)
                            else if (modelData.name === "removeSourceButton")
                                root.sourceRemovalDialogRequested(root.state.currentSceneId, root.state.currentSourceId)
                        }
                    }
                }
                Rectangle {
                    implicitWidth: 1
                    implicitHeight: 18
                    color: "#444444"
                }
                SourceButton {
                    objectName: "sourceFiltersButton"
                    text: qsTr("来源滤镜")
                    icon.source: "qrc:/assets/icons/sliders-horizontal.svg"
                    Layout.preferredWidth: 30
                }
                Item { Layout.fillWidth: true }
                SourceButton {
                    objectName: "sourcePropertiesButton"
                    text: qsTr("来源属性")
                    icon.source: "qrc:/assets/icons/settings.svg"
                    Layout.preferredWidth: 30
                }
            }
        }
    }
}
