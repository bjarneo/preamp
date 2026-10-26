import QtCore
import QtQuick
import "Format.js" as F

Window {
    id: win

    width: 1096
    height: 650
    minimumWidth: 960
    minimumHeight: 600
    visible: true
    title: "preamp"
    color: theme.bg

    Settings {
        id: settings
        category: "window"
        property string view: "presets"
        property string mode: "graphic"
    }

    Item {
        id: app
        objectName: "app"
        anchors.fill: parent
        focus: true

        // The window keeps the design's left to right layout in every
        // language, as the frequency axis reads low to high from the left.
        // Text in right to left languages still reads right to left.
        LayoutMirroring.enabled: false
        LayoutMirroring.childrenInherit: true

        property int sel: 0
        property alias view: settings.view
        property alias mode: settings.mode
        property bool help: false
        property bool picker: false

        readonly property var st: preamp.state
        readonly property string channel: st.link === false ? (st.channel || "L") : "L"
        readonly property var bands: (st.bands && st.bands[channel]) || []
        readonly property var otherBands: st.link === false && st.bands ? st.bands[channel === "L" ? "R" : "L"] : []
        readonly property bool bypass: st.bypass === true
        readonly property int channelIndex: st.link === false ? (channel === "L" ? 1 : 2) : 0
        readonly property string channelLabel: st.link === false ? channel : "L+R"
        readonly property string chainChannel: st.link === false ? qsTr("L / R split, editing %1").arg(channel)
                                                                 : qsTr("L+R linked")
        readonly property string presetLabel: (st.preset || "") + (st.dirty ? " · " + qsTr("edited") : "")
        readonly property var output: (st.outputs || []).find(o => o.name === st.output) || null
        readonly property string outputLabel: st.outputLabel || qsTr("no output")
        readonly property string outputMeta: output ? output.kind + " · " + output.rate : ""
        readonly property var slots: st.slots || []
        readonly property string preampText: F.gain(st.preamp || 0) + " dB"
        readonly property var selBand: bands[sel] || ({f: 1000, g: 0, q: 1, t: "pk"})
        readonly property string selInfo: F.two(sel) + " · " + F.freqLong(selBand.f) + " · " + typeLabel(selBand)
                                          + " · q " + selBand.q.toFixed(2) + " · " + F.gain(selBand.g) + " dB"
                                          + (st.link === false ? " · " + channel : "")
        readonly property string balanceLabel: {
            const b = Math.round(st.balance || 0)
            return b === 0 ? qsTr("center") : b < 0 ? "L " + (-b) : "R " + b
        }
        readonly property string autoSwitchText: {
            const quiet = s => "<font color=\"" + theme.mu + "\">" + s + "</font>"
            const plain = s => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
            if (!st.pin)
                return quiet(qsTr("on")) + " " + plain(outputLabel) + " " + quiet(qsTr("nothing pinned · type :pin"))
            return quiet(qsTr("on")) + " " + plain(outputLabel) + " " + quiet(qsTr("use")) + " " + plain(st.pin)
        }

        function bandsOf(c) { return (st.bands && st.bands[c]) || [] }
        function setChannel(i) { preamp.send({cmd: "setChannel", channel: ["L+R", "L", "R"][i]}) }
        function typeLabel(b) { return b.t === "pk" ? qsTr("peak") : qsTr("shelf") }
        function roomLabel(r) {
            return r === "off" ? qsTr("off") : r === "small room" ? qsTr("small room") : r === "studio" ? qsTr("studio")
                 : r === "hall" ? qsTr("hall") : (r || "")
        }
        function appStateLabel(s) {
            return s === "playing" ? qsTr("playing") : s === "paused" ? qsTr("paused") : s === "in call" ? qsTr("in call")
                 : qsTr("idle")
        }
        function ruleKindLabel(k) {
            return k === "output" ? qsTr("output is") : k === "call" ? qsTr("app in call") : qsTr("app fullscreen")
        }
        function nudgeGain(i, d) {
            const b = bands[i]
            if (b)
                preamp.send({cmd: "setBand", i: i, g: F.clamp(Math.round((b.g + d) * 10) / 10, -12, 12)})
        }
        function addRule() {
            const apps = st.apps || []
            preamp.send({cmd: "addRule", kind: "call", when: apps.length ? apps[0].name : "Discord", then: st.preset})
        }
        function cycleRuleTarget(rule, room) {
            if (room) {
                const rooms = ["", "small room", "studio", "hall"]
                const next = rooms[(rooms.indexOf(rule.room || "") + 1) % rooms.length]
                if (rule.kind !== "output")
                    preamp.send({cmd: "updateRule", index: rule.index, room: next})
                return
            }
            const list = slots.length ? slots : [st.preset]
            const next = list[(list.indexOf(rule.then) + 1) % list.length]
            if (rule.kind === "output")
                preamp.send({cmd: "pin", output: rule.key, preset: next})
            else
                preamp.send({cmd: "updateRule", index: rule.index, then: next})
        }
        function cycleView(step) {
            const views = ["presets", "studio", "modes", "chain"]
            view = views[(views.indexOf(view) + step + views.length) % views.length]
        }
        // In the presets view the arrows move between cards and use the
        // preset at once: left and right wrap, up and down stop at the edge.
        function stepPreset(step, wrap) {
            const names = presetsView.names
            if (!names.length)
                return
            const at = names.indexOf(st.preset)
            let next = at < 0 ? 0 : at + step
            if (wrap)
                next = (next + names.length) % names.length
            else if (next < 0 || next >= names.length)
                return
            if (next !== at)
                preamp.send({cmd: "applyPreset", name: names[next]})
        }
        function nudgeVolume(d) {
            preamp.send({cmd: "setVolume", v: F.clamp((st.volume || 0) + d, 0, 150)})
        }
        function openCommand() {
            if (view === "modes")
                modes.commandBar.open()
            else
                floating.open()
        }
        function closeOverlays() {
            help = false
            picker = false
            app.forceActiveFocus()
        }

        Keys.onPressed: event => {
            if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                return
            if (help || picker) {
                if (event.key === Qt.Key_Escape || event.text === "?")
                    closeOverlays()
                event.accepted = true
                return
            }
            const b = bands[sel]
            const t = event.text
            const simple = view === "presets"
            let hit = true
            if (event.key === Qt.Key_Tab)
                cycleView(1)
            else if (event.key === Qt.Key_Backtab)
                cycleView(-1)
            else if (simple && (t === "h" || event.key === Qt.Key_Left))
                stepPreset(-1, true)
            else if (simple && (t === "l" || event.key === Qt.Key_Right))
                stepPreset(1, true)
            else if (simple && (t === "k" || event.key === Qt.Key_Up))
                stepPreset(-presetsView.columns, false)
            else if (simple && (t === "j" || event.key === Qt.Key_Down))
                stepPreset(presetsView.columns, false)
            else if (simple && (t === "-" || t === "_"))
                nudgeVolume(-5)
            else if (simple && (t === "+" || t === "="))
                nudgeVolume(5)
            else if (simple && ["H", "L", "J", "K", "0", "c"].includes(t))
                hit = true // No band shows here, so the band keys do nothing.
            else if (t === "h" || event.key === Qt.Key_Left)
                sel = (sel + 9) % 10
            else if (t === "l" || event.key === Qt.Key_Right)
                sel = (sel + 1) % 10
            else if ((t === "k" || event.key === Qt.Key_Up) && b)
                nudgeGain(sel, .5)
            else if ((t === "j" || event.key === Qt.Key_Down) && b)
                nudgeGain(sel, -.5)
            else if ((t === "L" || t === "H") && b)
                preamp.send({cmd: "setBand", i: sel, f: F.clamp(Math.round(b.f * Math.pow(2, t === "L" ? 1 / 6 : -1 / 6)), 20, 20000)})
            else if ((t === "K" || t === "J") && b)
                preamp.send({cmd: "setBand", i: sel, q: F.clamp(+(b.q * (t === "K" ? 1.15 : 1 / 1.15)).toFixed(2), 0.3, 10)})
            else if (t === "0" && b)
                preamp.send({cmd: "setBand", i: sel, g: 0})
            else if (t === "b")
                preamp.send({cmd: "setBypass"})
            else if (t === "c")
                preamp.send({cmd: "cycleChannel"})
            else if (t === "m") {
                if (view !== "modes")
                    view = "modes"
                else
                    mode = ["basic", "graphic", "parametric"][(["basic", "graphic", "parametric"].indexOf(mode) + 1) % 3]
            } else if (t === ":")
                openCommand()
            else if (t >= "1" && t <= "8") {
                const name = slots[parseInt(t) - 1]
                if (name)
                    preamp.send({cmd: "applyPreset", name: name})
            } else if (t === "?")
                help = true
            else if (t === "q")
                Qt.quit()
            else
                hit = false
            event.accepted = hit
        }

        // The analyzer runs only while someone looks at it.
        Binding {
            target: preamp
            property: "spectrumWanted"
            value: app.view === "studio" && win.visibility !== Window.Hidden && win.visibility !== Window.Minimized
        }

        PresetsView {
            id: presetsView
            anchors.fill: parent
            app: app
            visible: app.view === "presets"
        }
        StudioView {
            anchors.fill: parent
            app: app
            visible: app.view === "studio"
        }
        ModesView {
            id: modes
            anchors.fill: parent
            app: app
            visible: app.view === "modes"
            onCommandFinished: app.forceActiveFocus()
        }
        ChainView {
            anchors.fill: parent
            app: app
            visible: app.view === "chain"
            onAddPlugin: {
                preamp.loadPlugins()
                app.picker = true
            }
        }

        // The command bar on the views that do not have one: it rises over
        // the footer when ":" is pressed.
        CommandBar {
            id: floating
            app: app
            y: parent.height - 31 - height
            width: parent.width
            visible: app.view !== "modes" && active
            onFinished: app.forceActiveFocus()
        }

        PluginPicker {
            anchors.fill: parent
            visible: app.picker
            onClosed: app.closeOverlays()
        }

        HelpOverlay {
            anchors.fill: parent
            simple: app.view === "presets"
            visible: app.help
            onClosed: app.closeOverlays()
        }

        // Until the audio service answers, say so instead of showing a
        // window full of nothing.
        Rectangle {
            anchors.fill: parent
            color: theme.bg
            visible: !preamp.connected

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 64, 560)
                spacing: 18

                Glyph {
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: 56
                    tile: theme.bd
                    input: theme.ln
                    output: theme.ac
                }
                Txt {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: preamp.problem !== "" ? theme.fg : theme.mu
                    text: preamp.problem !== "" ? preamp.problem : qsTr("Starting the audio service…")
                }
            }
        }
    }
}
