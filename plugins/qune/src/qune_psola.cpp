#include "qune_params.hpp"
#include "qune_shifters.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::qune {

namespace {

constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kMinRatio = 0.25;
constexpr double kMaxRatio = 4.0;
constexpr double kMinFormant = 0.5;
constexpr double kMaxFormant = 2.0;
constexpr int kMaxCorrelationSegment = 512;

// Latency is two periods of the lowest pitch plus a margin: a grain reaches one
// period past its centre into the input, and its output half-window has to be
// finished before the first sample of it is emitted.
constexpr double kLatencyPeriods = 2.4;
constexpr int kLatencyMargin = 64;
constexpr float kMinWeight = 0.3f;

// Output grain length. The natural length is two periods of the grain's own
// content (2 T0 / formant); it is stretched so neighbouring grains always
// overlap by at least 1.6x, and held under 2.5 periods so the output half of
// the window fits in the latency. Where those limits conflict (a big drop in
// pitch), the weight normalisation in the emit loop absorbs the ripple.
[[nodiscard]] int grainLength(const double period, const double ratio, const double formant, const int maxGrain)
{
    const double spacing = period / ratio;
    const double length = std::min(std::max(2.0 * period / formant, 1.6 * spacing), 2.5 * period);
    return std::clamp(static_cast<int>(std::lround(length)), 16, maxGrain);
}

[[nodiscard]] std::uint32_t nextPow2(std::uint64_t value)
{
    std::uint64_t p = 1;
    while (p < value)
        p <<= 1;
    return static_cast<std::uint32_t>(p);
}

}  // namespace

void PsolaShifter::prepare(const double sampleRate)
{
    sampleRate_ = sampleRate;
    maxGrain_ = static_cast<int>(sampleRate / 10.0);
    const std::uint32_t size = nextPow2(static_cast<std::uint64_t>(maxGrain_) * 4u);
    for (auto& channel : accumulator_)
        channel.assign(size, 0.0f);
    weight_.assign(size, 0.0f);
    accumulatorMask_ = size - 1u;
    setLowestPitch(kRanges[1].minHz);
    reset();
}

void PsolaShifter::setLowestPitch(const float minHz)
{
    latency_ = static_cast<int>(std::ceil(kLatencyPeriods * sampleRate_ / std::max(minHz, 20.0f))) + kLatencyMargin;
}

void PsolaShifter::reset()
{
    for (auto& channel : accumulator_)
        std::fill(channel.begin(), channel.end(), 0.0f);
    std::fill(weight_.begin(), weight_.end(), 0.0f);
    initialised_ = false;
    wasEngaged_ = false;
    havePrevious_ = false;
    engage_ = 0.0f;
}

