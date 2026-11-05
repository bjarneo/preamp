# Socket protocol

The window, the bar widget and the command line all control the service through one Unix socket. Your program can use the same socket. Every action of the window and the bar widget is a command here, and each command also has a [command line](command-line.md).

## Connect

The socket is `$XDG_RUNTIME_DIR/preamp.sock`. The `PREAMP_SOCKET` variable can set another path.

Each message is one JSON object on one line, in UTF-8, with `\n` at the end. A line can have up to 1 MiB. If a line is longer, the service closes the connection.

```python
import json, os, socket

sock = socket.socket(socket.AF_UNIX)
sock.connect(os.environ["XDG_RUNTIME_DIR"] + "/preamp.sock")
sock.sendall(b'{"cmd": "setVolume", "v": 60, "id": 1}\n')
print(json.loads(sock.makefile().readline()))
# {'id': 1, 'ok': True, 'type': 'reply'}
```

## Requests and replies

A request has a `cmd` field and the fields of that command. To get a reply, add an integer `id`. Without `id`, the service does the command and sends no reply.

A reply has `"type": "reply"`, the same `id`, and `ok`:

```json
{"type": "reply", "id": 7, "ok": true}
{"type": "reply", "id": 8, "ok": false, "error": "no preset named Jazz"}
```

Do not use `id` for data. It only numbers the request.

## Events

Send `subscribe` to get events on the same connection:

```json
{"cmd": "subscribe", "state": true, "spectrum": true}
```

| Event | When | Content |
|---|---|---|
| `state` | At once, then after each change, at most every 16 ms | The [state object](#state-object) with `"type": "state"` |
| `spectrum` | About 30 times a second, while `spectrum` is `true` | `l`: 64 levels in dB, log-spaced from 20 Hz to 20 kHz, after the equalizer |
| `search` | After a `search` request | See [Search and AutoEq](#search-and-autoeq) |

`subscribe` has no reply. Both fields are optional. `state` is `true` and `spectrum` is `false` by default.

## Commands

Places in a list count from 0 on the socket. The command line counts from 1.

### State and presets

| `cmd` | Fields | Reply fields |
|---|---|---|
| `state` | | `state` |
| `library` | | `presets`: each with `name`, `kind`, `L` and `R` bands. `revision` goes up when the list changes. |
| `applyPreset` | `name` | |
| `nextPreset`, `prevPreset` | | |
| `savePreset` | `name`, up to 120 characters | |
| `deletePreset` | `name` of a saved preset | |
| `pin` | `preset` and `output`, both optional. The defaults are the current ones. | |
| `unpin` | `output`, optional | |

### Equalizer

| `cmd` | Fields | Reply fields |
|---|---|---|
| `setBand` | `i`: 0 to 9. Any of `f` in Hz, `g` in dB, `q`, and `t`: `pk`, `ls` or `hs`. | |
| `setMacro` | `k`: 0 bass, 1 mid, 2 treble. `v` in dB. | |
| `setChannel` | `channel`: `L+R`, `L` or `R` | |
| `cycleChannel` | | |
| `setBypass` | `on`, optional. Without it, the command switches the bypass. | `bypass` |

The service keeps the values in range. A gain is -12 to 12 dB, a frequency is 20 to 20,000 Hz and Q is 0.3 to 10.

### Output

| `cmd` | Fields | Reply fields |
|---|---|---|
| `setOutput` | `name`: the node name from `state.outputs` | |
| `nextOutput` | | |
| `setVolume` | `v`: 0 to 150 | |
| `setBalance` | `v`: -100 to 100. Below 0 is left. | |
| `setWidth` | `v`: 0 to 200 | |
| `setRoom` | `room`: `off`, `small room`, `studio` or `hall` | |
| `nextRoom` | | |

### Apps

| `cmd` | Fields | Reply fields |
|---|---|---|
| `setAppVolume` | `key` from `state.apps`, `v`: 0 to 100. This also unmutes the app. | |
| `toggleAppMute` | `key` | |

### Effects

| `cmd` | Fields | Reply fields |
|---|---|---|
| `plugins` | | `plugins`: the installed effects, each with `id`, `name` and `kind` |
| `addPlugin` | `plugin`: the `id` from `plugins` | |
| `togglePlugin` | `i` | |
| `removePlugin` | `i` | |
| `movePlugin` | `i`, `to` | |
| `pluginParams` | `i` | `params`: the controls of effect `i`, each with `symbol`, `name`, `min`, `max`, `default`, `value`, `integer`, `toggled`, `logarithmic` and `options`, a list of `value` and `label`. A side without a bound is ±3.4e38. |
| `setPluginParam` | `i`, `param`: a `symbol` from `pluginParams`, `value`. Without `value`, the control goes back to its default. | `value`: the value after it is held in range |

### Rules

| `cmd` | Fields | Reply fields |
|---|---|---|
| `addRule` | `kind`: `call` or `fullscreen`. `when`: the app name. `then`: a preset, the current one by default. `room`: optional. | |
| `updateRule` | `index`, then any of `kind`, `when`, `then` and `room` | |
| `removeRule` | `index` | |

`index` counts the `call` and `fullscreen` rules only. Pins are `output` rules, and `pin` and `unpin` change them.

### Search and AutoEq

`search` takes `q`, up to 100 characters. The answer comes as one or more `search` messages, not as a reply:

```json
{"type": "search", "id": 3, "q": "hd 650", "pending": false, "error": "",
 "results": [{"t": "Sennheiser HD 650", "s": "autoeq", "n": "HD 650 · AutoEQ", "path": "..."}]}
```

- `pending` is `true` while the service still loads the AutoEq index. Wait for the message with `pending: false`.
- `s` is `preset` for a preset that you have, and `autoeq` for an AutoEq profile.
- `n` is the name of the preset. Apply a `preset` result with `applyPreset`.
- `error` is not empty when the service cannot reach AutoEq.

To download and apply an AutoEq profile, send `autoeq` with the `path` of a result. The reply comes when the profile is applied. It has `preset`, the name of the new preset. This can take some seconds.

## State object

| Field | Content |
|---|---|
| `preset`, `dirty` | The current preset, and `true` when you changed its curve |
| `bypass` | `true` while the equalizer is off |
| `link`, `channel` | `link` is `true` for L+R. Otherwise `channel` is `L` or `R`. |
| `bands` | `L` and `R`: ten bands each, with `f`, `g`, `q` and `t` |
| `preamp` | The automatic input gain in dB |
| `macros` | Bass, mid and treble in dB |
| `slots` | The presets on keys `1` to `8` |
| `presets` | All presets, each with `name` and `kind`: `builtin`, `autoeq` or `user` |
| `library` | The revision of the preset list. Send `library` when it changes. |
| `outputs` | Each output with `name`, `label`, `kind`, `rate`, `pin` and `id` |
| `output`, `outputLabel` | The current output |
| `volume`, `balance`, `width`, `room`, `rooms` | The output values, and the rooms to pick from |
| `apps` | Each app with `key`, `name`, `volume`, `muted` and `state`: `playing`, `paused`, `in call` or `idle` |
| `plugins` | The chain, each with `id`, `name`, `kind`, `on` and `error` |
| `rules` | The pins and the rules, each with `kind`, `when`, `then`, `room` and `active`. Pins have `key`, rules have `index`. |
| `pin`, `auto` | The pin of the current output. `auto` is `true` when that pin chose the preset and the curve is unchanged. |
| `rate`, `ready` | The sample rate, and `true` when preamp is connected to PipeWire and plays into an output |

To see a full example, run `preamp --json status`.
