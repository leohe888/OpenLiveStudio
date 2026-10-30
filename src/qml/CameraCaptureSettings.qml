import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var cameraCaptureController: null
    property string deviceId: ""
    property var selectedMode: ({})
    property var modeList: []
    property bool loadingModes: false
    property string errorText: ""
    readonly property var colors: uiColors
    readonly property var targets: {
        const entries = cameraCaptureController ? cameraCaptureController.devices.slice() : []
        if (deviceId.length && !entries.some(e => e.id === deviceId))
            entries.unshift({id: deviceId, name: qsTr("所选摄像头不可用，请刷新或重新选择")})
        return entries
    }
    function loadModes() {
        if (!visible || !cameraCaptureController || !deviceId.length) return
        loadingModes = true
        errorText = ""
        cameraCaptureController.requestModes(deviceId)
    }
    function modeIndex() {
        if (!selectedMode.width || selectedMode.width <= 0) return 0
        return modeList.findIndex(e => e.width === selectedMode.width && e.height === selectedMode.height
            && e.numerator * selectedMode.denominator === selectedMode.numerator * e.denominator)
    }
    onDeviceIdChanged: loadModes()
    onVisibleChanged: {
        if (visible && cameraCaptureController) { cameraCaptureController.refreshDevices(); loadModes() }
    }
    Component.onCompleted: {
        if (visible && cameraCaptureController) cameraCaptureController.refreshDevices()
        loadModes()
    }
    Connections {
        target: root.cameraCaptureController
        function onDevicesChanged() {
            if (root.visible && !root.deviceId.length && root.targets.length) root.deviceId = root.targets[0].id
        }
        function onModesChanged(id, error) {
            if (id !== root.deviceId) return
            root.loadingModes = false
            root.errorText = error
            const entries = [{name: qsTr("设备默认")}].concat(root.cameraCaptureController.modes(id))
            if (root.selectedMode.width > 0 && !entries.some(e => e.width === root.selectedMode.width
                && e.height === root.selectedMode.height && e.numerator * root.selectedMode.denominator
                    === root.selectedMode.numerator * e.denominator))
                entries.push(Object.assign({}, root.selectedMode, {name: qsTr("原采集模式（当前未列出）")}))
            root.modeList = entries
        }
    }
    component CameraCombo: ComboBox {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: 34
        textRole: "name"
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
            leftPadding: 8
            rightPadding: 28
            color: root.colors.text.primary
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    Label { text: qsTr("摄像头设备"); color: root.colors.text.primary }
    RowLayout {
        Layout.fillWidth: true
        CameraCombo {
            id: device
            objectName: "cameraDeviceInput"
            model: root.targets
            currentIndex: root.targets.findIndex(e => e.id === root.deviceId)
            displayText: currentIndex < 0 ? qsTr("暂无可用摄像头") : currentText
            Accessible.name: qsTr("摄像头设备")
            onActivated: {
                root.selectedMode = ({})
                root.modeList = []
                root.deviceId = root.targets[currentIndex].id
            }
        }
        Button {
            objectName: "refreshCamerasButton"
            text: root.cameraCaptureController && root.cameraCaptureController.refreshing ? qsTr("刷新中…") : qsTr("刷新")
            enabled: root.cameraCaptureController !== null && !root.cameraCaptureController.refreshing
            palette.button: root.colors.button.background.normal
            palette.buttonText: root.colors.text.primary
            onClicked: { root.cameraCaptureController.refreshDevices(); root.loadModes() }
        }
    }
    Label { text: qsTr("采集模式"); color: root.colors.text.primary }
    CameraCombo {
        objectName: "cameraModeInput"
        model: root.modeList
        currentIndex: root.modeIndex()
        enabled: !root.loadingModes
        displayText: root.loadingModes ? qsTr("正在读取设备模式…") : currentText
        Accessible.name: qsTr("采集模式")
        onActivated: root.selectedMode = root.modeList[currentIndex]
    }
    Label {
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.errorText || (root.cameraCaptureController ? root.cameraCaptureController.catalogError : "")
        textFormat: Text.PlainText
        wrapMode: Text.WrapAnywhere
        color: root.colors.status.error
    }
}
