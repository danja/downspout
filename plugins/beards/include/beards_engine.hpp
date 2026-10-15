#pragma once

#include "beards_params.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace downspout::beards {

struct EngineImpl;

// Runtime state of the processor. Holds the delay buffer, grains, reverb and
// smoothing; none of it is part of the saved patch.
class EngineState {
public:
    EngineState();
    ~EngineState();
    EngineState(const EngineState&) = delete;
    EngineState& operator=(const EngineState&) = delete;

    std::unique_ptr<EngineImpl> impl;
};

// Allocates buffers for the sample rate and clears all runtime state. Never
// touches the patch (Parameters), so a host activate/rate change keeps settings.
void activate(EngineState& state, double sampleRate);

// Stereo in, stereo out. `inL`/`inR` may alias `outL`/`outR`.
void processBlock(EngineState& state, const Parameters& params, std::uint32_t frames,
                  const float* inL, const float* inR, float* outL, float* outR);

// Host transport at the start of a block. `quarters` is the absolute position
// in quarter notes (from BBT), `bpm` the tempo. Set before each processBlock.
struct Transport {
    bool playing = false;
    double quarters = 0.0;
    double bpm = 120.0;
};
void setTransport(EngineState& state, const Transport& transport);

// Applies one raw MIDI message (status + data bytes) to the parameters:
// controller map on MIDI in, Conductor CCs on the Conductor channel.
// Returns true when a parameter changed or a grain was requested.
bool applyMidi(Parameters& params, EngineState& state, const std::uint8_t* data, std::size_t size);

// Number of grains currently sounding (for the UI).
[[nodiscard]] int activeGrains(const EngineState& state);

}  // namespace downspout::beards
