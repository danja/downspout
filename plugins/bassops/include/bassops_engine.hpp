#pragma once

#include "bassops_core_types.hpp"

#include <cstdint>

namespace downspout::bassops {

struct AudioBlock {
    const float* mainL    = nullptr;  // input 1: main stereo L
    const float* mainR    = nullptr;  // input 2: main stereo R
    const float* controlL = nullptr;  // input 3: sidechain L (typically bass drum send)
    const float* controlR = nullptr;  // input 4: sidechain R
    float* outL = nullptr;
    float* outR = nullptr;
};

struct BusParameters {
    bool midiEnabled    = false;
    int  controlChannel = 0;   // 0=Omni, 1-16
    bool requireGate    = false;
};

void handleMidi(EngineState& state,
                const BusParameters& busParams,
                const MidiControlEvent& ev);

[[nodiscard]] float effectiveDuckDepth(const EngineState& state,
                                        const Parameters& params) noexcept;
[[nodiscard]] float effectiveWet(const EngineState& state,
                                  const Parameters& params) noexcept;

[[nodiscard]] Parameters clampParameters(const Parameters& raw);

// Clears runtime state only (filters, follower, delay contents); never the patch.
void activate(EngineState& state);

// Sizes the input delay for `sampleRate`. Call from activate() and sampleRateChanged() so the audio
// thread never allocates. A host that skips it is still served: processBlock then allocates once, on
// its first block.
void prepare(EngineState& state, double sampleRate);

// Input delay in seconds for the given parameters and tempo: the beat part at `bpm` plus the ms part,
// cut to kMaxDelaySeconds. A tempo <= 0 or not finite is read as kDefaultBpm.
[[nodiscard]] double delaySeconds(const Parameters& parameters, double bpm) noexcept;

void processBlock(EngineState& state,
                  const Parameters& parameters,
                  const BusParameters& busParams,
                  std::uint32_t nframes,
                  double sampleRate,
                  const AudioBlock& audio,
                  const Transport& transport = Transport {});

// Latency introduced by the linear-phase FIR (frames)
inline constexpr std::uint32_t kLatencyFrames = kFirOrder / 2;

}  // namespace downspout::bassops
