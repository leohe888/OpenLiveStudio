import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    readonly property var themeColors: uiTheme.colors
    objectName: "sourcesPanel"
    implicitWidth: 238
    implicitHeight: 320
    color: themeColors.background.panel
    readonly property var state: typeof sceneManager !== "undefined" ? sceneManager : null
    signal sourceAddDialogRequested(string sceneId)
    signal sourceRemovalDialogRequested(string sceneId, string sourceId)

    // Each operation opts in once its business implementation is available.
    component SourceButton: ToolButton {
        id: sourceButton
        hoverEnabled: true
        width: 30
        height: 30
        display: AbstractButton.IconOnly
        icon.width: 16
        icon.height: 16
        icon.color: enabled ? root.themeColors.icon.primary : root.themeColors.icon.disabled
        palette.toolTipBase: root.themeColors.background.header
        palette.toolTipText: root.themeColors.text.primary
        enabled: false
        Accessible.name: text
        ToolTip.text: text
        ToolTip.visible: hovered
        background: Rectangle {
            radius: 2
            color: !sourceButton.enabled ? "transparent"
                : (sourceButton.down ? root.themeColors.button.background.pressed
                    : (sourceButton.hovered ? root.themeColors.button.background.hovered : "transparent"))
            border.color: sourceButton.enabled && sourceButton.activeFocus
                ? root.themeColors.border.focus : "transparent"
        }
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
                        color: sourceScrollBar.pressed ? root.themeColors.scrollbar.thumb.pressed
                            : (sourceScrollBar.hovered ? root.themeColors.scrollbar.thumb.hovered
                                : root.themeColors.scrollbar.thumb.normal)
                    }
                    background: Rectangle { color: root.themeColors.background.header }
                }

                delegate: Rectangle {
                    required property string sourceId
                    required property string sourceName
                    required property bool sourceVisible
                    required property bool sourceLocked
                    readonly property bool selected: root.state !== null
                        && root.state.currentSourceId === sourceId
                    objectName: "sourceRow_" + sourceId
                    width: sourceList.width - (sourceScrollBar.visible ? sourceScrollBar.width : 0)
                    height: 32
                    color: selected ? root.themeColors.background.selected
                        : (rowHover.hovered ? root.themeColors.background.hovered : "transparent")
                    Accessible.role: Accessible.ListItem
                    Accessible.name: sourceName
                    Accessible.selected: selected

                    HoverHandler {
                        id: rowHover
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        spacing: 2

                        Label {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 0
                            text: sourceName
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: root.themeColors.text.primary
                            verticalAlignment: Text.AlignVCenter
                            TapHandler {
                                onTapped: root.state.selectSource(sourceId)
                            }
                        }
                        SourceButton {
                            objectName: "sourceVisibilityButton"
                            enabled: root.state !== null
                            checked: sourceVisible
                            text: sourceVisible ? qsTr("来源已显示") : qsTr("来源已隐藏")
                            icon.source: sourceVisible ? "qrc:/assets/icons/eye.svg"
                                : "qrc:/assets/icons/eye-off.svg"
                            onClicked: root.state.setSourceVisible(root.state.currentSceneId, sourceId, !sourceVisible)
                        }
                        SourceButton {
                            objectName: "sourceLockButton"
                            enabled: root.state !== null
                            checked: sourceLocked
                            text: sourceLocked ? qsTr("来源已锁定") : qsTr("来源未锁定")
                            icon.source: sourceLocked ? "qrc:/assets/icons/lock.svg"
                                : "qrc:/assets/icons/lock-open.svg"
                            onClicked: root.state.setSourceLocked(root.state.currentSceneId, sourceId, !sourceLocked)
                        }
                    }
                }
            }

            Label {
                objectName: "emptySourcesLabel"
                anchors.centerIn: parent
                text: qsTr("暂无来源")
                color: root.themeColors.text.secondary
                visible: sourceList.count === 0
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
                        enabled: {
                            if (!root.state) return false
                            switch (modelData.name) {
                            case "addSourceButton": return root.state.currentSceneId.length > 0
                            case "removeSourceButton": return root.state.currentSourceId.length > 0
                            case "moveSourceUpButton": return root.state.canMoveCurrentSourceUp
                            case "moveSourceDownButton": return root.state.canMoveCurrentSourceDown
                            default: return false
                            }
                        }
                        onClicked: {
                            if (modelData.name === "addSourceButton")
                                root.sourceAddDialogRequested(root.state.currentSceneId)
                            else if (modelData.name === "removeSourceButton")
                                root.sourceRemovalDialogRequested(root.state.currentSceneId, root.state.currentSourceId)
                            else if (modelData.name === "moveSourceUpButton")
                                root.state.moveSource(root.state.currentSceneId, root.state.currentSourceId, -1)
                            else if (modelData.name === "moveSourceDownButton")
                                root.state.moveSource(root.state.currentSceneId, root.state.currentSourceId, 1)
                        }
                    }
                }
                Rectangle {
                    implicitWidth: 1
                    implicitHeight: 18
                    color: root.themeColors.border.subtle
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
