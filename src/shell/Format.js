// Number formats from the design, shared by every view.
.pragma library

var LMIN = Math.log(20) / Math.LN10
var LMAX = Math.log(20000) / Math.LN10

function lx(f) { return (Math.log(f) / Math.LN10 - LMIN) / (LMAX - LMIN) }
function fromX(x) { return Math.pow(10, LMIN + x * (LMAX - LMIN)) }
function clamp(v, a, b) { return Math.max(a, Math.min(b, v)) }

// 2000 is "2k", 16000 is "16k", 1500 is "1.5k".
function freqShort(f) {
  return f >= 1000 ? (+(f / 1000).toFixed(f >= 10000 ? 0 : 1)) + "k" : String(Math.round(f))
}

// "2 kHz", "32 Hz".
function freqLong(f) {
  return f >= 1000 ? (+(f / 1000).toFixed(1)) + " kHz" : Math.round(f) + " Hz"
}

// "+5.5", "−1.6", "±0.0".
function gain(g) {
  return (g > 0.04 ? "+" : g < -0.04 ? "−" : "±") + Math.abs(g).toFixed(1)
}

function two(i) { return (i + 1 < 10 ? "0" : "") + (i + 1) }

function shortPreset(name) { return name.replace(" · AutoEQ", "") }
