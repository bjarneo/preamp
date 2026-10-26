import QtQuick

// The ":" bar: type a preset or a headphone, pick with the arrows, apply
// with Enter. It also takes a few commands: "w NAME" saves the curve as a
// preset, "pin" and "unpin" tie the preset to the output, "rm NAME" deletes
// a saved preset.
Rectangle {
    id: root

    required property var app
    property alias input: field
    property int cursor: 0
    readonly property bool active: field.activeFocus || field.text !== ""
    signal finished()

    height: 45
    color: theme.bd

    readonly property string text: field.text.trim()
    readonly property var command: {
        const t = root.text
        let m = t.match(/^(?:w|save)\s+(.+)$/)
        if (m)
            return {t: qsTr("save as %1").arg(m[1]), s: qsTr("command"), run: {cmd: "savePreset", name: m[1]}}
        m = t.match(/^rm\s+(.+)$/)
        if (m)
            return {t: qsTr("delete %1").arg(m[1]), s: qsTr("command"), run: {cmd: "deletePreset", name: m[1]}}
        if (t === "pin")
            return {t: qsTr("pin %1 to %2").arg(preamp.state.preset).arg(root.app.outputLabel), s: qsTr("command"),
                    run: {cmd: "pin"}}
        if (t === "unpin")
            return {t: qsTr("unpin %1").arg(root.app.outputLabel), s: qsTr("command"), run: {cmd: "unpin"}}
        return null
    }
    readonly property var rows: {
        if (command)
            return [command]
        const list = preamp.searchResults.slice(0, 5).map(r => ({t: r.t, s: r.s === "autoeq" ? "autoeq" : qsTr("preset"), result: r}))
        if (list.length < 5 && preamp.searchPending)
            list.push({t: qsTr("searching AutoEq…"), s: "", note: true})
        else if (list.length === 0 && preamp.searchError !== "" && root.text !== "")
            list.push({t: qsTr("AutoEq is out of reach: %1").arg(preamp.searchError), s: "", note: true})
        return list
    }

    function run(row) {
        if (!row || row.note)
            return
        if (row.run)
            preamp.send(row.run)
        else
            preamp.applySearchResult(row.result)
        field.text = ""
        root.finished()
    }

    function open() {
        field.forceActiveFocus()
    }

    Rectangle {
        width: parent.width
        height: 1
        color: theme.ln
    }

    // Suggestions, floating above the bar.
    Rectangle {
        visible: root.rows.length > 0 && field.text !== ""
        x: 16
        y: 1 + 44 - 50 - height
        width: 440
        height: list.implicitHeight + 10
        color: theme.bg
        border.width: 1
        border.color: theme.ac

        Column {
            id: list
            x: 5
            y: 5
            width: parent.width - 10

            Repeater {
                model: root.rows

                Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    width: list.width
                    height: 28
                    color: index === root.cursor && !modelData.note ? theme.hl : "transparent"

                    Txt {
                        x: 10
                        y: 6
                        width: parent.width - 20 - source.implicitWidth - 12
                        elide: Text.ElideRight
                        color: row.modelData.note ? theme.mu : theme.fg
                        text: row.modelData.t
                    }
                    Txt {
                        id: source
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        y: 6
                        color: theme.mu
                        text: row.modelData.s
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onPressed: root.run(row.modelData)
                    }
                }
            }
        }
    }

    Row {
        x: 16
        y: 1
        width: parent.width - 32
        height: 44
        spacing: 10

        Txt {
            id: prompt
            anchors.verticalCenter: parent.verticalCenter
            font.pixelSize: 15
            color: theme.ac
            text: ":"
        }
        Item {
            width: parent.width - prompt.width - hint.width - 20
            height: parent.height

            TextInput {
                id: field
                // Centred rounding down, as the browser places the field.
                y: Math.ceil((parent.height - height) / 2)
                width: parent.width
                font.family: theme.font
                font.pixelSize: 13
                color: theme.fg
                selectionColor: theme.hl
                selectedTextColor: theme.fg
                renderType: Text.NativeRendering
                clip: true
                cursorDelegate: Rectangle {
                    width: 1
                    color: theme.ac
                    visible: field.activeFocus && field.cursorVisible
                }
                onTextChanged: {
                    root.cursor = 0
                    preamp.search(root.command ? "" : text)
                }
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Down) {
                        root.cursor = Math.min(root.rows.length - 1, root.cursor + 1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Up) {
                        root.cursor = Math.max(0, root.cursor - 1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        root.run(root.rows[Math.max(0, Math.min(root.cursor, root.rows.length - 1))])
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape) {
                        field.text = ""
                        root.finished()
                        event.accepted = true
                    }
                }
            }
            Txt {
                y: Math.ceil((parent.height - height) / 2)
                visible: field.text === ""
                font.pixelSize: 13
                // The design keeps the browser's own placeholder grey, which
                // reads on light and dark themes alike.
                color: "#757575"
                text: qsTr("preset or headphone, try hd 6")
            }
        }
        Txt {
            id: hint
            anchors.verticalCenter: parent.verticalCenter
            font.pixelSize: 11
            color: theme.mu
            text: field.text !== "" ? qsTr("⏎ apply · ↑↓ · esc") : qsTr("press : anywhere")
        }
    }
}
