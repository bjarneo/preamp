import QtQuick

// "bypass" while the equalizer runs, "bypassed" in the warning color while
// it does not.
Rectangle {
    id: root

    property bool bypassed: preamp.state.bypass === true
    property int padX: 10
    property int padY: 4
    property int fontSize: 12

    implicitWidth: label.implicitWidth + 2 * padX + 2
    implicitHeight: label.implicitHeight + 2 * padY + 2
    color: bypassed ? theme.yl : "transparent"
    border.width: 1
    border.color: bypassed ? theme.yl : theme.ln

    Txt {
        id: label
        x: root.padX + 1
        y: root.padY + 1
        font.pixelSize: root.fontSize
        text: root.bypassed ? qsTr("bypassed") : qsTr("bypass")
        color: root.bypassed ? theme.bg : theme.mu
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: preamp.send({cmd: "setBypass"})
    }
}
