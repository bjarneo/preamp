---
name: preamp
description: Drive preamp, the system audio equalizer on Omarchy, from the command line. Use when asked to change the EQ, apply or save a preset or a headphone profile, switch the audio output, change or boost the volume, set the balance, width or room, change the volume of one app, add or remove audio effects, add rules that switch presets, or bypass the equalizer.
---

# preamp

preamp runs as a user service, `preamp.service`. The service owns the `preamp` sink and plays it through a ten-band equalizer into the output that the user picked. Every command talks to that service and starts it through systemd when it does not run. Each command exits with 0 or 1 and writes errors to stderr. Add `--json` to any command to get the full answer of the service.

## Read

```sh
preamp status --json   # preset, bands, preamp gain, output, volume, room, apps, plugins, rules
preamp presets         # * marks the current preset, a number marks its key in the window
preamp bands           # the ten bands of the channel that is edited
preamp outputs         # * marks the current output, the last column is the node name
preamp apps            # the apps that play, with volume and state
preamp plugins         # the effects in the chain, in order
preamp rules           # the pins and the auto-switch rules
preamp watch           # one JSON line for each change, --spectrum adds the analyzer
```

## Change

```sh
preamp preset "Bass boost"                     # apply a preset by name, or use --next and --prev
preamp autoeq "hd 650"                         # fetch and apply the best AutoEq profile
preamp search "sonos"                          # find presets and AutoEq profiles, apply nothing
preamp save "My curve"                         # save the curve, delete NAME removes a saved preset
preamp band 3 --freq 125 --gain -2 --q 1.4     # bands 1 to 10, gain -12 to 12 dB, q 0.3 to 10
preamp band 1 --type lowshelf                  # peak, lowshelf or highshelf
preamp tone --bass 3 --treble -1               # move groups of bands, as in the basic mode
preamp channel L                               # edit L, R or L+R
preamp bypass on                               # on or off, no argument switches it
preamp output "Laptop Speakers"                # by label or by node name, or use --next
preamp volume 80                               # 0 to 150, above 100 is boost
preamp balance -20                             # -100 to 100, below 0 is left
preamp width 120                               # 0 to 200 percent, 0 is mono
preamp room studio                             # off, small room, studio or hall
preamp app Spotify --volume 50                 # or --mute and --unmute
preamp plugin add "LSP Compressor"             # see preamp plugins --available
preamp plugin off 1                            # on N, off N, remove N, move N TO
preamp pin                                     # apply this preset each time this output is picked
preamp rule add call Discord --preset Podcast  # or fullscreen, then rule set N and rule remove N
```

## Notes

- The preamp gain is automatic. It cuts the input so that the loudest point of the curve stays at 0 dB.
- Bypass turns off the preamp gain and the ten bands. Effects, room, width and balance stay on.
- `preamp` with no arguments opens the window. The user can press `?` there to see every key.
- Do not edit `~/.config/preamp/config.json` while the service runs. The service writes the file again.
- The socket protocol is in https://github.com/bjarneo/preamp/blob/main/docs/ipc.md.
