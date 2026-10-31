import QtQuick
import QtQuick.Controls.Basic
import "ControlMetrics.js" as Metrics

TabButton {
    id: control
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? uiColors.text.primary : uiColors.text.disabled
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        color: control.checked ? uiColors.background.selected : uiColors.background.header
        border.color: control.enabled && control.activeFocus ? uiColors.border.focus : uiColors.border.default
        radius: Metrics.radius
    }
}
