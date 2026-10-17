#include "fathom_processor.hpp"

#include "fathom_params.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::fathom {
namespace {

// Line length ratios: unrelated enough that the network does not ring at a pitch.
constexpr std::array<double, 4> kRatios = {{1.0, 1.31, 1.73, 2.29}};
constexpr std::array<double, 2> kPingRatios = {{1.0, 1.27}};
constexpr std::array<double, 4> kLfoHertz = {{0.13, 0.19, 0.27, 0.37}};
constexpr float kDamping = 0.55f;
constexpr float kMaxFeedback = 0.97f;

// Diffuser lengths at 48 kHz, mutually prime.
constexpr std::array<std::size_t, 4> kDiffuserSamples = {{142, 107, 379, 277}};
constexpr std::array<std::size_t, 2> kLoopDiffuserSamples = {{211, 167}};
constexpr std::array<std::size_t, 2> kInputDiffuserSamples = {{113, 337}};

[[nodiscard]] float lerp(const float a, const float b, const float t) noexcept
{
    return a + (b - a) * t;
}

}  // namespace

void Processor::prepare(const double sampleRate)
{
    sampleRate_ = std::max(sampleRate, 1.0);

    // Longest read: 0.8 s base x 2.29 plus modulation.
    const auto capacity = static_cast<std::size_t>(sampleRate_ * 2.2) + 16u;
    for (DelayLine& line : lines_)
        line.allocate(capacity);
    for (DelayLine& line : pingLines_)
        line.allocate(capacity);

    const double scale = sampleRate_ / 48000.0;
    const auto scaled = [&](const std::size_t n) { return static_cast<std::size_t>(static_cast<double>(n) * scale) + 1u; };
    for (std::size_t i = 0; i < diffusers_.size(); ++i)
        diffusers_[i].allocate(scaled(kDiffuserSamples[i]));
    for (std::size_t i = 0; i < loopDiffusers_.size(); ++i)
        loopDiffusers_[i].allocate(scaled(kLoopDiffuserSamples[i]));
    for (std::size_t i = 0; i < inputDiffusers_.size(); ++i)
        inputDiffusers_[i].allocate(scaled(kInputDiffuserSamples[i]));

    controlCoefficient_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.01 * sampleRate_)));
    timeCoefficient_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.15 * sampleRate_)));
    clear();
}

void Processor::clear() noexcept
{
    for (DelayLine& line : lines_)
        line.clear();
    for (DelayLine& line : pingLines_)
        line.clear();
    for (Allpass& a : diffusers_)
        a.clear();
    for (Allpass& a : loopDiffusers_)
        a.clear();
    for (Allpass& a : inputDiffusers_)
        a.clear();
    for (FrequencyShifter& s : shifters_)
        s.reset();
    for (FrequencyShifter& s : pingShifters_)
        s.reset();
    damping_.fill(0.0f);
    pingDamping_.fill(0.0f);
    for (std::size_t i = 0; i < lfoPhase_.size(); ++i)
        lfoPhase_[i] = 0.25 * static_cast<double>(i);
    shiftPhase_ = 0.0;
    time_ = target_.time;
    form_ = target_.form;
    feed_ = target_.feed;
}

void Processor::process(const float input, float& wetLeft, float& wetRight) noexcept
{
    time_ += (target_.time - time_) * timeCoefficient_;
    form_ += (target_.form - form_) * controlCoefficient_;
    feed_ += (target_.feed - feed_) * controlCoefficient_;

    const double shiftHertz = shiftToHertz(target_.shift);
    const bool shifting = std::fabs(shiftHertz) > 0.05;
    float cosine = 1.0f;
    float sine = 0.0f;
    if (shifting)
    {
        shiftPhase_ += shiftHertz / sampleRate_;
        shiftPhase_ -= std::floor(shiftPhase_);
        cosine = static_cast<float>(std::cos(kTwoPi * shiftPhase_));
        sine = static_cast<float>(std::sin(kTwoPi * shiftPhase_));
    }

    if (target_.kind == 0)
        processMetallic(input, cosine, sine, shifting, wetLeft, wetRight);
    else
        processDiffuse(input, cosine, sine, shifting, wetLeft, wetRight);
}

