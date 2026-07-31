#pragma once

#include "generative_common.hpp"

#include <array>
#include <cstdint>

namespace downspout::mixgen {

using downspout::generative::MidiBlock;
using downspout::generative::ParamSpec;
using downspout::generative::Transport;

inline constexpr int kLaneCount = 8;
inline constexpr std::uint8_t kTargetCcBase = 20;

enum Parameter : std::uint32_t {
    kMode = 0,
    kRate,
    kSteps,
    kDensity,
    kDepth,
    kVariation,
    kSpread,
    kSeed,
    kMidiChannel,
    kEnabled,
    kStatusStep,
    kStatusEvents,
    kStatusGainBase,
    kParameterCount = kStatusGainBase + kLaneCount
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs {{
    {"mode", "Pattern mode", 0.0f, 2.0f, 2.0f, true},
    {"rate", "Step rate (quarters)", 0.125f, 8.0f, 0.5f},
    {"steps", "Pattern length", 2.0f, 32.0f, 16.0f, true},
    {"density", "Active density", 0.0f, 1.0f, 0.5f},
    {"depth", "Gain depth", 0.0f, 1.0f, 0.85f},
    {"variation", "Level variation", 0.0f, 1.0f, 0.22f},
    {"spread", "Lane spread", 0.0f, 1.0f, 0.72f},
    {"seed", "Pattern seed", 1.0f, 65535.0f, 4271.0f, true},
    {"midi_channel", "MIDI channel", 1.0f, 16.0f, 1.0f, true},
    {"enabled", "Producer enabled", 0.0f, 1.0f, 1.0f, true},
    {"status_step", "Current step", 0.0f, 31.0f, 0.0f, true, true},
    {"status_events", "Events this block", 0.0f, 512.0f, 0.0f, true, true},
    {"gain_1", "Channel 1 gain", 0.0f, 1.0f, 1.0f, false, true},
    {"gain_2", "Channel 2 gain", 0.0f, 1.0f, 1.0f, false, true},
    {"gain_3", "Channel 3 gain", 0.0f, 1.0f, 1.0f, false, true},
    {"gain_4", "Channel 4 gain", 0.0f, 1.0f, 1.0f, false, true},
    {"gain_5", "Channel 5 gain", 0.0f, 1.0f, 1.0f, false, true},
    {"gain_6", "Channel 6 gain", 0.0f, 1.0f, 1.0f, false, true},
    {"gain_7", "Channel 7 gain", 0.0f, 1.0f, 1.0f, false, true},
    {"gain_8", "Channel 8 gain", 0.0f, 1.0f, 1.0f, false, true},
}};

struct State {
    std::int64_t lastStep = -1;
    bool havePosition = false;
    double previousEnd = 0.0;
    int statusStep = 0;
    int statusEvents = 0;
    std::array<float, kLaneCount> gains {{1, 1, 1, 1, 1, 1, 1, 1}};
};

void reset(State& state) noexcept;
[[nodiscard]] float gainForStep(const std::array<float, kParameterCount>& parameters,
                                int lane,
                                std::int64_t step) noexcept;
[[nodiscard]] MidiBlock process(State& state,
                                const std::array<float, kParameterCount>& parameters,
                                const Transport& transport,
                                std::uint32_t frames,
                                double sampleRate) noexcept;

} // namespace downspout::mixgen
