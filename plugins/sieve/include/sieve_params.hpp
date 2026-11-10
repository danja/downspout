#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace downspout::sieve {

inline constexpr int kFftSize = 1024;
inline constexpr int kHop = 256;
inline constexpr int kBins = kFftSize / 2 + 1;

enum class Source : int { scale = 0, held = 1 };
inline constexpr const char* kSourceNames[2] = {"Scale", "Held MIDI"};

// 24 canonical scales (docs/scales.md)
inline constexpr int kScaleCount = 24;
// semitone intervals per scale (chromatic default)
inline constexpr std::array<std::array<int, 12>, kScaleCount> kScaleIntervals = {{
    {0,1,2,3,4,5,6,7,8,9,10,11},
    {0,2,4,5,7,9,11},
    {0,2,4,5,7,9,11},
    {0,2,3,5,7,8,10},
    {0,2,3,5,7,8,11},
    {0,2,3,5,7,9,11},
    {0,2,3,5,7,9,10},
    {0,1,3,5,7,8,10},
    {0,2,4,6,7,9,11},
    {0,2,4,5,7,9,10},
    {0,1,3,5,6,8,10},
    {0,1,4,5,7,8,10},
    {0,1,3,5,7,9,11},
    {0,1,3,5,7,8,11},
    {0,2,4,7,9},
    {0,3,5,7,10},
    {0,3,5,6,7,10},
    {0,2,4,6,8,10},
    {0,1,3,4,6,8,10},
    {0,1,3,4,6,7,9,10},
    {0,2,3,5,6,8,9,11},
    {0,2,4,5,7,9,10,11},
    {0,2,4,5,7,8,9,11},
    {0,2,3,4,5,7,9,10},
}};

inline constexpr std::array<int, kScaleCount> kScaleDegreeCount = {
    12,7,7,7,7,7,7,7,7,7,7,7,7,7,5,5,6,6,7,8,8,8,8,8
};

// Display names in the canonical ordinals of docs/scales.md.
inline constexpr std::array<const char*, kScaleCount> kScaleNames = {{
    "Chromatic", "Major", "Ionian", "Minor", "Harmonic Minor", "Melodic Minor", "Dorian", "Phrygian",
    "Lydian", "Mixolydian", "Locrian", "Phrygian Dominant", "Neapolitan Major", "Neapolitan Minor",
    "Pentatonic Major", "Pentatonic Minor", "Blues", "Whole Tone", "Altered", "Half-Whole Dim",
    "Whole-Half Dim", "Bebop Dominant", "Bebop Major", "Bebop Minor",
}};

inline constexpr std::array<const char*, 12> kRootNames = {{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
}};

inline const char* scaleName(const int scale) noexcept
{
    return kScaleNames[static_cast<std::size_t>(std::clamp(scale, 0, kScaleCount - 1))];
}

inline const char* rootName(const int root) noexcept
{
    return kRootNames[static_cast<std::size_t>(((root % 12) + 12) % 12)];
}

// Parameters
enum ParamId : int {
    kRoot = 0,
    kScale,
    kSource,
    kMidiCh,
    kWidth,      // cents half-width of each passband
    kFloor,      // dB floor (min gain)
    kTilt,       // odd/even tilt -1..1
    kMix,
    kLevel,
    kParamCount
};

struct Params {
    float root = 0.0f;
    float scale = 1.0f;
    float source = 0.0f;
    float midiCh = 0.0f;
    float width = 50.0f;
    float floor = -60.0f;
    float tilt = 0.0f;
    float mix = 1.0f;
    float level = 0.0f;
};

// DPF parameter spec
struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer;
};

inline constexpr int kParameterCount = 9;

inline constexpr std::array<ParameterSpec, kParameterCount> kParameterSpecs = {{
    {"root", "Root", "", 0.0f, 11.0f, 0.0f, true},
    {"scale", "Scale", "", 0.0f, 23.0f, 1.0f, true},
    {"source", "Source", "", 0.0f, 1.0f, 0.0f, true},
    {"midi_ch", "MIDI In", "", 0.0f, 16.0f, 0.0f, true},
    {"width", "Width", "cents", 1.0f, 200.0f, 50.0f, false},
    {"floor", "Floor", "dB", -80.0f, 0.0f, -60.0f, false},
    {"tilt", "Tilt", "", -1.0f, 1.0f, 0.0f, false},
    {"mix", "Mix", "", 0.0f, 1.0f, 1.0f, false},
    {"level", "Level", "dB", -24.0f, 12.0f, 0.0f, false},
}};

}  // namespace downspout::sieve