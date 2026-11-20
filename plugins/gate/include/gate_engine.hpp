#pragma once

#include "gate_params.hpp"

#include <array>
#include <cstdint>

namespace downspout::gate {

// Linked stereo noise gate. One detector drives one gain for both main channels.
// The sidechain can be the main inputs or a second stereo input pair; external
// silence therefore closes the gate when External is selected. No latency.
struct Engine {
    double sampleRate = 48000.0;
    bool primed = false;

    double hpIn[2] = {0.0, 0.0};
    double hpOut[2] = {0.0, 0.0};
    double detector = 0.0;

    bool open = false;
    int holdRemaining = 0;
    double envelope = 0.0;

    double bypassMix = 0.0;
    double outputGain = 1.0;

    double detectorAttack = 0.1;
    double detectorRelease = 0.01;
    double gateAttack = 0.1;
    double gateRelease = 0.01;
    double controlAlpha = 0.1;

    double lastLevelDb = -100.0;
    double lastReductionDb = 0.0;
    bool lastOpen = false;
};

void activate(Engine& e, double sampleRate);
void reset(Engine& e);
void processBlock(Engine& e, const Parameters& p, std::uint32_t frames, const float* const* inputs,
                  float* const* outputs) noexcept;

[[nodiscard]] double detectorDb(const Engine& e) noexcept;
[[nodiscard]] double reductionDb(const Engine& e) noexcept;
[[nodiscard]] bool gateIsOpen(const Engine& e) noexcept;
[[nodiscard]] double gateEnvelope(const Engine& e) noexcept;

}  // namespace downspout::gate
