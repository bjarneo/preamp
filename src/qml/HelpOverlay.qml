import QtQuick

// Every key, on "?". The presets view has a shorter list of its own.
Rectangle {
    id: root

    property bool simple: false
    signal closed()

    readonly property var keys: simple ? simpleKeys : allKeys
    readonly property var simpleKeys: [
        ["h l  ← →", qsTr("previous and next preset")],
        ["j k  ↓ ↑", qsTr("preset above and below")],
        ["1–8", qsTr("presets")],
        ["- +", qsTr("volume down and up")],
        ["b", qsTr("bypass the equalizer")],
        ["tab", qsTr("presets, studio, modes, signal chain")],
        ["?", qsTr("this list")],
        ["q", qsTr("quit")],
    ]
    readonly property var allKeys: [
        ["h l  ← →", qsTr("pick a band")],
        ["j k  ↓ ↑", qsTr("gain down and up")],
        ["H L", qsTr("frequency down and up")],
        ["J K", qsTr("q down and up")],
        ["0", qsTr("band gain to zero")],
        ["1–8", qsTr("presets")],
        ["c", qsTr("channel: L+R, L, R")],
        ["b", qsTr("bypass the equalizer")],
        ["m", qsTr("basic, graphic, parametric")],
        [":", qsTr("preset, headphone or command")],
        ["tab", qsTr("presets, studio, modes, signal chain")],
        ["?", qsTr("this list")],
        ["q", qsTr("quit")],
    ]

    color: Qt.rgba(theme.bg.r, theme.bg.g, theme.bg.b, .85)

    MouseArea {
        anchors.fill: parent
        onClicked: root.closed()
    }

    Rectangle {
        anchors.centerIn: parent
        width: 460
        height: list.implicitHeight + 64
        color: theme.bg
        border.width: 2
        border.color: theme.ac
        radius: theme.radius

        Txt {
            x: 22
            y: 18
            font.pixelSize: 11
            color: theme.mu
            text: qsTr("keys")
        }
        Column {
            id: list
            x: 22
            y: 44
            width: parent.width - 44
            spacing: 6

            Repeater {
                model: root.keys
                Item {
                    required property var modelData
                    width: list.width
                    height: 16
                    Txt {
                        color: theme.ac
                        text: parent.modelData[0]
                    }
                    Txt {
                        x: 120
                        text: parent.modelData[1]
                    }
                }
            }
        }
    }
}
