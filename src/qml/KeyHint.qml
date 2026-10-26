import QtQuick

// "h l band": the keys in the accent, what they do quiet.
Row {
    property string keys
    property string label
    spacing: 0

    Txt {
        text: parent.keys
        font.pixelSize: 11
        color: theme.ac
    }
    Txt {
        text: " " + parent.label
        font.pixelSize: 11
        color: theme.mu
    }
}
