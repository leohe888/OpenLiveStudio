import QtQuick
import QtQuick.Controls.Basic
import "ControlMetrics.js" as Metrics

Slider {
    id: control
    readonly property var colors: uiColors
    background: Rectangle {
        x: control.horizontal ? control.leftPadding : (control.width - width) / 2
        y: control.horizontal ? (control.height - height) / 2 : control.topPadding
        width: control.horizontal ? control.availableWidth : Metrics.sliderTrack
        height: control.horizontal ? Metrics.sliderTrack : control.availableHeight
        radius: control.horizontal ? Metrics.radius : 0
        color: control.colors.slider.track
    }
    handle: Rectangle {
        x: control.horizontal ? control.leftPadding + control.visualPosition * (control.availableWidth - width)
            : (control.width - width) / 2
        y: control.horizontal ? (control.height - height) / 2
            : control.topPadding + control.visualPosition * (control.availableHeight - height)
        implicitWidth: Metrics.sliderHandle
        implicitHeight: control.horizontal ? Metrics.sliderHandle : Metrics.iconSize
        radius: control.horizontal ? width / 2 : 1
        color: control.enabled ? control.colors.slider.handle : control.colors.slider.handleDisabled
        border.color: control.enabled && control.activeFocus ? control.colors.border.focus : control.colors.border.default
    }
}
