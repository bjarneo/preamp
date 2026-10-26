import QtQuick

// Volume from 0 to 150%, with a handle. Past the mark at 100% is boost, in
// the warning color.
Item {
  id: root

  required property var pal
  property int volume: 0
  signal moved(int volume)

  readonly property real at: width * Math.min(1, volume / 150)
  height: 16

  Rectangle {
    y: 7
    width: parent.width
    height: 2
    color: root.pal.ln
  }
  Rectangle {
    x: parent.width * 2 / 3
    y: 7
    width: parent.width / 3
    height: 2
    color: root.pal.yl
    opacity: .5
  }
  Rectangle {
    y: 6
    width: root.at
    height: 4
    color: root.volume > 100 ? root.pal.yl : root.pal.ac
  }
  Rectangle {
    x: parent.width * 2 / 3
    y: 2
    width: 1
    height: 12
    color: root.pal.mu
  }
  Rectangle {
    x: root.at - 3
    y: 1
    width: 6
    height: 14
    color: root.pal.fg
  }
  MouseArea {
    anchors.fill: parent
    preventStealing: true
    cursorShape: Qt.PointingHandCursor
    onPressed: function(mouse) { root.moved(Math.round(Math.max(0, Math.min(1, mouse.x / width)) * 150)) }
    onPositionChanged: function(mouse) { if (pressed) root.moved(Math.round(Math.max(0, Math.min(1, mouse.x / width)) * 150)) }
  }
}
