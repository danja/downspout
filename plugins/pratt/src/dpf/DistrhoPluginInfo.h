#ifndef DOWNSPOUT_PRATT_DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DOWNSPOUT_PRATT_DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "Downspout"
#define DISTRHO_PLUGIN_NAME    "Pratt"
#define DISTRHO_PLUGIN_URI     "https://danja.github.io/downspout/plugins/pratt"
#define DISTRHO_PLUGIN_CLAP_ID "it.hyperdata.downspout.pratt"

#define DISTRHO_PLUGIN_BRAND_ID DnSp
#define DISTRHO_PLUGIN_UNIQUE_ID Prat

#define DOWNSPOUT_PLUGIN_VERSION_MAJOR  0
#define DOWNSPOUT_PLUGIN_VERSION_MINOR  1
#define DOWNSPOUT_PLUGIN_VERSION_PATCH  0
#define DOWNSPOUT_PLUGIN_VERSION_STRING "v0.1.0"

// An instrument with an audio input: Synth mode ignores the input, Filter and
// Synth + Filter modes run it through the Pratt filter.
#define DISTRHO_PLUGIN_HAS_UI           1
#define DISTRHO_PLUGIN_IS_RT_SAFE       1
#define DISTRHO_PLUGIN_IS_SYNTH         1
#define DISTRHO_PLUGIN_NUM_INPUTS       2
#define DISTRHO_PLUGIN_NUM_OUTPUTS      2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_STATE       1
#define DISTRHO_PLUGIN_WANT_FULL_STATE  1
#define DISTRHO_PLUGIN_VST3_CATEGORIES  "Instrument|Synth"
#define DISTRHO_UI_DEFAULT_WIDTH        920
#define DISTRHO_UI_DEFAULT_HEIGHT       640
#define DISTRHO_UI_USE_NANOVG           1
#define DISTRHO_UI_FILE_BROWSER         0

#endif
