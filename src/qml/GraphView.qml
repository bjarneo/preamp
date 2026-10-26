import QtQuick
import Preamp
import "Format.js" as F

// The response graph with its scale, band labels and draggable nodes. Drag
// a node: across sets the frequency, up and down the gain.
Item {
    id: root

    property string look: "studio"
    property var bands: []
    property var other: []
    property int selected: 0
    property bool bypass: false
    property bool spectrum: false
    signal select(int index)

    // While a node is dragged it follows the pointer at once, and the
    // daemon's answer takes over when it arrives.
    property int dragIndex: -1
    property var dragValue: null
    readonly property var shown: {
        if (dragIndex < 0 || !dragValue)
            return bands
        return bands.map((b, i) => i === dragIndex ? Object.assign({}, b, dragValue) : b)
    }

    Graph {
        anchors.fill: parent
        look: root.look
        bands: root.shown
        otherBands: root.other
        selected: root.selected
        bypass: root.bypass
        spectrum: root.spectrum ? preamp.spectrum : []
        showSpectrum: root.spectrum
        accent: theme.ac
        accent2: theme.a2
        muted: theme.mu
        line: theme.ln
        text: theme.fg
    }

    Repeater {
        model: [12, 6, 0, -6, -12]

        Txt {
            required property int modelData
            x: root.look === "studio" ? 6 : 0
            y: root.height / 2 - modelData * root.height / 30 - 14
            font.pixelSize: 10
            color: theme.mu
            text: modelData > 0 ? "+" + modelData : modelData < 0 ? "−" + (-modelData) : "0 dB"
        }
    }

    Repeater {
        model: [50, 100, 200, 500, 1000, 2000, 5000, 10000]

        Txt {
            required property int modelData
            x: F.lx(modelData) * root.width - width / 2
            y: root.look === "studio" ? root.height - 6 - height : root.height + 2 - height
            font.pixelSize: 10
            color: theme.mu
            text: F.freqShort(modelData)
        }
    }

    Repeater {
        model: root.shown.length

        Item {
            id: node
            required property int index
            readonly property var band: root.shown[index] || ({f: 1000, g: 0})
            readonly property bool on: index === root.selected
            readonly property real px: F.lx(band.f) * root.width
            readonly property real py: root.height / 2 - band.g * root.height / 30
            readonly property real size: on ? 14 : 10

            Txt {
                x: node.px - width / 2
                y: node.band.g >= 0 ? node.py - 26 : node.py + 12
                font.pixelSize: 10
                color: node.on ? theme.fg : theme.mu
                text: F.two(node.index)
            }

            // Snapped to whole pixels like a browser box, so square nodes
            // stay crisp; rounded ones keep their smoothing.
            Rectangle {
                x: Math.round(node.px - node.size / 2)
                y: Math.round(node.py - node.size / 2)
                width: node.size
                height: node.size
                radius: Math.min(theme.radius, node.size / 2)
                color: node.on ? theme.ac : theme.bg
                border.width: 2
                border.color: theme.ac
                antialiasing: radius > 0

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    preventStealing: true
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    onPressed: {
                        root.select(node.index)
                        root.dragIndex = node.index
                        root.dragValue = null
                    }
                    onPositionChanged: mouse => {
                        if (root.dragIndex !== node.index)
                            return
                        const p = mapToItem(root, mouse.x, mouse.y)
                        const x = F.clamp(p.x / root.width, 0, 1)
                        const y = F.clamp(p.y / root.height, 0, 1)
                        const f = F.clamp(Math.round(F.fromX(x)), 20, 20000)
                        const g = F.clamp(Math.round((0.5 - y) * 300) / 10, -12, 12)
                        root.dragValue = {f: f, g: g}
                        preamp.send({cmd: "setBand", i: node.index, f: f, g: g})
                    }
                    onReleased: root.dragIndex = -1
                    onCanceled: root.dragIndex = -1
                    onDoubleClicked: preamp.send({cmd: "setBand", i: node.index, g: 0})
                }
            }
        }
    }
}
