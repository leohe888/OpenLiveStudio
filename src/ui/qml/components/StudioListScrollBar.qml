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
        color: control.pressed ? control.colors.scrollbar.thumb.pressed
            : control.hovered ? control.colors.scrollbar.thumb.hovered : control.colors.scrollbar.thumb.normal
    }
    background: Rectangle { color: control.colors.background.header }
}
