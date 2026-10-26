import QtQuick

// Every installed LV2 and LADSPA effect that fits a stereo chain. Type to
// filter, Enter or a click adds the effect at the end of the chain.
Rectangle {
    id: root

    signal closed()
    property int cursor: 0
    readonly property var matches: {
        const q = filter.text.trim().toLowerCase()
        return preamp.availablePlugins.filter(p => q === "" || p.name.toLowerCase().includes(q) || p.kind.includes(q))
    }

    color: Qt.rgba(theme.bg.r, theme.bg.g, theme.bg.b, .85)
    onVisibleChanged: {
        if (visible) {
            filter.text = ""
            cursor = 0
            filter.forceActiveFocus()
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.closed()
    }

    Rectangle {
        anchors.centerIn: parent
        width: 520
        height: Math.min(parent.height - 80, 440)
        color: theme.bg
        border.width: 2
        border.color: theme.ac
        radius: theme.radius

        MouseArea {
            anchors.fill: parent
        }

        Item {
            id: top
            x: 14
            width: parent.width - 28
            height: 44
            Txt {
                id: prompt
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 15
                color: theme.ac
                text: "+"
            }
            TextInput {
                id: filter
                x: prompt.width + 10
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - x - count.width - 10
                font.family: theme.font
                font.pixelSize: 13
                color: theme.fg
                selectionColor: theme.hl
                renderType: Text.NativeRendering
                onTextChanged: root.cursor = 0
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Down) {
                        root.cursor = Math.min(root.matches.length - 1, root.cursor + 1)
                        list.positionViewAtIndex(root.cursor, ListView.Contain)
                    } else if (event.key === Qt.Key_Up) {
                        root.cursor = Math.max(0, root.cursor - 1)
                        list.positionViewAtIndex(root.cursor, ListView.Contain)
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        const p = root.matches[root.cursor]
                        if (p) {
                            preamp.send({cmd: "addPlugin", id: p.id})
                            root.closed()
                        }
                    } else if (event.key === Qt.Key_Escape) {
                        root.closed()
                    } else {
                        return
                    }
                    event.accepted = true
                }
            }
            Txt {
                anchors.verticalCenter: parent.verticalCenter
                x: filter.x
                visible: filter.text === ""
                font.pixelSize: 13
                color: theme.mu
                text: qsTr("add a plugin after the equalizer")
            }
            Txt {
                id: count
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 11
                color: theme.mu
                text: qsTr("%n plugins", "", root.matches.length)
            }
        }
        Rectangle {
            y: top.height
            width: parent.width
            height: 1
            color: theme.ln
        }

        ListView {
            id: list
            x: 4
            y: top.height + 5
            width: parent.width - 8
            height: parent.height - y - 5
            clip: true
            model: root.matches
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: item
                required property var modelData
                required property int index
                width: list.width
                height: 28
                color: index === root.cursor ? theme.hl : "transparent"

                Txt {
                    x: 10
                    y: 6
                    width: parent.width - 20 - kind.implicitWidth - 12
                    elide: Text.ElideRight
                    text: item.modelData.name
                }
                Txt {
                    id: kind
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    y: 6
                    color: theme.mu
                    text: item.modelData.kind
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: root.cursor = item.index
                    onClicked: {
                        preamp.send({cmd: "addPlugin", id: item.modelData.id})
                        root.closed()
                    }
                }
            }
        }

        Txt {
            anchors.centerIn: list
            visible: root.matches.length === 0
            width: list.width - 40
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: theme.mu
            text: preamp.availablePlugins.length === 0
                  ? qsTr("No stereo LV2 or LADSPA plugins are installed. Install some with: omarchy pkg add lsp-plugins-lv2")
                  : qsTr("No plugin matches.")
        }
    }
}
