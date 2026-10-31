import QtQuick
import QtQuick.Controls.Basic

RadioButton {
    palette.windowText: enabled ? uiColors.text.primary : uiColors.text.disabled
    palette.text: enabled ? uiColors.text.primary : uiColors.text.disabled
    palette.base: uiColors.background.input
    palette.button: uiColors.button.background.normal
    palette.highlight: uiColors.border.focus
    palette.light: uiColors.border.default
    palette.mid: uiColors.border.default
    palette.dark: uiColors.border.focus
}
