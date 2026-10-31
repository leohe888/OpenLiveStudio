import QtQuick
import QtQuick.Controls.Basic

ScrollBar {
    hoverEnabled: true
    palette.mid: hovered ? uiColors.scrollbar.thumb.hovered : uiColors.scrollbar.thumb.normal
    palette.dark: uiColors.scrollbar.thumb.pressed
    palette.text: uiColors.text.primary
}
