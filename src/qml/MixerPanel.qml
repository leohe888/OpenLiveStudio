import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "mixerPanel"
    readonly property var themeColors: uiTheme.colors
    readonly property var audioState: typeof audioSourceModel !== "undefined" ? audioSourceModel : null
    implicitWidth: 238
    implicitHeight: 240
    color: themeColors.background.panel
    signal soundSourceAddDialogRequested()
    signal soundSourceRemovalDialogRequested(string sourceId)
    signal soundSourcePropertiesDialogRequested(string sourceId)

    component AudioScrollBar: ScrollBar {
        hoverEnabled: true
        palette.mid: hovered ? root.themeColors.scrollbar.thumb.hovered : root.themeColors.scrollbar.thumb.normal
        palette.dark: root.themeColors.scrollbar.thumb.pressed
        palette.text: root.themeColors.text.primary
    }

    component AudioButton: ToolButton {
        id: button
        implicitWidth: 28
        implicitHeight: 28
        hoverEnabled: true
        display: AbstractButton.IconOnly
        icon.width: 16
        icon.height: 16
        icon.color: enabled ? root.themeColors.icon.primary : root.themeColors.icon.disabled
        Accessible.name: text
        ToolTip.text: text
        ToolTip.visible: hovered
        palette.toolTipBase: root.themeColors.background.header
        palette.toolTipText: root.themeColors.text.primary
        background: Rectangle {
            radius: 2
            color: !button.enabled ? "transparent" : button.down ? root.themeColors.button.background.pressed
                : button.hovered ? root.themeColors.button.background.hovered : "transparent"
            border.color: button.enabled && button.activeFocus ? root.themeColors.border.focus : "transparent"
        }
    }
    component Meter: Rectangle {
        property real peak: 0
        property bool vertical: true
        objectName: "levelTrack"
        implicitWidth: vertical ? 7 : 80
        implicitHeight: vertical ? 78 : 5
        color: root.themeColors.background.input
        border.color: root.themeColors.border.subtle
        Rectangle {
            objectName: "levelFill"
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            width: parent.vertical ? parent.width : parent.width * Math.min(1, Math.max(0, parent.peak))
            height: parent.vertical ? parent.height * Math.min(1, Math.max(0, parent.peak)) : parent.height
            color: parent.peak >= 1 ? root.themeColors.status.error
                : parent.peak >= .8 ? root.themeColors.status.warning : root.themeColors.status.success
        }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 6
            spacing: 6
            Label { text: qsTr("混音"); color: root.themeColors.text.secondary; font.pixelSize: 11 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Meter { Layout.fillWidth: true; vertical: false; peak: root.audioState ? root.audioState.masterPeakLeft : 0 }
                Meter { Layout.fillWidth: true; vertical: false; peak: root.audioState ? root.audioState.masterPeakRight : 0 }
            }
            Label {
                objectName: "mixedClipLabel"
                text: root.audioState && root.audioState.masterClipped ? qsTr("过载") : qsTr("输出")
                color: root.audioState && root.audioState.masterClipped ? root.themeColors.status.error : root.themeColors.text.secondary
                font.pixelSize: 11
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollView {
                id: scroll
                objectName: "mixerScrollView"
                anchors.fill: parent
                clip: true
                contentWidth: Math.max(availableWidth, channels.implicitWidth + 12)
                contentHeight: channels.implicitHeight + 12
                ScrollBar.horizontal: AudioScrollBar {
                    objectName: "mixerHorizontalScrollBar"
                    parent: scroll
                    x: scroll.leftPadding
                    y: scroll.height - height
                    width: scroll.availableWidth
                    active: scroll.ScrollBar.vertical.active
                    policy: scroll.contentWidth > scroll.availableWidth ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                }
                ScrollBar.vertical: AudioScrollBar {
                    objectName: "mixerVerticalScrollBar"
                    parent: scroll
                    x: scroll.mirrored ? 0 : scroll.width - width
                    y: scroll.topPadding
                    height: scroll.availableHeight
                    active: scroll.ScrollBar.horizontal.active
                    policy: scroll.contentHeight > scroll.availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                }
                RowLayout {
                    id: channels
                    objectName: "mixerChannels"
                    x: 6; y: 6
                    spacing: 6
                    Repeater {
                        model: root.audioState
                        delegate: Rectangle {
                            id: channel
                            required property string soundId
                            required property string soundName
                            required property string soundType
                            required property string soundDeviceName
                            required property real soundVolume
                            required property bool soundMuted
                            required property string soundState
                            required property string soundError
                            required property real soundPeakLeft
                            required property real soundPeakRight
                            objectName: "soundChannel_" + soundId
                            implicitWidth: 96
                            implicitHeight: 168
                            color: root.audioState && root.audioState.currentSourceId === soundId
                                ? root.themeColors.background.selected : "transparent"
                            border.color: root.themeColors.border.subtle
                            radius: 2
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 4
                                spacing: 3
                                Label {
                                    Layout.fillWidth: true
                                    text: channel.soundName
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignHCenter
                                    color: root.themeColors.text.primary
                                    ToolTip.visible: nameHover.hovered
                                    ToolTip.text: channel.soundDeviceName
                                    HoverHandler { id: nameHover }
                                    TapHandler { onTapped: if (root.audioState) root.audioState.selectSource(channel.soundId) }
                                }
                                RowLayout {
                                    Layout.alignment: Qt.AlignHCenter
                                    spacing: 4
                                    Meter { peak: channel.soundPeakLeft }
                                    Meter { peak: channel.soundPeakRight }
                                    Slider {
                                        id: volume
                                        objectName: channel.soundId + "VolumeSlider"
                                        orientation: Qt.Vertical
                                        implicitHeight: 78
                                        implicitWidth: 25
                                        from: 0; to: 1
                                        value: channel.soundVolume
                                        enabled: root.audioState !== null
                                        Accessible.name: channel.soundName + qsTr("音量")
                                        onMoved: {
                                            root.audioState.selectSource(channel.soundId)
                                            root.audioState.setVolume(channel.soundId, value)
                                        }
                                        background: Rectangle {
                                            x: (volume.width - width) / 2; y: volume.topPadding
                                            width: 4; height: volume.availableHeight
                                            color: root.themeColors.slider.track
                                        }
                                        handle: Rectangle {
                                            x: (volume.width - width) / 2
                                            y: volume.topPadding + volume.visualPosition * (volume.availableHeight - height)
                                            width: 14; height: 16; radius: 1
                                            color: root.themeColors.slider.handle
                                            border.color: volume.activeFocus ? root.themeColors.border.focus : root.themeColors.border.default
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.alignment: Qt.AlignHCenter
                                    AudioButton {
                                        objectName: channel.soundId + "MuteButton"
                                        text: channel.soundMuted ? qsTr("取消静音") : qsTr("静音")
                                        icon.source: channel.soundMuted ? "qrc:/assets/icons/volume-x.svg" : "qrc:/assets/icons/volume-2.svg"
                                        enabled: root.audioState !== null
                                        onClicked: {
                                            root.audioState.selectSource(channel.soundId)
                                            root.audioState.setMuted(channel.soundId, !channel.soundMuted)
                                        }
                                    }
                                    Label {
                                        objectName: "channelValue"
                                        text: Math.round(channel.soundVolume * 100) + "%"
                                        color: root.themeColors.text.secondary
                                        font.pixelSize: 10
                                    }
                                }
                                Label {
                                    objectName: "channelStatus"
                                    Layout.fillWidth: true
                                    text: channel.soundState === "error" ? qsTr("等待设备恢复")
                                        : channel.soundState === "starting" ? qsTr("正在连接…")
                                        : channel.soundMuted ? qsTr("已静音") : qsTr("采集中")
                                    color: channel.soundState === "error" ? root.themeColors.status.error : root.themeColors.text.secondary
                                    horizontalAlignment: Text.AlignHCenter
                                    font.pixelSize: 10
                                    ToolTip.text: channel.soundError
                                    ToolTip.visible: errorHover.hovered && channel.soundError.length > 0
                                    HoverHandler { id: errorHover }
                                }
                            }
                        }
                    }
                }
            }
            Label {
                objectName: "emptySoundSourcesLabel"
                anchors.centerIn: parent
                text: qsTr("暂无声音来源")
                color: root.themeColors.text.secondary
                visible: !root.audioState || root.audioState.count === 0
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 32
            color: root.themeColors.background.header
            RowLayout {
                anchors.fill: parent
                spacing: 2
                AudioButton {
                    objectName: "addSoundSourceButton"
                    text: qsTr("添加声音来源")
                    icon.source: "qrc:/assets/icons/plus.svg"
                    enabled: root.audioState !== null
                    onClicked: root.soundSourceAddDialogRequested()
                }
                AudioButton {
                    objectName: "removeSoundSourceButton"
                    text: qsTr("移除声音来源")
                    icon.source: "qrc:/assets/icons/minus.svg"
                    enabled: root.audioState !== null && root.audioState.currentSourceId.length > 0
                    onClicked: root.soundSourceRemovalDialogRequested(root.audioState.currentSourceId)
                }
                Item { Layout.fillWidth: true }
                AudioButton {
                    objectName: "soundSourcePropertiesButton"
                    text: qsTr("声音来源属性")
                    icon.source: "qrc:/assets/icons/settings.svg"
                    enabled: root.audioState !== null && root.audioState.currentSourceId.length > 0
                    onClicked: root.soundSourcePropertiesDialogRequested(root.audioState.currentSourceId)
                }
            }
        }
    }
}
