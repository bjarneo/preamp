---
name: preamp
description: Drive preamp, the system audio equalizer on Omarchy, from the command line. Use when asked to change the EQ, apply a preset or a headphone profile, switch the audio output, change or boost the volume, or bypass the equalizer.
---

# preamp

preamp runs as a user service (`preamp.service`) that owns the "preamp" sink and plays it, through a ten-band equalizer, into the output the user picked. Every command talks to that service, starts it through systemd when it is not running, exits 0 or 1, and prints errors on stderr. Add `--json` to any command for the service's full answer.

## Read

```sh
preamp status --json     # preset, bands, preamp, output, volume, room, apps, plugins, rules
preamp presets           # the current preset has a star
preamp outputs           # the current output has a star; the last column is the name to pass
```

## Change

```sh
preamp preset "Bass boost"                 # apply a preset by name
preamp autoeq "hd 650"                     # fetch and apply the best AutoEq profile for a headphone
preamp band 3 --freq 125 --gain -2 --q 1.4 # bands are 1 to 10; gain -12 to 12 dB; q 0.3 to 10
preamp bypass on | off                     # switch the equalizer off or on
preamp output "Laptop Speakers"            # by label or by node name
preamp volume 80                           # 0 to 150; above 100 is boost
preamp pin                                 # use the current preset whenever this output is picked
```

## Notes

- The preamp gain is automatic: it cuts the input so the loudest point of the curve stays at 0 dB.
- `preamp` with no arguments opens the window; the user can also press `?` there for every key.
- Do not edit `~/.config/preamp/config.json` while the service runs; it rewrites the file.
