# Output, effects and room

The audio goes through preamp in this order:

```text
apps → preamp sink → preamp gain → ten bands → effects → width → room → balance → output
```

The Signal chain view shows the same order. Bypass turns off the preamp gain and the ten bands. The other steps stay on.

## Select the output

preamp plays into one output: speakers, headphones, a USB interface or an HDMI display. Select it in one of these places:

- The presets view or the Signal chain view.
- The bar popover. Press `o` in the popover to go to the next output.
- The Omarchy audio panel. preamp plays into that output and stays the default.
- The command line: `preamp output "Laptop Speakers"`, or `preamp output --next`.

When you connect headphones that you used before, preamp moves to them. It applies their pinned preset and shows a notification. A virtual sink, for example a loopback or another equalizer, does not take the output.

## Volume

The volume in preamp is the volume of the output device. It goes up to 150%. Above 100%, the device plays louder than its own maximum, so the sound can distort.

```sh
preamp volume 60
```

## Volume for each app

The Signal chain view lists the apps that play. Drag the slider of an app to change its volume. Click the percentage to mute the app.

```sh
preamp apps
preamp app Spotify --volume 40
preamp app Spotify --mute
```

## Effects

LV2 and LADSPA effects run after the equalizer, in the order of the list. preamp shows only effects that fit a stereo chain. A mono effect runs once for each channel.

To add an effect:

1. Open the Signal chain view.
2. Click `+ add plugin`.
3. Type to filter the list, then press `Enter`.

Click an effect to turn it off or on. Right-click it to remove it.

## Effect controls

Each effect starts with its own defaults. To change a control, list the controls of the effect, then set one by its symbol or its name:

```sh
preamp plugin params 1
preamp plugin set 1 cr 6
preamp plugin set 1 "Compression mode" Up
preamp plugin set 1 cr default
```

A new value takes effect in the next block of audio. The effect keeps playing, so a compressor or a limiter does not reset. preamp keeps the values of each effect in the chain when it restarts.

To get a compressor, a limiter and more, install `lsp-plugins-lv2`:

```sh
sudo pacman -S lsp-plugins-lv2
```

preamp finds LV2 effects through `lilv`. It looks for LADSPA effects in `LADSPA_PATH`. Without that variable, it looks in `/usr/lib/ladspa`, `/usr/local/lib/ladspa`, `/usr/lib64/ladspa` and `~/.ladspa`.

From the command line:

```sh
preamp plugins --available
preamp plugin add "LSP Limiter"
preamp plugin off 1
preamp plugin move 2 1
preamp plugin params 1
preamp plugin set 1 cr 6
preamp plugin remove 2
```

## Width

Width changes the stereo image. 0% is mono, 100% leaves the sound as it is, and 200% is twice as wide.

```sh
preamp width 120
```

## Room

The room adds a reverb: `off`, `small room`, `studio` or `hall`. An auto-switch rule can also select a room while it holds.

```sh
preamp room studio
```

## Balance

Balance turns one side down. -100 is left only, 0 is the center, and 100 is right only.

```sh
preamp balance -20
```
