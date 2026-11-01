#ifndef DOWNSPOUT_PRISM_DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DOWNSPOUT_PRISM_DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "Downspout"
#define DISTRHO_PLUGIN_NAME    "Prism"
#define DISTRHO_PLUGIN_URI     "https://danja.github.io/downspout/plugins/prism"
#define DISTRHO_PLUGIN_CLAP_ID "it.hyperdata.downspout.prism"

#define DISTRHO_PLUGIN_BRAND_ID DnSp
#define DISTRHO_PLUGIN_UNIQUE_ID Prsm

#define DOWNSPOUT_PLUGIN_VERSION_MAJOR  0
#define DOWNSPOUT_PLUGIN_VERSION_MINOR  1
#define DOWNSPOUT_PLUGIN_VERSION_PATCH  0
#define DOWNSPOUT_PLUGIN_VERSION_STRING "v0.1.0"

#define DISTRHO_PLUGIN_HAS_UI           1
#define DISTRHO_PLUGIN_IS_RT_SAFE       1
#define DISTRHO_PLUGIN_NUM_INPUTS       2
#define DISTRHO_PLUGIN_NUM_OUTPUTS      2
// The FFT size sets the processing delay; the host is told when it changes.
#define DISTRHO_PLUGIN_WANT_LATENCY     1
// Synced delays follow the host tempo (BBT).
#define DISTRHO_PLUGIN_WANT_TIMEPOS     1
// Every setting is a parameter, which DPF saves by symbol. No plugin state.
#define DISTRHO_PLUGIN_VST3_CATEGORIES  "Fx|Delay"
#define DISTRHO_UI_DEFAULT_WIDTH        920
#define DISTRHO_UI_DEFAULT_HEIGHT       672
#define DISTRHO_UI_USE_NANOVG           1

#endif
