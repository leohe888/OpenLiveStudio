import QtQuick
import QtQuick.Controls.Basic

StudioScrollBar {
    id: control
    implicitWidth: 12
    padding: 2
    minimumSize: Math.min(1, 24 / Math.max(1, height))
    contentItem: Rectangle {
        implicitWidth: 8
        implicitHeight: 24
        radius: 4
        color: control.pressed ? uiColors.scrollbar.thumb.pressed
            : control.hovered ? uiColors.scrollbar.thumb.hovered : uiColors.scrollbar.thumb.normal
    }
    background: Rectangle { color: uiColors.background.header }
}
