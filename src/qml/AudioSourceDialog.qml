import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "audioSourceDialog"
    required property var audioSources
    property string sourceId: ""
    readonly property bool editing: sourceId.length > 0
    property string deviceId: ""
    property string errorText: ""
    readonly property var colors: uiTheme.colors
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
        target: root.audioSources
        function onDevicesChanged() { root.updateChoices() }
        function onRowsInserted() { root.updateChoices() }
        function onRowsRemoved() {
            if (root.editing && !root.audioSources.source(root.sourceId).id) root.closeRequested()
            root.updateChoices()
        }
    }
    component DialogCombo: ComboBox {
        Layout.fillWidth: true
        implicitHeight: 34
        palette.text: root.colors.text.primary
        palette.buttonText: root.colors.text.primary
        palette.base: root.colors.background.input
        palette.window: root.colors.background.panel
        palette.highlight: root.colors.background.selected
        palette.highlightedText: root.colors.text.primary
        background: Rectangle {
            color: root.colors.background.input
            border.color: parent.activeFocus ? root.colors.border.focus : root.colors.border.default
            radius: 2
        }
        contentItem: Text {
            text: parent.displayText
            leftPadding: 8; rightPadding: 28
            color: parent.enabled ? root.colors.text.primary : root.colors.text.secondary
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    component ActionButton: Button {
        implicitHeight: 32
        implicitWidth: 80
        palette.buttonText: enabled ? root.colors.text.primary : root.colors.text.disabled
        background: Rectangle {
            color: !parent.enabled ? root.colors.button.background.disabled : parent.down ? root.colors.button.background.pressed
                : parent.hovered ? root.colors.button.background.hovered : root.colors.button.background.normal
            border.color: parent.activeFocus ? root.colors.border.focus : root.colors.border.default
            radius: 2
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10
        Label { text: qsTr("来源名称"); color: root.colors.text.primary }
        TextField {
            id: nameInput
            objectName: "soundNameInput"
            Layout.fillWidth: true
            maximumLength: 80
            selectByMouse: true
            color: root.colors.text.primary
            selectionColor: root.colors.background.selected
            selectedTextColor: root.colors.text.primary
            background: Rectangle {
                color: root.colors.background.input
                border.color: nameInput.activeFocus ? root.colors.border.focus : root.colors.border.default
                radius: 2
            }
        }
        Label { text: qsTr("来源类型"); color: root.colors.text.primary }
        DialogCombo {
            id: typeInput
            objectName: "soundTypeInput"
            enabled: !root.editing
            model: [qsTr("麦克风"), qsTr("系统声音")]
            onActivated: {
                nameInput.text = root.audioSources.suggestedSourceName(currentIndex === 0 ? qsTr("麦克风") : qsTr("系统声音"))
                root.deviceId = ""
                root.updateChoices()
            }
        }
        Label { text: qsTr("设备"); color: root.colors.text.primary }
        RowLayout {
            Layout.fillWidth: true
            DialogCombo {
                objectName: "soundDeviceInput"
                enabled: !root.editing && !root.audioSources.refreshing
                textRole: "name"
                model: root.targets
                currentIndex: root.targets.findIndex(e => e.id === root.deviceId)
                displayText: root.editing ? root.original.deviceName || root.original.deviceId || ""
                    : currentIndex >= 0 ? currentText : qsTr("暂无未使用的设备")
                onActivated: root.deviceId = root.targets[currentIndex].id
            }
            ActionButton {
                objectName: "refreshSoundDevicesButton"
                visible: !root.editing
                text: root.audioSources.refreshing ? qsTr("刷新中…") : qsTr("刷新")
                enabled: !root.audioSources.refreshing
                onClicked: root.audioSources.refreshDevices()
            }
        }
        Label {
            Layout.fillWidth: true
            text: root.errorText || (!root.editing ? root.audioSources.catalogError : "")
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: root.colors.status.error
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            ActionButton {
                objectName: "confirmSoundSourceButton"
                text: root.editing ? qsTr("确定") : qsTr("添加")
                enabled: nameInput.text.trim().length > 0 && (root.editing || root.deviceId.length > 0)
                onClicked: {
                    const success = root.editing ? root.audioSources.renameSource(root.sourceId, nameInput.text)
                        : root.audioSources.addSource(nameInput.text, root.selectedType, root.deviceId).length > 0
                    if (success) root.closeRequested()
                    else root.errorText = qsTr("名称为空或重复，或设备已被其他声音来源使用。")
                }
            }
            ActionButton {
                objectName: "cancelSoundSourceButton"
                text: qsTr("取消")
                onClicked: root.closeRequested()
            }
        }
    }
}
