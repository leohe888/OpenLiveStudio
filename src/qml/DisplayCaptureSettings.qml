import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var displayCaptureController: null
    property string displayId: ""
    property bool captureCursor: true
    readonly property var colors: uiTheme.colors
    readonly property var targets: {
        const entries = displayCaptureController ? displayCaptureController.displays.slice() : []
        if (displayId.length && !entries.some(e => e.id === displayId))
            entries.unshift({ id: displayId, name: qsTr("所选显示器已断开，请重新选择") })
        return entries
    }
    Component.onCompleted: {
        if (!displayId.length && targets.length) displayId = targets[0].id
    }
    RowLayout {
        Layout.fillWidth: true
        ComboBox {
            id: target
            objectName: "displayTargetInput"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: 32
            model: root.targets
            textRole: "name"
            valueRole: "id"
            currentIndex: root.targets.findIndex(e => e.id === root.displayId)
            displayText: currentIndex < 0 ? qsTr("暂无可用显示器") : currentText
            onActivated: root.displayId = currentValue
            Accessible.name: qsTr("显示器")
            palette.text: root.colors.text.primary
            palette.buttonText: root.colors.text.primary
            palette.base: root.colors.background.input
            palette.window: root.colors.background.panel
            palette.highlight: root.colors.background.selected
            palette.highlightedText: root.colors.text.primary
            palette.light: root.colors.background.hovered
            palette.midlight: root.colors.background.hovered
            palette.mid: root.colors.border.default
            palette.dark: root.colors.border.focus
            contentItem: Text {
                leftPadding: 8
                rightPadding: target.indicator.width + 12
                text: target.displayText
                font: target.font
                color: root.colors.text.primary
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                color: root.colors.background.input
                border.color: target.activeFocus ? root.colors.border.focus : root.colors.border.default
                radius: 2
            }
        }
        Button {
            objectName: "refreshDisplaysButton"
            text: qsTr("刷新")
            enabled: root.displayCaptureController !== null
            palette.button: root.colors.button.background.normal
            palette.buttonText: root.colors.text.primary
            onClicked: {
                root.displayCaptureController.refreshDisplays()
                if (!root.displayId.length && root.targets.length) root.displayId = root.targets[0].id
            }
        }
    }
    CheckBox {
        objectName: "captureCursorInput"
        text: qsTr("采集鼠标光标")
        checked: root.captureCursor
        onToggled: root.captureCursor = checked
        palette.windowText: root.colors.text.primary
        palette.text: root.colors.text.primary
        palette.base: root.colors.background.input
        palette.button: root.colors.button.background.normal
        palette.highlight: root.colors.border.focus
        palette.light: root.colors.border.default
        palette.mid: root.colors.border.default
    }
    Label {
        objectName: "displayCaptureHint"
        Layout.fillWidth: true
        text: qsTr("原始分辨率 · 最高 30 FPS · SDR")
        color: root.colors.text.secondary
        wrapMode: Text.WordWrap
    }
}
