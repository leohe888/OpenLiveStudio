import QtQuick
import QtQuick.Controls.Basic
import "ControlMetrics.js" as Metrics

TabButton {
    id: control
    readonly property var colors: uiColors
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? control.colors.text.primary : control.colors.text.disabled
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        color: control.checked ? control.colors.background.selected : control.colors.background.header
        border.color: control.enabled && control.activeFocus ? control.colors.border.focus : control.colors.border.default
        radius: Metrics.radius
    }
}
