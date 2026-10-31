import QtQuick
import QtQuick.Controls.Basic
import "ControlMetrics.js" as Metrics

TextField {
    id: control
    readonly property var colors: uiColors
    implicitHeight: Math.max(Metrics.controlHeight, contentHeight + topPadding + bottomPadding)
    color: enabled ? colors.text.primary : colors.text.disabled
    placeholderTextColor: colors.text.secondary
    selectedTextColor: colors.text.primary
    selectionColor: colors.background.selected
    selectByMouse: true
    background: Rectangle {
        color: control.colors.background.input
        border.color: control.enabled && control.activeFocus ? control.colors.border.focus : control.colors.border.default
        radius: Metrics.radius
    }
}
