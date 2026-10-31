import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"

ColumnLayout {
    id: root
    property string displayId: ""
    property bool captureCursor: true
    readonly property var targets: {
        const entries = displaySourceController ? displaySourceController.displays.slice() : []
        if (displayId.length && !entries.some(e => e.id === displayId))
            entries.unshift({ id: displayId, name: qsTr("所选显示器已断开，请重新选择") })
        return entries
    }
    Component.onCompleted: {
        if (!displayId.length && targets.length) displayId = targets[0].id
    }
    RowLayout {
        Layout.fillWidth: true
        StudioComboBox {
            id: target
            objectName: "displayTargetInput"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            model: root.targets
            textRole: "name"
            valueRole: "id"
            currentIndex: root.targets.findIndex(e => e.id === root.displayId)
            displayText: currentIndex < 0 ? qsTr("暂无可用显示器") : currentText
            onActivated: root.displayId = currentValue
            Accessible.name: qsTr("显示器")
        }
        StudioButton {
            objectName: "refreshDisplaysButton"
            text: qsTr("刷新")
            enabled: displaySourceController !== null
            onClicked: {
                displaySourceController.refreshDisplays()
                if (!root.displayId.length && root.targets.length) root.displayId = root.targets[0].id
            }
        }
    }
    StudioCheckBox {
        objectName: "captureCursorInput"
        text: qsTr("采集鼠标光标")
        checked: root.captureCursor
        onToggled: root.captureCursor = checked
    }
    StudioLabel {
        objectName: "displayCaptureHint"
        Layout.fillWidth: true
        text: qsTr("原始分辨率 · 最高 30 FPS · SDR")
        color: uiColors.text.secondary
        wrapMode: Text.WordWrap
    }
}
