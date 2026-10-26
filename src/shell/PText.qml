import QtQuick
import qs.Commons

// Plain text only: names here come from devices and apps. Lines stack on
// the rounded ascent plus descent, as the preamp window lays them out.
Text {
  id: label

  textFormat: Text.PlainText
  font.family: Style.font.family
  font.pixelSize: 12
  renderType: Text.NativeRendering
  lineHeightMode: Text.FixedHeight
  lineHeight: Math.round(metrics.ascent) + Math.round(metrics.descent)
  height: Math.max(1, lineCount) * lineHeight

  FontMetrics {
    id: metrics
    font: label.font
  }
}
