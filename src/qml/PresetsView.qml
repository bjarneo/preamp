import QtQuick
import Preamp

// Presets: the everyday view. One card per preset with its curve, then the
// output and the volume. The other views hold the detail.
Item {
    id: root

    required property var app
    readonly property var st: preamp.state
    // The library carries the curves. Until it arrives, the state's names do.
    readonly property var list: preamp.library.length ? preamp.library : (st.presets || [])
    readonly property var names: list.map(p => p.name)
    readonly property int columns: grid.width >= 760 ? 3 : 2
    readonly property int rows: Math.max(1, Math.ceil(list.length / columns))
    readonly property real cardHeight: Math.max(112, Math.min(150, (grid.height - (rows - 1) * cards.rowSpacing) / rows))

    function describe(p) {
        if (p.kind === "builtin") {
            switch (p.name) {
            case "Flat":
                return qsTr("the sound as it is")
            case "Bass boost":
                return qsTr("more low end")
            case "Vocal":
                return qsTr("clearer voices")
            case "Loudness":
                return qsTr("fuller sound at low volume")
            case "Night":
                return qsTr("softer lows and highs for late hours")
            case "Podcast":
                return qsTr("clear speech, less rumble")
            case "Treble boost":
                return qsTr("more detail and air")
            case "Bass cut":
                return qsTr("less boom from small speakers")
            case "Warm":
                return qsTr("rich lows, smooth highs")
            case "Rock":
                return qsTr("punchy drums, bright guitars")
            case "Movie":
                return qsTr("deep effects, clear dialog")
            case "Gaming":
                return qsTr("footsteps and cues stand out")
            }
        }
        return p.kind === "autoeq" ? qsTr("headphone correction") : qsTr("your own preset")
    }

    // Keeps the card in use in sight when the keys move past the edge.
    function reveal(card) {
        if (card.y < grid.contentY)
            grid.contentY = card.y
        else if (card.y + card.height > grid.contentY + grid.height)
            grid.contentY = card.y + card.height - grid.height
    }

    Header {
        id: header
        width: parent.width

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            Brand {
                anchors.verticalCenter: parent.verticalCenter
            }
            Txt {
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 13
                color: theme.mu
                text: "/"
            }
            Txt {
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 13
                text: qsTr("presets")
            }
        }
        BypassButton {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Txt {
        id: heading
        x: 16
        y: header.height + 16
        font.pixelSize: 11
        color: theme.mu
        text: qsTr("pick a sound")
    }

    Flickable {
        id: grid
        x: 16
        y: heading.y + heading.height + 10
        width: parent.width - 32
        height: rule.y - y - 16
        contentHeight: cards.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Grid {
            id: cards
            width: parent.width
            columns: root.columns
            columnSpacing: 12
            rowSpacing: 12

            Repeater {
                model: root.list

                Rectangle {
                    id: card
                    required property var modelData
                    readonly property bool on: modelData.name === root.st.preset
                    readonly property int slot: root.app.slots.indexOf(modelData.name)
                    width: Math.floor((cards.width - (root.columns - 1) * cards.columnSpacing) / root.columns)
                    height: Math.floor(root.cardHeight)
                    radius: theme.radius
                    color: on ? theme.hl : "transparent"
                    border.width: 1
                    border.color: on ? theme.ac : area.containsMouse ? theme.mu : theme.ln

                    onOnChanged: if (on) root.reveal(card)

                    Txt {
                        x: 14
                        y: 13
                        color: theme.mu
                        text: card.slot >= 0 ? card.slot + 1 : ""
                    }
                    Txt {
                        id: title
                        x: 31
                        y: 12
                        width: parent.width - x - 14 - (mark.implicitWidth > 0 ? mark.implicitWidth + 10 : 0)
                        elide: Text.ElideRight
                        font.pixelSize: 13
                        text: card.modelData.name
                    }
                    Txt {
                        id: mark
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        y: 13
                        color: root.app.bypass ? theme.yl : theme.ac
                        text: card.on ? (root.app.bypass ? qsTr("bypassed") : root.st.dirty ? qsTr("edited") : "●") : ""
                    }
                    Txt {
                        x: title.x
                        y: title.y + title.height + 4
                        width: parent.width - x - 14
                        elide: Text.ElideRight
                        font.pixelSize: 11
                        color: theme.mu
                        text: root.describe(card.modelData)
                    }
                    Graph {
                        x: 14
                        y: 60
                        width: parent.width - 28
                        height: parent.height - y - 14
                        look: "mini"
                        bands: card.modelData.L || []
                        bypass: card.on && root.app.bypass
                        accent: card.on ? theme.ac : theme.mu
                        muted: theme.mu
                        line: theme.ln
                    }
                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: preamp.send({cmd: "applyPreset", name: card.modelData.name})
                    }
                }
            }
        }
    }

    Rectangle {
        id: rule
        y: controls.y - 16
        width: parent.width
        height: 1
        color: theme.ln
    }

    // Where the sound goes, and how loud.
    Item {
        id: controls
        x: 16
        y: footer.y - height - 16
        width: parent.width - 32
        height: Math.max(outputBox.implicitHeight, volumeBox.implicitHeight)

        Column {
            id: outputBox
            width: Math.round((parent.width - 32) * .62)
            spacing: 8

            Txt {
                font.pixelSize: 11
                color: theme.mu
                text: qsTr("output")
            }
            Flow {
                width: parent.width
                spacing: 8

                Repeater {
                    model: root.st.outputs || []

                    Rectangle {
                        id: chip
                        required property var modelData
                        readonly property bool on: modelData.name === root.st.output
                        width: chipLabel.implicitWidth + 22
                        height: 26
                        radius: theme.radius
                        color: on ? theme.hl : "transparent"
                        border.width: 1
                        border.color: on ? theme.ac : chipArea.containsMouse ? theme.mu : theme.ln

                        Txt {
                            id: chipLabel
                            x: 11
                            y: Math.round((parent.height - height) / 2)
                            color: chip.on ? theme.fg : theme.mu
                            text: chip.modelData.label
                        }
                        MouseArea {
                            id: chipArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: preamp.send({cmd: "setOutput", name: chip.modelData.name})
                        }
                    }
                }
            }
        }

        Column {
            id: volumeBox
            x: outputBox.width + 32
            width: parent.width - x
            spacing: 8

            Item {
                width: parent.width
                height: volumeValue.height
                Txt {
                    y: volumeValue.baselineOffset - baselineOffset
                    font.pixelSize: 11
                    color: theme.mu
                    text: qsTr("volume")
                }
                Txt {
                    id: volumeValue
                    anchors.right: parent.right
                    font.pixelSize: 18
                    color: (root.st.volume || 0) > 100 ? theme.yl : theme.fg
                    text: (root.st.volume || 0) + "%"
                }
            }
            VolumeSlider {
                width: parent.width
                knob: true
                volume: root.st.volume || 0
                onMoved: v => preamp.send({cmd: "setVolume", v: v})
            }
        }
    }

    Footer {
        id: footer
        y: parent.height - height
        width: parent.width
        hints: [["h l", qsTr("preset")], ["- +", qsTr("volume")], ["b", qsTr("bypass")], ["tab", qsTr("more views")]]
        rightText: qsTr("click a preset to use it")
    }
}
