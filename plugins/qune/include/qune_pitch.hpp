#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace downspout::qune {

// Monophonic pitch tracker.
//
// Stage 1 decimates the mono input to roughly 8-16x the highest pitch of
// interest and runs a normalised square difference function (McLeod) on it.
// Stage 2 refines the winning lag on the full-rate signal, so the estimate is
// not limited by the decimated sample period. A hop of about 4 ms is run through
// a small state machine that rejects octave errors and one-off jumps.
class PitchTracker {
public:
    struct Result {
        float f0Hz = 0.0f;
        float periodSamples = 0.0f;  // full-rate period, 0 when unvoiced
        float clarity = 0.0f;        // peak height of the NSDF, 0..1
        float level = 0.0f;          // RMS of the last hop
        bool voiced = false;
        std::uint32_t hop = 0;       // increments once per analysis hop
    };

    // Allocates for the absolute pitch limits. Call from a non-realtime thread.
    void prepare(double sampleRate);

    // Restricts the search range; resets the analysis history. No allocation.
    void setRange(float minHz, float maxHz);

    // 0..1, higher accepts less clear pitches.
    void setVoicing(float voicing) noexcept;

    void reset();

    void process(const float* mono, int frames);

    [[nodiscard]] const Result& result() const noexcept { return result_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

private:
    struct Biquad {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
        float run(const float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void designFilter();
    void pushDecimated(float value);
    void analyse();
    bool coarseSearch(float& lag, float& clarity);
    void refine(float coarsePeriod, float& period, float& value);
    void finalise(float period, float clarity, float level, bool candidate);

    double sampleRate_ = 48000.0;
    float minHz_ = 90.0f;
    float maxHz_ = 900.0f;
    float threshold_ = 0.6f;

    int decimation_ = 1;
    double decimatedRate_ = 48000.0;
    int tauMin_ = 2;
    int tauMax_ = 100;
    int window_ = 100;
    int hopSamples_ = 192;

    std::array<Biquad, 2> lowpass_ {};
    float dcX_ = 0.0f;
    float dcY_ = 0.0f;
    float dcCoeff_ = 0.999f;
    int decimationPhase_ = 0;
    int sinceHop_ = 0;

    std::vector<float> fullRing_;
    std::uint32_t fullMask_ = 0;
    std::uint64_t fullCount_ = 0;

    std::vector<float> decRing_;
    std::uint32_t decMask_ = 0;
    std::uint64_t decCount_ = 0;

    std::vector<float> linear_;
    std::vector<float> nsdf_;
    std::vector<float> refineBuffer_;

    // Tracking state machine.
    bool havePitch_ = false;
    float trackedHz_ = 0.0f;
    float candidateHz_ = 0.0f;
    int candidateCount_ = 0;
    int missed_ = 0;

    Result result_ {};
};

}  // namespace downspout::qune
