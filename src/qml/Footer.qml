import QtQuick

// The key strip along the bottom of every view. A notice from the daemon
// takes the right-hand side for a few seconds.
Item {
    id: root

    property var hints: []
    property string leftText: ""
    property string rightText: ""

    height: 31

    Rectangle {
        width: parent.width
        height: 1
        color: theme.ln
    }
    Row {
        x: 16
        y: 1
        height: 30
        spacing: 18

        Repeater {
            model: root.hints

            KeyHint {
                required property var modelData
                anchors.verticalCenter: parent.verticalCenter
                keys: modelData[0]
                label: modelData[1]
            }
        }
    }
    Txt {
        x: 16
        y: 1 + (30 - height) / 2
        visible: root.leftText !== ""
        font.pixelSize: 11
        color: theme.mu
        text: root.leftText
    }
    Txt {
        anchors.right: parent.right
        anchors.rightMargin: 16
        y: 1 + (30 - height) / 2
        font.pixelSize: 11
        text: preamp.notice !== "" ? preamp.notice : root.rightText
        color: preamp.notice !== "" ? theme.yl : theme.mu
    }
}
