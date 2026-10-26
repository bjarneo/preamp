import QtQuick

// Turns a press or drag across the parent into a fraction from 0 to 1.
MouseArea {
    signal moved(real fraction)

    anchors.fill: parent
    preventStealing: true
    cursorShape: Qt.PointingHandCursor
    onPressed: mouse => moved(Math.max(0, Math.min(1, mouse.x / width)))
    onPositionChanged: mouse => {
        if (pressed)
            moved(Math.max(0, Math.min(1, mouse.x / width)))
    }
}
