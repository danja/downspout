#include "mouth_engine.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace downspout::mouth {

namespace {

constexpr std::size_t kSubBlock = 32;
constexpr double kPi = 3.14159265358979323846;
constexpr float kSmoothSeconds = 0.02f;
constexpr float kAirCornerHz = 4000.0f;

float dbToGain(const float db) noexcept { return std::pow(10.0f, db / 20.0f); }

FormantSet formantsFor(const Parameters& p, const float x, const float y, const float shift, const float resonance,
                       const float spread, const int channelIndex, const double sampleRate) noexcept
{
    const FormantSet blended = blendVowel(x, y, static_cast<int>(std::lround(p.voice)));
    return shapeFormants(blended, shift, resonance, spread, channelIndex == 0 ? -1 : 1, static_cast<float>(sampleRate));
}

// Level that keeps white noise about as loud after the filter as before it.
float noiseMakeup(const FormantSet& set, const double sampleRate) noexcept
{
    double power = 0.0;
    for (const Formant& f : set)
        power += static_cast<double>(f.gain) * f.gain * (kPi * 0.5) * f.bw / (sampleRate * 0.5);
    return static_cast<float>(std::clamp(1.0 / std::sqrt(std::max(power, 1.0e-9)), 0.25, 16.0));
}

void computeCoefficients(Engine& e, const Parameters& p, const int channelIndex) noexcept
{
    const FormantSet set = formantsFor(p, e.x, e.y, e.shift, e.resonance, e.spread, channelIndex, e.sampleRate);
    Engine::Coefficients& c = e.coefficients[static_cast<std::size_t>(channelIndex)];
    for (std::size_t f = 0; f < kFormants; ++f) {
        const float g = std::tan(static_cast<float>(kPi) * set[f].freq / static_cast<float>(e.sampleRate));
        const float k = set[f].bw / set[f].freq;  // 1 / Q
        const float a1 = 1.0f / (1.0f + g * (g + k));
        c.a1[f] = a1;
        c.a2[f] = g * a1;
        c.a3[f] = g * g * a1;
        c.k[f] = k;
        c.gain[f] = set[f].gain;
    }
    c.makeup = noiseMakeup(set, e.sampleRate);
}

void snap(Engine& e, const Parameters& p) noexcept
{
    e.x = p.vowelX;
    e.y = p.vowelY;
    e.shift = p.shiftSt;
    e.resonance = p.resonance;
    e.spread = p.spread;
    e.mix = p.mix * 0.01f;
    e.output = dbToGain(p.outputDb);
    e.primed = true;
}

}  // namespace

bool ccChannelMatches(const unsigned char status, const float ccChannel) noexcept
{
    const int channel = static_cast<int>(std::lround(ccChannel));
    return channel >= 1 && channel <= 16 && (status & 0xF0u) == 0xB0u && (status & 0x0Fu) == static_cast<unsigned>(channel - 1);
}

void handleController(LiveControl& live, const int controller, const int value) noexcept
{
    if (controller < 1 || controller > 4)
        return;
    live.value[static_cast<std::size_t>(controller - 1)] = std::clamp(static_cast<float>(value) / 127.0f, 0.0f, 1.0f);
}

Parameters withLive(const Parameters& p, const LiveControl& live) noexcept
{
    Parameters out = p;
    if (live.value[0] >= 0.0f) out.vowelX = live.value[0] * 100.0f;
    if (live.value[1] >= 0.0f) out.vowelY = live.value[1] * 100.0f;
    if (live.value[2] >= 0.0f) out.shiftSt = -12.0f + live.value[2] * 24.0f;
    if (live.value[3] >= 0.0f) out.resonance = live.value[3] * 100.0f;
    return out;
}

void reset(Engine& engine) noexcept
{
    for (Channel& c : engine.channel)
        c = Channel {};
    engine.primed = false;
}

void activate(Engine& engine, const double sampleRate) noexcept
{
    engine.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset(engine);
}

