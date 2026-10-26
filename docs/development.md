# Development

preamp is C++ and Qt Quick in one binary. The daemon uses `libpipewire` and `lilv`. You build it with `qmake6` and `make`.

## Build and test

| Command | Action |
|---|---|
| `bin/build` | Builds `build/preamp` |
| `bin/test` | Builds and runs the tests offscreen |
| `bin/install` | Builds and installs the package, then restarts the service |

The tests compile the sources in `src/` directly and need no display. Two tests read the sources:

- `everyWindowStringIsTranslated` fails when a `qsTr()` string in `src/qml/` is missing from a catalog in `src/i18n/`.
- `everySocketCommandHasACliCommand` fails when the daemon handles a socket command that `src/cli.cpp` does not send.

## Source layout

| Path | Content |
|---|---|
| `src/main.cpp` | Starts the window or hands the arguments to the command line |
| `src/cli.cpp` | The commands, the shell and skill installers, and `preamp daemon` |
| `src/daemon.cpp` | The service: presets, outputs, rules, plugins and the socket commands |
| `src/pwengine.cpp` | The PipeWire sink, the output stream and the graph |
| `src/dsp.cpp`, `src/eq.cpp` | The audio processing, the filters and the built-in presets |
| `src/plugins.cpp` | The LV2 and LADSPA hosts |
| `src/autoeq.cpp` | The AutoEq index and the profile download |
| `src/ipc.cpp` | The socket server and client |
| `src/qml/` | The window |
| `src/shell/` | The bar widget, which the binary carries |
| `src/i18n/` | The translations, one JSON file for each language |
| `src/skill/SKILL.md` | The agent skill, which the binary carries |
| `pkgbuild/` | The package, the service unit, the desktop file and the icon |

## Add a feature

Every action of the window must also work from scripts. When you add an action:

1. Add the socket command in `Daemon::handle()` in `src/daemon.cpp`.
2. Add a command that sends it in `src/cli.cpp`, and add the command to `printHelp()`.
3. Describe the command in [Command line](command-line.md) and [Socket protocol](ipc.md).
4. Run `bin/test`.

## Add a string

1. Write the string as `qsTr("text")` in QML, or `tr("text")` in C++.
2. Add the English text and the translation to each file in `src/i18n/`. Keep the keys sorted.
3. Run `bin/test`. The test names each catalog that misses the string.

## Change the bar widget

The widget in `src/shell/` must load on Omarchy 4.0.6 and later. Use only the shell API that Omarchy 4.0.6 has:

- Get the strings from `Strings.qml`, not from `Commons.Translations`.
- Put the popover in `KeyboardPanel` as children, not in its `content` property.
- Before you use a newer property, check it with `in`, as `BarWidget.qml` does for `pressScale`.

The test `barWidgetLoadsOnOmarchy40` fails when the widget uses `Commons.Translations`, `Commons.I18n` or the lazy content of `KeyboardPanel`.

To check the widget against the Omarchy 4.0.6 shell, run:

```sh
git clone --depth 1 --branch v4.0.6 https://github.com/omacom/omarchy /tmp/omarchy-4.0.6
mkdir -p /tmp/qs-4.0.6/qs
ln -s /tmp/omarchy-4.0.6/shell/Commons /tmp/omarchy-4.0.6/shell/Ui /tmp/qs-4.0.6/qs/
cd src/shell && /usr/lib/qt6/bin/qmllint -I /tmp/qs-4.0.6 -I /usr/lib/qt6/qml *.qml
```

Only `was not found` and `Could not find property` show an API that Omarchy 4.0.6 does not have. You can ignore the other warnings. `qmllint` cannot see the members of `Style.bar`, `Style.font`, `parent` and `Loader.item`.

## Run a second daemon for tests

To test the service without a change to your own setup, run a second daemon with its own socket, sink, settings and output:

```sh
export PREAMP_SOCKET=/tmp/preamp-test.sock PREAMP_SINK_NAME=preamp-test PREAMP_NO_DEFAULT=1
export PREAMP_OUTPUT=alsa_output.pci-0000_00_1f.3-platform-sof_sdw.HiFi__HDMI3__sink
export XDG_CONFIG_HOME=/tmp/preamp-test/config XDG_CACHE_HOME=/tmp/preamp-test/cache
build/preamp daemon &
build/preamp status
```

Pick an output that you do not use for `PREAMP_OUTPUT`. The command line and the window use `PREAMP_SOCKET` too.

The second daemon also lists your real `preamp` sink as an output. Do not select it. The volume commands would then change the volume of your real device.

## Environment variables

| Variable | Effect |
|---|---|
| `PREAMP_SOCKET` | The path of the socket |
| `PREAMP_SINK_NAME` | The name of the sink. The default is `preamp`. |
| `PREAMP_NO_DEFAULT` | The daemon does not make its sink the default output |
| `PREAMP_OUTPUT` | The output that the daemon plays into at start |
| `PREAMP_NO_AUTOSTART` | The window does not start the service |
| `PREAMP_THEME_DIR` | An Omarchy theme directory in place of the current theme |
| `PREAMP_LANGUAGE_FILE` | A file in place of `~/.config/omarchy/language` |
| `PREAMP_VIEW` | The view to open, for example `presets`, `studio`, `modes:parametric` or `chain` |
| `PREAMP_SEL` | The band to select, from 0 to 9 |
| `PREAMP_SCREENSHOT` | Save the window to this file, then quit |
| `PREAMP_SCREENSHOT_DELAY` | The time before the screenshot in milliseconds. The default is 1500. |

To save a screenshot of the window without a display, run:

```sh
QT_QPA_PLATFORM=offscreen PREAMP_VIEW=studio PREAMP_SCREENSHOT=studio.png build/preamp
```

## Release

1. Set `Version` in `src/main.cpp` and `pkgver` in `pkgbuild/PKGBUILD` to the new version.
2. Change the version in the install commands in `README.md`, `docs/install.md` and `docs/index.html`.
3. Build the package:

   ```sh
   cd pkgbuild && makepkg -f
   ```

4. Commit, tag `v<version>` and push the commit and the tag.
5. Make a GitHub release with the package, the release notes and the SHA-256 of the package.
