import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    readonly property var themeColors: uiTheme.colors
    objectName: "controlsPanel"
    implicitWidth: 286
    implicitHeight: 174
    color: themeColors.background.panel

    // Retain Basic's thumb geometry, visibility and fade behavior.
    component PanelScrollBar: ScrollBar {
        hoverEnabled: true
        palette.mid: hovered ? root.themeColors.scrollbar.thumb.hovered : root.themeColors.scrollbar.thumb.normal
        palette.dark: root.themeColors.scrollbar.thumb.pressed
        palette.text: root.themeColors.text.primary
    }

    component ControlButton: Button {
        id: controlButton
        hoverEnabled: true
        implicitHeight: 32
        Layout.maximumHeight: 32
        enabled: false
        display: AbstractButton.TextBesideIcon
        icon.width: 16
        icon.height: 16
        icon.color: enabled ? root.themeColors.icon.primary : root.themeColors.icon.disabled
        palette.buttonText: enabled ? root.themeColors.text.primary : root.themeColors.text.disabled
        Accessible.name: text + qsTr("（未接入）")
        background: Rectangle {
            color: !controlButton.enabled ? root.themeColors.button.background.disabled
                : (controlButton.down ? root.themeColors.button.background.pressed
                    : (controlButton.hovered ? root.themeColors.button.background.hovered : root.themeColors.button.background.normal))
            border.color: controlButton.enabled && controlButton.activeFocus
                ? root.themeColors.border.focus : root.themeColors.border.default
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
        ScrollBar.horizontal: PanelScrollBar {
            objectName: "controlsHorizontalScrollBar"
            parent: scroll
            x: scroll.leftPadding
            y: scroll.height - height
            width: scroll.availableWidth
            active: scroll.ScrollBar.vertical.active
        }
        ScrollBar.vertical: PanelScrollBar {
            objectName: "controlsVerticalScrollBar"
            parent: scroll
            x: scroll.mirrored ? 0 : scroll.width - width
            y: scroll.topPadding
            height: scroll.availableHeight
            active: scroll.ScrollBar.horizontal.active
        }

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
