import QtQuick
import QtQuick.Controls.Basic
import "ControlMetrics.js" as Metrics

ComboBox {
    id: control
    implicitHeight: Math.max(Metrics.controlHeight, implicitContentHeight + topPadding + bottomPadding)
    hoverEnabled: true
    palette.buttonText: enabled ? uiColors.text.primary : uiColors.text.disabled
    palette.text: enabled ? uiColors.text.primary : uiColors.text.disabled
    palette.base: uiColors.background.input
    palette.window: uiColors.background.input
    palette.highlight: uiColors.background.selected
    palette.highlightedText: uiColors.text.primary
    palette.light: uiColors.background.selected
    palette.midlight: uiColors.background.hovered
    palette.mid: uiColors.border.default
    palette.dark: uiColors.border.focus
    contentItem: Text {
        text: control.displayText
        font: control.font
        leftPadding: control.mirrored ? control.indicator.width + Metrics.inputPadding : Metrics.inputPadding
        rightPadding: control.mirrored ? Metrics.inputPadding : control.indicator.width + Metrics.inputPadding
        color: control.enabled ? uiColors.text.primary : uiColors.text.disabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Canvas {
        readonly property color arrowColor: control.enabled ? uiColors.icon.primary : uiColors.icon.disabled
        width: 10
        height: 6
        x: control.mirrored ? 10 : control.width - width - 10
        y: (control.height - height) / 2
        onArrowColorChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = arrowColor
            ctx.beginPath()
            ctx.moveTo(0, 0)
            ctx.lineTo(width, 0)
            ctx.lineTo(width / 2, height)
            ctx.closePath()
            ctx.fill()
        }
    }
    background: Rectangle {
        color: !control.enabled ? uiColors.button.background.disabled
            : control.down ? uiColors.button.background.pressed
            : control.hovered ? uiColors.background.hovered : uiColors.background.input
        border.color: control.enabled && control.activeFocus ? uiColors.border.focus : uiColors.border.default
        radius: Metrics.radius
    }
}
