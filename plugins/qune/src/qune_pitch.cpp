#include "qune_pitch.hpp"

#include "qune_params.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::qune {

namespace {

constexpr float kLevelGate = 0.0008f;   // about -62 dBFS RMS
constexpr float kLobeTolerance = 0.95f;  // first lobe within 90% of the best wins
constexpr float kProminence = 0.1f;     // rise needed above the preceding valley
constexpr float kSubharmonicRatio = 0.88f;  // half-period must score this well to win
constexpr int kHoldHops = 3;           // dropouts shorter than this keep the pitch
constexpr float kJumpSemitones = 2.5f;  // bigger moves must persist to be believed
constexpr int kJumpConfirmHops = 2;
constexpr std::size_t kMaxLobes = 48;

[[nodiscard]] std::uint32_t nextPow2(std::uint64_t value)
{
    std::uint64_t p = 1;
    while (p < value)
        p <<= 1;
    return static_cast<std::uint32_t>(p);
}

[[nodiscard]] float semitoneDistance(const float a, const float b)
{
    return 12.0f * std::fabs(std::log2(std::max(a, 1.0e-3f) / std::max(b, 1.0e-3f)));
}

}  // namespace

void PitchTracker::prepare(const double sampleRate)
{
    sampleRate_ = sampleRate;
    const double longestPeriod = sampleRate / static_cast<double>(kAbsoluteMinHz);

    fullRing_.assign(nextPow2(static_cast<std::uint64_t>(4.0 * longestPeriod) + 64u), 0.0f);
    fullMask_ = static_cast<std::uint32_t>(fullRing_.size() - 1u);
    decRing_.assign(4096, 0.0f);
    decMask_ = 4095u;
    linear_.assign(4096, 0.0f);
    nsdf_.assign(4096, 0.0f);
    refineBuffer_.assign(static_cast<std::size_t>(3.0 * longestPeriod) + 4096u, 0.0f);

    dcCoeff_ = 1.0f - static_cast<float>(2.0 * 3.14159265358979323846 * 25.0 / sampleRate);
    hopSamples_ = std::max(32, static_cast<int>(std::lround(0.004 * sampleRate)));

    setRange(minHz_, maxHz_);
}

void PitchTracker::setRange(const float minHz, const float maxHz)
{
    minHz_ = std::max(minHz, kAbsoluteMinHz);
    maxHz_ = std::min(std::max(maxHz, minHz_ * 2.0f), kAbsoluteMaxHz);

    decimation_ = std::max(1, static_cast<int>(std::floor(sampleRate_ / (8.0 * maxHz_))));
    decimatedRate_ = sampleRate_ / static_cast<double>(decimation_);
    tauMin_ = std::max(2, static_cast<int>(std::floor(decimatedRate_ / maxHz_)));
    tauMax_ = static_cast<int>(std::ceil(decimatedRate_ / minHz_)) + 1;
    window_ = tauMax_ + tauMax_ / 4 + 16;

    designFilter();
    reset();
}

void PitchTracker::setVoicing(const float voicing) noexcept
{
    threshold_ = 0.9f - 0.6f * std::clamp(voicing, 0.0f, 1.0f);
}

void PitchTracker::designFilter()
{
    // Two cascaded RBJ lowpass sections (Butterworth Q) at 0.4 of the decimated
    // rate. With no decimation the filters are transparent.
    for (Biquad& section : lowpass_)
    {
        section = Biquad {};
        section.b0 = 1.0f;
    }
    if (decimation_ <= 1)
        return;

    const double fc = 0.4 * decimatedRate_;
    const double w0 = 2.0 * 3.14159265358979323846 * fc / sampleRate_;
    const double alpha = std::sin(w0) / (2.0 * 0.70710678);
    const double cosw = std::cos(w0);
    const double a0 = 1.0 + alpha;
    for (Biquad& section : lowpass_)
    {
        section.b0 = static_cast<float>((1.0 - cosw) * 0.5 / a0);
        section.b1 = static_cast<float>((1.0 - cosw) / a0);
        section.b2 = section.b0;
        section.a1 = static_cast<float>(-2.0 * cosw / a0);
        section.a2 = static_cast<float>((1.0 - alpha) / a0);
    }
}

