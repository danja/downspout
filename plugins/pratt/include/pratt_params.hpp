#pragma once

// Host-facing parameter table for the Pratt plugin, shared by the DPF wrapper,
// the UI and the state serializer. Framework-free.

#include "pratt_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::pratt {

enum class ParamId : std::uint32_t {
    // Voice
    mode = 0,
    preset,
    base,
    brightness,
    darkness,
    bendRange,
    percussion,
    room,
    level,
    // Filter
    filterA,
    filterB,
    cutoff,
    mix,
    // Voice (appended after the filter group so existing host automation indices stay put)
    timbre,     // extra fixed Pratt module chain, H_timbre, cascaded onto every note
    // Read-only status
    outIndex,   // effective filter index n
    outVoices,  // sounding voices
};

inline constexpr std::size_t kParameterCount = 16;
inline constexpr std::size_t kInputParameterCount = 14;

struct ParamSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool output = false;
    bool logarithmic = false;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    {"mode", "Mode", "", 0.0f, 2.0f, 0.0f, true, false, false},
    {"preset", "Voice", "", 0.0f, 11.0f, 0.0f, true, false, false},
    {"base", "Pratt Base", "", 0.0f, 64.0f, 0.0f, true, false, false},
    {"brightness", "Brightness", "", 0.25f, 3.0f, 1.0f, false, false, true},
    {"darkness", "Rolloff", "", -0.5f, 1.5f, 0.0f, false, false, false},
    {"bend_range", "Bend Range", "st", 0.0f, 12.0f, 2.0f, true, false, false},
    {"percussion", "Drums Ch10", "", 0.0f, 1.0f, 1.0f, true, false, false},
    {"room", "Room", "", 0.0f, 1.0f, 1.0f, false, false, false},
    {"level", "Level", "", 0.0f, 2.0f, 0.5f, false, false, false},
    {"filter_a", "Index A", "", 1.0f, 128.0f, 5.0f, true, false, false},
    {"filter_b", "Index B", "", 1.0f, 64.0f, 7.0f, true, false, false},
    {"cutoff", "Cutoff", "Hz", 20.0f, 4000.0f, 800.0f, false, false, true},
    {"mix", "Filter Mix", "", 0.0f, 1.0f, 1.0f, false, false, false},
    {"timbre", "Timbre Index", "", 1.0f, 8192.0f, 1.0f, true, false, true},
    {"out_index", "Index n", "", 1.0f, 8192.0f, 35.0f, true, true, false},
    {"out_voices", "Voices", "", 0.0f, 64.0f, 0.0f, true, true, false},
}};

inline constexpr std::array<const char*, 3> kModeNames = {{"Synth", "Filter", "Synth + Filter"}};

// Index 0 follows each MIDI channel's General MIDI program.
inline constexpr std::array<const char*, kPresetCount + 1> kVoiceNames = {{
    "GM Program", "Piano", "Electric Piano", "Organ", "Pluck", "Bass", "Strings", "Brass", "Reed",
    "Flute", "Pad", "Timpani",
}};

struct Settings {
    std::array<float, kInputParameterCount> v{};

    Settings() {
        for (std::size_t i = 0; i < kInputParameterCount; ++i) v[i] = kParameterSpecs[i].defaultValue;
    }
    float& operator[](ParamId id) { return v[static_cast<std::size_t>(id)]; }
    float operator[](ParamId id) const { return v[static_cast<std::size_t>(id)]; }
};

inline float clampParameter(std::size_t index, float value) {
    const ParamSpec& s = kParameterSpecs[index];
    if (!std::isfinite(value)) value = s.defaultValue;
    value = std::min(std::max(value, s.minimum), s.maximum);
    return s.integer ? std::round(value) : value;
}

inline Settings clampSettings(Settings s) {
    for (std::size_t i = 0; i < kInputParameterCount; ++i) s.v[i] = clampParameter(i, s.v[i]);
    return s;
}

// Settings -> engine parameters. Voice 0 means "follow GM program".
inline EngineParams toEngineParams(const Settings& s) {
    EngineParams p;
    p.mode = static_cast<Mode>(static_cast<int>(s[ParamId::mode]));
    p.presetOverride = static_cast<int>(s[ParamId::preset]) - 1;
    p.baseOverride = static_cast<int>(s[ParamId::base]);
    p.xiScale = s[ParamId::brightness];
    p.rollOffset = s[ParamId::darkness];
    p.bendRange = s[ParamId::bendRange];
    p.percussion = s[ParamId::percussion] >= 0.5f;
    p.room = s[ParamId::room];
    p.masterGain = s[ParamId::level];
    p.filterIndexA = static_cast<int>(s[ParamId::filterA]);
    p.filterIndexB = static_cast<int>(s[ParamId::filterB]);
    p.filterCutoffHz = s[ParamId::cutoff];
    p.filterMix = s[ParamId::mix];
    p.timbreIndex = static_cast<int>(s[ParamId::timbre]);
    return p;
}

// Effective filter index, as the engine will use it.
inline int effectiveIndex(const Settings& s) {
    const long long n = static_cast<long long>(s[ParamId::filterA]) * static_cast<long long>(s[ParamId::filterB]);
    return static_cast<int>(std::min<long long>(n, kMaxIndex));
}

// Stable text state: "version=1\n" then one "symbol=value" line per input
// parameter. Strict parse: any malformed line, unknown symbol, duplicate or
// missing key rejects the whole document; success is clamped.
inline constexpr int kStateVersion = 1;
std::string serializeSettings(const Settings& s);
std::optional<Settings> deserializeSettings(const std::string& text);

}  // namespace downspout::pratt
