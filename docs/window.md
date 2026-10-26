# The window

To open the window, run `preamp` or click `open preamp` in the bar popover. You can also add a key binding. See [Install](install.md#open-the-window-with-a-key).

Press `tab` to go to the next view. The window opens on the view that you used last. Press `?` to show the keys for the view.

## Views

| View | What it shows |
|---|---|
| Presets | One card for each preset with the shape of its curve. The output and the volume are below the cards. |
| Studio | The curve over the live analyzer, the ten bands, the presets on keys `1` to `8`, and the effects. |
| Modes | Three ways to edit the same ten bands, and the command bar. |
| Signal chain | Apps, equalizer, effects, room and output, in the order that the audio flows. The auto-switch rules are below. |

![The presets view](presets.png)

| Studio | Modes | Signal chain |
|---|---|---|
| ![studio](studio.png) | ![modes](modes.png) | ![signal chain](chain.png) |

The Modes view has three modes. Press `m` to go to the next mode.

- **basic:** Three controls, bass, mid and treble. Each control moves a group of bands.
- **graphic:** One slider for each band.
- **parametric:** The frequency, gain and Q of each band, over the curve.

## Keys

In the presets view:

| Key | Action |
|---|---|
| `h` `l` or `←` `→` | Use the previous or next preset |
| `j` `k` or `↓` `↑` | Use the preset above or below |
| `-` `+` | Decrease or increase the volume by 5% |
| `1` to `8` | Apply a preset |
| `b` | Bypass the equalizer |
| `tab` | Go to the next view |
| `q` | Quit |

In the other views:

| Key | Action |
|---|---|
| `h` `l` or `←` `→` | Select a band |
| `j` `k` or `↓` `↑` | Decrease or increase the gain of the band |
| `H` `L` | Decrease or increase the frequency of the band |
| `J` `K` | Decrease or increase the Q of the band |
| `0` | Set the gain of the band to 0 |
| `1` to `8` | Apply a preset |
| `c` | Select the channel: L+R, L or R |
| `b` | Bypass the equalizer |
| `m` | Select the next mode: basic, graphic or parametric |
| `:` | Open the command bar |
| `tab` | Go to the next view |
| `q` | Quit |

In the Studio view, you can also drag a node. Drag it sideways to change the frequency, and up or down to change the gain.

## Command bar

Press `:` to open the command bar. Type to find presets and AutoEq profiles. Press `Enter` to apply the selected row.

The command bar also accepts these commands:

| Command | Action |
|---|---|
| `w NAME` | Save the curve as a preset |
| `rm NAME` | Delete a saved preset |
| `pin` | Apply the current preset each time this output is selected |
| `unpin` | Remove the pin from this output |

## Channels

With `L+R`, an edit changes both channels. With `L` or `R`, you edit one channel and the other one stays as it is. When you go back to `L+R`, both channels take the curve of the channel that you edited.

## Bypass

Bypass turns off the preamp gain and the ten bands, so you hear the difference. Effects, room, width and balance stay on. Press `b`, click `bypass`, or right-click the bar widget.
