# Troubleshooting

## There is no sound

1. Check the service:

   ```sh
   systemctl --user status preamp.service
   preamp status
   ```

2. Check that the default sink is `preamp` and that preamp plays into your device:

   ```sh
   wpctl inspect @DEFAULT_AUDIO_SINK@ | grep node.name
   preamp outputs
   ```

3. If the output is wrong, select the right one:

   ```sh
   preamp output "Laptop Speakers"
   ```

4. If the sound is still missing, restart the service:

   ```sh
   systemctl --user restart preamp.service
   ```

## The window says that the service is not running

Start the service now and at each login:

```sh
systemctl --user enable --now preamp.service
```

## Audio breaks up when the computer is busy

If audio breaks up under load, check that `rtkit` is installed:

```sh
pacman -Q rtkit
```

Without `rtkit`, PipeWire and preamp run audio at normal priority. Omarchy on x86_64 does not install `rtkit` at this time. The fix for Omarchy is in [omacom/omarchy#12428](https://github.com/omacom/omarchy/pull/12428) and [omacom/omarchy#16353](https://github.com/omacom/omarchy/pull/16353).

To install it:

1. Install the package:

   ```sh
   sudo pacman -S rtkit
   ```

2. Log out and log in again.

If you install `rtkit` and do not log in again, the desktop portal still reports a realtime budget of 0. Apps that ask through the portal then get a hard realtime limit of 0, and the kernel stops them when they play audio. preamp asks RTKit directly, so it is safe, but WirePlumber and your other apps are not.

## Bluetooth headphones sound thin

If an app records from the microphone of your headset, WirePlumber switches the headset to the call profile. The call profile has mono sound at 8, 16 or 32 kHz. Voice assistants and dictation tools record all the time, so the headset stays in the call profile.

To get the music profile back, make the app record from another microphone. preamp itself does not record.

To see the profile, run:

```sh
pactl list cards | grep -E 'Name: bluez|Active Profile'
```

`a2dp-sink` is the music profile. A profile with `headset-head-unit` in its name is the call profile.

## The volume keys and the speaker tuning

On a laptop with an Omarchy speaker tuning, the Omarchy volume keys change the volume of the tuning, not of the speakers. The preamp volume and the keys then add up. If the sound is too quiet, set the volume in preamp:

```sh
preamp volume 60
```

The fix for Omarchy is in [omacom/omarchy#16687](https://github.com/omacom/omarchy/pull/16687).

## The AutoEq search finds nothing

The window shows `AutoEq is out of reach` when preamp cannot download the AutoEq index. Check the network connection, then search again. When the index is in `~/.cache/preamp/`, the search works without a network for one week.

## The Signal chain view is cut off

The Signal chain view needs a window that is 1096 pixels wide or more. In a narrower window, the boxes on the right are cut off. Make the window wider, or use the command line for the same actions.

## A translation is wrong

The translations need a review by native speakers, mostly for Swahili, Irish, Sinhala, Icelandic, Uzbek, Tamil and Urdu. To correct a string, edit `src/i18n/<language>.json` and open a pull request.
