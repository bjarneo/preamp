import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons as Commons

// The window's color tokens, read from the theme's colors.toml the same way
// the preamp window derives them, so the popover and the window match.
QtObject {
  id: root

  property color bg: "#1a1b26"
  property color bd: "#16161e"
  property color hl: "#292e42"
  property color ln: "#3b4261"
  property color fg: "#c0caf5"
  property color mu: "#737aa2"
  property color ac: "#7aa2f7"
  property color a2: "#7dcfff"
  property color yl: "#e0af68"
  property color rd: "#f7768e"

  function mix(a, b, t) {
    return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1)
  }

  function luminance(c) {
    function lin(v) { return v <= .04045 ? v / 12.92 : Math.pow((v + .055) / 1.055, 2.4) }
    return .2126 * lin(c.r) + .7152 * lin(c.g) + .0722 * lin(c.b)
  }

  function contrast(a, b) {
    var la = luminance(a), lb = luminance(b)
    return (Math.max(la, lb) + .05) / (Math.min(la, lb) + .05)
  }

  function load(text) {
    var keys = {}
    var lines = String(text || "").split("\n")
    for (var i = 0; i < lines.length; i++) {
      var m = lines[i].match(/^\s*([a-z_]+)\s*=\s*["'](#[0-9a-fA-F]{6})["']/)
      if (m) keys[m[1]] = Qt.color(m[2])
    }
    function key(name, fallback) { return keys[name] !== undefined ? keys[name] : Qt.color(fallback) }
    bg = key("background", "#1a1b26")
    fg = key("foreground", "#c0caf5")
    bd = keys.dark_background !== undefined ? keys.dark_background : mix(bg, Qt.color("black"), .18)
    hl = keys.selection !== undefined ? keys.selection : keys.lighter_background !== undefined ? keys.lighter_background : mix(bg, fg, .1)
    var quiet = keys.dark_foreground !== undefined ? keys.dark_foreground : mix(fg, bg, .42)
    mu = contrast(quiet, bg) < 2.4 ? mix(fg, bg, .42) : quiet
    ln = mix(hl, mu, .25)
    ac = key("accent", "#7aa2f7")
    a2 = keys.cyan !== undefined ? keys.cyan : key("blue", "#7dcfff")
    yl = key("yellow", "#e0af68")
    rd = key("red", "#f7768e")
  }

  property FileView file: FileView {
    path: Quickshell.env("HOME") + "/.local/state/omarchy/current/theme/colors.toml"
    watchChanges: true
    printErrors: false
    onLoaded: root.load(text())
    onFileChanged: reload()
  }

  // A theme switch replaces the file behind a symlink, which a file watch
  // can miss; the shell's own palette changing is the dependable signal.
  readonly property var themeSignal: [Commons.Color.accent, Commons.Color.background, Commons.Color.foreground]
  onThemeSignalChanged: file.reload()
}
