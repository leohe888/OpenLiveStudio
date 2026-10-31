import QtQuick
import QtQuick.Controls.Basic

CheckBox {
    readonly property var colors: uiColors
    palette.windowText: enabled ? colors.text.primary : colors.text.disabled
    palette.text: enabled ? colors.text.primary : colors.text.disabled
    palette.base: colors.background.input
    palette.button: colors.button.background.normal
    palette.highlight: colors.border.focus
    palette.light: colors.border.default
    palette.mid: colors.border.default
}
