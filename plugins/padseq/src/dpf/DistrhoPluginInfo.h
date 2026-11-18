#ifndef DOWNSPOUT_PADSEQ_DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DOWNSPOUT_PADSEQ_DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "Downspout"
#define DISTRHO_PLUGIN_NAME    "PadSeq"
#define DISTRHO_PLUGIN_URI     "https://danja.github.io/downspout/plugins/padseq"
#define DISTRHO_PLUGIN_CLAP_ID "it.hyperdata.downspout.padseq"

#define DISTRHO_PLUGIN_BRAND_ID DnSp
#define DISTRHO_PLUGIN_UNIQUE_ID PdSq

#define DOWNSPOUT_PLUGIN_VERSION_MAJOR  0
#define DOWNSPOUT_PLUGIN_VERSION_MINOR  1
#define DOWNSPOUT_PLUGIN_VERSION_PATCH  0
#define DOWNSPOUT_PLUGIN_VERSION_STRING "v0.1.0"

#define DISTRHO_PLUGIN_HAS_UI           1
#define DISTRHO_PLUGIN_IS_RT_SAFE       1
#define DISTRHO_PLUGIN_IS_SYNTH         1
#define DISTRHO_PLUGIN_NUM_INPUTS       0
#define DISTRHO_PLUGIN_NUM_OUTPUTS      2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS     1
// Every pattern cell and setting is an ordinary parameter, which DPF saves by
// symbol. There is no non-parameter state to serialize.
#define DISTRHO_PLUGIN_VST3_CATEGORIES  "Fx|Generator"
#define DISTRHO_UI_DEFAULT_WIDTH        980
#define DISTRHO_UI_DEFAULT_HEIGHT       720
#define DISTRHO_UI_USE_NANOVG           1
#define DISTRHO_UI_FILE_BROWSER         0

#endif
