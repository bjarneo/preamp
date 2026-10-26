import QtQuick

// The window title as the brand sheet draws it: glyph, then "preamp" bold.
Row {
    spacing: 10

    Glyph {
        anchors.verticalCenter: parent.verticalCenter
        size: 18
    }
    Txt {
        anchors.verticalCenter: parent.verticalCenter
        text: "preamp"
        font.pixelSize: 13
        font.bold: true
    }
}
