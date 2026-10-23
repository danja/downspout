#pragma once

#include <array>
#include <cstdint>

namespace downspout::octet {

// Eight-band parametric equalizer. Inspired by the EQ Eight in oxide-studio's oxide-fx but a
// redesign (see docs/design.md): proper 8th-order Butterworth cuts, per-band channel
// assignment, a transparent default and double-precision filters.

inline constexpr int kBands = 8;
inline constexpr int kMaxSections = 4;

enum class BandType : int { HighPass12 = 0, HighPass48, LowShelf, Bell, Notch, HighShelf, LowPass12, LowPass48 };
inline constexpr int kBandTypeCount = 8;

enum class StereoMode : int { Stereo = 0, LeftRight, MidSide };

// Which channel of the current mode a band acts on: both, A (left or mid) or B (right or side).
// Ignored in Stereo mode, where every band acts on both.
enum class Channel : int { Both = 0, A, B };

struct Band {
    float on      = 0.0f;       // 0 off, 1 on
    float type    = 3.0f;       // BandType
    float freqHz  = 1000.0f;    // 20..20000
    float gainDb  = 0.0f;       // -18..+18, shelves and bell
    float q       = 1.0f;       // 0.1..18, shelves, bell and notch
    float channel = 0.0f;       // Channel
};

struct Parameters {
    float mode     = 0.0f;      // StereoMode
    float outputDb = 0.0f;      // -12..+12
    std::array<Band, kBands> bands {};
    Parameters();               // the eight default bands
};

struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

struct Sections {
    int count = 0;
    std::array<Biquad, kMaxSections> bq {};
};

struct SectionState {
    double z1 = 0.0, z2 = 0.0;
};

struct BandRuntime {
    Sections sections {};
    std::array<std::array<SectionState, kMaxSections>, 2> state {};  // per channel
    bool active = false;
    int channels = 3;      // bit 0 = channel A, bit 1 = channel B
    Band last {};          // parameters the sections were computed from
    bool computed = false;
};

struct EngineState {
    std::array<BandRuntime, kBands> bands {};
    float outputLinear = 1.0f;
    bool primed = false;
};

[[nodiscard]] Band clampBand(const Band& b) noexcept;
[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// A band that changes the signal at all (a bell or shelf at 0 dB, or a band that is off, does not).
[[nodiscard]] bool bandIsActive(const Band& b) noexcept;
[[nodiscard]] bool bandUsesGain(const Band& b) noexcept;
[[nodiscard]] bool bandUsesQ(const Band& b) noexcept;

[[nodiscard]] Sections makeSections(const Band& b, double sampleRate) noexcept;

// Exact digital magnitude response in dB of one band, and of the whole equalizer (output gain
// not included). The panel plots these; the tests hold the audio to them.
[[nodiscard]] double bandResponseDb(const Band& b, double hz, double sampleRate) noexcept;
[[nodiscard]] double responseDb(const Parameters& p, double hz, double sampleRate) noexcept;

// Clears filter memory only; never touches the patch.
void resetState(EngineState& state) noexcept;

void processBlock(EngineState&      state,
                  const Parameters& params,
                  float             sampleRate,
                  uint32_t          frames,
                  const float* const* inputs,
                  float* const*       outputs) noexcept;

}  // namespace downspout::octet
