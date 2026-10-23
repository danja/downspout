#include "ladder_core.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>

namespace downspout::ladder {

namespace {

constexpr float kPi = 3.14159265358979f;

float clampf(float v, float lo, float hi) noexcept
{
    if (!std::isfinite(v)) return lo;
    return std::min(std::max(v, lo), hi);
}

// Pade [4/4] for tanh(x)/x, exact asymptote beyond |x| = 4.5.
inline float tanhOverX(float x) noexcept
{
    const float ax = std::fabs(x);
    if (ax > 4.5f) return 1.0f / ax;
    const float a = x * x;
    return ((a + 105.0f) * a + 945.0f) / ((15.0f * a + 420.0f) * a + 945.0f);
}

inline float processSample(ChannelState& c, float x, const Coefficients& co) noexcept
{
    const float f = co.f;
    const float r = co.k;
    const float prev = c.prevInput;
    c.prevInput = x;
    float y4 = c.s4;

    for (int pass = 0; pass < 2; ++pass) {
        const float in = pass == 0 ? 0.5f * (x + prev) : x;
        const float s1 = c.s1, s2 = c.s2, s3 = c.s3, s4 = c.s4;

        const float t0 = tanhOverX(in - r * s4);
        const float t1 = tanhOverX(s1);
        const float t2 = tanhOverX(s2);
        const float t3 = tanhOverX(s3);
        const float t4 = tanhOverX(s4);

        const float g0 = 1.0f / (1.0f + f * t1);
        const float g1 = 1.0f / (1.0f + f * t2);
        const float g2 = 1.0f / (1.0f + f * t3);
        const float g3 = 1.0f / (1.0f + f * t4);

        const float f3 = f * t3 * g3;
        const float f2 = f * t2 * g2 * f3;
        const float f1 = f * t1 * g1 * f2;
        const float f0 = f * t0 * g0 * f1;

        const float num = g3 * s4 + f3 * g2 * s3 + f2 * g1 * s2 + f1 * g0 * s1 + f0 * in;
        y4 = num / (1.0f + r * f0);

        const float u0 = t0 * (in - r * y4);
        const float v1 = g0 * (s1 + f * u0);
        const float v2 = g1 * (s2 + f * t1 * v1);
        const float v3 = g2 * (s3 + f * t2 * v2);

        c.s1 = 2.0f * v1 - s1;
        c.s2 = 2.0f * v2 - s2;
        c.s3 = 2.0f * v3 - s3;
        c.s4 = 2.0f * y4 - s4;
    }
    return y4;
}

// A recursive filter on a silent input decays into denormals (several times the CPU),
// so tiny states are zeroed once per block.
inline void flushTiny(ChannelState& c) noexcept
{
    constexpr float kTiny = 1.0e-20f;
    if (std::fabs(c.s1) < kTiny) c.s1 = 0.0f;
    if (std::fabs(c.s2) < kTiny) c.s2 = 0.0f;
    if (std::fabs(c.s3) < kTiny) c.s3 = 0.0f;
    if (std::fabs(c.s4) < kTiny) c.s4 = 0.0f;
    if (std::fabs(c.prevInput) < kTiny) c.prevInput = 0.0f;
}

}  // namespace

Parameters clampParameters(const Parameters& p) noexcept
{
    Parameters o = p;
    o.cutoffHz  = clampf(p.cutoffHz, 20.0f, 18000.0f);
    o.resonance = clampf(p.resonance, 0.0f, 100.0f);
    o.driveDb   = clampf(p.driveDb, 0.0f, 24.0f);
    o.bassComp  = clampf(p.bassComp, 0.0f, 100.0f);
    o.mix       = clampf(p.mix, 0.0f, 100.0f);
    o.outputDb  = clampf(p.outputDb, -24.0f, 12.0f);
    o.ccChannel = std::round(clampf(p.ccChannel, 0.0f, 16.0f));
    return o;
}

float resonanceToFeedback(float resonance01) noexcept
{
    const float u = clampf(resonance01, 0.0f, 1.0f);
    // Peak height near the cutoff is about -20 log10(1 - k/4), so an exponential
    // taper in k spreads the peak roughly evenly in dB instead of piling it up
    // in the last tenth of the control as the source's power law does.
    return 4.2f * (1.0f - std::pow(10.0f, -u)) / 0.9f;
}

Coefficients makeCoefficients(float cutoffHz, float resonance01, float sampleRate) noexcept
{
    const float sr = sampleRate > 0.0f ? sampleRate : 44100.0f;
    const float fc = clampf(cutoffHz, 15.0f, std::min(sr * 0.45f, 18000.0f));
    // Prewarped at the 2x rate, rational tan approximation from the source.
    const float w  = fc * kPi / (2.0f * sr);
    const float w2 = w * w;
    Coefficients c;
    c.f = (w * (0.99999949f - 0.096524608f * w2)) / (1.0f - 0.42986726f * w2 + 0.009981878f * w2 * w2);
    c.k = resonanceToFeedback(resonance01);
    return c;
}

float ccToValue(int slot, int cc) noexcept
{
    const float u = static_cast<float>(std::min(std::max(cc, 0), 127)) / 127.0f;
    switch (slot) {
    case 0:  return 20.0f * std::pow(18000.0f / 20.0f, u);   // cutoff, logarithmic across the whole range
    case 1:  return 100.0f * u;                               // resonance
    case 2:  return 24.0f * u;                                // drive
    default: return 100.0f * u;                               // mix
    }
}

bool ccChannelMatches(unsigned char status, float ccChannel) noexcept
{
    return (status & 0xF0) == 0xB0 && ccChannel >= 0.5f && static_cast<int>(status & 0x0F) + 1 == static_cast<int>(std::lround(ccChannel));
}

void handleController(LiveControl& live, int controller, int value) noexcept
{
    if (controller >= 1 && controller <= kCcSlots)
        live.value[static_cast<size_t>(controller - 1)] = ccToValue(controller - 1, value);
}

Parameters withLive(const Parameters& p, const LiveControl& live) noexcept
{
    Parameters o = p;
    if (live.value[0] >= 0.0f) o.cutoffHz = live.value[0];
    if (live.value[1] >= 0.0f) o.resonance = live.value[1];
    if (live.value[2] >= 0.0f) o.driveDb = live.value[2];
    if (live.value[3] >= 0.0f) o.mix = live.value[3];
    return o;
}

double responseDb(const Parameters& raw, double hz) noexcept
{
    const Parameters p = clampParameters(raw);
    const double k = resonanceToFeedback(p.resonance * 0.01f);
    const std::complex<double> s(0.0, hz / static_cast<double>(p.cutoffHz));
    const std::complex<double> g = 1.0 / (1.0 + s);
    const std::complex<double> g4 = g * g * g * g;
    const std::complex<double> h = g4 / (1.0 + k * g4) * (1.0 + p.bassComp * 0.01 * k);
    const double wet = p.mix * 0.01;
    const std::complex<double> total = (1.0 - wet) + wet * h;
    return 20.0 * std::log10(std::abs(total) + 1e-9) + p.outputDb;
}

void resetState(EngineState& state) noexcept
{
    state.channels = {};
    state.primed = false;
}

void processBlock(EngineState& state, const Parameters& rawParams, float sampleRate, uint32_t frames,
                  const float* const* inputs, float* const* outputs) noexcept
{
    const Parameters p = clampParameters(rawParams);
    const Coefficients target = makeCoefficients(p.cutoffHz, p.resonance * 0.01f, sampleRate);
    Gains goal;
    goal.drive = std::pow(10.0f, p.driveDb / 20.0f);
    goal.out   = std::pow(10.0f, p.outputDb / 20.0f);
    goal.wet   = p.mix * 0.01f;
    goal.comp  = p.bassComp * 0.01f;
    if (!state.primed) {
        state.current = target;
        state.gains = goal;
        state.primed = true;
    }
    const Coefficients start = state.current;
    const Gains from = state.gains;
    const float invFrames = frames > 0 ? 1.0f / static_cast<float>(frames) : 0.0f;

    for (int ch = 0; ch < 2; ++ch) {
        const float* in = inputs[ch] ? inputs[ch] : inputs[0];
        float* dst = outputs[ch];
        ChannelState& cs = state.channels[static_cast<size_t>(ch)];
        for (uint32_t i = 0; i < frames; ++i) {
            const float t = static_cast<float>(i + 1) * invFrames;
            Coefficients co;
            co.f = start.f + (target.f - start.f) * t;
            co.k = start.k + (target.k - start.k) * t;
            const float drive = from.drive + (goal.drive - from.drive) * t;
            const float comp  = from.comp + (goal.comp - from.comp) * t;
            const float wet   = from.wet + (goal.wet - from.wet) * t;
            const float out   = from.out + (goal.out - from.out) * t;
            const float x = in[i];
            float y = processSample(cs, x * drive, co);
            // Restore the DC gain the feedback removes (1 / (1 + k)), by the chosen amount.
            y *= 1.0f + comp * co.k;
            if (!std::isfinite(y)) {
                cs = ChannelState {};
                y = 0.0f;
            }
            dst[i] = (x + (y - x) * wet) * out;
        }
        flushTiny(cs);
    }
    state.current = target;
    state.gains = goal;
}

}  // namespace downspout::ladder
