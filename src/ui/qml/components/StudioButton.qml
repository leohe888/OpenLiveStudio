import QtQuick
import QtQuick.Controls.Basic
import "ControlMetrics.js" as Metrics

Button {
    id: control
    readonly property var colors: uiColors
    implicitWidth: Math.max(Metrics.buttonWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(Metrics.controlHeight, implicitContentHeight + topPadding + bottomPadding)
    hoverEnabled: true
    icon.width: Metrics.iconSize
    icon.height: Metrics.iconSize
    icon.color: enabled ? colors.icon.primary : colors.icon.disabled
    palette.buttonText: enabled ? colors.text.primary : colors.text.disabled
    Accessible.name: text
    background: Rectangle {
        radius: Metrics.radius
        color: !control.enabled ? control.colors.button.background.disabled
            : control.down ? control.colors.button.background.pressed
            : control.hovered ? control.colors.button.background.hovered : control.colors.button.background.normal
        border.color: control.enabled && control.activeFocus ? control.colors.border.focus : control.colors.border.default
    }
}