void processBlock(Engine& e, const Parameters& raw, const float* inL, const float* inR, float* outL, float* outR,
                  const std::size_t frames) noexcept
{
    const Parameters p = clampParameters(raw);
    if (!e.primed)
        snap(e, p);

    const float fs = static_cast<float>(e.sampleRate);
    const float blockAlpha = 1.0f - std::exp(-static_cast<float>(kSubBlock) / (kSmoothSeconds * fs));
    const float sampleAlpha = 1.0f - std::exp(-1.0f / (kSmoothSeconds * fs));
    const float airAlpha = 1.0f - std::exp(-2.0f * static_cast<float>(kPi) * kAirCornerHz / fs);
    const float drive = dbToGain(p.driveDb);
    const bool driven = p.driveDb > 0.01f;
    const float driveTrim = 1.0f / std::sqrt(drive);
    const float air = p.tilt * 0.01f;
    const float mixTarget = p.mix * 0.01f;
    const float outTarget = dbToGain(p.outputDb);

    for (std::size_t base = 0; base < frames; base += kSubBlock) {
        const std::size_t count = std::min(kSubBlock, frames - base);
        e.x += blockAlpha * (p.vowelX - e.x);
        e.y += blockAlpha * (p.vowelY - e.y);
        e.shift += blockAlpha * (p.shiftSt - e.shift);
        e.resonance += blockAlpha * (p.resonance - e.resonance);
        e.spread += blockAlpha * (p.spread - e.spread);
        computeCoefficients(e, p, 0);
        computeCoefficients(e, p, 1);

        for (std::size_t n = 0; n < count; ++n) {
            const std::size_t i = base + n;
            const float dryIn[2] = {inL[i], inR != nullptr ? inR[i] : inL[i]};
            e.mix += sampleAlpha * (mixTarget - e.mix);
            e.output += sampleAlpha * (outTarget - e.output);
            float result[2];
            for (std::size_t s = 0; s < 2; ++s) {
                Channel& ch = e.channel[s];
                const Engine::Coefficients& c = e.coefficients[s];
                const float x = driven ? std::tanh(dryIn[s] * drive) * driveTrim : dryIn[s];
                float sum = 0.0f;
                for (std::size_t f = 0; f < kFormants; ++f) {
                    const float v3 = x - ch.ic2[f];
                    const float v1 = c.a1[f] * ch.ic1[f] + c.a2[f] * v3;
                    const float v2 = ch.ic2[f] + c.a2[f] * ch.ic1[f] + c.a3[f] * v3;
                    ch.ic1[f] = 2.0f * v1 - ch.ic1[f];
                    ch.ic2[f] = 2.0f * v2 - ch.ic2[f];
                    sum += c.gain[f] * c.k[f] * v1;  // k * v1 is a band-pass with unity peak
                }
                ch.airLow += airAlpha * (x - ch.airLow);
                const float wet = (sum * c.makeup + air * (x - ch.airLow)) * e.output;
                result[s] = dryIn[s] + e.mix * (wet - dryIn[s]);
            }
            outL[i] = result[0];
            if (outR != nullptr)
                outR[i] = result[1];
        }
    }

    // Idle filters decay into denormals; flush them once per block.
    for (Channel& ch : e.channel) {
        for (std::size_t f = 0; f < kFormants; ++f) {
            if (std::fabs(ch.ic1[f]) < 1.0e-20f) ch.ic1[f] = 0.0f;
            if (std::fabs(ch.ic2[f]) < 1.0e-20f) ch.ic2[f] = 0.0f;
        }
        if (std::fabs(ch.airLow) < 1.0e-20f) ch.airLow = 0.0f;
    }
}

float responseDb(const Parameters& raw, const double sampleRate, const float hz, const int channel) noexcept
{
    const Parameters p = clampParameters(raw);
    const FormantSet set = formantsFor(p, p.vowelX, p.vowelY, p.shiftSt, p.resonance, p.spread, channel, sampleRate);
    const float makeup = noiseMakeup(set, sampleRate);
    std::complex<double> total = 0.0;
    for (const Formant& f : set) {
        const std::complex<double> s(0.0, static_cast<double>(hz) / f.freq);
        const double k = static_cast<double>(f.bw) / f.freq;
        total += static_cast<double>(f.gain) * (k * s) / (s * s + k * s + 1.0);
    }
    const double magnitude = std::abs(total) * makeup * dbToGain(p.outputDb);
    return static_cast<float>(20.0 * std::log10(std::max(magnitude, 1.0e-6)));
}

}  // namespace downspout::mouth
