import QtQuick

// The preamp mark: a quiet input block and a louder output block on one
// baseline. Without a tile it reads as a glyph next to text.
Item {
    id: root

    property real size: 18
    property color tile: "transparent"
    property color input: theme.mu
    property color output: theme.ac

    width: size
    height: size

    Rectangle {
        anchors.fill: parent
        color: root.tile
        radius: theme.radius > 0 ? root.size * .22 : 0
        visible: root.tile.a > 0
    }
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
