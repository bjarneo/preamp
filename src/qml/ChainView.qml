import QtQuick
import Preamp
import "Format.js" as F

// Signal chain: apps, the equalizer, plugins, the room and the output, left
// to right as the audio flows, with the auto-switch rules underneath.
Item {
    id: root

    required property var app
    readonly property var st: preamp.state
    signal addPlugin()

    component Box: Rectangle {
        property alias content: inner.data
        property real spacing: 6
        color: "transparent"
        border.width: 1
        border.color: theme.ln
        radius: theme.radius
        height: 318
        Column {
            id: inner
            x: 13
            y: 13
            width: parent.width - 26
            height: parent.height - 26
            spacing: parent.spacing
        }
    }
    component Arrow: Item {
        width: 16
        height: 318
        Txt {
            anchors.centerIn: parent
            color: theme.mu
            text: "→"
        }
    }
    component Heading: Item {
        id: heading
        property string label
        property string detail
        property real below: 0
        width: parent ? parent.width : 0
        height: 14 + below
        Txt {
            font.pixelSize: 11
            color: theme.mu
            text: heading.label
        }
        Txt {
            anchors.right: parent.right
            font.pixelSize: 11
            color: theme.mu
            text: heading.detail
        }
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
                text: qsTr("signal chain")
            }
        }
        BypassButton {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Row {
        id: chain
        x: 16
        y: header.height + 16
        // The boxes keep the design's widths. Extra room goes to the apps.
        readonly property real extra: Math.max(0, root.width - 1096)

        // ------------------------------------------------------------ apps
        Box {
            width: 200 + chain.extra
            spacing: 14
            clip: true
            content: [
                Heading {
                    label: qsTr("apps")
                    detail: qsTr("%n streams", "", (root.st.apps || []).length)
                },
                Repeater {
                    model: root.st.apps || []

                    Column {
                        id: appRow
                        required property var modelData
                        width: parent.width
                        spacing: 5

                        Item {
                            width: parent.width
                            height: 16
                            Txt {
                                width: parent.width - pct.implicitWidth - 8
                                elide: Text.ElideRight
                                text: appRow.modelData.name
                            }
                            Txt {
                                id: pct
                                anchors.right: parent.right
                                color: appRow.modelData.muted ? theme.rd : theme.fg
                                text: appRow.modelData.muted ? qsTr("muted") : appRow.modelData.volume + "%"
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -3
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: preamp.send({cmd: "toggleAppMute", key: appRow.modelData.key})
                                }
                            }
                        }
                        LineSlider {
                            width: parent.width
                            fraction: appRow.modelData.volume / 100
                            fill: appRow.modelData.muted ? theme.ln : theme.ac
                            onMoved: f => preamp.send({cmd: "setAppVolume", key: appRow.modelData.key, v: Math.round(f * 100)})
                        }
                        Txt {
                            font.pixelSize: 10
                            color: theme.mu
                            text: root.app.appStateLabel(appRow.modelData.state)
                        }
                    }
                },
                Txt {
                    visible: (root.st.apps || []).length === 0
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: theme.mu
                    text: qsTr("Nothing is playing.")
                }
            ]
        }

        Arrow {}

        // -------------------------------------------------------------- eq
        Box {
            width: 270
            border.color: theme.ac
            spacing: 12
            content: [
                Heading {
                    label: qsTr("eq")
                    detail: root.app.chainChannel
                },
                Txt {
                    width: parent.width
                    elide: Text.ElideRight
                    font.pixelSize: 13
                    text: root.app.presetLabel
                },
                Rectangle {
                    width: 244
                    height: 120
                    color: theme.bd
                    Graph {
                        anchors.fill: parent
                        look: "bars"
                        bands: root.app.bands
                        otherBands: root.app.otherBands
                        bypass: root.app.bypass
                        accent: theme.ac
                        accent2: theme.a2
                        muted: theme.mu
                        line: theme.ln
                        text: theme.fg
                    }
                },
                Item {
                    width: parent.width
                    height: 13 + 3 + 16
                    Repeater {
                        model: 3
                        Column {
                            required property int index
                            spacing: 3
                            x: index === 0 ? 0 : index === 1 ? (parent.width - width) / 2 : parent.width - width
                            Txt {
                                font.pixelSize: 10
                                color: theme.mu
                                text: [qsTr("bass"), qsTr("mid"), qsTr("treble")][parent.index]
                            }
                            Txt {
                                text: F.gain(root.st.macros ? Math.round(root.st.macros[parent.index] * 10) / 10 : 0)
                            }
                        }
                    }
                }
            ]
            Seg {
                x: 13
                y: parent.height - 13 - height
                padX: 9
                padY: 3
                labels: ["L+R", "L", "R"]
                current: root.app.channelIndex
                onPicked: index => root.app.setChannel(index)
            }
        }

        Arrow {}

        // --------------------------------------------------------- plugins
        Box {
            id: pluginBox
            width: 190
            spacing: 6
            clip: true
            content: [
                Heading {
                    label: qsTr("plugins")
                    detail: qsTr("in order")
                    below: 6
                },
                Repeater {
                    model: root.st.plugins || []

                    Item {
                        id: plug
                        required property var modelData
                        required property int index
                        width: parent.width
                        height: 41

                        Txt {
                            y: 5
                            color: plug.modelData.on ? theme.ac : theme.mu
                            text: plug.modelData.on ? "■" : "□"
                        }
                        Txt {
                            x: 7.2 + 8
                            y: 5
                            width: parent.width - x
                            elide: Text.ElideRight
                            color: plug.modelData.on ? theme.fg : theme.mu
                            text: plug.modelData.name
                        }
                        Txt {
                            x: 18
                            y: 5 + 16 + 2
                            font.pixelSize: 10
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
            ]
            Txt {
                x: 13
                y: parent.height - 13 - height
                font.pixelSize: 11
                color: addArea.containsMouse ? theme.fg : theme.mu
                text: qsTr("+ add plugin")
                MouseArea {
                    id: addArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.addPlugin()
                }
            }
        }

        Arrow {}

        // --------------------------------------------------------- spatial
        Box {
            width: 150
            spacing: 6
            content: [
                Heading {
                    label: qsTr("spatial")
                    below: 6
                },
                Repeater {
                    model: root.st.rooms || []

                    Item {
                        id: roomRow
                        required property string modelData
                        readonly property bool on: modelData === root.st.room
                        width: parent.width
                        height: 22
                        Txt {
                            y: 3
                            color: roomRow.on ? theme.ac : theme.mu
                            text: roomRow.on ? "●" : "○"
                        }
                        Txt {
                            x: 7.2 + 8
                            y: 3
                            color: roomRow.on ? theme.fg : theme.mu
                            text: root.app.roomLabel(roomRow.modelData)
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: preamp.send({cmd: "setRoom", room: roomRow.modelData})
                        }
                    }
                }
            ]
            Column {
                x: 13
                y: parent.height - 13 - height
                width: parent.width - 26
                spacing: 6
                Item {
                    width: parent.width
                    height: 14
                    Txt {
                        font.pixelSize: 11
                        color: theme.mu
                        text: qsTr("width")
                    }
                    Txt {
                        anchors.right: parent.right
                        font.pixelSize: 11
                        text: Math.round(root.st.width || 0) + "%"
                    }
                }
                LineSlider {
                    width: parent.width
                    fraction: (root.st.width || 0) / 200
                    onMoved: f => preamp.send({cmd: "setWidth", v: Math.round(f * 200)})
                }
            }
        }

        Arrow {}

        // ---------------------------------------------------------- output
        Box {
            width: 190
            spacing: 6
            clip: true
            content: [
                Heading {
                    label: qsTr("output")
                    below: 6
                },
                Repeater {
                    model: root.st.outputs || []

                    Item {
                        id: outRow
                        required property var modelData
                        readonly property bool on: modelData.name === root.st.output
                        width: parent.width
                        height: 36
                        Txt {
                            y: 3
                            color: outRow.on ? theme.ac : theme.mu
                            text: outRow.on ? "●" : "○"
                        }
                        Txt {
                            x: 7.2 + 8
                            y: 3
                            width: parent.width - x
                            elide: Text.ElideRight
                            color: outRow.on ? theme.fg : theme.mu
                            text: outRow.modelData.label
                        }
                        Txt {
                            x: 18
                            y: 3 + 16 + 1
                            font.pixelSize: 10
                            color: theme.mu
                            text: outRow.modelData.kind + " · " + outRow.modelData.rate
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: preamp.send({cmd: "setOutput", name: outRow.modelData.name})
                        }
                    }
                }
            ]
            Column {
                x: 13
                y: parent.height - 13 - height
                width: parent.width - 26
                spacing: 6

                Item {
                    width: parent.width
                    height: 23
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
                    volume: root.st.volume || 0
                    onMoved: v => preamp.send({cmd: "setVolume", v: v})
                }
                Item {
                    width: parent.width
                    height: 14 + 4
                    Txt {
                        y: 4
                        font.pixelSize: 11
                        color: theme.mu
                        text: qsTr("balance")
                    }
                    Txt {
                        anchors.right: parent.right
                        y: 4
                        font.pixelSize: 11
                        text: root.app.balanceLabel
                    }
                }
                Item {
                    width: parent.width
                    height: 12
                    Rectangle {
                        y: 5
                        width: parent.width
                        height: 2
                        color: theme.ln
                    }
                    Rectangle {
                        x: parent.width / 2
                        y: 1
                        width: 1
                        height: 10
                        color: theme.mu
                    }
                    Rectangle {
                        x: parent.width * ((root.st.balance || 0) + 100) / 200 - 3
                        y: 1
                        width: 6
                        height: 10
                        color: theme.fg
                    }
                    SliderArea {
                        onMoved: f => preamp.send({cmd: "setBalance", v: Math.round(f * 200 - 100)})
                        onDoubleClicked: preamp.send({cmd: "setBalance", v: 0})
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ rules
    Column {
        id: rules
        x: 16
        y: chain.y + 318 + 18
        width: parent.width - 32
        spacing: 2

        Heading {
            label: qsTr("auto-switch · first match wins")
            below: 6
            Txt {
                anchors.right: parent.right
                font.pixelSize: 11
                color: addRule.containsMouse ? theme.fg : theme.mu
                text: qsTr("+ add rule")
                MouseArea {
                    id: addRule
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.app.addRule()
                }
            }
        }

        Repeater {
            model: root.st.rules || []

            Rectangle {
                id: rule
                required property var modelData
                required property int index
                readonly property bool fixed: modelData.kind === "output"
                width: rules.width
                height: 26
                color: modelData.active ? theme.hl : "transparent"

                HoverHandler {
                    id: hover
                }

                Txt {
                    x: 8
                    y: 5
                    color: theme.mu
                    text: rule.index + 1
                }
                Txt {
                    x: 8 + 24 + 12
                    y: 5
                    color: theme.mu
                    text: root.app.ruleKindLabel(rule.modelData.kind)
                    MouseArea {
                        anchors.fill: parent
                        enabled: !rule.fixed
                        cursorShape: Qt.PointingHandCursor
                        onClicked: preamp.send({cmd: "updateRule", index: rule.modelData.index,
                                                kind: rule.modelData.kind === "call" ? "fullscreen" : "call"})
                    }
                }
                Item {
                    id: whenCell
                    x: 8 + 24 + 12 + 130 + 12
                    y: 5
                    width: rule.width - 16 - 24 - 130 - 240 - 70 - 48
                    height: 16
                    property bool editing: false

                    Txt {
                        visible: !whenCell.editing
                        width: parent.width
                        elide: Text.ElideRight
                        text: rule.modelData.when
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: !rule.fixed && !whenCell.editing
                        cursorShape: Qt.IBeamCursor
                        onClicked: {
                            whenCell.editing = true
                            whenInput.text = rule.modelData.when
                            whenInput.forceActiveFocus()
                            whenInput.selectAll()
                        }
                    }
                    TextInput {
                        id: whenInput
                        visible: whenCell.editing
                        width: parent.width
                        font.family: theme.font
                        font.pixelSize: 12
                        color: theme.fg
                        selectionColor: theme.hl
                        renderType: Text.NativeRendering
                        onEditingFinished: {
                            if (whenCell.editing && text.trim() !== "")
                                preamp.send({cmd: "updateRule", index: rule.modelData.index, when: text.trim()})
                            whenCell.editing = false
                        }
                        Keys.onEscapePressed: whenCell.editing = false
                    }
                }
                Item {
                    x: rule.width - 8 - 70 - 12 - 240
                    y: 5
                    width: 240
                    height: 16
                    Row {
                        Txt {
                            color: theme.mu
                            text: "→ "
                        }
                        Txt {
                            width: 240 - 7.2 * 2
                            elide: Text.ElideRight
                            text: rule.modelData.then + (rule.modelData.room ? " + " + root.app.roomLabel(rule.modelData.room) : "")
                        }
                    }
                    // A click picks the next preset. A right-click picks the next room.
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: mouse => root.app.cycleRuleTarget(rule.modelData, mouse.button === Qt.RightButton)
                    }
                }
                Txt {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    y: 5
                    color: removeArea.containsMouse ? theme.rd : theme.ac
                    text: removeArea.containsMouse || (hover.hovered && !rule.modelData.active)
                          ? (rule.fixed ? qsTr("unpin") : qsTr("remove"))
                          : (rule.modelData.active ? qsTr("active") : "")
                    opacity: removeArea.containsMouse || rule.modelData.active ? 1 : .7
                    MouseArea {
                        id: removeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: rule.fixed ? preamp.send({cmd: "unpin", output: rule.modelData.key})
                                              : preamp.send({cmd: "removeRule", index: rule.modelData.index})
                    }
                }
            }
        }

        Txt {
            visible: (root.st.rules || []).length === 0
            x: 8
            color: theme.mu
            text: qsTr("No rules yet. Type :pin to tie the preset to this output.")
        }
    }

    Footer {
        id: footer
        y: parent.height - height
        width: parent.width
        leftText: qsTr("click to toggle · drag sliders · click a percentage to mute")
        rightText: qsTr("%1 preamp").arg(root.app.preampText)
    }
}
