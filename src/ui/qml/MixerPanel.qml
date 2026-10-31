import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"

Rectangle {
    id: root
    objectName: "mixerPanel"
    readonly property var themeColors: uiColors
    readonly property var audioState: typeof audioSourceModel !== "undefined" ? audioSourceModel : null
    implicitWidth: 220
    implicitHeight: 240
    color: themeColors.background.panel
    signal soundSourceAddDialogRequested()
    signal soundSourceRemovalDialogRequested(string sourceId)
    signal soundSourcePropertiesDialogRequested(string sourceId)

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
            StudioLabel { text: qsTr("混音"); color: root.themeColors.text.secondary; font.pixelSize: 11 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Meter { Layout.fillWidth: true; vertical: false; peak: root.audioState ? root.audioState.masterPeakLeft : 0 }
                Meter { Layout.fillWidth: true; vertical: false; peak: root.audioState ? root.audioState.masterPeakRight : 0 }
            }
            StudioLabel {
                objectName: "mixedClipLabel"
                text: root.audioState && root.audioState.masterClipped ? qsTr("过载") : qsTr("输出")
                color: root.audioState && root.audioState.masterClipped ? root.themeColors.status.error : root.themeColors.text.secondary
                font.pixelSize: 11
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            StudioScrollView {
                id: scroll
                objectName: "mixerScrollView"
                anchors.fill: parent
                clip: true
                contentWidth: Math.max(availableWidth, channels.implicitWidth + 12)
                contentHeight: channels.implicitHeight + 12
                ScrollBar.horizontal: StudioScrollBar {
                    objectName: "mixerHorizontalScrollBar"
                    parent: scroll
                    x: scroll.leftPadding
                    y: scroll.height - height
                    width: scroll.availableWidth
                    active: scroll.ScrollBar.vertical.active
                    policy: scroll.contentWidth > scroll.availableWidth ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                }
                ScrollBar.vertical: StudioScrollBar {
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
                                StudioLabel {
                                    Layout.fillWidth: true
                                    text: channel.soundName
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignHCenter
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
                                    StudioSlider {
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
                                    }
                                }
                                RowLayout {
                                    Layout.alignment: Qt.AlignHCenter
                                    StudioIconButton {
                                        implicitWidth: 28
                                        implicitHeight: 28
                                        objectName: channel.soundId + "MuteButton"
                                        text: channel.soundMuted ? qsTr("取消静音") : qsTr("静音")
                                        icon.source: channel.soundMuted ? "qrc:/assets/icons/volume-x.svg" : "qrc:/assets/icons/volume-2.svg"
                                        enabled: root.audioState !== null
                                        onClicked: {
                                            root.audioState.selectSource(channel.soundId)
                                            root.audioState.setMuted(channel.soundId, !channel.soundMuted)
                                        }
                                    }
                                    StudioLabel {
                                        objectName: "channelValue"
                                        text: Math.round(channel.soundVolume * 100) + "%"
                                        color: root.themeColors.text.secondary
                                        font.pixelSize: 10
                                    }
                                }
                                StudioLabel {
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
            StudioLabel {
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
                StudioIconButton {
                    implicitWidth: 28
                    implicitHeight: 28
                    objectName: "addSoundSourceButton"
                    text: qsTr("添加声音来源")
                    icon.source: "qrc:/assets/icons/plus.svg"
                    enabled: root.audioState !== null
                    onClicked: root.soundSourceAddDialogRequested()
                }
                StudioIconButton {
                    implicitWidth: 28
                    implicitHeight: 28
                    objectName: "removeSoundSourceButton"
                    text: qsTr("移除声音来源")
                    icon.source: "qrc:/assets/icons/minus.svg"
                    enabled: root.audioState !== null && root.audioState.currentSourceId.length > 0
                    onClicked: root.soundSourceRemovalDialogRequested(root.audioState.currentSourceId)
                }
                Item { Layout.fillWidth: true }
                StudioIconButton {
                    implicitWidth: 28
                    implicitHeight: 28
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
