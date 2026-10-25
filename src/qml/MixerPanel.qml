import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    readonly property var themeColors: uiTheme.colors
    objectName: "mixerPanel"
    implicitWidth: 238
    implicitHeight: 178
    color: themeColors.background.panel

    // Retain Basic's thumb geometry, visibility and fade behavior.
    component PanelScrollBar: ScrollBar {
        hoverEnabled: true
        palette.mid: hovered ? root.themeColors.scrollbar.thumb.hovered : root.themeColors.scrollbar.thumb.normal
        palette.dark: root.themeColors.scrollbar.thumb.pressed
        palette.text: root.themeColors.text.primary
    }

    component MixerButton: ToolButton {
        id: mixerButton
        hoverEnabled: true
        implicitWidth: 22
        implicitHeight: 18
        padding: 1
        display: AbstractButton.IconOnly
        icon.width: 16
        icon.height: 16
        icon.color: enabled ? root.themeColors.icon.primary : root.themeColors.icon.disabled
        palette.toolTipBase: root.themeColors.background.header
        palette.toolTipText: root.themeColors.text.primary
        enabled: false
        Accessible.name: text
        ToolTip.text: text
        background: Rectangle {
            radius: 2
            color: !mixerButton.enabled ? "transparent"
                : (mixerButton.down ? root.themeColors.button.background.pressed
                    : (mixerButton.hovered ? root.themeColors.button.background.hovered : "transparent"))
            border.color: mixerButton.enabled && mixerButton.activeFocus
                ? root.themeColors.border.focus : "transparent"
        }
    }

    ScrollView {
        id: scroll
        objectName: "mixerScrollView"
        anchors.fill: parent
        clip: true
        contentWidth: Math.max(availableWidth, 180)
        contentHeight: channels.implicitHeight + 12
        ScrollBar.horizontal: PanelScrollBar {
            objectName: "mixerHorizontalScrollBar"
            parent: scroll
            x: scroll.leftPadding
            y: scroll.height - height
            width: scroll.availableWidth
            active: scroll.ScrollBar.vertical.active
        }
        ScrollBar.vertical: PanelScrollBar {
            objectName: "mixerVerticalScrollBar"
            parent: scroll
            x: scroll.mirrored ? 0 : scroll.width - width
            y: scroll.topPadding
            height: scroll.availableHeight
            active: scroll.ScrollBar.horizontal.active
        }

        RowLayout {
            id: channels
            objectName: "mixerChannels"
            x: 8
            y: 6
            width: implicitWidth
            spacing: 12

            Repeater {
                // Channel slots only; no device or audio state is connected.
                model: [
                    { key: "desktop", label: qsTr("桌面音频") },
                    { key: "microphone", label: qsTr("麦克风") }
                ]

                delegate: ColumnLayout {
                    id: channel
                    required property var modelData
                    objectName: modelData.key + "Channel"
                    Layout.minimumWidth: 72
                    Layout.preferredWidth: 72
                    Layout.maximumWidth: 72
                    spacing: 4

                    Row {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 2
                        MixerButton {
                            objectName: channel.modelData.key + "MuteButton"
                            text: channel.modelData.label + qsTr("静音")
                            icon.source: "qrc:/assets/icons/volume-2.svg"
                        }
                        MixerButton {
                            objectName: channel.modelData.key + "PropertiesButton"
                            text: channel.modelData.label + qsTr("属性")
                            icon.source: "qrc:/assets/icons/settings.svg"
                        }
                    }

                    Row {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 3
                        Repeater {
                            model: 2
                            delegate: Rectangle {
                                objectName: "levelTrack"
                                width: 6
                                height: 92
                                color: root.themeColors.background.input
                                border.color: root.themeColors.border.subtle
                                // Empty until real channel levels are available.
                            }
                        }
                        Slider {
                            id: volumeSlider
                            objectName: channel.modelData.key + "VolumeSlider"
                            orientation: Qt.Vertical
                            width: 26
                            height: 92
                            from: 0
                            to: 1
                            value: 0.5 // Visual placeholder, not an audio gain.
                            enabled: false
                            Accessible.name: channel.modelData.label + qsTr("音量（未接入）")
                            background: Rectangle {
                                x: (volumeSlider.width - width) / 2
                                y: volumeSlider.topPadding
                                width: 4
                                height: volumeSlider.availableHeight
                                color: root.themeColors.slider.track
                                border.color: root.themeColors.border.default
                            }
                            handle: Rectangle {
                                x: (volumeSlider.width - width) / 2
                                y: volumeSlider.topPadding + volumeSlider.visualPosition
                                   * (volumeSlider.availableHeight - height)
                                width: 14
                                height: 16
                                radius: 1
                                color: volumeSlider.enabled ? root.themeColors.slider.handle : root.themeColors.slider.handleDisabled
                                border.color: volumeSlider.enabled && volumeSlider.activeFocus
                                    ? root.themeColors.border.focus : root.themeColors.border.default
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: channel.modelData.label
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        color: root.themeColors.text.primary
                    }
                    Label {
                        objectName: "channelStatus"
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("未接入")
                        font.pixelSize: 10
                        color: root.themeColors.text.secondary
                    }
                    Label {
                        objectName: "channelValue"
                        Layout.alignment: Qt.AlignHCenter
                        text: "—"
                        font.pixelSize: 10
                        color: root.themeColors.text.secondary
                    }
                }
            }
        }
    }
}
