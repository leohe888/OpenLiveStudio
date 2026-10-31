import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"

ColumnLayout {
    id: root
    property var cameraSourceController: null
    property string deviceId: ""
    property var selectedMode: ({})
    property var modeList: []
    property bool loadingModes: false
    property string errorText: ""
    readonly property var colors: uiColors
    readonly property var targets: {
        const entries = cameraSourceController ? cameraSourceController.devices.slice() : []
        if (deviceId.length && !entries.some(e => e.id === deviceId))
            entries.unshift({id: deviceId, name: qsTr("所选摄像头不可用，请刷新或重新选择")})
        return entries
    }
    function loadModes() {
        if (!visible || !cameraSourceController || !deviceId.length) return
        loadingModes = true
        errorText = ""
        cameraSourceController.requestModes(deviceId)
    }
    function modeIndex() {
        if (!selectedMode.width || selectedMode.width <= 0) return 0
        return modeList.findIndex(e => e.width === selectedMode.width && e.height === selectedMode.height
            && e.numerator * selectedMode.denominator === selectedMode.numerator * e.denominator)
    }
    onDeviceIdChanged: loadModes()
    onVisibleChanged: {
        if (visible && cameraSourceController) { cameraSourceController.refreshDevices(); loadModes() }
    }
    Component.onCompleted: {
        if (visible && cameraSourceController) cameraSourceController.refreshDevices()
        loadModes()
    }
    Connections {
        target: root.cameraSourceController
        function onDevicesChanged() {
            if (root.visible && !root.deviceId.length && root.targets.length) root.deviceId = root.targets[0].id
        }
        function onModesChanged(id, error) {
            if (id !== root.deviceId) return
            root.loadingModes = false
            root.errorText = error
            const entries = [{name: qsTr("设备默认")}].concat(root.cameraSourceController.modes(id))
            if (root.selectedMode.width > 0 && !entries.some(e => e.width === root.selectedMode.width
                && e.height === root.selectedMode.height && e.numerator * root.selectedMode.denominator
                    === root.selectedMode.numerator * e.denominator))
                entries.push(Object.assign({}, root.selectedMode, {name: qsTr("原采集模式（当前未列出）")}))
            root.modeList = entries
        }
    }
    StudioLabel { text: qsTr("摄像头设备") }
    RowLayout {
        Layout.fillWidth: true
        StudioComboBox {
            implicitHeight: 34
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            textRole: "name"
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
        StudioButton {
            objectName: "refreshCamerasButton"
            text: root.cameraSourceController && root.cameraSourceController.refreshing ? qsTr("刷新中…") : qsTr("刷新")
            enabled: root.cameraSourceController !== null && !root.cameraSourceController.refreshing
            onClicked: { root.cameraSourceController.refreshDevices(); root.loadModes() }
        }
    }
    StudioLabel { text: qsTr("采集模式") }
    StudioComboBox {
        implicitHeight: 34
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        textRole: "name"
        objectName: "cameraModeInput"
        model: root.modeList
        currentIndex: root.modeIndex()
        enabled: !root.loadingModes
        displayText: root.loadingModes ? qsTr("正在读取设备模式…") : currentText
        Accessible.name: qsTr("采集模式")
        onActivated: root.selectedMode = root.modeList[currentIndex]
    }
    StudioLabel {
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.errorText || (root.cameraSourceController ? root.cameraSourceController.catalogError : "")
        textFormat: Text.PlainText
        wrapMode: Text.WrapAnywhere
        color: root.colors.status.error
    }
}
