#pragma once

#include "generative_common.hpp"

#include <array>
#include <cstdint>

namespace downspout::mixgen {

using downspout::generative::MidiBlock;
using downspout::generative::ParamSpec;
using downspout::generative::Transport;

inline constexpr int kLaneCount = 8;
inline constexpr int kFxLaneCount = 4;
inline constexpr std::uint8_t kTargetCcBase = 20;
inline constexpr std::uint8_t kProducerLifecycleCc = 19;

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
    kRoutingProfile = kStatusGainBase + kLaneCount,
    kFxSourceBase,
    kFxCcBase = kFxSourceBase + kFxLaneCount,
    kFxMinimumBase = kFxCcBase + kFxLaneCount,
    kFxMaximumBase = kFxMinimumBase + kFxLaneCount,
    kFxInvertBase = kFxMaximumBase + kFxLaneCount,
    kStatusFxBase = kFxInvertBase + kFxLaneCount,
    kStatusBusActive = kStatusFxBase + kFxLaneCount,
    kFxEditLane,
    kParameterCount
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
    {"routing_profile", "Routing profile", 0.0f, 2.0f, 0.0f, true},
    {"fx_source_1", "Delay time source", 1.0f, 8.0f, 1.0f, true},
    {"fx_source_2", "Delay feedback source", 1.0f, 8.0f, 2.0f, true},
    {"fx_source_3", "Reverb mix source", 1.0f, 8.0f, 3.0f, true},
    {"fx_source_4", "Reverb space source", 1.0f, 8.0f, 4.0f, true},
    {"fx_cc_1", "Delay time CC", 0.0f, 119.0f, 30.0f, true},
    {"fx_cc_2", "Delay feedback CC", 0.0f, 119.0f, 31.0f, true},
    {"fx_cc_3", "Reverb mix CC", 0.0f, 119.0f, 32.0f, true},
    {"fx_cc_4", "Reverb space CC", 0.0f, 119.0f, 33.0f, true},
    {"fx_min_1", "Delay time minimum", 0.0f, 1.0f, 0.25f},
    {"fx_min_2", "Delay feedback minimum", 0.0f, 1.0f, 0.24f},
    {"fx_min_3", "Reverb mix minimum", 0.0f, 1.0f, 0.08f},
    {"fx_min_4", "Reverb space minimum", 0.0f, 1.0f, 0.30f},
    {"fx_max_1", "Delay time maximum", 0.0f, 1.0f, 0.78f},
    {"fx_max_2", "Delay feedback maximum", 0.0f, 1.0f, 0.68f},
    {"fx_max_3", "Reverb mix maximum", 0.0f, 1.0f, 0.38f},
    {"fx_max_4", "Reverb space maximum", 0.0f, 1.0f, 0.76f},
    {"fx_invert_1", "Invert delay time", 0.0f, 1.0f, 1.0f, true},
    {"fx_invert_2", "Invert delay feedback", 0.0f, 1.0f, 1.0f, true},
    {"fx_invert_3", "Invert reverb mix", 0.0f, 1.0f, 1.0f, true},
    {"fx_invert_4", "Invert reverb space", 0.0f, 1.0f, 1.0f, true},
    {"status_fx_1", "Effective delay time", 0.0f, 1.0f, 0.0f, false, true},
    {"status_fx_2", "Effective delay feedback", 0.0f, 1.0f, 0.0f, false, true},
    {"status_fx_3", "Effective reverb mix", 0.0f, 1.0f, 0.0f, false, true},
    {"status_fx_4", "Effective reverb space", 0.0f, 1.0f, 0.0f, false, true},
    {"status_bus_active", "Producer bus active", 0.0f, 1.0f, 0.0f, true, true},
    {"fx_edit_lane", "FX lane editor", 0.0f, 3.0f, 0.0f, true},
}};

inline constexpr std::array<const char*, 3> kRoutingProfileNames {{"T-Mix", "FX only", "Full bus"}};
inline constexpr std::array<const char*, kFxLaneCount> kFxLaneNames {{
    "Delay time", "Delay feedback", "Reverb mix", "Reverb space"
}};

struct State {
    std::int64_t lastStep = -1;
    bool havePosition = false;
    double previousEnd = 0.0;
    int statusStep = 0;
    int statusEvents = 0;
    std::array<float, kLaneCount> gains {{1, 1, 1, 1, 1, 1, 1, 1}};
    std::array<float, kFxLaneCount> fxValues {};
    bool busActive = false;
    int activeChannel = 1;
};

void reset(State& state) noexcept;
[[nodiscard]] float gainForStep(const std::array<float, kParameterCount>& parameters,
                                int lane,
                                std::int64_t step) noexcept;
[[nodiscard]] float fxValueForStep(const std::array<float, kParameterCount>& parameters,
                                   int fxLane,
                                   std::int64_t step) noexcept;
[[nodiscard]] MidiBlock process(State& state,
                                const std::array<float, kParameterCount>& parameters,
                                const Transport& transport,
                                std::uint32_t frames,
                                double sampleRate) noexcept;

} // namespace downspout::mixgen
