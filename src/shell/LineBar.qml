import QtQuick

// A thin track with a thicker fill, 14 px tall, for app volume.
Item {
  id: root

  required property var pal
  property real fraction: 0
  property color fill: pal.ac
  signal moved(real fraction)

  height: 14

  Rectangle {
    y: 6
    width: parent.width
    height: 2
    color: root.pal.ln
  }
  Rectangle {
    y: 5
    width: parent.width * Math.max(0, Math.min(1, root.fraction))
    height: 4
    color: root.fill
  }
  MouseArea {
    anchors.fill: parent
    preventStealing: true
    cursorShape: Qt.PointingHandCursor
    onPressed: function(mouse) { root.moved(Math.max(0, Math.min(1, mouse.x / width))) }
    onPositionChanged: function(mouse) { if (pressed) root.moved(Math.max(0, Math.min(1, mouse.x / width))) }
  }
}
