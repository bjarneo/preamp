import QtQuick

// Text as the design sets it: the system monospace font at 12 px, plain
// text only, since names come from devices and apps. The line box is the
// rounded ascent plus the rounded descent, as browsers lay out "normal"
// line height, so rows stack to the same pixel.
Text {
    id: label

    font.family: theme.font
    font.pixelSize: 12
    color: theme.fg
    textFormat: Text.PlainText
    renderType: Text.NativeRendering
    lineHeightMode: Text.FixedHeight
    lineHeight: Math.round(metrics.ascent) + Math.round(metrics.descent)
    // Qt reports the unrounded height when it is taller; stack by the box.
    height: Math.max(1, lineCount) * lineHeight

    FontMetrics {
        id: metrics
        font: label.font
    }
}
