#pragma once

// Opens the plugin's own window for an LV2 effect in the chain.
//
// The effect runs in the daemon, so the window runs in a process of its own:
// a crash in a plugin's window cannot stop the sound. The window loads the
// plugin's X11 UI, which shows through XWayland, and talks to the daemon over
// the socket like any other client. A knob turned in the window sends
// setPluginParam, and a control set elsewhere reaches the window when the
// daemon sends its state. The meters come from pluginOutputs, 30 times a
// second.

namespace pluginui {

// Runs the window of effect `index` until it closes. Returns the exit code.
int run(int index);

} // namespace pluginui
