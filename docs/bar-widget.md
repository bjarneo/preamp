# Bar widget

The widget is the preamp mark in the bar color. The tall block dims while the equalizer is off.

| Action | Result |
|---|---|
| Click | Open or close the popover |
| Right-click | Bypass the equalizer, or switch it on again |
| Scroll | Change the volume in steps of 5% |

To put the widget on the bar, run `preamp shell install`. The command copies the widget to `~/.config/omarchy/plugins/preamp/` and puts it before the audio widget.

The widget works on Omarchy 4.0.6 and later. If it does not show in the bar, see [Troubleshooting](troubleshooting.md#the-bar-widget-does-not-show).

## Popover

The full popover shows the outputs, the volume, the everyday presets with the ten bands, and the volume of each app. The quick popover is smaller.

| Full popover | Quick popover |
|---|---|
| ![full popover](popover-full.png) | ![quick popover](popover-quick.png) |

To show the quick popover, run:

```sh
omarchy bar set preamp variant quick
```

To go back to the full popover, set `variant` to `full`.

Keys in the popover:

| Key | Action |
|---|---|
| `↑` `↓` | Increase or decrease the volume by 5% |
| `←` `→` | Use the previous or next preset on keys `1` to `8` |
| `b` | Bypass the equalizer |
| `o` | Play into the next output |
| `e` | Open the window |
| `esc` | Close the popover |

The popover shows the key that opens the window. The default is `super alt e`. If you use another key, set it with:

```sh
omarchy bar set preamp shortcut "super shift e"
```

This setting changes only the text. To make the key binding, see [Install](install.md#open-the-window-with-a-key).

When the service does not run, the popover says so. Click `start it now and at every login` to start it.

## Open the popover from a script

The Omarchy shell gives the widget the IPC target `preamp`:

```sh
omarchy-shell preamp open
omarchy-shell preamp close
omarchy-shell preamp toggle
```

To open and close the popover with a key, add this line to `~/.config/hypr/bindings.lua`:

```lua
o.bind("SUPER + ALT + P", "preamp popover", { ipc = "preamp.toggle" })
```