void PsolaShifter::process(const History& history, const ShiftControl& control, float* outLeft,
                           float* outRight, const int frames)
{
    const std::int64_t end = history.count();
    const std::int64_t start = end - frames;
    const std::int64_t firstOutput = start - latency_;

    if (!initialised_)
    {
        initialised_ = true;
        nextEpoch_ = static_cast<double>(firstOutput);
    }

    const double ratio = std::clamp(static_cast<double>(control.ratio), kMinRatio, kMaxRatio);
    const double formant = std::clamp(static_cast<double>(control.formantRatio), kMinFormant, kMaxFormant);
    const bool engaged = control.voiced && control.period > 16.0f
                         && (std::fabs(std::log(ratio)) > 1.0e-3 || std::fabs(std::log(formant)) > 1.0e-3);

    if (engaged)
    {
        const int grain = grainLength(control.period, ratio, formant, maxGrain_);
        if (!wasEngaged_)
        {
            havePrevious_ = false;
            nextEpoch_ = static_cast<double>(firstOutput) + 0.5 * grain;
        }
        // Every grain that touches an output sample due in this block has to
        // exist before the block is emitted.
        const double horizon = static_cast<double>(end - latency_);
        while (nextEpoch_ - 0.5 * grain < horizon)
            scheduleGrain(history, control, end, firstOutput);
    }
    wasEngaged_ = engaged;

    const float riseCoeff = 1.0f - std::exp(-1.0f / static_cast<float>(0.002 * sampleRate_));
    const float fallCoeff = 1.0f - std::exp(-1.0f / static_cast<float>(0.004 * sampleRate_));
    const float target = engaged ? 1.0f : 0.0f;

    for (int i = 0; i < frames; ++i)
    {
        const std::int64_t index = firstOutput + i;
        const std::size_t slot = static_cast<std::size_t>(static_cast<std::uint64_t>(index) & accumulatorMask_);

        engage_ += (target - engage_) * (target > engage_ ? riseCoeff : fallCoeff);
        // Divide by the window weight actually present, so uneven grain overlap
        // does not become amplitude ripple. Thin edges are left windowed.
        const float norm = 1.0f / std::max(weight_[slot], kMinWeight);
        const float wetLeft = accumulator_[0][slot] * norm;
        const float wetRight = accumulator_[1][slot] * norm;
        accumulator_[0][slot] = 0.0f;
        accumulator_[1][slot] = 0.0f;
        weight_[slot] = 0.0f;

        const float dryLeft = history.sample(0, index);
        const float dryRight = history.sample(1, index);
        outLeft[i] = dryLeft + engage_ * (wetLeft - dryLeft);
        outRight[i] = dryRight + engage_ * (wetRight - dryRight);
    }
}

double PsolaShifter::snapToPeak(const History& history, const double ideal, const double period,
                                const double limit) const
{
    const auto reach = static_cast<std::int64_t>(period * 0.5);
    const auto middle = static_cast<std::int64_t>(std::llround(ideal));
    std::int64_t bestPosition = middle;
    float bestValue = -1.0f;
    for (std::int64_t i = middle - reach; i <= middle + reach; ++i)
    {
        if (static_cast<double>(i) > limit)
            break;
        // Three-point smoothing keeps a single noisy sample from winning.
        const float v = std::fabs(0.25f * history.mono(i - 1) + 0.5f * history.mono(i) + 0.25f * history.mono(i + 1));
        if (v > bestValue)
        {
            bestValue = v;
            bestPosition = i;
        }
    }
    const float a = std::fabs(history.mono(bestPosition - 1));
    const float b = std::fabs(history.mono(bestPosition));
    const float c = std::fabs(history.mono(bestPosition + 1));
    const float curvature = a - 2.0f * b + c;
    double offset = 0.0;
    if (curvature < -1.0e-12f)
        offset = std::clamp(0.5 * static_cast<double>(a - c) / static_cast<double>(curvature), -1.0, 1.0);
    return static_cast<double>(bestPosition) + offset;
}

