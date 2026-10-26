# Command line

Every action of the window and the bar widget has a command. Scripts, key bindings and coding agents can use these commands.

Each command talks to `preamp.service`. If the service does not run, the command starts it. Each command exits with 0 or 1 and writes errors to stderr. Add `--json` to get the full answer of the service.

```sh
preamp status
preamp preset Warm
preamp autoeq "hd 650"
preamp volume 60
preamp --json bands
```

## Read the state

| Command | Output |
|---|---|
| `preamp status` | The preset, the output, the volume, the room and the other main values |
| `preamp presets` | All presets. `*` marks the current one, and a number marks its key in the window. |
| `preamp bands` | The ten bands of the channel that you edit, or of both channels when they differ |
| `preamp outputs` | All outputs. `*` marks the current one, and the last column is the node name. |
| `preamp apps` | The apps that play, with their volume and state |
| `preamp plugins` | The effects in the chain, in order |
| `preamp plugins --available` | The installed LV2 and LADSPA effects that fit a stereo chain |
| `preamp rules` | The pins and the auto-switch rules |
| `preamp watch` | One JSON line for each change of the state. Add `--spectrum` to get the analyzer levels too. |
| `preamp version` | The version of preamp |

## Presets

| Command | Action |
|---|---|
| `preamp preset NAME` | Apply a preset |
| `preamp preset --next` | Apply the next preset on keys `1` to `8` |
| `preamp preset --prev` | Apply the previous preset on keys `1` to `8` |
| `preamp save NAME` | Save the curve as a preset |
| `preamp delete NAME` | Delete a saved preset |
| `preamp search QUERY` | Find presets and AutoEq profiles. Nothing changes. |
| `preamp autoeq QUERY` | Apply the best AutoEq profile for a headphone |
| `preamp pin [PRESET] [--output OUT]` | Pin a preset to an output. The defaults are the current preset and output. |
| `preamp unpin [--output OUT]` | Remove the pin from an output |

## Equalizer

| Command | Action |
|---|---|
| `preamp bypass [on\|off]` | Turn off the equalizer, or turn it on. Without an argument, the command switches it. |
| `preamp band N [--freq HZ] [--gain DB] [--q Q] [--type TYPE]` | Change band `N`, from 1 to 10 |
| `preamp tone [--bass DB] [--mid DB] [--treble DB]` | Show or change bass, mid and treble, as in the basic mode |
| `preamp channel [L+R\|L\|R\|--next]` | Show or select the channel that you edit |

The limits for a band are:

- **Frequency:** 20 to 20,000 Hz.
- **Gain:** -12 to 12 dB.
- **Q:** 0.3 to 10.
- **Type:** `peak`, `lowshelf` or `highshelf`.

## Output

| Command | Action |
|---|---|
| `preamp output NAME` | Play into an output. Use the label or the node name. |
| `preamp output --next` | Play into the next output |
| `preamp volume [0..150]` | Show or set the volume of the output. Above 100 is boost. |
| `preamp balance [-100..100]` | Show or set the balance. Below 0 is left. |
| `preamp width [0..200]` | Show or set the stereo width in percent. 0 is mono and 100 is unchanged. |
| `preamp room [ROOM\|--next]` | Show or set the room: `off`, `small room`, `studio` or `hall` |

## Apps

| Command | Action |
|---|---|
| `preamp app NAME --volume 0..100` | Set the volume of one app |
| `preamp app NAME --mute` | Mute one app |
| `preamp app NAME --unmute` | Unmute one app |

## Effects

| Command | Action |
|---|---|
| `preamp plugin add ID\|NAME` | Add an effect at the end of the chain |
| `preamp plugin on N` | Turn on effect `N` |
| `preamp plugin off N` | Turn off effect `N` |
| `preamp plugin remove N` | Remove effect `N` |
| `preamp plugin move N TO` | Move effect `N` to place `TO` |

Effects and rules count from 1, in the order that `preamp plugins` and `preamp rules` show.

## Rules

| Command | Action |
|---|---|
| `preamp rule add call\|fullscreen APP [--preset P] [--room ROOM]` | Add a rule. The default preset is the current one. |
| `preamp rule set N [--kind call\|fullscreen] [--app APP] [--preset P] [--room ROOM]` | Change rule `N` |
| `preamp rule remove N` | Remove rule `N` |

Pins are rules too, but `preamp pin` and `preamp unpin` own them. See [Presets](presets.md#switch-presets-automatically).

## Setup

| Command | Action |
|---|---|
| `preamp` or `preamp open` | Open the window |
| `preamp daemon` | Run the audio service. systemd starts it for you. |
| `preamp shell install` | Put the widget on the Omarchy bar |
| `preamp skill install` | Teach coding agents the preamp commands |

## Use preamp from an agent

`preamp skill install` writes a skill to `~/.claude/skills/preamp/SKILL.md`. Claude Code then knows the preamp commands. Other agents can read the same file.

Then ask the agent in your own words, for example:

- "Load the AutoEq profile for my Sonos Ace."
- "Make the bass stronger on the laptop speakers and pin it."
- "Lower Spotify to 40% while Discord is in a call."

## Script examples

To show the preset in a script:

```sh
preamp --json status | jq -r .preset
```

To bind a key to the next output, add this line to `~/.config/hypr/bindings.lua`:

```lua
o.bind("SUPER + ALT + O", "Next audio output", "preamp output --next")
```

To follow every change of the volume:

```sh
preamp watch | jq --unbuffered '.volume'
```

For programs that keep a connection open, see the [socket protocol](ipc.md).