// Four delay lines through a Hadamard feedback matrix. Form moves the feedback
// from each line returning to itself (a bank of echoes) to all four mixing
// (a diffuse tail), and adds input diffusion and delay modulation on the way.
void Processor::processMetallic(const float input,
                                const float cosine,
                                const float sine,
                                const bool shifting,
                                float& left,
                                float& right) noexcept
{
    const float form = form_;
    const float gain = feed_ * kMaxFeedback;

    float x = input;
    if (form > 0.001f)
    {
        const float diffused = inputDiffusers_[1].process(inputDiffusers_[0].process(input, 0.5f), 0.5f);
        x = lerp(input, diffused, form);
    }

    const double base = timeToSeconds(time_) * sampleRate_;
    const double depth = static_cast<double>(form) * 0.002 * sampleRate_;

    std::array<float, 4> tap {};
    std::array<float, 4> fed {};
    for (std::size_t i = 0; i < 4; ++i)
    {
        lfoPhase_[i] += kLfoHertz[i] / sampleRate_;
        lfoPhase_[i] -= std::floor(lfoPhase_[i]);
        const double modulation = depth * sineTable()(lfoPhase_[i]);
        float y = lines_[i].read(base * kRatios[i] + modulation);
        damping_[i] = flush(damping_[i] + (y - damping_[i]) * kDamping);
        y = damping_[i];
        tap[i] = y;
        fed[i] = shifting ? shifters_[i].process(y, cosine, sine) : y;
    }

    const std::array<float, 4> mixed = {{
        0.5f * (fed[0] + fed[1] + fed[2] + fed[3]),
        0.5f * (fed[0] - fed[1] + fed[2] - fed[3]),
        0.5f * (fed[0] + fed[1] - fed[2] - fed[3]),
        0.5f * (fed[0] - fed[1] - fed[2] + fed[3]),
    }};

    for (std::size_t i = 0; i < 4; ++i)
        lines_[i].write(flush(softClip(x * 0.5f + gain * lerp(fed[i], mixed[i], form))));

    left = tap[0] + tap[2];
    right = tap[1] + tap[3];
}

// A stereo ping-pong delay. The input and the loop both pass through allpass
// diffusers whose strength Form sets, so the repeats smear from distinct echoes
// into a wash.
void Processor::processDiffuse(const float input,
                               const float cosine,
                               const float sine,
                               const bool shifting,
                               float& left,
                               float& right) noexcept
{
    const float form = form_;
    const float gain = feed_ * kMaxFeedback;
    const float diffusion = 0.15f + 0.55f * form;

    float x = input;
    for (Allpass& a : diffusers_)
        x = a.process(x, diffusion);
    x = lerp(input, x, std::min(1.0f, form * 1.5f + 0.2f));

    const double base = timeToSeconds(time_) * sampleRate_;
    std::array<float, 2> tap {};
    std::array<float, 2> fed {};
    for (std::size_t i = 0; i < 2; ++i)
    {
        float y = pingLines_[i].read(base * kPingRatios[i]);
        pingDamping_[i] = flush(pingDamping_[i] + (y - pingDamping_[i]) * kDamping);
        y = pingDamping_[i];
        tap[i] = y;
        fed[i] = shifting ? pingShifters_[i].process(y, cosine, sine) : y;
        fed[i] = lerp(fed[i], loopDiffusers_[i].process(fed[i], diffusion), form);
    }

    // Each side feeds the other, so repeats alternate between the ears.
    pingLines_[0].write(flush(softClip(x * 0.7f + gain * fed[1])));
    pingLines_[1].write(flush(softClip(x * 0.7f + gain * fed[0])));

    left = tap[0];
    right = tap[1];
}

}  // namespace downspout::fathom