void PitchTracker::reset()
{
    std::fill(fullRing_.begin(), fullRing_.end(), 0.0f);
    std::fill(decRing_.begin(), decRing_.end(), 0.0f);
    fullCount_ = 0;
    decCount_ = 0;
    decimationPhase_ = 0;
    sinceHop_ = 0;
    dcX_ = 0.0f;
    dcY_ = 0.0f;
    for (Biquad& section : lowpass_)
        section.z1 = section.z2 = 0.0f;

    havePitch_ = false;
    trackedHz_ = 0.0f;
    candidateHz_ = 0.0f;
    candidateCount_ = 0;
    missed_ = 0;
    const std::uint32_t hop = result_.hop;
    result_ = Result {};
    result_.hop = hop;
}

void PitchTracker::process(const float* mono, const int frames)
{
    if (fullRing_.empty())
        return;

    for (int i = 0; i < frames; ++i)
    {
        // DC blocker: the NSDF is a normalised correlation and an offset inflates it.
        const float x = mono[i];
        const float y = x - dcX_ + dcCoeff_ * dcY_;
        dcX_ = x;
        dcY_ = y;

        fullRing_[static_cast<std::size_t>(fullCount_ & fullMask_)] = y;
        ++fullCount_;

        if (decimation_ <= 1)
        {
            pushDecimated(y);
        }
        else
        {
            const float filtered = lowpass_[1].run(lowpass_[0].run(y));
            if (++decimationPhase_ >= decimation_)
            {
                decimationPhase_ = 0;
                pushDecimated(filtered);
            }
        }

        if (++sinceHop_ >= hopSamples_)
        {
            sinceHop_ = 0;
            analyse();
        }
    }

    // Idle input decays the filter state towards denormals.
    for (Biquad& section : lowpass_)
    {
        if (std::fabs(section.z1) < 1.0e-20f)
            section.z1 = 0.0f;
        if (std::fabs(section.z2) < 1.0e-20f)
            section.z2 = 0.0f;
    }
    if (std::fabs(dcY_) < 1.0e-20f)
        dcY_ = 0.0f;
}

void PitchTracker::pushDecimated(const float value)
{
    decRing_[static_cast<std::size_t>(decCount_ & decMask_)] = value;
    ++decCount_;
}

void PitchTracker::analyse()
{
    ++result_.hop;

    // Level over the last hop.
    double energy = 0.0;
    for (int i = 1; i <= hopSamples_; ++i)
    {
        const float v = fullRing_[static_cast<std::size_t>((fullCount_ - static_cast<std::uint64_t>(i)) & fullMask_)];
        energy += static_cast<double>(v) * static_cast<double>(v);
    }
    const float level = static_cast<float>(std::sqrt(energy / hopSamples_));
    result_.level = level;

    const std::uint64_t needed = static_cast<std::uint64_t>(window_ + tauMax_ + 2);
    float lag = 0.0f;
    float clarity = 0.0f;
    bool found = false;
    if (decCount_ >= needed && level > kLevelGate)
        found = coarseSearch(lag, clarity);

    float period = 0.0f;
    if (found)
    {
        period = lag * static_cast<float>(decimation_);
        if (decimation_ > 1)
        {
            // The decimated NSDF samples a bright waveform's peak coarsely, so
            // a period that is not a whole number of decimated samples scores
            // lower than its own double, which would be reported an octave low.
            // The full-rate NSDF resolves the peak, so test the half-period.
            float value = 0.0f;
            refine(period, period, value);
            for (int octave = 0; octave < 2; ++octave)
            {
                const float half = period * 0.5f;
                if (half < static_cast<float>(sampleRate_ / maxHz_) / 1.05f)
                    break;
                float halfPeriod = 0.0f;
                float halfValue = 0.0f;
                refine(half, halfPeriod, halfValue);
                if (halfValue > 0.5f && halfValue >= kSubharmonicRatio * value)
                {
                    period = halfPeriod;
                    value = halfValue;
                }
                else
                    break;
            }
        }
    }
    finalise(period, clarity, level, found);
}

