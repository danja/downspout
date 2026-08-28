#pragma once

#include "generative_common.hpp"

#include <array>
#include <cstdint>

namespace downspout::harmonic_atlas {

using downspout::generative::MidiBlock;
using downspout::generative::MidiEvent;
using downspout::generative::ParamSpec;
using downspout::generative::Transport;

enum Param : std::uint32_t {
    kStyle,
    kRhythmBars,
    kTension,
    kCadenceBars,
    kInversionRange,
    kVoiceCount,
    kVoiceLeading,
    kScaleNotes,
    kFollowInput,
    kRoot,
    kChannel,
    kSeed,
    kConductorChannel,
    kStatusRoot,
    kStatusChord,
    kParameterCount
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs {{
    {"style", "Movement", 0, 3, 0, true},
    {"rhythm_bars", "Harmonic bars", 1, 8, 2, true},
    {"tension", "Tension", 0, 1, 0.35f},
    {"cadence_bars", "Cadence frequency", 1, 16, 8, true},
    {"inversion_range", "Inversion range", 0, 3, 1, true},
    {"voice_count", "Voices", 2, 6, 4, true},
    {"voice_leading", "Voice-leading", 0, 1, 0.75f},
    {"scale_notes", "Scale-note chance", 0, 1, 0.15f},
    {"follow_input", "Follow input", 0, 1, 0, true},
    {"root", "Root pitch class", 0, 11, 0, true},
    {"channel", "MIDI channel", 1, 16, 1, true},
    {"seed", "Seed", 1, 65535, 1701, true},
    {"conductor_ch", "Conductor Ch", 0, 16, 0, true},
    {"status_root", "Effective root", 0, 11, 0, true, true},
    {"status_chord", "Chord index", 0, 4096, 0, true, true},
}};

struct State {
    std::array<int, 8> activeNotes {};
    int activeCount = 0;
    int followedRoot = -1;
    bool wasPlaying = false;
    bool havePosition = false;
    double previousEnd = 0.0;
    std::int64_t lastChord = -1;
    int statusRoot = 0;
};

void reset(State& state) noexcept;
MidiBlock process(State& state,
                  const std::array<float, kParameterCount>& parameters,
                  const Transport& transport,
                  std::uint32_t frames,
                  double sampleRate,
                  const MidiEvent* input,
                  std::uint32_t inputCount) noexcept;

} // namespace downspout::harmonic_atlas
