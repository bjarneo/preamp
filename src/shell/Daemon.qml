import QtQuick
import Quickshell
import Quickshell.Io

// The link to the preamp daemon: one JSON object per line over its socket.
// The daemon pushes its whole state on every change.
QtObject {
  id: root

  property var state: ({})
  property bool ready: false
  readonly property bool connected: socket.connected && ready
  readonly property var bands: state && state.bands ? (state.link === false && state.channel === "R" ? state.bands.R : state.bands.L) || [] : []

  function send(message) {
    if (!socket.connected) return
    socket.write(JSON.stringify(message) + "\n")
    socket.flush()
  }

  function start() {
    Quickshell.execDetached(["systemctl", "--user", "enable", "--now", "preamp.service"])
  }

  property Socket socket: Socket {
    path: Quickshell.env("XDG_RUNTIME_DIR") + "/preamp.sock"
    connected: true
    onConnectionStateChanged: {
      if (connected) {
        root.send({ cmd: "subscribe", state: true, spectrum: false })
      } else {
        root.ready = false
      }
    }
    parser: SplitParser {
      onRead: function(data) {
        // Our own daemon on a private socket, but still only take what
        // parses and has the shape we expect.
        if (data.length > (1 << 20)) return
        var message
        try { message = JSON.parse(data) } catch (e) { return }
        if (!message || message.type !== "state" || typeof message !== "object") return
        root.state = message
        root.ready = true
      }
    }
  }

  property Timer retry: Timer {
    interval: 2000
    repeat: true
    running: !root.socket.connected
    onTriggered: root.socket.connected = true
  }
}
