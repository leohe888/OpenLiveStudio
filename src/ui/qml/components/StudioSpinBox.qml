import QtQuick
import QtQuick.Controls.Basic

SpinBox {
    id: control
    readonly property var colors: uiColors
    palette.text: enabled ? colors.text.primary : colors.text.disabled
    palette.buttonText: enabled ? colors.text.primary : colors.text.disabled
    palette.base: colors.background.input
    palette.button: colors.button.background.normal
    palette.mid: colors.border.default
    palette.highlight: colors.background.selected
    palette.highlightedText: colors.text.primary
    background: Rectangle {
        color: control.colors.background.input
        border.color: control.enabled && control.activeFocus ? control.colors.border.focus : control.colors.border.default
    }
}