void PsolaShifter::scheduleGrain(const History& history, const ShiftControl& control,
                                 const std::int64_t now, const std::int64_t firstOutput)
{
    const double period = control.period;
    const double ratio = std::clamp(static_cast<double>(control.ratio), kMinRatio, kMaxRatio);
    const double formant = std::clamp(static_cast<double>(control.formantRatio), kMinFormant, kMaxFormant);
    const int grain = grainLength(period, ratio, formant, maxGrain_);
    // The newest centre whose grain (a period either side, plus room to slide
    // the centre) is already in the history.
    const double centreLimit = static_cast<double>(now) - std::ceil(period * 1.125 + 4.0);

    // Centres are fractional. Rounding them to whole samples puts up to half a
    // sample of timing noise on every pitch mark, which at a few hundred hertz
    // is several cents of pitch error.
    double centre = 0.0;
    if (!havePrevious_)
    {
        // First grain of a run: put the pitch mark on the waveform's largest
        // peak near the ideal position. Later marks inherit that alignment
        // through the correlation below. A mark that lands mid-cycle makes
        // neighbouring harmonics inside the grain interfere, which shows up as
        // notches in the output spectrum whenever pitch and formants move apart.
        centre = snapToPeak(history, nextEpoch_, period, centreLimit);
    }
    else
    {
        const double steps = std::floor((nextEpoch_ - previousCentre_) / period + 0.5);
        if (steps <= 0.0)
        {
            centre = previousCentre_;  // pitching up: repeat the grain
        }
        else
        {
            const double candidate = previousCentre_ + steps * period;

            // Slide the candidate so the waveform around it lines up with the
            // waveform around the previous centre, to a fraction of a sample.
            const int half = std::clamp(static_cast<int>(period * 0.5), 16, kMaxCorrelationSegment);
            const int stride = 1 + static_cast<int>(period / 350.0);
            const int slide = static_cast<int>(period / 8.0);
            const int length = (2 * half) / stride;
            const int span = 2 * half + 2 * slide + 2;

            const auto sampleMono = [&history](const double position) {
                return 0.5f * (history.cubic(0, position) + history.cubic(1, position));
            };

            referenceScratch_.resize(static_cast<std::size_t>(length));
            for (int j = 0; j < length; ++j)
                referenceScratch_[static_cast<std::size_t>(j)] = sampleMono(previousCentre_ - half + j * stride);

            const double origin = std::floor(candidate) - slide - half;
            const double fraction = candidate - std::floor(candidate);
            candidateScratch_.resize(static_cast<std::size_t>(span));
            for (int i = 0; i < span; ++i)
                candidateScratch_[static_cast<std::size_t>(i)] = sampleMono(origin + fraction + i);

            scoreScratch_.assign(static_cast<std::size_t>(2 * slide + 1), 0.0f);
            int bestIndex = 0;
            float bestScore = -1.0e30f;
            for (int shift = 0; shift <= 2 * slide; ++shift)
            {
                float corr = 0.0f;
                float energy = 1.0e-9f;
                const float* c = candidateScratch_.data() + shift;
                for (int j = 0; j < length; ++j)
                {
                    const float v = c[j * stride];
                    corr += referenceScratch_[static_cast<std::size_t>(j)] * v;
                    energy += v * v;
                }
                const float score = corr / std::sqrt(energy);
                scoreScratch_[static_cast<std::size_t>(shift)] = score;
                if (score > bestScore)
                {
                    bestScore = score;
                    bestIndex = shift;
                }
            }

            double offset = 0.0;
            if (bestIndex > 0 && bestIndex < 2 * slide)
            {
                const float a = scoreScratch_[static_cast<std::size_t>(bestIndex - 1)];
                const float b = scoreScratch_[static_cast<std::size_t>(bestIndex)];
                const float c = scoreScratch_[static_cast<std::size_t>(bestIndex + 1)];
                const float curvature = a - 2.0f * b + c;
                if (std::fabs(curvature) > 1.0e-12f)
                    offset = std::clamp(0.5 * static_cast<double>(a - c) / static_cast<double>(curvature), -1.0, 1.0);
            }
            centre = candidate + static_cast<double>(bestIndex - slide) + offset;
        }
    }

    if (centre > centreLimit)
        centre -= std::ceil((centre - centreLimit) / period) * period;

    const double epoch = nextEpoch_;
    const auto first = static_cast<std::int64_t>(std::ceil(epoch - 0.5 * grain));
    const auto last = static_cast<std::int64_t>(std::floor(epoch + 0.5 * grain));
    for (std::int64_t i = std::max(first, firstOutput); i <= last; ++i)
    {
        const double offset = static_cast<double>(i) - epoch;
        const float window = static_cast<float>(0.5 + 0.5 * std::cos(kTwoPi * offset / grain));
        const double position = centre + offset * formant;
        const std::size_t slot = static_cast<std::size_t>(static_cast<std::uint64_t>(i) & accumulatorMask_);
        accumulator_[0][slot] += window * history.cubic(0, position);
        accumulator_[1][slot] += window * history.cubic(1, position);
        weight_[slot] += window;
    }

    nextEpoch_ += period / ratio;
    previousCentre_ = centre;
    havePrevious_ = true;
}

}  // namespace downspout::qune
