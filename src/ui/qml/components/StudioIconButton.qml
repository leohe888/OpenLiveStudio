import QtQuick
import QtQuick.Controls.Basic
import "ControlMetrics.js" as Metrics

ToolButton {
    id: control
    property bool outlined: false
    implicitWidth: Metrics.iconButtonSize
    implicitHeight: Metrics.iconButtonSize
    hoverEnabled: true
    display: AbstractButton.IconOnly
    icon.width: Metrics.iconSize
    icon.height: Metrics.iconSize
    icon.color: enabled ? uiColors.icon.primary : uiColors.icon.disabled
    palette.toolTipBase: uiColors.background.header
    palette.toolTipText: uiColors.text.primary
    Accessible.name: text
    ToolTip.text: text
    ToolTip.visible: hovered
    background: Rectangle {
        radius: Metrics.radius
        color: !control.enabled ? (control.outlined ? uiColors.button.background.disabled : "transparent")
            : control.down ? uiColors.button.background.pressed
            : control.hovered ? uiColors.button.background.hovered
            : control.outlined ? uiColors.button.background.normal : "transparent"
        border.color: control.enabled && control.activeFocus ? uiColors.border.focus
            : control.outlined ? uiColors.border.default : "transparent"
    }
}
