import QtQuick
import Quickshell
import qs.Ui
import qs.Commons

// The preamp mark. Click for the popover; right click switches the
// equalizer off and on; scroll changes the volume.
//
// The widget uses only the shell API that Omarchy 4.0 has too: its own
// strings, and the popover as children of KeyboardPanel, not its lazy
// content property from Omarchy 4.5.
Panel {
  id: root
  moduleName: "preamp"
  ipcTarget: "preamp"

  readonly property string variant: setting("variant", "full") === "quick" ? "quick" : "full"
  readonly property string shortcut: setting("shortcut", "super alt e")
  readonly property var st: link.state || {}
  // The popover is built the first time it opens and stays after that.
  property bool popoverBuilt: false
  onOpenedChanged: if (opened) popoverBuilt = true

  function openWindow() {
    root.close()
    Quickshell.execDetached(["preamp"])
  }

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  Strings {
    id: catalog
  }

  Tokens {
    id: swatch
  }

  Daemon {
    id: link
  }

  WidgetButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: ""
    hasVisualContent: true
    // The same slot as the icons beside it, along either bar.
    fixedWidth: button.vertical ? -1 : Style.bar.iconSlot
    fixedHeight: button.vertical ? Style.bar.iconSlot : -1
    tooltipText: link.connected ? "preamp · " + String(root.st.outputLabel || "") : catalog.t("preamp is not running")

    onPressed: function(b) {
      if (b === Qt.RightButton && link.connected) link.send({ cmd: "setBypass" })
      else root.toggle()
    }
    onWheelMoved: function(delta) {
      if (!link.connected) return
      var v = Math.max(0, Math.min(150, (root.st.volume || 0) + (delta > 0 ? 5 : -5)))
      link.send({ cmd: "setVolume", v: v })
    }

    Rectangle {
      anchors.fill: parent
      color: swatch.hl
      visible: root.opened
    }
    // The bar's text color, like every other bar icon. The output block
    // dims as much as a dimmed bar widget while the equalizer is off.
    Mark {
      readonly property color ink: button.foreground
      anchors.centerIn: parent
      size: 14
      scale: "pressScale" in button ? button.pressScale : 1
      input: ink
      output: link.connected && root.st.bypass !== true ? ink : Qt.rgba(ink.r, ink.g, ink.b, .45)
    }
  }

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    padding: 0
    focusTarget: keys
    contentWidth: panel.fittedContentWidth(root.variant === "quick" ? 356 : 384)
    contentHeight: panel.fittedContentHeight(keys.implicitHeight)

    PanelKeyCatcher {
      id: keys
      implicitHeight: body.item ? body.item.implicitHeight : 0
      anchors.fill: parent
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }
      onMoveRequested: function(dx, dy) {
        if (!link.connected) return
        if (dy !== 0) link.send({ cmd: "setVolume", v: Math.max(0, Math.min(150, (root.st.volume || 0) - dy * 5)) })
        else if (dx !== 0) link.send({ cmd: dx > 0 ? "nextPreset" : "prevPreset" })
      }
      onTextKey: function(t) {
        if (!link.connected) return
        if (t === "b") link.send({ cmd: "setBypass" })
        else if (t === "o") link.send({ cmd: "nextOutput" })
        else if (t === "e") root.openWindow()
      }

      Loader {
        id: body
        width: parent.width
        active: link.connected && root.popoverBuilt
        sourceComponent: root.variant === "quick" ? quick : full
      }

      // Without the daemon there is nothing to show but how to start it.
      Item {
        visible: !link.connected
        width: parent.width
        height: visible ? 120 : 0
        Column {
          anchors.centerIn: parent
          width: parent.width - 32
          spacing: 10
          PText {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            color: swatch.fg
            text: catalog.t("preamp is not running")
          }
          PText {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 11
            color: startArea.containsMouse ? swatch.fg : swatch.ac
            text: catalog.t("start it now and at every login")
            MouseArea {
              id: startArea
              anchors.fill: parent
              hoverEnabled: true
              cursorShape: Qt.PointingHandCursor
              onClicked: link.start()
            }
          }
        }
      }
    }
  }

  Component {
    id: full
    FullPopover {
      daemon: link
      pal: swatch
      strings: catalog
      shortcut: root.shortcut
      onOpenWindow: root.openWindow()
    }
  }

  Component {
    id: quick
    QuickPopover {
      daemon: link
      pal: swatch
      strings: catalog
      shortcut: root.shortcut
      onOpenWindow: root.openWindow()
    }
  }
}
