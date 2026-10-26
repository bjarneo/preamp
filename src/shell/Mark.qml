import QtQuick

// The preamp mark as a glyph: a quiet input block and a louder output
// block on one baseline.
Item {
  id: root

  property real size: 14
  property color input: "#737aa2"
  property color output: "#7aa2f7"

  width: size
  height: size

  Rectangle {
    x: root.size * .16
    y: root.size * .57
    width: root.size * .28
    height: root.size * .25
    color: root.input
    antialiasing: true
  }
  Rectangle {
    x: root.size * .56
    y: root.size * .18
    width: root.size * .28
    height: root.size * .64
    color: root.output
    antialiasing: true
  }
}
