#pragma once

#include "box_params.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::box {

// ── Portable physical model ────────────────────────────────────────────────
//
// The wooden structure is a thin rectangular plate. For simply supported
// plate mode (m, n):
//
//   D   = E h^3 / (12 (1 - nu^2))     bending stiffness, nu = 0.3
//   f   = (pi / 2) sqrt(D / (rho h)) (m^2 / a^2 + n^2 / b^2)
//
// a and b come from Body Type and Body Size; h from Thickness; E from Stiffness;
// rho from Density. The excitation and microphone XY pads give each mode a
// sin(m pi x) sin(n pi y) weight at both ends of the chain. Damping sets a T60
// that is converted to a biquad Q. An open soundhole adds a Helmholtz air mode
//
//   f = (c / 2pi) sqrt(A / (V (t + 1.7 r)))
//
// and the enclosed volume adds two axial cavity modes. Drive soft-clips the
// excitation and moves the modal frequencies upward with its envelope, which is
// a deliberately exaggerated stand-in for a wooden panel stiffening at large
// amplitude. Everything is deterministic and independent of the host.

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
    float gain = 0.0f;
    bool haveCoeffs = false;
};

struct ModeResponse {
    BiquadCoeffs filter {};
    float gain = 0.0f;
};

inline constexpr std::size_t kModeCount = 12;
inline constexpr std::size_t kAudioChannels = 2;

// Mode numbering of the plate, low to high. Deliberately includes pairs that
// the XY pads can null out, so moving the excitation really changes the timbre.
struct ModeIndex {
    int m;
    int n;
};

inline constexpr std::array<ModeIndex, kModeCount> kModeIndices = {{
    {1, 1}, {1, 2}, {2, 1}, {2, 2},
    {1, 3}, {3, 1}, {2, 3}, {3, 2},
    {1, 4}, {4, 1}, {3, 3}, {4, 4},
}};

struct Parameters {
    float body = 0.0f;
    float size = 1.0f;
    float thickness = 3.0f;
    float stiffness = 11.0f;
    float density = 450.0f;
    float damping = 1.0f;
    float hole = 90.0f;
    float volume = 15.0f;
    float depth = 100.0f;

    float drive = 6.0f;
    float nonlinear = 15.0f;
    float excX = 50.0f;
    float excY = 35.0f;
    float micX = 50.0f;
    float micY = 50.0f;
    float width = 30.0f;
    float mix = 35.0f;
    float output = 0.0f;

    float bypass = 0.0f;
    float ccChannel = 1.0f;
    float seed = 1.0f;
};

// One control-grid snapshot. Both channels share the physics but have their own
// microphone position and a small width-controlled detune.
struct BodyState {
    std::array<std::array<ModeResponse, kModeCount>, kAudioChannels> modes {};
    std::array<ModeResponse, kAudioChannels> air {};
    std::array<ModeResponse, kAudioChannels> lowCavity {};
    std::array<ModeResponse, kAudioChannels> highCavity {};
    float directGain = 0.0f;
    float bodyHz = 0.0f;
    float airHz = 0.0f;
    float decaySec = 0.0f;
    float q = 0.0f;
    bool valid = false;
};

struct EngineState {
    std::array<std::array<BiquadState, kModeCount>, kAudioChannels> modes {};
    std::array<BiquadState, kAudioChannels> air {};
    std::array<BiquadState, kAudioChannels> lowCavity {};
    std::array<BiquadState, kAudioChannels> highCavity {};
    // Smoothed physical inputs. The filter coefficients are always designed
    // from a complete, stable parameter set; only the parameters glide.
    Parameters smooth {};
    double sampleRate = 0.0;
    std::uint32_t controlCounter = 0;
    float envelope = 0.0f;
    float wetMix = 0.0f;
    bool initialised = false;
    std::uint32_t randomiseSerial = 0;
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// Pure analysis used by the DSP, the tests and the panel plot. `level` is the
// current smoothed drive envelope; it stiffens the modes when Nonlinearity is
// turned up.
[[nodiscard]] BodyState analyseBody(const Parameters& raw, double sampleRate, float level = 0.0f) noexcept;

void activate(EngineState& state) noexcept;

// Deterministic seeded variation of the physical properties. Mix, Output,
// Bypass and CC Channel are left alone so a randomise is a body change, not a
// volume jump. Seed advances with each press so repeated clicks differ.
[[nodiscard]] Parameters randomiseParameters(const Parameters& p) noexcept;

// CC 1-4: Body Size, Damping, Soundhole, Drive. Returns false for other CCs.
[[nodiscard]] bool applyController(Parameters& p, std::uint8_t controller, std::uint8_t value) noexcept;

void processBlock(EngineState& state,
                  const Parameters& params,
                  std::uint32_t frames,
                  double sampleRate,
                  const float* const* inputs,
                  float* const* outputs) noexcept;

}  // namespace downspout::box
