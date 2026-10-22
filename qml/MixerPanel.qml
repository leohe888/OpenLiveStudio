import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    objectName: "mixerPanel"
    implicitWidth: 238
    implicitHeight: 178
    color: "#1d1d1d"

    component MixerButton: ToolButton {
        implicitWidth: 22
        implicitHeight: 18
        padding: 1
        display: AbstractButton.IconOnly
        icon.width: 16
        icon.height: 16
        icon.color: enabled ? "#d6d6d6" : "#666666"
        enabled: false
        Accessible.name: text
        ToolTip.text: text
        background: Item {}
    }

    ScrollView {
        id: scroll
        objectName: "mixerScrollView"
        anchors.fill: parent
        clip: true
        contentWidth: Math.max(availableWidth, 180)
        contentHeight: channels.implicitHeight + 12

        RowLayout {
            id: channels
            objectName: "mixerChannels"
            x: 8
            y: 6
            width: scroll.contentWidth - 16
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
                    Layout.fillWidth: true
                    Layout.minimumWidth: 72
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
                                color: "#0c0c0c"
                                border.color: "#000000"
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
                                color: "#111111"
                                border.color: "#3a3a3a"
                            }
                            handle: Rectangle {
                                x: (volumeSlider.width - width) / 2
                                y: volumeSlider.topPadding + volumeSlider.visualPosition
                                   * (volumeSlider.availableHeight - height)
                                width: 14
                                height: 16
                                radius: 1
                                color: volumeSlider.enabled ? "#b8b8b8" : "#666666"
                                border.color: "#444444"
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: channel.modelData.label
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        color: "#b9b9b9"
                    }
                    Label {
                        objectName: "channelStatus"
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("未接入")
                        font.pixelSize: 10
                        color: "#8c8c8c"
                    }
                    Label {
                        objectName: "channelValue"
                        Layout.alignment: Qt.AlignHCenter
                        text: "—"
                        font.pixelSize: 10
                        color: "#8c8c8c"
                    }
                }
            }
        }
    }
}
