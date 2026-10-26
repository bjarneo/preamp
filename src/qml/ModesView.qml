import QtQuick
import "Format.js" as F

// Modes: basic with bass, mid and treble, graphic with ten meters, and
// parametric with the curve of every band. The command bar is at the bottom.
Item {
    id: root

    required property var app
    readonly property var st: preamp.state
    readonly property var bands: app.bands
    readonly property int sel: app.sel
    readonly property string mode: app.mode
    property alias commandBar: command
    signal commandFinished()

    Header {
        id: header
        width: parent.width

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 18

            Brand {
                anchors.verticalCenter: parent.verticalCenter
            }
            Seg {
                anchors.verticalCenter: parent.verticalCenter
                boxed: false
                gap: 2
                labels: [qsTr("basic"), qsTr("graphic"), qsTr("parametric")]
                current: ["basic", "graphic", "parametric"].indexOf(root.mode)
                onColor: theme.hl
                onText: theme.fg
                onPicked: index => root.app.mode = ["basic", "graphic", "parametric"][index]
            }
            Txt {
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 11
                color: theme.mu
                text: qsTr("m to cycle")
            }
        }

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14

            Txt {
                anchors.verticalCenter: parent.verticalCenter
                text: root.app.presetLabel
            }
            Seg {
                anchors.verticalCenter: parent.verticalCenter
                labels: ["L+R", "L", "R"]
                current: root.app.channelIndex
                onPicked: index => root.app.setChannel(index)
            }
        }
    }

    Item {
        id: body
        x: 16
        y: header.height + 16
        width: parent.width - 32
        height: parent.height - header.height - command.height - footer.height - 32

        // ------------------------------------------------------- graphic
        Row {
            id: graphic
            visible: root.mode === "graphic"
            width: parent.width
            height: body.height - 14 - 12
            spacing: 4

            Repeater {
                model: root.bands.length

                Item {
                    id: column
                    required property int index
                    readonly property var band: root.bands[index]
                    readonly property bool on: index === root.sel
                    readonly property var channels: root.st.link ? [[root.app.bandsOf("L"), ""]]
                                                                 : [[root.app.bandsOf("L"), "L"], [root.app.bandsOf("R"), "R"]]
                    width: (graphic.width - 36) / 10
                    height: graphic.height

                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.width: 1
                        border.color: column.on ? theme.ac : "transparent"
                    }

                    Column {
                        anchors.centerIn: parent
                        spacing: 10

                        Txt {
                            anchors.horizontalCenter: parent.horizontalCenter
                            font.pixelSize: column.on ? 18 : 13
                            color: column.on ? theme.fg : theme.mu
                            text: F.gain(column.band.g)
                        }
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: 4

                            Repeater {
                                model: column.channels

                                Column {
                                    id: meter
                                    required property var modelData
                                    readonly property real g: (modelData[0][column.index] || {g: 0}).g
                                    readonly property int w: root.st.link ? 40 : 18
                                    spacing: 3

                                    Repeater {
                                        model: 12
                                        Rectangle {
                                            required property int index
                                            readonly property int k: 12 - index
                                            width: meter.w
                                            height: 10
                                            color: meter.g >= k - .5
                                                   ? (root.app.bypass ? theme.mu : k <= 5 ? theme.gn : k <= 9 ? theme.yl : theme.rd)
                                                   : theme.hl
                                        }
                                    }
                                    Item {
                                        width: meter.w
                                        height: 6
                                        Rectangle {
                                            y: 2
                                            width: parent.width
                                            height: 2
                                            color: theme.mu
                                        }
                                    }
                                    Repeater {
                                        model: 12
                                        Rectangle {
                                            required property int index
                                            readonly property int k: index + 1
                                            width: meter.w
                                            height: 10
                                            color: meter.g <= -(k - .5) ? (root.app.bypass ? theme.mu : theme.a2) : theme.hl
                                        }
                                    }
                                    // Empty when the channels are linked. A fixed width keeps it
                                    // in the column, which skips items of no size.
                                    Txt {
                                        width: meter.w
                                        horizontalAlignment: Text.AlignHCenter
                                        height: 12
                                        font.pixelSize: 10
                                        color: theme.mu
                                        text: meter.modelData[1]
                                    }
                                }
                            }
                        }
                        Txt {
                            anchors.horizontalCenter: parent.horizontalCenter
                            color: column.on ? theme.fg : theme.mu
                            text: F.freqShort(column.band.f)
                        }
                    }

                    // A click picks the band. A drag up or down sets its gain.
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        property real startY: 0
                        property real startGain: 0
                        property bool dragging: false
                        onPressed: mouse => {
                            root.app.sel = column.index
                            startY = mouse.y
                            startGain = column.band.g
                            dragging = false
                        }
                        onPositionChanged: mouse => {
                            if (!dragging && Math.abs(mouse.y - startY) < 4)
                                return
                            dragging = true
                            const g = F.clamp(Math.round((startGain + (startY - mouse.y) / 13) * 2) / 2, -12, 12)
                            if (g !== column.band.g)
                                preamp.send({cmd: "setBand", i: column.index, g: g})
                        }
                        onWheel: wheel => root.app.nudgeGain(column.index, wheel.angleDelta.y > 0 ? .5 : -.5)
                    }
                }
            }
        }

        // --------------------------------------------------------- basic
        Item {
            id: basic
            visible: root.mode === "basic"
            width: parent.width
            height: body.height - 14 - 12

            Column {
                x: 48
                width: parent.width - 96
                anchors.verticalCenter: parent.verticalCenter
                spacing: 40

                Repeater {
                    model: 3

                    Item {
                        id: macroRow
                        required property int index
                        readonly property real v: Math.round((root.st.macros ? root.st.macros[index] : 0) * 10) / 10
                        width: parent.width
                        height: 48

                        Txt {
                            anchors.verticalCenter: parent.verticalCenter
                            font.pixelSize: 16
                            text: [qsTr("bass"), qsTr("mid"), qsTr("treble")][macroRow.index]
                        }
                        Row {
                            id: cells
                            x: 110 + 28
                            width: parent.width - 110 - 150 - 56
                            height: 30
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3

                            Repeater {
                                model: 48
                                Rectangle {
                                    required property int index
                                    readonly property real c: -12 + index * .5 + .25
                                    readonly property bool lit: macroRow.v > 0 ? (c > 0 && c < macroRow.v + .01)
                                                                              : (c < 0 && c > macroRow.v - .01)
                                    width: (cells.width - 47 * 3) / 48
                                    height: 30
                                    color: lit ? (root.app.bypass ? theme.mu : theme.ac)
                                               : (index === 23 || index === 24 ? theme.ln : theme.hl)
                                }
                            }
                            MouseArea {
                                width: cells.width
                                height: cells.height
                                preventStealing: true
                                cursorShape: Qt.SizeHorCursor
                                function set(mouse) {
                                    const u = F.clamp(mouse.x / width, 0, 1)
                                    preamp.send({cmd: "setMacro", k: macroRow.index, v: Math.round((u * 24 - 12) * 2) / 2})
                                }
                                onPressed: mouse => set(mouse)
                                onPositionChanged: mouse => set(mouse)
                            }
                        }
                        Txt {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            font.pixelSize: 36
                            text: F.gain(macroRow.v)
                        }
                    }
                }

                Item {
                    width: parent.width
                    height: 14
                    Item {
                        x: 110 + 28
                        width: parent.width - 110 - 150 - 56
                        height: parent.height
                        Txt {
                            font.pixelSize: 11
                            color: theme.mu
                            text: "−12 dB"
                        }
                        Txt {
                            anchors.horizontalCenter: parent.horizontalCenter
                            font.pixelSize: 11
                            color: theme.mu
                            text: "0"
                        }
                        Txt {
                            anchors.right: parent.right
                            font.pixelSize: 11
                            color: theme.mu
                            text: "+12 dB"
                        }
                    }
                }
            }
        }

        // ---------------------------------------------------- parametric
        GraphView {
            id: param
            visible: root.mode === "parametric"
            width: parent.width
            height: 420 + Math.max(0, root.height - 650)
            look: "param"
            bands: root.bands
            other: root.app.otherBands
            selected: root.sel
            bypass: root.app.bypass
            onSelect: index => root.app.sel = index
        }

        Item {
            id: info
            // Basic and graphic fill the space above it. The curve keeps its
            // size, and the line follows right under it.
            y: root.mode === "parametric" ? param.height + 12 : parent.height - height
            width: parent.width
            height: 14

            Row {
                Txt {
                    font.pixelSize: 11
                    color: theme.mu
                    text: qsTr("band") + " "
                }
                Txt {
                    font.pixelSize: 11
                    text: root.app.selInfo
                }
            }
            Row {
                anchors.right: parent.right
                Txt {
                    font.pixelSize: 11
                    color: theme.mu
                    text: qsTr("preamp") + " "
                }
                Txt {
                    font.pixelSize: 11
                    text: root.app.preampText
                }
            }
        }
    }

    CommandBar {
        id: command
        y: footer.y - height
        width: parent.width
        app: root.app
        onFinished: root.commandFinished()
    }

    Footer {
        id: footer
        y: parent.height - height
        width: parent.width
        hints: [["h l", qsTr("band")], ["j k", qsTr("gain")], ["m", qsTr("mode")], ["c", qsTr("channel")],
                [":", qsTr("command")], ["b", qsTr("bypass")]]
        rightText: root.app.bypass ? qsTr("eq bypassed") : qsTr("eq active")
    }
}
