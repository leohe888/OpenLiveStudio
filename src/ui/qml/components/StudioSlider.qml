import QtQuick
import QtQuick.Controls.Basic
import "ControlMetrics.js" as Metrics

Slider {
    id: control
    background: Rectangle {
        x: control.horizontal ? control.leftPadding : (control.width - width) / 2
        y: control.horizontal ? (control.height - height) / 2 : control.topPadding
        width: control.horizontal ? control.availableWidth : Metrics.sliderTrack
        height: control.horizontal ? Metrics.sliderTrack : control.availableHeight
        radius: control.horizontal ? Metrics.radius : 0
        color: uiColors.slider.track
    }
    handle: Rectangle {
        x: control.horizontal ? control.leftPadding + control.visualPosition * (control.availableWidth - width)
            : (control.width - width) / 2
        y: control.horizontal ? (control.height - height) / 2
            : control.topPadding + control.visualPosition * (control.availableHeight - height)
        implicitWidth: Metrics.sliderHandle
        implicitHeight: control.horizontal ? Metrics.sliderHandle : Metrics.iconSize
        radius: control.horizontal ? width / 2 : 1
        color: control.enabled ? uiColors.slider.handle : uiColors.slider.handleDisabled
        border.color: control.enabled && control.activeFocus ? uiColors.border.focus : uiColors.border.default
    }
}
