#pragma once

#include <array>
#include <cstdint>
#include <cmath>

namespace downspout::syrinx {

static constexpr std::uint32_t kPresetCount = 10;
static constexpr std::uint32_t kParamsPerPreset = 10;

// Within each preset block (offset from preset_index * kParamsPerPreset):
static constexpr std::uint32_t kPresetParamLevel      = 0;
static constexpr std::uint32_t kPresetParamNoise      = 1;
static constexpr std::uint32_t kPresetParamRoughness  = 2;
static constexpr std::uint32_t kPresetParamTimbre     = 3;
static constexpr std::uint32_t kPresetParamVibRate    = 4;  // normalized 0-1 → 0-20 Hz
static constexpr std::uint32_t kPresetParamVibDepth   = 5;  // normalized 0-1 → 0-300 cents
static constexpr std::uint32_t kPresetParamBend       = 6;  // -1 to +1
static constexpr std::uint32_t kPresetParamHarmonic   = 7;
static constexpr std::uint32_t kPresetParamAMRate     = 8;  // normalized 0-1 → 0-30 Hz
static constexpr std::uint32_t kPresetParamMute       = 9;

// Master parameters (after all preset blocks)
static constexpr std::uint32_t kParamDistance         = kPresetCount * kParamsPerPreset + 0;
static constexpr std::uint32_t kParamMasterGain       = kPresetCount * kParamsPerPreset + 1;
static constexpr std::uint32_t kParamSelectedPreset   = kPresetCount * kParamsPerPreset + 2;
static constexpr std::uint32_t kParameterCount        = kPresetCount * kParamsPerPreset + 3;

inline constexpr std::uint32_t presetParam(std::uint32_t preset, std::uint32_t param)
{
    return preset * kParamsPerPreset + param;
}

struct ParameterSpec {
    const char* name;
    const char* symbol;
    float minimum;
    float maximum;
    float defaultValue;
    bool boolean;
    bool integer;
};

static const char* const kPresetNames[kPresetCount] = {
    "Wren", "Thrush", "Warbler", "Finch", "Robin",
    "Nightjar", "Pigeon", "Hummingbird", "Starling", "Custom"
};

// Default values per preset [preset][param_within_preset]
static constexpr float kPresetDefaults[kPresetCount][kParamsPerPreset] = {
    // Level  Noise  Rough  Timbre  VibR   VibD   Bend   Harm   AM     Mute
    {  0.80f, 0.05f, 0.00f, 0.15f, 0.60f, 0.20f, 0.30f, 0.70f, 0.00f, 0.00f }, // Wren
    {  0.90f, 0.02f, 0.00f, 0.30f, 0.33f, 0.15f, 0.10f, 0.60f, 0.00f, 0.00f }, // Thrush
    {  0.85f, 0.03f, 0.00f, 0.20f, 0.30f, 0.25f, 0.00f, 0.50f, 0.00f, 0.00f }, // Warbler
    {  0.80f, 0.08f, 0.00f, 0.10f, 0.67f, 0.10f, 0.40f, 0.30f, 0.00f, 0.00f }, // Finch
    {  0.85f, 0.02f, 0.00f, 0.25f, 0.40f, 0.15f, 0.05f, 0.50f, 0.00f, 0.00f }, // Robin
    {  0.90f, 0.40f, 0.60f, 0.90f, 0.13f, 0.10f, 0.00f, 0.30f, 0.30f, 0.00f }, // Nightjar
    {  0.70f, 0.05f, 0.10f, 0.30f, 0.20f, 0.05f, 0.00f, 0.40f, 0.00f, 0.00f }, // Pigeon
    {  0.75f, 0.10f, 0.00f, 0.15f, 0.80f, 0.30f, 0.50f, 0.60f, 0.50f, 0.00f }, // Hummingbird
    {  0.85f, 0.15f, 0.20f, 0.70f, 0.47f, 0.20f, 0.15f, 0.80f, 0.00f, 0.00f }, // Starling
    {  0.80f, 0.10f, 0.00f, 0.30f, 0.33f, 0.20f, 0.00f, 0.50f, 0.00f, 0.00f }, // Custom
};

// Build the flat parameter table at compile time via a helper
namespace detail {

constexpr const char* kParamNames[kParameterCount] = {};
constexpr const char* kParamSymbols[kParameterCount] = {};

inline ParameterSpec makePresetSpec(std::uint32_t preset, std::uint32_t p, float defVal)
{
    static const char* names[kParamsPerPreset] = {
        " Level", " Noise", " Roughness", " Timbre",
        " Vibrato Rate", " Vibrato Depth", " Bend", " Harmonic",
        " AM Rate", " Mute"
    };
    static const char* syms[kParamsPerPreset] = {
        "_level", "_noise", "_roughness", "_timbre",
        "_vib_rate", "_vib_depth", "_bend", "_harmonic",
        "_am_rate", "_mute"
    };
    static const float mins[kParamsPerPreset]  = { 0,0,0,0, 0,0,-1,0, 0,0 };
    static const float maxs[kParamsPerPreset]  = { 1.4f,1,1,1, 1,1, 1,1, 1,1 };
    static const bool  bools[kParamsPerPreset] = { 0,0,0,0, 0,0, 0,0, 0,1 };
    (void)preset; (void)p;
    return { names[p], syms[p], mins[p], maxs[p], defVal, bools[p], false };
}

} // namespace detail

// Runtime-initialized flat table (filled in syrinx_engine.cpp / syrinx_params.cpp)
// Access via getParameterSpec(index).
inline ParameterSpec getParameterSpec(std::uint32_t index)
{
    if (index < kPresetCount * kParamsPerPreset) {
        const std::uint32_t preset = index / kParamsPerPreset;
        const std::uint32_t p     = index % kParamsPerPreset;
        const float defVal = kPresetDefaults[preset][p];
        return detail::makePresetSpec(preset, p, defVal);
    }
    switch (index) {
    case kParamDistance:
        return { "Distance",        "distance",         0.0f, 1.0f, 0.25f, false, false };
    case kParamMasterGain:
        return { "Master Gain",     "master_gain",      0.0f, 1.0f, 0.75f, false, false };
    case kParamSelectedPreset:
        return { "Selected Preset", "selected_preset",  0.0f, 9.0f, 0.0f,  false, true  };
    default:
        return { "Unknown", "unknown", 0.0f, 1.0f, 0.0f, false, false };
    }
}

// Preset voice parameters decoded from normalized storage
struct PresetParams {
    float level;
    float noise;
    float roughness;
    float timbre;
    float vibratoRateHz;   // 0-15 Hz
    float vibratoDepthCents; // 0-300 cents
    float bend;            // -1 to +1
    float harmonic;
    float amRateHz;        // 0-30 Hz
};

inline PresetParams decodePreset(const float* values, std::uint32_t preset)
{
    const std::uint32_t base = preset * kParamsPerPreset;
    PresetParams p;
    p.level            = values[base + kPresetParamLevel];
    p.noise            = values[base + kPresetParamNoise];
    p.roughness        = values[base + kPresetParamRoughness];
    p.timbre           = values[base + kPresetParamTimbre];
    p.vibratoRateHz    = values[base + kPresetParamVibRate]  * 20.0f;
    p.vibratoDepthCents= values[base + kPresetParamVibDepth] * 300.0f;
    p.bend             = values[base + kPresetParamBend];
    p.harmonic         = values[base + kPresetParamHarmonic];
    p.amRateHz         = values[base + kPresetParamAMRate]   * 30.0f;
    return p;
}

} // namespace downspout::syrinx
