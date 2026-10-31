import QtQuick
import QtQuick.Controls.Basic
import "ControlMetrics.js" as Metrics

TextField {
    id: control
    implicitHeight: Math.max(Metrics.controlHeight, contentHeight + topPadding + bottomPadding)
    color: enabled ? uiColors.text.primary : uiColors.text.disabled
    placeholderTextColor: uiColors.text.secondary
    selectedTextColor: uiColors.text.primary
    selectionColor: uiColors.background.selected
    selectByMouse: true
    background: Rectangle {
        color: uiColors.background.input
        border.color: control.enabled && control.activeFocus ? uiColors.border.focus : uiColors.border.default
        radius: Metrics.radius
    }
}
