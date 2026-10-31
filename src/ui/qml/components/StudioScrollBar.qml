import QtQuick
import QtQuick.Controls.Basic

ScrollBar {
    readonly property var colors: uiColors
    hoverEnabled: true
    palette.mid: hovered ? colors.scrollbar.thumb.hovered : colors.scrollbar.thumb.normal
    palette.dark: colors.scrollbar.thumb.pressed
    palette.text: colors.text.primary
}
