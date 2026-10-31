import QtQuick
import QtQuick.Controls.Basic
import "ControlMetrics.js" as Metrics

Button {
    id: control
    implicitWidth: Math.max(Metrics.buttonWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(Metrics.controlHeight, implicitContentHeight + topPadding + bottomPadding)
    hoverEnabled: true
    icon.width: Metrics.iconSize
    icon.height: Metrics.iconSize
    icon.color: enabled ? uiColors.icon.primary : uiColors.icon.disabled
    palette.buttonText: enabled ? uiColors.text.primary : uiColors.text.disabled
    Accessible.name: text
    background: Rectangle {
        radius: Metrics.radius
        color: !control.enabled ? uiColors.button.background.disabled
            : control.down ? uiColors.button.background.pressed
            : control.hovered ? uiColors.button.background.hovered : uiColors.button.background.normal
        border.color: control.enabled && control.activeFocus ? uiColors.border.focus : uiColors.border.default
    }
}
