import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"

Rectangle {
    id: root
    objectName: "audioSourceDialog"
    readonly property bool editing: sourceId.length > 0
    property string deviceId: ""
    property string errorText: ""
    readonly property var colors: uiColors
    readonly property var original: editing ? audioSources.source(sourceId) : ({})
    readonly property string selectedType: typeInput.currentIndex === 0 ? "microphone" : "system"
    readonly property var targets: {
        const count = audioSources.count
        return audioSources.devices.filter(e => e.type === selectedType && !audioSources.deviceInUse(e.id))
    }
    implicitWidth: 540
    implicitHeight: editing ? 300 : 370
    color: colors.background.panel
    signal closeRequested()

    function updateChoices() {
        if (!editing && !targets.some(e => e.id === deviceId)) deviceId = targets.length ? targets[0].id : ""
    }
    Component.onCompleted: {
        if (editing) {
            nameInput.text = original.name || ""
            typeInput.currentIndex = original.type === "system" ? 1 : 0
        } else {
            nameInput.text = audioSources.suggestedSourceName(qsTr("麦克风"))
            audioSources.refreshDevices()
        }
    }
    Connections {
        target: audioSources
        function onDevicesChanged() { root.updateChoices() }
        function onRowsInserted() { root.updateChoices() }
        function onRowsRemoved() {
            if (root.editing && !audioSources.source(sourceId).id) root.closeRequested()
            root.updateChoices()
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10
        StudioLabel { text: qsTr("来源名称") }
        StudioTextField {
            id: nameInput
            objectName: "soundNameInput"
            Layout.fillWidth: true
            maximumLength: 80
        }
        StudioLabel { text: qsTr("来源类型") }
        StudioComboBox {
            implicitHeight: 34
            Layout.fillWidth: true
            id: typeInput
            objectName: "soundTypeInput"
            enabled: !root.editing
            model: [qsTr("麦克风"), qsTr("系统声音")]
            onActivated: {
                nameInput.text = audioSources.suggestedSourceName(currentIndex === 0 ? qsTr("麦克风") : qsTr("系统声音"))
                root.deviceId = ""
                root.updateChoices()
            }
        }
        StudioLabel { text: qsTr("设备") }
        RowLayout {
            Layout.fillWidth: true
            StudioComboBox {
                implicitHeight: 34
                Layout.fillWidth: true
                objectName: "soundDeviceInput"
                enabled: !root.editing && !audioSources.refreshing
                textRole: "name"
                model: root.targets
                currentIndex: root.targets.findIndex(e => e.id === root.deviceId)
                displayText: root.editing ? root.original.deviceName || root.original.deviceId || ""
                    : currentIndex >= 0 ? currentText : qsTr("暂无未使用的设备")
                onActivated: root.deviceId = root.targets[currentIndex].id
            }
            StudioButton {
                implicitWidth: 80
                objectName: "refreshSoundDevicesButton"
                visible: !root.editing
                text: audioSources.refreshing ? qsTr("刷新中…") : qsTr("刷新")
                enabled: !audioSources.refreshing
                onClicked: audioSources.refreshDevices()
            }
        }
        StudioLabel {
            Layout.fillWidth: true
            text: root.errorText || (!root.editing ? audioSources.catalogError : "")
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: root.colors.status.error
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            StudioButton {
                implicitWidth: 80
                objectName: "confirmSoundSourceButton"
                text: root.editing ? qsTr("确定") : qsTr("添加")
                enabled: nameInput.text.trim().length > 0 && (root.editing || root.deviceId.length > 0)
                onClicked: {
                    const success = root.editing ? audioSources.renameSource(sourceId, nameInput.text)
                        : audioSources.addSource(nameInput.text, root.selectedType, root.deviceId).length > 0
                    if (success) root.closeRequested()
                    else root.errorText = qsTr("名称为空或重复，或设备已被其他声音来源使用。")
                }
            }
            StudioButton {
                implicitWidth: 80
                objectName: "cancelSoundSourceButton"
                text: qsTr("取消")
                onClicked: root.closeRequested()
            }
        }
    }
}