bool PitchTracker::coarseSearch(float& lagOut, float& clarityOut)
{
    const int total = window_ + tauMax_ + 2;
    for (int i = 0; i < total; ++i)
    {
        linear_[static_cast<std::size_t>(i)] =
            decRing_[static_cast<std::size_t>((decCount_ - static_cast<std::uint64_t>(total - i)) & decMask_)];
    }
    const float* x = linear_.data();
    const int w = window_;

    float energyHead = 0.0f;
    float energyShift = 0.0f;
    for (int j = 0; j < w; ++j)
    {
        energyHead += x[j] * x[j];
        energyShift += x[j + 1] * x[j + 1];
    }

    // NSDF for lags 1 .. tauMax + 1. energyShift tracks the energy of the lagged
    // window as the lag grows.
    for (int tau = 1; tau <= tauMax_ + 1; ++tau)
    {
        float corr = 0.0f;
        const float* y = x + tau;
        for (int j = 0; j < w; ++j)
            corr += x[j] * y[j];
        const float denom = energyHead + energyShift;
        nsdf_[static_cast<std::size_t>(tau)] = (denom > 1.0e-12f) ? 2.0f * corr / denom : 0.0f;

        energyShift += x[w + tau] * x[w + tau] - x[tau] * x[tau];
        if (energyShift < 0.0f)
            energyShift = 0.0f;
    }

    struct Lobe {
        int index;
        float value;
    };
    // Height of the parabola through a peak and its neighbours. Integer lags
    // undersample a short period, and comparing raw samples would favour a
    // lobe that happens to land on a sample.
    const auto interpolatedHeight = [this](const int index) {
        const float a = nsdf_[static_cast<std::size_t>(index - 1)];
        const float b = nsdf_[static_cast<std::size_t>(index)];
        const float c = nsdf_[static_cast<std::size_t>(index + 1)];
        const float curvature = a - 2.0f * b + c;
        if (std::fabs(curvature) < 1.0e-9f)
            return b;
        const float offset = std::clamp(0.5f * (a - c) / curvature, -1.0f, 1.0f);
        return b - 0.25f * (a - c) * offset;
    };
    std::array<Lobe, kMaxLobes> lobes {};
    std::size_t lobeCount = 0;

    // Peaks are found by prominence rather than by zero crossings: a bright
    // waveform can keep the NSDF positive from lag zero straight through its
    // first period peak, which a zero-crossing search merges into the zero-lag
    // lobe and then reports an octave low.
    int t = 1;
    while (t < tauMax_ && nsdf_[static_cast<std::size_t>(t + 1)] < nsdf_[static_cast<std::size_t>(t)])
        ++t;  // descend off the lag-zero lobe
    float valley = nsdf_[static_cast<std::size_t>(t)];
    float peakValue = -2.0f;
    int peakIndex = -1;
    for (; t <= tauMax_ && lobeCount < kMaxLobes; ++t)
    {
        const float v = nsdf_[static_cast<std::size_t>(t)];
        if (peakIndex < 0)
        {
            valley = std::min(valley, v);
            if (v - valley >= kProminence)
            {
                peakIndex = t;
                peakValue = v;
            }
        }
        else if (v > peakValue)
        {
            peakIndex = t;
            peakValue = v;
        }
        else if (peakValue - v >= kProminence)
        {
            if (peakIndex >= tauMin_)
                lobes[lobeCount++] = {peakIndex, interpolatedHeight(peakIndex)};
            peakIndex = -1;
            valley = v;
        }
    }
    if (peakIndex >= tauMin_ && lobeCount < kMaxLobes)
        lobes[lobeCount++] = {peakIndex, interpolatedHeight(peakIndex)};
    if (lobeCount == 0)
        return false;

    float peak = 0.0f;
    for (std::size_t i = 0; i < lobeCount; ++i)
        peak = std::max(peak, lobes[i].value);

    std::size_t chosen = 0;
    for (std::size_t i = 0; i < lobeCount; ++i)
    {
        if (lobes[i].value >= kLobeTolerance * peak)
        {
            chosen = i;
            break;
        }
    }

    const int index = lobes[chosen].index;
    const float a = nsdf_[static_cast<std::size_t>(index - 1)];
    const float b = nsdf_[static_cast<std::size_t>(index)];
    const float c = nsdf_[static_cast<std::size_t>(index + 1)];
    const float curvature = a - 2.0f * b + c;
    float offset = 0.0f;
    if (std::fabs(curvature) > 1.0e-9f)
        offset = std::clamp(0.5f * (a - c) / curvature, -1.0f, 1.0f);

    lagOut = static_cast<float>(index) + offset;
    clarityOut = std::clamp(b - 0.25f * (a - c) * offset, 0.0f, 1.0f);
    return true;
}

