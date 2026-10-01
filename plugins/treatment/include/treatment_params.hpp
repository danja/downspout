#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::treatment {

// ── Parameters ─────────────────────────────────────────────────────────────
//
// The physical model is a mass-air-mass panel absorber with a porous fill:
//
//   facing mass m  --[air spring, depth D = cavity + gap]--  rigid wall
//   flow resistance r = resistivity * cavity
//
// Analytically at normal incidence (c = 343 m/s, rho0 = 1.2041 kg/m^3):
//
//   w0    = c * sqrt(rho0 / (m * D))          mass-air-mass resonance
//   r     = R * cavity                        specific flow resistance
//   Z     = r + j(w * m - rho0*c^2/(w * D))   specific surface impedance
//   alpha = 4*rho0*c*r / (|Z + rho0*c|^2)     absorption coefficient
//   Q     = w0 * m / (r + rho0 * c)           damping of the panel resonance
//   f_d   = R / (2*pi*rho0*cavity)            fill diffusion corner
//
// alpha peaks exactly at w0, where the imaginary part of Z vanishes, and rolls
// off at both ends. Peak absorption is maximal when r = rho0*c, so the panel has
// a genuine optimum flow resistivity rather than "more damping is better".
//
// The audio path is the panel's complement: what the panel does not absorb
// passes through, |H(f)| = sqrt(1 - alpha(f)). See docs/design.md.

enum class ParamId : std::uint32_t {
    // Panel
    cavity = 0,
    gap,
    mass,
    resist,
    amount,
    bypass,
    seed,
    // MIDI CC routing
    ccAmount,
    ccCavity,
    ccGap,
    ccMass,
    ccResist,
    ccChannel,
    // Action
    randomise,
    // Read-only status
    outResonance,
    outAbsorb,
    outDiffusion,
};

inline constexpr std::size_t kParameterCount = 18;

struct ParamSpec {
    const char* symbol;
    const char* name;
    const char* unit;  // empty string when unitless
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool output = false;
    bool trigger = false;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    {"cavity", "Cavity", "mm", 20.0f, 400.0f, 100.0f, false, false, false},
    {"gap", "Gap", "mm", 0.0f, 400.0f, 50.0f, false, false, false},
    {"mass", "Facing Mass", "kg/m2", 0.2f, 8.0f, 0.8f, false, false, false},
    {"resist", "Flow Resist", "Rayl/m", 1000.0f, 60000.0f, 8000.0f, false, false, false},
    {"amount", "Amount", "%", 0.0f, 100.0f, 100.0f, false, false, false},
    {"bypass", "Bypass", "", 0.0f, 1.0f, 0.0f, true, false, false},
    {"seed", "Seed", "", 1.0f, 9999.0f, 1.0f, true, false, false},

    {"cc_amount", "CC Amount", "", 0.0f, 127.0f, 1.0f, true, false, false},
    {"cc_cavity", "CC Cavity", "", 0.0f, 127.0f, 2.0f, true, false, false},
    {"cc_gap", "CC Gap", "", 0.0f, 127.0f, 3.0f, true, false, false},
    {"cc_mass", "CC Mass", "", 0.0f, 127.0f, 4.0f, true, false, false},
    {"cc_resist", "CC Resist", "", 0.0f, 127.0f, 5.0f, true, false, false},
    {"cc_channel", "CC Channel", "", 1.0f, 16.0f, 1.0f, true, false, false},

    {"randomise", "Randomise", "", 0.0f, 1.0f, 0.0f, true, false, true},

    {"out_resonance", "Resonance", "Hz", 20.0f, 800.0f, 0.0f, false, true, false},
    {"out_absorb", "Peak Absorb", "", 0.0f, 1.0f, 0.0f, false, true, false},
    {"out_diffusion", "Diffusion", "Hz", 200.0f, 20000.0f, 0.0f, false, true, false},
}};

// ── Physical constants ─────────────────────────────────────────────────────
//
// Air properties at 20 degrees C, treated as fixed: a panel's behaviour is
// dominated by its geometry and fill, not by the room temperature.

inline constexpr float kSpeedOfSound = 343.0f;
inline constexpr float kAirDensity = 1.2041f;
inline constexpr float kCharacteristicImpedance = kAirDensity * kSpeedOfSound;  // ~413 Pa*s/m^2

// The fill's broadband absorption above its diffusion corner reaches this
// fraction of the panel's peak absorption at its own corner. A backed porous
// layer does not reach alpha = 1 in practice; 0.55 keeps the shelf honest.
inline constexpr float kFillShare = 0.55f;
inline constexpr float kDiffusionQ = 0.7071f;   // Butterworth
inline constexpr float kMinQ = 0.7f;
inline constexpr float kMaxQ = 12.0f;
inline constexpr float kMaxNotchDepthDb = 24.0f;  // shelf/notch magnitude ceiling

// ── Fixed MIDI controller map ──────────────────────────────────────────────
//
// Downspout convention (see magneto/include/magneto_params.hpp): CC 1-4 are
// the four most audible controls because that is what Drift's four lanes emit
// by default. CC 5 continues the block here because Treatment has five
// performance-relevant panel properties.

inline constexpr std::uint8_t kCcAmount = 1;
inline constexpr std::uint8_t kCcCavity = 2;
inline constexpr std::uint8_t kCcGap = 3;
inline constexpr std::uint8_t kCcMass = 4;
inline constexpr std::uint8_t kCcResist = 5;

struct ControllerMapping {
    std::uint8_t controller;
    ParamId target;
    const char* label;
};

inline constexpr std::array<ControllerMapping, 5> kControllerMap = {{
    {kCcAmount, ParamId::amount, "Amount"},
    {kCcCavity, ParamId::cavity, "Cavity"},
    {kCcGap, ParamId::gap, "Gap"},
    {kCcMass, ParamId::mass, "Facing mass"},
    {kCcResist, ParamId::resist, "Flow resist"},
}};

[[nodiscard]] inline std::uint32_t index(const ParamId id) noexcept
{
    return static_cast<std::uint32_t>(id);
}

}  // namespace downspout::treatment
