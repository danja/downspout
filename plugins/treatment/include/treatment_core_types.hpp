#pragma once

#include "treatment_params.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::treatment {

struct PanelGeometry {
    float cavity;
    float gap;
    float mass;
    float resist;
};

struct Parameters {
    float cavity = 100.0f;   // mm, depth of the absorptive fill
    float gap = 50.0f;       // mm, air gap between panel and wall
    float mass = 0.8f;       // kg/m^2, facing surface mass density
    float resist = 8000.0f;  // Rayl/m, specific flow resistivity of the fill
    float amount = 100.0f;   // %, treatment amount
    float bypass = 0.0f;     // 0 active, 1 bypassed (DSP keeps running)
    float seed = 1.0f;       // Randomise seed

    // Panel bank. Panel 1 is cavity/gap/mass/resist above; panels 2-4 follow.
    float panels = 1.0f;     // 1-4 panels in series
    std::array<PanelGeometry, 3> extra {{
        {200.0f, 80.0f, 1.5f, 12000.0f},
        {50.0f, 20.0f, 0.4f, 6000.0f},
        {300.0f, 150.0f, 3.0f, 20000.0f},
    }};

    // Special effect.
    float mode = 0.0f;         // 0 Absorb (never boosts), 1 Ring (boosts resonances)
    float sweepRate = 0.4f;    // Hz
    float sweepDepth = 0.0f;   // %, 0 = static. 100 = +-1 octave of resonance
    float stereo = 0.0f;       // %, L/R sweep phase offset, 100 = opposite
    float sweepSync = 0.0f;    // 1 = LFO follows the host tempo/transport
    float sweepBeats = 4.0f;   // index into kSweepBeats (default one 4/4 bar)
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

struct BiquadCoeffs {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
};

// One-pole smoother coefficient: exp(-1 / (seconds * sampleRate)).
[[nodiscard]] float smootherCoeff(float seconds, double sampleRate) noexcept;

// The panel's two biquad coefficients at a given configuration, exposed so the
// panel can draw the response it is actually applying without re-deriving the
// model or duplicating the coefficient maths.
struct PanelResponse {
    BiquadCoeffs notch {};
    BiquadCoeffs diffusion {};
    bool valid = false;
};

[[nodiscard]] PanelResponse panelCoeffs(const Parameters& p, double sampleRate) noexcept;

// Panel `panel` (0-3) of a bank as a stand-alone single-panel Parameters, so
// analysePanel/panelCoeffs and the UI treat every panel the same way.
[[nodiscard]] Parameters panelView(const Parameters& p, int panel) noexcept;

// The same panel with its facing mass flexed by the sweep LFO value `lfo`
// (-1..1): resonance moves by -lfo * depth octaves.
[[nodiscard]] Parameters sweptPanel(const Parameters& panel, float depthPercent, float lfo) noexcept;

// Magnitude of the panel's full two-section response at `hz`, linear. 1.0 is
// transparent; less than 1.0 is absorption.
[[nodiscard]] float transmissionAt(const PanelResponse& response, double hz, double sampleRate) noexcept;


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
    // Panels 2-4 of the bank (panel 1 is notch/diffusion above).
    std::array<std::array<BiquadState, kAudioChannels>, 3> extraNotch {};
    std::array<std::array<BiquadState, kAudioChannels>, 3> extraDiffusion {};
    // Latest per-panel, per-channel targets; kept across blocks so the sweep
    // grid (every 32 samples) is independent of the host's block size.
    std::array<std::array<PanelResponse, kAudioChannels>, 4> targets {};
    double sampleRate = 0.0;
    double sweepPhase = 0.0;          // cycles, 0-1
    std::uint32_t controlCounter = 0; // sample grid for sweep updates
    // Host transport, set by the wrapper before each block (not saved).
    double hostBeat = 0.0;            // quarter notes at the block start
    double hostBpm = 0.0;             // 0 = unknown
    bool hostPlaying = false;
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
