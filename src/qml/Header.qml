import QtQuick

// The 42 px bar across the top of each view, with its rule underneath.
Item {
    id: root

    default property alias content: holder.data
    height: 43

    Item {
        id: holder
        x: 16
        width: parent.width - 32
        height: 42
    }
    Rectangle {
        y: 42
        width: parent.width
        height: 1
        color: theme.ln
    }
}
