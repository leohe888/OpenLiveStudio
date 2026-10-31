import QtQuick
import QtQuick.Controls.Basic

ItemDelegate {
    id: control
    palette.text: enabled ? uiColors.text.primary : uiColors.text.disabled
    palette.highlightedText: enabled ? uiColors.text.primary : uiColors.text.disabled
    background: Rectangle {
        color: control.highlighted ? uiColors.background.selected
            : control.hovered ? uiColors.background.hovered : "transparent"
        border.color: control.enabled && control.activeFocus ? uiColors.border.focus : "transparent"
    }
}
