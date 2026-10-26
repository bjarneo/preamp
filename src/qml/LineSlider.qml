import QtQuick

// A thin track with a thicker fill, 12 px tall as in the app and width
// sliders.
Item {
    id: root

    property real fraction: 0
    property color fill: theme.ac
    signal moved(real fraction)

    height: 12

    Rectangle {
        y: 5
        width: parent.width
        height: 2
        color: theme.ln
    }
    Rectangle {
        y: 4
        width: parent.width * Math.max(0, Math.min(1, root.fraction))
        height: 4
        color: root.fill
    }
    SliderArea {
        onMoved: f => root.moved(f)
    }
}
