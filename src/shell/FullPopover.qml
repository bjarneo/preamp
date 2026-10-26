import QtQuick
import qs.Commons
import "Format.js" as F

// Output, volume with boost, presets and the ten bands, and apps.
Item {
  id: root

  required property var daemon
  required property var pal
  required property var strings
  property string shortcut: "super alt e"
  signal openWindow()

  readonly property var st: daemon.state || {}
  readonly property var bands: daemon.bands
  readonly property bool bypass: st.bypass === true
  readonly property int volume: st.volume || 0
  readonly property string pin: st.pin || ""
  property int selected: 0

  // The popover keeps the design's left to right layout, like the window.
  LayoutMirroring.enabled: false
  LayoutMirroring.childrenInherit: true

  implicitWidth: 380
  implicitHeight: column.implicitHeight

  component Rule: Rectangle {
    width: parent ? parent.width : 0
    height: 1
  }
  component Section: Item {
    default property alias content: inner.data
    property real spacing: 0
    property real padY: 12
    width: parent ? parent.width : 0
    height: inner.implicitHeight + padY * 2
    Column {
      id: inner
      x: 14
      y: parent.padY
      width: parent.width - 28
      spacing: parent.spacing
    }
  }

  Column {
    id: column
    width: parent.width

    // ------------------------------------------------------------ output
    Section {
      spacing: 2
      PText {
        width: parent.width
        height: 14 + 6
        font.pixelSize: 11
        color: root.pal.mu
        text: root.strings.t("output")
      }
      Repeater {
        model: root.st.outputs || []
        Rectangle {
          id: out
          required property var modelData
          readonly property bool on: modelData.name === root.st.output
          width: parent.width
          height: 24
          color: on ? root.pal.hl : "transparent"
          PText {
            x: 6
            y: 4
            color: out.on ? root.pal.ac : root.pal.mu
            text: out.on ? "●" : "○"
          }
          PText {
            x: 6 + 7.2 + 10
            y: 4
            width: parent.width - x - 6 - kind.implicitWidth - 10
            elide: Text.ElideRight
            color: root.pal.fg
            text: String(out.modelData.label || "")
          }
          PText {
            id: kind
            anchors.right: parent.right
            anchors.rightMargin: 6
            y: 4
            color: root.pal.mu
            text: String(out.modelData.kind || "")
          }
          MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.daemon.send({ cmd: "setOutput", name: out.modelData.name })
          }
        }
      }
    }
    Rule { color: root.pal.ln }

    // ------------------------------------------------------------ volume
    Section {
      spacing: 6
      Item {
        width: parent.width
        height: 16
        PText {
          font.pixelSize: 11
          color: root.pal.mu
          text: root.strings.t("volume")
        }
        PText {
          anchors.right: parent.right
          color: root.volume > 100 ? root.pal.yl : root.pal.fg
          text: root.volume + "%"
        }
      }
      VolumeBar {
        width: parent.width
        pal: root.pal
        volume: root.volume
        onMoved: function(v) { root.daemon.send({ cmd: "setVolume", v: v }) }
      }
      Item {
        width: parent.width
        height: 12
        PText {
          font.pixelSize: 10
          color: root.pal.mu
          text: "0"
        }
        PText {
          x: parent.width * 2 / 3 - width / 2
          font.pixelSize: 10
          color: root.pal.mu
          text: "100"
        }
        PText {
          anchors.right: parent.right
          font.pixelSize: 10
          color: root.pal.mu
          text: root.strings.t("150 boost")
        }
      }
    }
    Rule { color: root.pal.ln }

    // ---------------------------------------------------------------- eq
    Section {
      spacing: 12
      Item {
        width: parent.width
        height: 20
        PText {
          anchors.verticalCenter: parent.verticalCenter
          font.pixelSize: 11
          color: root.pal.mu
          text: root.strings.t("eq") + " · " + (root.st.link === false
            ? root.strings.t("L / R split, editing %1").arg(root.st.channel || "L")
            : root.strings.t("L+R linked"))
        }
        Rectangle {
          anchors.right: parent.right
          anchors.verticalCenter: parent.verticalCenter
          width: bypassLabel.implicitWidth + 18
          height: 20
          color: root.bypass ? root.pal.yl : "transparent"
          border.width: 1
          border.color: root.bypass ? root.pal.yl : root.pal.ln
          PText {
            id: bypassLabel
            x: 9
            y: 3
            font.pixelSize: 11
            color: root.bypass ? root.pal.bg : root.pal.mu
            text: root.bypass ? root.strings.t("bypassed") : root.strings.t("bypass")
          }
          MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.daemon.send({ cmd: "setBypass" })
          }
        }
      }
      Flow {
        width: parent.width
        spacing: 6
        Repeater {
          // The everyday presets, then the one pinned to this output.
          model: {
            var list = ["Flat", "Bass boost", "Vocal", "Loudness", "Night"]
            if (root.pin !== "" && list.indexOf(root.pin) < 0) list.push(root.pin)
            return list
          }
          Rectangle {
            id: chip
            required property string modelData
            readonly property bool on: modelData === root.st.preset
            width: chipLabel.implicitWidth + 18
            height: 24
            radius: Style.cornerRadius
            color: on ? root.pal.ac : "transparent"
            border.width: 1
            border.color: on ? root.pal.ac : root.pal.ln
            PText {
              id: chipLabel
              x: 9
              y: 4
              color: chip.on ? root.pal.bg : root.pal.fg
              text: chip.modelData
            }
            MouseArea {
              anchors.fill: parent
              cursorShape: Qt.PointingHandCursor
              onClicked: root.daemon.send({ cmd: "applyPreset", name: chip.modelData })
            }
          }
        }
      }
      Item {
        width: parent.width
        height: 88 + 6 + 13
        Repeater {
          model: root.bands.length
          Item {
            id: band
            required property int index
            readonly property var b: root.bands[index] || { f: 1000, g: 0 }
            readonly property real px: 88 / 24
            readonly property real ty: 44 - b.g * px
            x: index * (parent.width - 30) / 9
            width: 30
            height: parent.height

            Rectangle {
              x: 14
              width: 2
              height: 88
              color: root.pal.ln
            }
            Rectangle {
              x: 6
              y: 44
              width: 18
              height: 1
              color: root.pal.mu
            }
            Rectangle {
              x: 13
              y: Math.min(band.ty, 44)
              width: 4
              height: Math.abs(band.b.g) * band.px
              color: root.bypass ? root.pal.mu : root.pal.ac
            }
            Rectangle {
              x: 7
              y: band.ty - 2
              width: 16
              height: 4
              color: root.pal.fg
            }
            PText {
              anchors.horizontalCenter: parent.horizontalCenter
              y: 88 + 6
              font.pixelSize: 10
              color: band.index === root.selected ? root.pal.fg : root.pal.mu
              text: F.freqShort(band.b.f)
            }
            MouseArea {
              width: 30
              height: 88
              preventStealing: true
              cursorShape: Qt.SizeVerCursor
              function set(mouse) {
                var u = Math.max(0, Math.min(1, 1 - mouse.y / height))
                root.selected = band.index
                root.daemon.send({ cmd: "setBand", i: band.index, g: Math.max(-12, Math.min(12, Math.round((u - .5) * 48) / 2)) })
              }
              onPressed: function(mouse) { set(mouse) }
              onPositionChanged: function(mouse) { if (pressed) set(mouse) }
            }
          }
        }
      }
    }
    Rule { color: root.pal.ln }

    // -------------------------------------------------------------- apps
    Section {
      spacing: 9
      PText {
        width: parent.width
        font.pixelSize: 11
        color: root.pal.mu
        text: root.strings.t("apps")
      }
      Repeater {
        model: root.st.apps || []
        Item {
          id: app
          required property var modelData
          width: parent.width
          height: 16
          PText {
            width: 76
            elide: Text.ElideRight
            color: root.pal.fg
            text: String(app.modelData.name || "")
          }
          LineBar {
            x: 76 + 12
            y: 1
            width: parent.width - 76 - 48 - 24
            pal: root.pal
            fraction: (app.modelData.volume || 0) / 100
            fill: app.modelData.muted ? root.pal.ln : root.pal.ac
            onMoved: function(f) { root.daemon.send({ cmd: "setAppVolume", key: app.modelData.key, v: Math.round(f * 100) }) }
          }
          PText {
            anchors.right: parent.right
            color: app.modelData.muted ? root.pal.rd : root.pal.fg
            text: app.modelData.muted ? root.strings.t("muted") : (app.modelData.volume || 0) + "%"
            MouseArea {
              anchors.fill: parent
              anchors.margins: -3
              cursorShape: Qt.PointingHandCursor
              onClicked: root.daemon.send({ cmd: "toggleAppMute", key: app.modelData.key })
            }
          }
        }
      }
      PText {
        visible: (root.st.apps || []).length === 0
        font.pixelSize: 11
        color: root.pal.mu
        text: root.strings.t("Nothing is playing.")
      }
    }
    Rule { color: root.pal.ln }

    Section {
      padY: 10
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
}