void PitchTracker::refine(const float coarsePeriod, float& periodOut, float& valueOut)
{
    valueOut = 0.0f;
    const int lagLow = std::max(2, static_cast<int>(std::floor(coarsePeriod)) - decimation_ - 1);
    const int lagHigh = static_cast<int>(std::ceil(coarsePeriod)) + decimation_ + 1;
    const int w = std::clamp(static_cast<int>(1.5f * coarsePeriod), 64, 4096);
    const int span = w + lagHigh;
    if (fullCount_ < static_cast<std::uint64_t>(span) || static_cast<std::size_t>(span) > refineBuffer_.size())
    {
        periodOut = coarsePeriod;
        return;
    }

    for (int i = 0; i < span; ++i)
    {
        refineBuffer_[static_cast<std::size_t>(i)] =
            fullRing_[static_cast<std::size_t>((fullCount_ - static_cast<std::uint64_t>(span - i)) & fullMask_)];
    }

    const float* head = refineBuffer_.data() + lagHigh;
    float energyHead = 0.0f;
    for (int j = 0; j < w; ++j)
        energyHead += head[j] * head[j];

    const int count = lagHigh - lagLow + 1;
    float bestValue = -2.0f;
    int bestIndex = 0;
    for (int k = 0; k < count; ++k)
    {
        const int lag = lagLow + k;
        const float* lagged = head - lag;
        float corr = 0.0f;
        float energyLag = 0.0f;
        for (int j = 0; j < w; ++j)
        {
            corr += head[j] * lagged[j];
            energyLag += lagged[j] * lagged[j];
        }
        const float denom = energyHead + energyLag;
        const float value = (denom > 1.0e-12f) ? 2.0f * corr / denom : 0.0f;
        nsdf_[static_cast<std::size_t>(k)] = value;
        if (value > bestValue)
        {
            bestValue = value;
            bestIndex = k;
        }
        if (k + 1 >= static_cast<int>(nsdf_.size()))
            break;
    }

    if (bestIndex <= 0 || bestIndex >= count - 1)
    {
        periodOut = coarsePeriod;
        return;
    }
    const float a = nsdf_[static_cast<std::size_t>(bestIndex - 1)];
    const float b = nsdf_[static_cast<std::size_t>(bestIndex)];
    const float c = nsdf_[static_cast<std::size_t>(bestIndex + 1)];
    const float curvature = a - 2.0f * b + c;
    float offset = 0.0f;
    if (std::fabs(curvature) > 1.0e-9f)
        offset = std::clamp(0.5f * (a - c) / curvature, -1.0f, 1.0f);
    periodOut = static_cast<float>(lagLow + bestIndex) + offset;
    valueOut = b - 0.25f * (a - c) * offset;
}

void PitchTracker::finalise(const float period, const float clarity, const float level, const bool candidate)
{
    const float hz = (period > 1.0f) ? static_cast<float>(sampleRate_) / period : 0.0f;
    const bool inRange = hz >= minHz_ * 0.95f && hz <= maxHz_ * 1.05f;
    const bool accepted = candidate && inRange && clarity >= threshold_ && level > kLevelGate;

    if (accepted)
    {
        missed_ = 0;
        if (!havePitch_)
        {
            havePitch_ = true;
            trackedHz_ = hz;
            candidateCount_ = 0;
        }
        else if (semitoneDistance(hz, trackedHz_) <= kJumpSemitones)
        {
            trackedHz_ = hz;
            candidateCount_ = 0;
        }
        else
        {
            // A large jump is either a new note or an octave error. Believe it
            // only once it repeats.
            if (candidateCount_ > 0 && semitoneDistance(hz, candidateHz_) < 0.7f)
                ++candidateCount_;
            else
            {
                candidateHz_ = hz;
                candidateCount_ = 1;
            }
            if (candidateCount_ >= kJumpConfirmHops)
            {
                trackedHz_ = hz;
                candidateCount_ = 0;
            }
        }
    }
    else if (havePitch_ && ++missed_ > kHoldHops)
    {
        havePitch_ = false;
        candidateCount_ = 0;
    }

    result_.voiced = havePitch_;
    result_.clarity = accepted ? clarity : 0.0f;
    result_.f0Hz = havePitch_ ? trackedHz_ : 0.0f;
    result_.periodSamples = havePitch_ ? static_cast<float>(sampleRate_) / trackedHz_ : 0.0f;
}

}  // namespace downspout::qune
