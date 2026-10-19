#ifndef DOWNSPOUT_QUNE_DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DOWNSPOUT_QUNE_DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "Downspout"
#define DISTRHO_PLUGIN_NAME    "Qune"
#define DISTRHO_PLUGIN_URI     "https://danja.github.io/downspout/plugins/qune"
#define DISTRHO_PLUGIN_CLAP_ID "it.hyperdata.downspout.qune"

#define DISTRHO_PLUGIN_BRAND_ID DnSp
#define DISTRHO_PLUGIN_UNIQUE_ID Qune

#define DOWNSPOUT_PLUGIN_VERSION_MAJOR  0
#define DOWNSPOUT_PLUGIN_VERSION_MINOR  1
#define DOWNSPOUT_PLUGIN_VERSION_PATCH  0
#define DOWNSPOUT_PLUGIN_VERSION_STRING "v0.1.0"

#define DISTRHO_PLUGIN_HAS_UI           1
#define DISTRHO_PLUGIN_IS_RT_SAFE       1
#define DISTRHO_PLUGIN_NUM_INPUTS       2
#define DISTRHO_PLUGIN_NUM_OUTPUTS      2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
// Note-on/off for the quantised pitch, one note at a time.
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 1
// Quality and range set the processing delay; the host is told when it changes.
#define DISTRHO_PLUGIN_WANT_LATENCY     1
// Every setting is a parameter, which DPF saves by symbol. No plugin state.
#define DISTRHO_PLUGIN_VST3_CATEGORIES  "Fx|Pitch Shift"
#define DISTRHO_UI_DEFAULT_WIDTH        920
#define DISTRHO_UI_DEFAULT_HEIGHT       672
#define DISTRHO_UI_USE_NANOVG           1

#endif
