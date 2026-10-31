import QtQuick
import QtQuick.Controls.Basic

SpinBox {
    id: control
    palette.text: enabled ? uiColors.text.primary : uiColors.text.disabled
    palette.buttonText: enabled ? uiColors.text.primary : uiColors.text.disabled
    palette.base: uiColors.background.input
    palette.button: uiColors.button.background.normal
    palette.mid: uiColors.border.default
    palette.highlight: uiColors.background.selected
    palette.highlightedText: uiColors.text.primary
    background: Rectangle {
        color: uiColors.background.input
        border.color: control.enabled && control.activeFocus ? uiColors.border.focus : uiColors.border.default
    }
}
