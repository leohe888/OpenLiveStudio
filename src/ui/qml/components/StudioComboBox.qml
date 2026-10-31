import QtQuick
import QtQuick.Controls.Basic
import "ControlMetrics.js" as Metrics

ComboBox {
    id: control
    readonly property var colors: uiColors
    implicitHeight: Math.max(Metrics.controlHeight, implicitContentHeight + topPadding + bottomPadding)
    hoverEnabled: true
    palette.buttonText: enabled ? colors.text.primary : colors.text.disabled
    palette.text: enabled ? colors.text.primary : colors.text.disabled
    palette.base: colors.background.input
    palette.window: colors.background.input
    palette.highlight: colors.background.selected
    palette.highlightedText: colors.text.primary
    palette.light: colors.background.selected
    palette.midlight: colors.background.hovered
    palette.mid: colors.border.default
    palette.dark: colors.border.focus
    contentItem: Text {
        text: control.displayText
        font: control.font
        leftPadding: control.mirrored ? control.indicator.width + Metrics.inputPadding : Metrics.inputPadding
        rightPadding: control.mirrored ? Metrics.inputPadding : control.indicator.width + Metrics.inputPadding
        color: control.enabled ? control.colors.text.primary : control.colors.text.disabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Canvas {
        readonly property color arrowColor: control.enabled ? control.colors.icon.primary : control.colors.icon.disabled
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
        color: !control.enabled ? control.colors.button.background.disabled
            : control.down ? control.colors.button.background.pressed
            : control.hovered ? control.colors.background.hovered : control.colors.background.input
        border.color: control.enabled && control.activeFocus ? control.colors.border.focus : control.colors.border.default
        radius: Metrics.radius
    }
}
