#pragma once

#include "magneto_core_types.hpp"

#include <cstdint>

namespace downspout::magneto {

// Clamp and snap every field to the ranges declared in magneto_params.hpp.
// This is the single validation choke point: the wrapper calls it after every
// parameter write and deserialization calls it on load.
[[nodiscard]] Parameters clampParameters(const Parameters& raw);

// Clear all delay memory and filter state and adopt a new sample rate.
void activate(EngineState& state, double sampleRate);

// Render one block. The engine takes no MIDI: the DPF wrapper maps incoming CC
// onto host parameters so the panel, automation and controller never disagree.
void processBlock(EngineState& state,
                  const Parameters& params,
                  const TransportSnapshot& transport,
                  std::uint32_t nframes,
                  double sampleRate,
                  float* outL,
                  float* outR);

// Live status for the wrapper's read-only output parameters.
[[nodiscard]] float currentRpm(const EngineState& state) noexcept;
[[nodiscard]] float backfireLamp(const EngineState& state) noexcept;

// Engine speed the model will settle on for the given parameters and transport,
// before inertia. Shared by the control tick and the UI readout.
[[nodiscard]] float resolveTargetRpm(const Parameters& params, const TransportSnapshot& transport) noexcept;

}  // namespace downspout::magneto
