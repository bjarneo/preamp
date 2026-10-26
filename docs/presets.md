# Presets and headphone profiles

A preset is a curve for the ten bands. You can apply a preset from the bar, from the window or from the command line.

## Built-in presets

| Key | Preset | What it does |
|---|---|---|
| `1` | Flat | The sound as it is |
| `2` | Bass boost | More low end |
| `3` | Vocal | Clearer voices |
| `4` | Loudness | Fuller sound at low volume |
| `5` | Night | Softer lows and highs for late hours |
| `6` | Podcast | Clear speech, less rumble |
| | Treble boost | More detail and air |
| | Bass cut | Less boom from small speakers |
| | Warm | Rich lows, smooth highs |
| | Rock | Punchy drums, bright guitars |
| | Movie | Deep effects, clear dialog |
| | Gaming | Footsteps and cues stand out |

Keys `1` to `6` always apply the first six presets. Keys `7` and `8` apply the two other presets that you used last. These can be built-in presets, headphone profiles or your own presets.

The gain of each band in dB:

| Preset | 32 | 64 | 125 | 250 | 500 | 1k | 2k | 4k | 8k | 16k |
|---|---|---|---|---|---|---|---|---|---|---|
| Flat | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| Bass boost | 6 | 5 | 3.5 | 1.5 | 0 | 0 | 0 | 0 | 0 | 0 |
| Vocal | -2 | -1.5 | -1 | 0 | 1.5 | 3 | 3.5 | 2 | 0 | -1 |
| Loudness | 5 | 4 | 2 | 0 | -1 | -1 | 0 | 2 | 4 | 4.5 |
| Night | -4 | -3 | -1.5 | 0 | 1 | 1.5 | 1.5 | 0 | -2 | -4 |
| Podcast | -6 | -4 | -1 | 1 | 2 | 2.5 | 2 | 0.5 | -1 | -3 |
| Treble boost | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 2.5 | 4 | 5 |
| Bass cut | -6 | -5 | -3.5 | -1.5 | 0 | 0 | 0 | 0 | 0 | 0 |
| Warm | 2.5 | 2.5 | 2 | 1 | 0 | -0.5 | -1 | -2 | -3 | -4 |
| Rock | 4 | 3 | 1.5 | 0 | -1 | -0.5 | 1 | 2.5 | 3 | 2.5 |
| Movie | 4 | 3.5 | 2 | 0.5 | 0 | 1 | 2.5 | 2 | 1 | 0 |
| Gaming | -2 | -1 | 0 | -1 | -1 | 0.5 | 2.5 | 4 | 3 | 1 |

The 32 Hz band is a low shelf and the 16 kHz band is a high shelf. The other bands are peaks. preamp lowers the input by the highest point of the curve, so a boost does not clip.

## Apply a preset

- **Bar:** Click the widget, then click a preset. In the popover, `←` and `→` step through the presets on keys `1` to `8`.
- **Window:** Press the number key, or click a card in the presets view.
- **Command line:** Run `preamp preset Warm`. To step, run `preamp preset --next`.

## Correct your headphones

preamp can load a correction curve from [AutoEq](https://github.com/jaakkopasanen/AutoEq). AutoEq has profiles for more than 6,000 headphones.

1. Press `:` in the window.
2. Type the name of your headphones, for example `hd 650`.
3. Select the profile and press `Enter`.

To do the same from the command line, run:

```sh
preamp autoeq "hd 650"
```

The profile becomes a preset with the name `<model> · AutoEQ`. To see the matches before you apply one, run `preamp search "hd 650"`.

The search needs a network connection. preamp keeps the AutoEq index in `~/.cache/preamp/` for one week.

## Save your own preset

Shape the bands in the Studio view or the Modes view. Then press `:` and type `w` and a name:

```text
w My curve
```

To delete a saved preset, type `rm My curve`. You cannot delete a built-in preset.

From the command line, run `preamp save "My curve"` and `preamp delete "My curve"`.

## Pin a preset to an output

A pin applies a preset each time preamp plays into that output. For example, pin the AutoEq profile to your headphones and `Loudness` to the laptop speakers.

1. Select the output.
2. Apply the preset.
3. Press `:` and type `pin`, or run `preamp pin`.

To pin a preset to another output, run `preamp pin Loudness --output "Laptop Speakers"`. To remove a pin, type `unpin`, or run `preamp unpin`.

## Switch presets automatically

The Signal chain view shows the pins and the auto-switch rules. The first rule that matches wins.

| Rule | It matches when |
|---|---|
| `output is` | preamp plays into this output. A pin makes this rule. |
| `app in call` | The app plays and records at the same time, or it plays as a call. |
| `app fullscreen` | The app has a fullscreen window. |

To add a rule in the window, click `+ add rule`. The new rule uses the first app that plays and the current preset. Then edit it:

- Click the rule type to switch between `app in call` and `app fullscreen`.
- Click the app name to type another app.
- Click the preset to select the next preset. Right-click it to select a room while the rule holds.
- Click `remove` to delete the rule.

When the rule stops matching, preamp goes back to the preset that you used before.

From the command line:

```sh
preamp rule add call Discord --preset Podcast
preamp rule add fullscreen Steam --preset Loudness --room hall
preamp rules
preamp rule set 2 --preset Movie
preamp rule remove 1
```
