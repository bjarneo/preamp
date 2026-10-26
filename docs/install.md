# Install preamp

preamp runs on Omarchy with the Omarchy shell, PipeWire and WirePlumber. The release package is for `x86_64`. On an ARM machine, build from source.

## Install the release package

To install preamp 0.1.2, run:

```sh
curl -LO https://github.com/bjarneo/preamp/releases/download/v0.1.2/preamp-0.1.2-1-x86_64.pkg.tar.zst
sudo pacman -U preamp-0.1.2-1-x86_64.pkg.tar.zst
systemctl --user enable --now preamp.service
preamp shell install
```

- `pacman` installs the dependencies.
- The service starts now and at each login.
- `preamp shell install` puts the preamp widget on the Omarchy bar.

To check the install, run `preamp status`. The first line shows the preset in use.

## Build from source

```sh
git clone https://github.com/bjarneo/preamp.git
cd preamp
./bin/install
```

`bin/install` builds the package with `makepkg` and installs it. It also starts the service and puts the widget on the bar. `makepkg` asks for your password when it installs the package.

## Install with an agent

A coding agent on your machine can do the install for you. Claude Code, Codex and opencode are examples. Paste this prompt into the agent:

```text
Install preamp, the system audio equalizer for Omarchy, on this machine.
Follow "Steps for agents" in https://github.com/bjarneo/preamp/blob/main/docs/install.md.
Ask me before you run a command that needs root.
When you finish, run `preamp status` and show me the output.
```

The agent needs root for one step, `pacman -U`. It uses `pkexec`, so Omarchy shows a password prompt.

### Steps for agents

Do the steps in order. If a step fails, stop and report the command and its error.

1. Make sure that the machine runs Omarchy with PipeWire:

   ```sh
   uname -m
   command -v omarchy-shell
   systemctl --user is-active pipewire.service wireplumber.service
   ```

   `uname -m` must print `x86_64`. Both services must be `active`. On `aarch64`, follow "Build from source" instead.

2. Find the package of the latest release:

   ```sh
   url=$(curl -fsSL https://api.github.com/repos/bjarneo/preamp/releases/latest \
     | grep -o '"browser_download_url": *"[^"]*x86_64\.pkg\.tar\.zst"' | cut -d'"' -f4)
   echo "$url"
   ```

3. Download the package into a new, empty directory:

   ```sh
   dir=$(mktemp -d)
   curl -fL --output-dir "$dir" -O "$url"
   ```

4. Install the package. Use `pkexec` when you have no terminal for a password prompt:

   ```sh
   pkexec pacman -U --noconfirm "$dir"/preamp-*.pkg.tar.zst
   ```

5. Start the service now and at each login:

   ```sh
   systemctl --user enable --now preamp.service
   ```

6. Put the widget on the Omarchy bar:

   ```sh
   preamp shell install
   ```

7. Teach coding agents the preamp commands:

   ```sh
   preamp skill install
   ```

8. Check the result:

   ```sh
   preamp status
   wpctl inspect @DEFAULT_AUDIO_SINK@ | grep node.name
   ```

   `preamp status` shows the preset and the output. The default sink must be `preamp`.

Do not edit files in `/usr/share/omarchy`. `omarchy update` replaces them.

## Open the window with a key

To open the window with `SUPER ALT E`, add this line to `~/.config/hypr/bindings.lua`:

```lua
o.bind("SUPER + ALT + E", "preamp", { launch = "preamp" })
```

You can also open preamp from the app launcher.

## Update

To update, install the package of the new release with `pacman -U`. Then restart the service:

```sh
systemctl --user restart preamp.service
```

When the release notes say that the bar widget changed, run `preamp shell install` again.

## Uninstall

To stop preamp and give the output back to your device, run:

```sh
systemctl --user disable --now preamp.service
sudo pacman -R preamp
```

To remove the bar widget, delete `~/.config/omarchy/plugins/preamp/`. To remove the agent skill, delete `~/.claude/skills/preamp/`. Your settings stay in `~/.config/preamp/` until you delete that directory.
