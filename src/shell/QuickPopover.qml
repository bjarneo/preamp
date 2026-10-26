import QtQuick
import "Format.js" as F

// The output, volume, bass, mid and treble, and the preset, in one glance.
Item {
  id: root

  required property var daemon
  required property var pal
  required property var strings
  property string shortcut: "super alt e"
  signal openWindow()

  readonly property var st: daemon.state || {}
  readonly property int volume: st.volume || 0
  readonly property var output: (st.outputs || []).find(function(o) { return o.name === st.output }) || null
  readonly property string pin: st.pin || ""

  // The popover keeps the design's left to right layout, like the window.
  LayoutMirroring.enabled: false
  LayoutMirroring.childrenInherit: true

  implicitWidth: 352
  implicitHeight: column.implicitHeight + 32

  function roomLabel(r) {
    return r === "off" ? strings.t("off") : r === "small room" ? strings.t("small room")
         : r === "studio" ? strings.t("studio") : r === "hall" ? strings.t("hall") : (r || "")
  }

  Column {
    id: column
    x: 16
    y: 16
    width: parent.width - 32
    spacing: 18

    // Output and volume.
    Item {
      width: parent.width
      height: 35
      Column {
        width: parent.width - big.width - 12
        spacing: 4
        PText {
          width: parent.width
          elide: Text.ElideRight
          font.pixelSize: 13
          color: root.pal.fg
          text: String(root.st.outputLabel || "")
        }
        PText {
          width: parent.width
          elide: Text.ElideRight
          font.pixelSize: 11
          color: root.pal.mu
          text: (root.output ? root.output.kind + " · " + root.output.rate + " · " : "") + root.strings.t("click to switch")
        }
        MouseArea {
          width: parent.width
          height: 35
          y: -parent.y
          cursorShape: Qt.PointingHandCursor
          onClicked: root.daemon.send({ cmd: "nextOutput" })
        }
      }
      Row {
        id: big
        anchors.right: parent.right
        // line-height 1 in the design: the 37 px line box shrinks to 28
        // and the text rises by half the difference.
        PText {
          id: number
          y: -4.5
          font.pixelSize: 28
          lineHeight: 37
          height: 28
          color: root.volume > 100 ? root.pal.yl : root.pal.fg
          text: root.volume
        }
        PText {
          y: number.y + number.baselineOffset - baselineOffset
          font.pixelSize: 13
          color: root.pal.mu
          text: "%"
        }
      }
    }

    // Pulled up 6 px under the output, as the design sets it.
    Item {
      width: parent.width
      height: 10
      VolumeBar {
        y: -6
        width: parent.width
        pal: root.pal
        volume: root.volume
        onMoved: function(v) { root.daemon.send({ cmd: "setVolume", v: v }) }
      }
    }

    // Bass, mid and treble.
    Item {
      width: parent.width
      height: macros.implicitHeight + 17
      Rectangle {
        width: parent.width
        height: 1
        color: root.pal.ln
      }
      Column {
        id: macros
        y: 17
        width: parent.width
        spacing: 14
        Repeater {
          model: 3
          Item {
            id: macro
            required property int index
            readonly property real v: root.st.macros ? Math.round(root.st.macros[index] * 10) / 10 : 0
            readonly property real p: (v + 12) / 24
            width: parent.width
            height: 16
            PText {
              color: root.pal.fg
              text: [root.strings.t("bass"), root.strings.t("mid"), root.strings.t("treble")][macro.index]
            }
            Item {
              id: track
              x: 56 + 12
              width: parent.width - 56 - 44 - 24
              height: 16
              Rectangle {
                y: 7
                width: parent.width
                height: 2
                color: root.pal.ln
              }
              Rectangle {
                x: parent.width / 2
                y: 2
                width: 1
                height: 12
                color: root.pal.mu
              }
              Rectangle {
                x: parent.width * Math.min(macro.p, .5)
                y: 6
                width: parent.width * Math.abs(macro.p - .5)
                height: 4
                color: root.st.bypass ? root.pal.mu : root.pal.ac
              }
              Rectangle {
                x: parent.width * macro.p - 3
                y: 1
                width: 6
                height: 14
                color: root.pal.fg
              }
              MouseArea {
                anchors.fill: parent
                preventStealing: true
                cursorShape: Qt.SizeHorCursor
                function set(mouse) {
                  var u = Math.max(0, Math.min(1, mouse.x / width))
                  root.daemon.send({ cmd: "setMacro", k: macro.index, v: Math.round((u * 24 - 12) * 2) / 2 })
                }
                onPressed: function(mouse) { set(mouse) }
                onPositionChanged: function(mouse) { if (pressed) set(mouse) }
              }
            }
            PText {
              anchors.right: parent.right
              color: root.pal.fg
              text: F.gain(macro.v)
            }
          }
        }
      }
    }

    // The preset, with the one auto-switch would pick.
    Item {
      width: parent.width
      height: 15 + 34
      Rectangle {
        width: parent.width
        height: 1
        color: root.pal.ln
      }
      Item {
        y: 15
        width: parent.width
        height: 34
        PText {
          x: 10
          anchors.verticalCenter: parent.verticalCenter
          font.pixelSize: 15
          color: root.pal.ac
          text: "‹"
          MouseArea {
            anchors.fill: parent
            anchors.margins: -10
            cursorShape: Qt.PointingHandCursor
            onClicked: root.daemon.send({ cmd: "prevPreset" })
          }
        }
        Column {
          anchors.horizontalCenter: parent.horizontalCenter
          spacing: 4
          PText {
            anchors.horizontalCenter: parent.horizontalCenter
            color: root.pal.fg
            text: String(root.st.preset || "") + (root.st.dirty ? " · " + root.strings.t("edited") : "")
          }
          PText {
            anchors.horizontalCenter: parent.horizontalCenter
            font.pixelSize: 11
            color: root.pal.mu
            text: root.st.auto ? root.strings.t("auto · matched this output")
                : root.pin !== "" ? root.strings.t("manual · auto would pick %1").arg(F.shortPreset(root.pin))
                : root.strings.t("manual · nothing pinned")
          }
        }
        PText {
          anchors.right: parent.right
          anchors.rightMargin: 10
          anchors.verticalCenter: parent.verticalCenter
          font.pixelSize: 15
          color: root.pal.ac
          text: "›"
          MouseArea {
            anchors.fill: parent
            anchors.margins: -10
            cursorShape: Qt.PointingHandCursor
            onClicked: root.daemon.send({ cmd: "nextPreset" })
          }
        }
      }
    }

    Item {
      width: parent.width
      height: 15 + 16
      Rectangle {
        width: parent.width
        height: 1
        color: root.pal.ln
      }
      PText {
        y: 15
        color: root.pal.fg
        text: root.strings.t("spatial")
      }
      PText {
        y: 15
        anchors.right: parent.right
        color: root.pal.ac
        text: root.roomLabel(root.st.room)
      }
      MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.daemon.send({ cmd: "nextRoom" })
      }
    }

    Item {
      width: parent.width
      height: 14
      PText {
        font.pixelSize: 11
        color: openArea.containsMouse ? root.pal.fg : root.pal.mu
        text: root.strings.t("open preamp")
        MouseArea {
          id: openArea
          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor
          onClicked: root.openWindow()
        }
      }
      PText {
        anchors.right: parent.right
        font.pixelSize: 11
        color: root.pal.ac
        text: root.shortcut
      }
    }
  }
}
