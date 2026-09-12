#pragma once

#include "modules/Waveguide.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace downspout::magneto {

inline constexpr int kMaxCylinders = 12;

// One-way delay bounds at 192 kHz drive these capacities:
//   chamber   0.115 m ->  65 samples
//   runner    1.65 m  -> 924 samples (1.5 m parameter maximum x 1.1 spread)
inline constexpr std::size_t kCylinderLineLen = 128;
inline constexpr std::size_t kIntakeLineLen = 1024;
inline constexpr std::size_t kExtractorLineLen = 1024;

// The paper specifies intake and extractor *average* length, so runners are
// detuned around it. Fixed, deterministic, +/-10%.
inline constexpr std::array<float, kMaxCylinders> kRunnerSpread = {{
    1.00f, 0.93f, 1.07f, 0.96f, 1.04f, 0.90f,
    1.10f, 0.98f, 1.02f, 0.94f, 1.06f, 0.92f,
}};

// Fixed reflection coefficients. The two free-end values are the paper's.
inline constexpr float kPistonEndReflection = 0.95f;   // near-rigid piston crown
inline constexpr float kIntakeFreeEnd = -0.5f;         // SIVE'15: intakes
inline constexpr float kExtractorFreeEnd = 0.1f;       // SIVE'15: extractors
inline constexpr float kValveClosedReflection = 0.95f;
inline constexpr float kValveOpenReflection = -0.10f;

// A closed valve recirculates, an open valve radiates. The sign change through
// zero is physically right (rigid seat -> pressure release) and is continuous,
// so it introduces no click.
[[nodiscard]] inline float valveReflection(const float opening) noexcept
{
    const float o = std::clamp(opening, 0.0f, 1.0f);
    return kValveClosedReflection - (kValveClosedReflection - kValveOpenReflection) * o;
}

// Acoustic length of a combustion chamber at bottom dead centre, modelled as a
// square cylinder (bore = stroke): V = 2*pi*r^3, length = 2r.
[[nodiscard]] inline float chamberLengthMetres(const float displacementCc) noexcept
{
    const float volume = std::max(displacementCc, 1.0f) * 1.0e-6f;  // m^3
    return 1.0838f * std::cbrt(volume);
}

// Chamber length scale as the piston travels: 1.0 at bottom dead centre, 1/CR
// at the top. p is the piston motion function, +1 at top dead centre.
[[nodiscard]] inline float chamberLengthScale(const float piston, const float compression) noexcept
{
    const float invCr = 1.0f / std::max(compression, 1.0f);
    return invCr + (1.0f - invCr) * 0.5f * (1.0f - piston);
}

struct CylinderState {
    Waveguide<kCylinderLineLen> chamber;
    Waveguide<kIntakeLineLen> intake;
    Waveguide<kExtractorLineLen> extractor;

    void reset() noexcept
    {
        chamber.reset();
        intake.reset();
        extractor.reset();
    }
};

}  // namespace downspout::magneto
