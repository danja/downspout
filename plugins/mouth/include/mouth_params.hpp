#pragma once

#include "mouth_vowels.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace downspout::mouth {

// Parameter indices are stable: never reorder, only append.
enum ParameterIndex : std::uint32_t {
    kVowelX = 0,
    kVowelY,
    kVoice,
    kShift,
    kResonance,
    kSpread,
    kDrive,
    kTilt,
    kMix,
    kOutput,
    kCcChannel,
    kParameterCount
};

struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum, maximum, defaultValue;
    bool integer;
};

inline constexpr std::array<ParameterSpec, kParameterCount> kParameterSpecs = {{
    {"vowel_x",    "Vowel X",    "%",    0.0f, 100.0f,  50.0f, false},
    {"vowel_y",    "Vowel Y",    "%",    0.0f, 100.0f,  95.0f, false},
    {"voice",      "Voice",      "",     0.0f,   3.0f,   1.0f, true },
    {"shift",      "Shift",      "st", -12.0f,  12.0f,   0.0f, false},
    {"resonance",  "Resonance",  "%",    0.0f, 100.0f,  50.0f, false},
    {"spread",     "Spread",     "%",    0.0f, 100.0f,   0.0f, false},
    {"drive",      "Drive",      "dB",   0.0f,  24.0f,   0.0f, false},
    {"tilt",       "Air",        "%",    0.0f, 100.0f,   0.0f, false},
    {"mix",        "Mix",        "%",    0.0f, 100.0f, 100.0f, false},
    {"output",     "Output",     "dB", -24.0f,  12.0f,   0.0f, false},
    {"cc_channel", "CC Channel", "",     0.0f,  16.0f,   1.0f, true },
}};

struct Parameters {
    float vowelX = 50.0f;
    float vowelY = 95.0f;
    float voice = 1.0f;
    float shiftSt = 0.0f;
    float resonance = 50.0f;
    float spread = 0.0f;
    float driveDb = 0.0f;
    float tilt = 0.0f;
    float mix = 100.0f;
    float outputDb = 0.0f;
    float ccChannel = 1.0f;
};

inline float& field(Parameters& p, const std::uint32_t index) noexcept
{
    switch (index) {
    case kVowelX:    return p.vowelX;
    case kVowelY:    return p.vowelY;
    case kVoice:     return p.voice;
    case kShift:     return p.shiftSt;
    case kResonance: return p.resonance;
    case kSpread:    return p.spread;
    case kDrive:     return p.driveDb;
    case kTilt:      return p.tilt;
    case kMix:       return p.mix;
    case kOutput:    return p.outputDb;
    default:         return p.ccChannel;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

[[nodiscard]] inline Parameters clampParameters(Parameters p) noexcept
{
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const ParameterSpec& s = kParameterSpecs[i];
        float& v = field(p, i);
        if (!std::isfinite(v))
            v = s.defaultValue;
        v = std::clamp(v, s.minimum, s.maximum);
        if (s.integer)
            v = std::round(v);
    }
    return p;
}

// The control a Drift CC overrides, or -1: moving it on the panel releases the override.
[[nodiscard]] constexpr int liveSlot(const std::uint32_t index) noexcept
{
    return index == kVowelX ? 0 : index == kVowelY ? 1 : index == kShift ? 2 : index == kResonance ? 3 : -1;
}

struct Formant {
    float freq = 1000.0f;  // Hz
    float gain = 1.0f;     // linear
    float bw = 100.0f;     // Hz, -3 dB width
};

using FormantSet = std::array<Formant, kFormants>;

// Blends the five vowels by inverse-distance weighting around their pad positions. Frequency
// and bandwidth blend in the log domain, level in dB. Exactly on a vowel it is that vowel.
[[nodiscard]] inline FormantSet blendVowel(const float x, const float y, const int voice) noexcept
{
    const auto& table = kVowelTable[static_cast<std::size_t>(std::clamp(voice, 0, static_cast<int>(kVoices) - 1))];
    std::array<float, kVowels> weight {};
    float total = 0.0f;
    for (std::size_t v = 0; v < kVowels; ++v) {
        const float dx = x - kPadPoints[v].x;
        const float dy = y - kPadPoints[v].y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < 1.0e-4f) {
            weight = {};
            weight[v] = 1.0f;
            total = 1.0f;
            break;
        }
        weight[v] = 1.0f / d2;
        total += weight[v];
    }
    FormantSet out {};
    for (std::size_t f = 0; f < kFormants; ++f) {
        float logF = 0.0f, logB = 0.0f, db = 0.0f;
        for (std::size_t v = 0; v < kVowels; ++v) {
            const float w = weight[v] / total;
            logF += w * std::log(table[v].freq[f]);
            logB += w * std::log(table[v].bw[f]);
            db += w * table[v].db[f];
        }
        out[f] = {std::exp(logF), std::pow(10.0f, db / 20.0f), std::exp(logB)};
    }
    return out;
}

// Scales a blended set for Shift (semitones) and Resonance (50 % = as tabulated, 0 = four times
// wider, 100 = four times narrower). Spread detunes the upper formants: side = -1 left, +1 right.
[[nodiscard]] inline FormantSet shapeFormants(FormantSet set, const float shiftSt, const float resonance,
                                              const float spread, const int side, const float sampleRate) noexcept
{
    const float ratio = std::exp2(shiftSt / 12.0f);
    const float width = std::pow(4.0f, (50.0f - resonance) / 50.0f);
    for (std::size_t f = 0; f < kFormants; ++f) {
        float detune = 1.0f;
        if (f > 0 && spread > 0.0f)
            detune = std::exp2(static_cast<float>(side) * spread * 0.01f * 0.12f * static_cast<float>(f) / 4.0f);
        set[f].freq = std::clamp(set[f].freq * ratio * detune, 40.0f, 0.45f * sampleRate);
        set[f].bw = std::clamp(set[f].bw * ratio * width, 8.0f, 0.5f * set[f].freq * 4.0f);
    }
    return set;
}

}  // namespace downspout::mouth
