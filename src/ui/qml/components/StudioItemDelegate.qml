import QtQuick
import QtQuick.Controls.Basic

ItemDelegate {
    id: control
    readonly property var colors: uiColors
    palette.text: enabled ? colors.text.primary : colors.text.disabled
    palette.highlightedText: enabled ? colors.text.primary : colors.text.disabled
    background: Rectangle {
        color: control.highlighted ? control.colors.background.selected
            : control.hovered ? control.colors.background.hovered : "transparent"
        border.color: control.enabled && control.activeFocus ? control.colors.border.focus : "transparent"
    }
}
