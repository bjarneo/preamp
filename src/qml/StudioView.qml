import QtQuick
import "Format.js" as F

// Studio: the parametric curve over a live analyzer, the ten bands under
// it, and presets, auto-switch and the effects chain in a sidebar.
Item {
    id: root

    required property var app
    readonly property var st: preamp.state
    readonly property var bands: app.bands
    readonly property int sel: app.sel

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
                text: "/"
                font.pixelSize: 13
                color: theme.mu
            }
            Txt {
                anchors.verticalCenter: parent.verticalCenter
                text: root.app.presetLabel
                font.pixelSize: 13
            }
        }

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14

            Seg {
                anchors.verticalCenter: parent.verticalCenter
                labels: ["L+R", "L", "R"]
                current: root.app.channelIndex
                onPicked: index => root.app.setChannel(index)
            }
            Row {
                anchors.verticalCenter: parent.verticalCenter
                Txt {
                    text: "→ "
                    color: theme.mu
                }
                Txt {
                    text: root.app.outputLabel
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, 260)
                }
                Txt {
                    text: " · " + root.app.outputMeta
                    color: theme.mu
                }
            }
            BypassButton {
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // ---------------------------------------------------------------- left
    Item {
        id: left
        y: header.height
        width: parent.width - 280
        height: parent.height - header.height - footer.height

        Item {
            id: legend
            x: 16
            y: 16
            width: parent.width - 32
            height: 14

            Txt {
                font.pixelSize: 11
                color: theme.mu
                text: qsTr("response · 20 Hz – 20 kHz")
            }
            Row {
                anchors.right: parent.right
                spacing: 16

                Row {
                    spacing: 6
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 14
                        height: 2
                        color: theme.ac
                    }
                    Txt {
                        font.pixelSize: 11
                        color: theme.mu
                        text: qsTr("total %1").arg(root.app.channelLabel)
                    }
                }
                Row {
                    spacing: 6
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 10
                        height: 10
                        color: theme.a2
                        opacity: .4
                    }
                    Txt {
                        font.pixelSize: 11
                        color: theme.mu
                        text: qsTr("band %1").arg(F.two(root.sel))
                    }
                }
                Row {
                    spacing: 6
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 10
                        height: 10
                        color: theme.a2
                        opacity: .18
                    }
                    Txt {
                        font.pixelSize: 11
                        color: theme.mu
                        text: qsTr("analyzer, post-eq")
                    }
                }
            }
        }

        Rectangle {
            id: graphBox
            x: 16
            y: Math.round(legend.y + legend.height + 12)
            width: parent.width - 32
            // The design's 360 at the default size. A taller window gives
            // the curve the extra room.
            height: 360 + Math.max(0, root.height - 650)
            color: theme.bd

            GraphView {
                anchors.fill: parent
                look: "studio"
                bands: root.bands
                other: root.app.otherBands
                selected: root.sel
                bypass: root.app.bypass
                spectrum: root.visible
                onSelect: index => root.app.sel = index
            }
        }

        // The ten bands, one column each.
        Rectangle {
            x: 16
            y: Math.round(legend.y + legend.height + 12 + graphBox.height + 12)
            width: graphBox.width
            height: 92
            color: "transparent"
            border.color: theme.ln
            border.width: 1

            Row {
                x: 1
                y: 1
                Repeater {
                    model: root.bands.length

                    Rectangle {
                        id: cell
                        required property int index
                        readonly property var band: root.bands[index]
                        readonly property bool on: index === root.sel
                        width: (graphBox.width - 2) / 10
                        height: 90
                        color: on ? theme.hl : "transparent"

                        Rectangle {
                            width: parent.width
                            height: 2
                            color: theme.ac
                            visible: cell.on
                        }
                        Column {
                            x: 8
                            y: 9
                            width: parent.width - 16
                            spacing: 4

                            Txt {
                                font.pixelSize: 11
                                color: theme.mu
                                text: F.two(cell.index) + " " + root.app.typeLabel(cell.band)
                            }
                            Txt {
                                font.pixelSize: 13
                                text: F.freqLong(cell.band.f)
                            }
                            Txt {
                                font.pixelSize: 11
                                color: cell.band.g > 0.04 ? theme.fg : cell.band.g < -0.04 ? theme.a2 : theme.mu
                                text: F.gain(cell.band.g) + " dB"
                            }
                            Txt {
                                font.pixelSize: 11
                                color: theme.mu
                                text: "q " + cell.band.q.toFixed(2)
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.app.sel = cell.index
                            onWheel: wheel => root.app.nudgeGain(cell.index, wheel.angleDelta.y > 0 ? .5 : -.5)
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------- sidebar
    Item {
        id: side
        x: parent.width - 280
        y: header.height
        width: 280
        height: left.height

        Rectangle {
            width: 1
            height: parent.height
            color: theme.ln
        }

        Column {
            x: 17
            y: 16
            width: 247
            spacing: 20

            // Presets, keys 1 to 8.
            Column {
                width: parent.width
                spacing: 2

                Item {
                    width: parent.width
                    height: 14 + 6
                    Txt {
                        font.pixelSize: 11
                        color: theme.mu
                        text: qsTr("presets")
                    }
                    Txt {
                        anchors.right: parent.right
                        font.pixelSize: 11
                        color: theme.mu
                        text: "1–" + Math.max(1, root.app.slots.length)
                    }
                }
                Repeater {
                    model: root.app.slots

                    Rectangle {
                        id: presetRow
                        required property string modelData
                        required property int index
                        readonly property bool on: modelData === root.st.preset
                        width: parent.width
                        height: 26
                        color: on ? theme.hl : "transparent"

                        Txt {
                            x: 8
                            y: 5
                            color: theme.mu
                            text: presetRow.index + 1
                        }
                        Txt {
                            x: 8 + 7.2 + 10
                            y: 5
                            width: parent.width - x - 8 - (mark.implicitWidth > 0 ? mark.implicitWidth + 10 : 0)
                            elide: Text.ElideRight
                            text: presetRow.modelData
                        }
                        Txt {
                            id: mark
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            y: 5
                            color: theme.ac
                            text: presetRow.on ? (root.st.dirty ? qsTr("edited") : "●") : ""
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: preamp.send({cmd: "applyPreset", name: presetRow.modelData})
                        }
                    }
                }
            }

            // Auto-switch: the preset this output switches to.
            Column {
                width: parent.width
                spacing: 6

                Txt {
                    font.pixelSize: 11
                    color: theme.mu
                    text: qsTr("auto-switch")
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    font.family: theme.font
                    font.pixelSize: 12
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 18
                    renderType: Text.NativeRendering
                    textFormat: Text.StyledText
                    color: theme.fg
                    text: root.app.autoSwitchText
                }
            }

            Item {
                width: parent.width
                height: 20

                Txt {
                    y: preampValue.baselineOffset - baselineOffset
                    font.pixelSize: 11
                    color: theme.mu
                    text: qsTr("preamp · auto")
                }
                Txt {
                    id: preampValue
                    anchors.right: parent.right
                    font.pixelSize: 15
                    text: root.app.preampText
                }
            }

            // Effects after the equalizer.
            Column {
                width: parent.width
                spacing: 2

                Item {
                    width: parent.width
                    height: 14 + 6
                    Txt {
                        font.pixelSize: 11
                        color: theme.mu
                        text: qsTr("after eq")
                    }
                    Txt {
                        anchors.right: parent.right
                        font.pixelSize: 11
                        color: theme.mu
                        text: "lv2 · ladspa"
                    }
                }
                Repeater {
                    model: root.st.plugins || []

                    Item {
                        id: plug
                        required property var modelData
                        required property int index
                        width: parent.width
                        height: 24

                        Txt {
                            x: 8
                            y: 4
                            color: plug.modelData.on ? theme.ac : theme.mu
                            text: plug.modelData.on ? "■" : "□"
                        }
                        Txt {
                            x: 8 + 7.2 + 10
                            y: 4
                            width: parent.width - x - 8 - kind.implicitWidth - 10
                            elide: Text.ElideRight
                            color: plug.modelData.on ? theme.fg : theme.mu
                            text: plug.modelData.name
                        }
                        Txt {
                            id: kind
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            y: 4
                            color: plug.modelData.error ? theme.rd : theme.mu
                            text: plug.modelData.error ? plug.modelData.error : plug.modelData.kind
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: mouse => preamp.send({cmd: mouse.button === Qt.RightButton ? "removePlugin" : "togglePlugin",
                                                             i: plug.index})
                        }
                    }
                }
                Item {
                    width: parent.width
                    height: 24

                    Txt {
                        x: 8
                        y: 4
                        color: theme.ac
                        text: "◆"
                    }
                    Txt {
                        x: 8 + 7.2 + 10
                        y: 4
                        text: qsTr("spatial")
                    }
                    Txt {
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        y: 4
                        color: theme.mu
                        text: root.app.roomLabel(root.st.room)
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: preamp.send({cmd: "nextRoom"})
                    }
                }
            }
        }
    }

    Footer {
        id: footer
        y: parent.height - height
        width: parent.width
        hints: [["h l", qsTr("band")], ["j k", qsTr("gain")], ["H L", qsTr("freq")], ["J K", qsTr("q")],
                ["c", qsTr("channel")], ["b", qsTr("bypass")]]
        rightText: qsTr("drag a node · x freq, y gain")
    }
}
