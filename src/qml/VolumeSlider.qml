import QtQuick

// Volume from 0 to 150%. Past the mark at 100% is boost, drawn in the
// warning color. `knob` adds the handle the popovers show.
Item {
    id: root

    property int volume: 0
    property int trackY: 6
    property bool knob: false
    signal moved(int volume)

    readonly property bool boosted: volume > 100
    height: trackY * 2 + 2

    Rectangle {
        y: root.trackY
        width: parent.width
        height: 2
        color: theme.ln
    }
    Rectangle {
        x: parent.width * 2 / 3
        y: root.trackY
        width: parent.width / 3
        height: 2
        color: theme.yl
        opacity: .5
    }
    Rectangle {
        y: root.trackY - 1
        width: parent.width * Math.min(1, root.volume / 150)
        height: 4
        color: root.boosted ? theme.yl : theme.ac
    }
    Rectangle {
        x: parent.width * 2 / 3
        y: root.trackY - 5
        width: 1
        height: 12
        color: theme.mu
    }
    Rectangle {
        visible: root.knob
        x: parent.width * Math.min(1, root.volume / 150) - 3
        y: root.trackY - 6
        width: 6
        height: 14
        color: theme.fg
    }
    SliderArea {
        onMoved: f => root.moved(Math.round(f * 150))
    }
}
