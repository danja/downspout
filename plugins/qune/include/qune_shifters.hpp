#pragma once

#include "qune_fft.hpp"
#include "qune_history.hpp"

#include <cstdint>
#include <vector>

namespace downspout::qune {

// All three shifters take the input history after the current sub-block has been
// pushed, and write `frames` samples of wet output at their own fixed (or, for
// Fast, nominal) latency.

// ── Fast ───────────────────────────────────────────────────────────────────
//
// Pitch-synchronous splice delay line. A single read head moves through the
// input history at the pitch ratio; when it has drifted by about one period
// it jumps by a whole number of periods (refined by cross-correlation) with a
// short crossfade, so the splice lands on the same point of the waveform. At
// ratio 1 the head is stationary and the signal passes through a plain delay.
// Formants move with pitch, as with any resampler.
class FastShifter {
public:
    void prepare(double sampleRate);
    void reset();
    [[nodiscard]] int latencySamples() const noexcept { return nominalDelay_; }
    void process(const History& history, const ShiftControl& control, float* outLeft, float* outRight,
                 int frames);

private:
    void startJump(const History& history, std::int64_t now, double target, float period, int fadeLength,
                   bool voiced);

    double sampleRate_ = 48000.0;
    int nominalDelay_ = 192;
    float minimumSpan_ = 144.0f;
    float lastPeriod_ = 240.0f;

    double delayA_ = 192.0;  // current read head, samples behind the newest
    double delayB_ = 192.0;  // outgoing head during a crossfade
    bool fading_ = false;
    int fadePosition_ = 0;
    int fadeLength_ = 1;
};

// ── Good ───────────────────────────────────────────────────────────────────
//
// Pitch-synchronous overlap-add. Two-period Hann grains are cut from the input
// at pitch marks and overlapped at the new period, which moves the pitch while
// leaving each grain's spectrum alone, so formants are preserved. Resampling
// the grain contents by `formantRatio` moves the formants as well.
class PsolaShifter {
public:
    void prepare(double sampleRate);
    void setLowestPitch(float minHz);
    void reset();
    [[nodiscard]] int latencySamples() const noexcept { return latency_; }
    void process(const History& history, const ShiftControl& control, float* outLeft, float* outRight,
                 int frames);

private:
    void scheduleGrain(const History& history, const ShiftControl& control, std::int64_t now,
                       std::int64_t firstOutput);
    [[nodiscard]] double snapToPeak(const History& history, double ideal, double period, double limit) const;

    double sampleRate_ = 48000.0;
    int latency_ = 1024;
    int maxGrain_ = 8192;

    std::vector<float> accumulator_[2];
    std::vector<float> weight_;
    std::uint32_t accumulatorMask_ = 0;

    bool initialised_ = false;
    bool wasEngaged_ = false;
    bool havePrevious_ = false;
    double nextEpoch_ = 0.0;
    double previousCentre_ = 0.0;
    float engage_ = 0.0f;
    std::vector<float> referenceScratch_, candidateScratch_, scoreScratch_;
};

// ── Best ───────────────────────────────────────────────────────────────────
//
// Phase vocoder with identity phase locking around spectral peaks (Laroche and
// Dolson). Peaks and their regions of influence are moved to the new pitch and
// their phases propagated at the new frequency. Formants are held by dividing
// out the shifted cepstral envelope and multiplying the original back in.
class SpectralShifter {
public:
    void prepare(double sampleRate);
    void reset();
    [[nodiscard]] int latencySamples() const noexcept { return size_; }
    void process(const History& history, const ShiftControl& control, float* outLeft, float* outRight,
                 int frames);

private:
    struct Channel {
        std::vector<float> re, im, mag, phase, prevPhase, psi, newPsi, outRe, outIm, outMag2, envelope;
    };

    void processFrame(const History& history, std::int64_t start, const ShiftControl& control);
    void computeEnvelopes(float period);
    void shiftChannel(Channel& channel, float ratio, float formantRatio, bool correctFormants);
    [[nodiscard]] static float envelopeAt(const Channel& channel, float bin, int bins);

    double sampleRate_ = 48000.0;
    int size_ = 2048;
    int hop_ = 512;
    int bins_ = 1025;
    float olaScale_ = 1.0f;

    Fft fft_;
    std::vector<float> window_;
    std::vector<float> work_;
    Channel channels_[2];
    std::vector<int> peaks_;

    std::vector<float> output_[2];
    std::uint32_t outputMask_ = 0;
    std::int64_t nextFrame_ = 0;
    bool initialised_ = false;
};

}  // namespace downspout::qune
