#pragma once

#include "treatment_params.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::treatment {

struct Parameters {
    float cavity = 100.0f;   // mm, depth of the absorptive fill
    float gap = 50.0f;       // mm, air gap between panel and wall
    float mass = 0.8f;       // kg/m^2, facing surface mass density
    float resist = 8000.0f;  // Rayl/m, specific flow resistivity of the fill
    float amount = 100.0f;   // %, treatment amount
    float bypass = 0.0f;     // 0 active, 1 bypassed (DSP keeps running)
    float seed = 1.0f;       // Randomise seed
};

// The analytic state of the panel, exposed so the tests and the panel can show
// the same derived numbers the DSP uses.
struct PanelState {
    float resonanceHz = 0.0f;   // mass-air-mass resonance f0
    float q = 1.0f;             // damping of that resonance
    float peakAbsorb = 0.0f;    // alpha at resonance, 0-1
    float diffusionHz = 0.0f;   // fill diffusion corner
    float impedanceMatch = 0.0f; // 1.0 when r == rho0*c, the absorption optimum
};

// One-pole smoother coefficient: exp(-1 / (seconds * sampleRate)).
[[nodiscard]] float smootherCoeff(float seconds, double sampleRate) noexcept;

struct BiquadCoeffs {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
};

struct BiquadState {
    BiquadCoeffs coeffs {};
    float x1 = 0.0f;
    float x2 = 0.0f;
    float y1 = 0.0f;
    float y2 = 0.0f;
    bool haveCoeffs = false;
};

inline constexpr std::size_t kAudioChannels = 2;

struct EngineState {
    // Panel resonance notch.
    std::array<BiquadState, kAudioChannels> notch {};
    // Fill diffusion shelf.
    std::array<BiquadState, kAudioChannels> diffusion {};
    // Smoothed Amount, so a CC sweep does not step.
    float amount = 1.0f;
    bool haveAmount = false;
    double sampleRate = 0.0;
    // Serial counter bumped by Randomise so the wrapper can repaint.
    std::uint32_t randomiseSerial = 0;
};

// Derived panel state from the analytic model. Pure: the DSP, the tests and the
// UI all read the same numbers.
[[nodiscard]] PanelState analysePanel(const Parameters& p) noexcept;

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

void activate(EngineState& state) noexcept;

// Randomise a plausible panel: geometry and fill drawn from the seed. Amount
// and Bypass are left alone so a sweep does not jump the depth around.
[[nodiscard]] Parameters randomiseParameters(const Parameters& p) noexcept;

void processBlock(EngineState& state,
                  const Parameters& params,
                  std::uint32_t frames,
                  double sampleRate,
                  const float* const* inputs,
                  float* const* outputs) noexcept;

[[nodiscard]] std::string serializeParameters(const Parameters& p);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::treatment
