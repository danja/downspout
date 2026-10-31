#pragma once

#include "interleaver_params.hpp"

#include <array>
#include <complex>
#include <cstdint>
#include <vector>

namespace downspout::interleaver {

constexpr int kFftSize = 2048;
constexpr int kHop = kFftSize / 4;
// Both modes are delayed by the same amount, so switching mode never moves the output in time and the
// host can compensate one fixed figure.
constexpr int kLatencySamples = kFftSize;

// ---- Pure pattern functions (shared by the engine, the UI plot and the tests) ----------------------

// 0 -> 1 across x in [0, 1] with the chosen edge shape. Hard is a step at 0.5.
[[nodiscard]] float edgeStep(float x, Shape shape);

// Width of an edge, in cycle-phase units, for a Crossfade percentage.
[[nodiscard]] float edgeWidth(float balance, float crossfadePercent, Shape shape);

// Fraction of source B at a cycle phase in [0, 1): A occupies [0, balance), B the rest. Edges are centred
// on the boundaries and `width` wide.
[[nodiscard]] float timeMask(float phase, float balance, float width, Shape shape);

// True when spectral bin `k` is taken from B. `offset` is the mask rotation in bins.
[[nodiscard]] bool spectralPicksB(int k, float offset, int binWidth, float balance);

// ---- Fundamental period tracker ---------------------------------------------------------------------

// Normalised autocorrelation on a 4x-decimated mono signal, 50 Hz to 1 kHz. It reports the shortest strong
// peak, so octave-down errors are avoided.
class PitchTracker {
public:
    void prepare(float sampleRate);
    void reset();
    void push(float mono);
    [[nodiscard]] bool voiced() const { return voiced_; }
    [[nodiscard]] float periodSamples() const { return periodSamples_; }  // in input samples
    [[nodiscard]] float confidence() const { return confidence_; }

private:
    void analyse();

    static constexpr int kRing = 1024;
    static constexpr int kWindow = 512;
    static constexpr int kHopDecimated = 128;
    std::array<float, kRing> ring_ {};
    int writeIndex_ = 0;
    int decimation_ = 4;
    int accumCount_ = 0;
    float accum_ = 0.0f;
    int sinceAnalysis_ = 0;
    int minLag_ = 12;
    int maxLag_ = 240;
    float decimatedRate_ = 12000.0f;
    int decimatedFactor_ = 4;
    bool voiced_ = false;
    float periodSamples_ = 0.0f;
    float confidence_ = 0.0f;
    int unvoicedRun_ = 0;
};

struct Status {
    float detectedHz = 0.0f;
    float cycleMs = 0.0f;
};

class Engine {
public:
    Engine();
    void prepare(float sampleRate);   // allocates nothing after construction; clears runtime state
    void reset();                     // clears runtime state only
    // inputs: A left, A right, B left, B right. outputs: left, right. Output is `kLatencySamples` late.
    void process(const Parameters& p, std::uint32_t frames, const float* const* inputs, float* const* outputs);
    [[nodiscard]] Status status() const { return status_; }

private:
    using Cx = std::complex<float>;

    void fft(Cx* data, bool inverse) const;
    void processFrame(const Parameters& p);
    [[nodiscard]] float nextRandom();

    float sampleRate_ = 48000.0f;

    // Time path
    float phase_ = 0.0f;
    float cycleSamples_ = 768.0f;
    float jitterFactor_ = 1.0f;
    PitchTracker tracker_;
    std::array<std::vector<float>, 2> delay_;
    int delayIndex_ = 0;

    // Spectral path
    std::array<std::array<std::vector<float>, 2>, 2> inRing_;  // [source][channel]
    int ringIndex_ = 0;
    int hopCounter_ = 0;
    std::array<std::vector<float>, 2> ola_;
    std::array<std::array<float, kHop>, 2> block_ {};
    int blockIndex_ = kHop;
    double driftBins_ = 0.0;
    std::vector<float> window_;
    std::vector<Cx> twiddle_;
    std::vector<int> bitReverse_;
    std::vector<Cx> zA_, zB_, zOut_;

    bool spectralWasActive_ = false;
    float modeMix_ = 0.0f;
    float gain_ = 1.0f;
    std::uint32_t rng_ = 0x1234567u;
    Status status_;
};

}  // namespace downspout::interleaver
