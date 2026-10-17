#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::fathom {

// Five-way generator. The algorithms mirror the families on the Vhikk X
// reference page: two analogue-style oscillators, a waveshaping pair, a
// multi-sine cluster bank, four wavetable oscillators, and four oscillators
// whose frequencies scan a diatonic-quantised cluster. Five shared controls
// (warp, span, morph, seed, fuse) mean something different in each; the table in
// docs/design.md lists them, and the panel shows the current meanings.
struct GeneratorControls {
    int algorithm = 2;
    float warp = 0.3f;
    float span = 0.25f;
    float morph = 0.3f;
    float seed = 0.5f;
    float fuse = 0.0f;
};

class Generator {
public:
    Generator();

    void setSampleRate(double sampleRate) noexcept;

    // Targets; the generator glides to them so a stepped CC does not click.
    void setControls(const GeneratorControls& controls) noexcept;
    // Jump to the targets, for the first note on an idle voice and for tests.
    void snap() noexcept;
    // Restart the oscillator phases (a new note).
    void restart() noexcept;

    // One sample at a fundamental of `hertz`; about +-1.
    [[nodiscard]] float process(double hertz) noexcept;

private:
    void tick() noexcept;
    void deriveShape() noexcept;

    [[nodiscard]] float analog(double hertz) noexcept;
    [[nodiscard]] float shaper(double hertz) noexcept;
    [[nodiscard]] float sines(double hertz) noexcept;
    [[nodiscard]] float table(double hertz) noexcept;
    [[nodiscard]] float cluster(double hertz) noexcept;

    double advance(std::size_t oscillator, double hertz) noexcept;

    double sampleRate_ = 48000.0;
    GeneratorControls target_ {};
    GeneratorControls now_ {};
    float smoothing_ = 0.03f;
    int tick_ = 0;
    bool snapPending_ = true;

    std::array<double, 8> phase_ {};
    std::array<float, 8> offsets_ {};  // seed-derived, each in [-1, 1]
    std::array<float, 8> ratio_ {};    // per-oscillator frequency ratio
    std::array<float, 8> weight_ {};   // per-oscillator level
    float norm_ = 1.0f;
    float lastModulator_ = 0.0f;
    std::array<float, 4> lastOut_ {};
};

}  // namespace downspout::fathom
