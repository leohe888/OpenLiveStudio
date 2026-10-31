import QtQuick
import QtQuick.Controls.Basic
import "ControlMetrics.js" as Metrics

ToolButton {
    id: control
    readonly property var colors: uiColors
    property bool outlined: false
    implicitWidth: Metrics.iconButtonSize
    implicitHeight: Metrics.iconButtonSize
    hoverEnabled: true
    display: AbstractButton.IconOnly
    icon.width: Metrics.iconSize
    icon.height: Metrics.iconSize
    icon.color: enabled ? colors.icon.primary : colors.icon.disabled
    palette.toolTipBase: colors.background.header
    palette.toolTipText: colors.text.primary
    Accessible.name: text
    ToolTip.text: text
    ToolTip.visible: hovered
    background: Rectangle {
        radius: Metrics.radius
        color: !control.enabled ? (control.outlined ? control.colors.button.background.disabled : "transparent")
            : control.down ? control.colors.button.background.pressed
            : control.hovered ? control.colors.button.background.hovered
            : control.outlined ? control.colors.button.background.normal : "transparent"
        border.color: control.enabled && control.activeFocus ? control.colors.border.focus
            : control.outlined ? control.colors.border.default : "transparent"
    }
}
