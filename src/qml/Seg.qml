import QtQuick

// A row of choices in one box, like the channel switch. `labels` are shown
// as given. `current` is the index that is on.
Rectangle {
    id: root

    property var labels: []
    property int current: 0
    property int padX: 10
    property int padY: 4
    property bool boxed: true
    property int gap: 0
    property color onColor: theme.ac
    property color onText: theme.bg
    property color offText: theme.mu
    signal picked(int index)

    color: "transparent"
    border.color: boxed ? theme.ln : "transparent"
    border.width: boxed ? 1 : 0
    implicitWidth: row.implicitWidth + 2 * border.width
    implicitHeight: row.implicitHeight + 2 * border.width

    Row {
        id: row
        x: root.border.width
        y: root.border.width
        spacing: root.gap

        Repeater {
            model: root.labels

            Rectangle {
                required property string modelData
                required property int index
                readonly property bool on: index === root.current

                width: label.implicitWidth + 2 * root.padX
                height: label.implicitHeight + 2 * root.padY
                color: on ? root.onColor : "transparent"

                Txt {
                    id: label
                    x: root.padX
                    y: root.padY
                    text: parent.modelData
                    color: parent.on ? root.onText : root.offText
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.picked(parent.index)
                }
            }
        }
    }
}
