#include "chorale_core.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::chorale {

namespace {

constexpr double kPi = 3.14159265358979323846;

float clampf(const float v, const float lo, const float hi, const float fallback) noexcept
{
    if (!std::isfinite(v)) return fallback;
    return std::min(hi, std::max(lo, v));
}

// The source's soft limiter: x * (27 + x^2) / (27 + 9 x^2).
double softLimit(const double x) noexcept { return x * (27.0 + x * x) / (27.0 + 9.0 * x * x); }

// First-order low-pass, bilinear, in the source's transposed form.
void setOnePole(OnePole& f, const double hz, const double sampleRate) noexcept
{
    const double k = std::tan(kPi * std::min(hz / sampleRate, 0.49));
    f.ff0 = k / (k + 1.0);
    f.fb1 = (k - 1.0) / (k + 1.0);
}

double runOnePole(OnePole& f, const double x) noexcept
{
    const double acc = f.ff0 * x + f.z;
    f.z = f.ff0 * x - f.fb1 * acc;
    if (std::abs(f.z) < 1e-20) f.z = 0.0;   // idle filters must not decay into denormals
    return acc;
}

void setTone(EngineState& s, const double fr, const double sampleRate) noexcept
{
    const double f2 = fr * fr;
    setOnePole(s.pre, kPreMinHz + f2 * (kPreMaxHz - kPreMinHz), sampleRate);
    setOnePole(s.postL, kPostMinHz + f2 * (kPostMaxHz - kPostMinHz), sampleRate);
    s.postR.ff0 = s.postL.ff0;
    s.postR.fb1 = s.postL.fb1;
}

// The delay line is read as the source reads it: position 1 is the sample just written (no delay), so a delay of d samples is position d + 1.
double readLine(const EngineState& s, const double pos) noexcept
{
    const int base = static_cast<int>(std::floor(pos));
    const double frac = pos - base;
    const double a = s.ring[static_cast<std::size_t>((s.widx + base) & kRingMask)];
    const double b = s.ring[static_cast<std::size_t>((s.widx + base + 1) & kRingMask)];
    return a + frac * (b - a);
}

double clampPos(const double pos) noexcept { return std::min(std::max(pos, 1.0), static_cast<double>(kRing - 3)); }

}  // namespace

Parameters clampParameters(const Parameters& p) noexcept
{
    Parameters c = p;
    c.mode = std::round(clampf(p.mode, 0.0f, kModeCount - 1.0f, 1.0f));
    c.tone = clampf(p.tone, 0.0f, 1.0f, 0.5f);
    c.mix = clampf(p.mix, 0.0f, 1.0f, 0.5f);
    c.rate = clampf(p.rate, kRateMin, kRateMax, 0.8f);
    c.depth = clampf(p.depth, kDepthMin, kDepthMax, 3.0f);
    c.delay = clampf(p.delay, kDelayMin, kDelayMax, 8.0f);
    c.voices = std::round(clampf(p.voices, 1.0f, static_cast<float>(kMaxVoices), 2.0f));
    c.width = clampf(p.width, 0.0f, 1.0f, 1.0f);
    c.ccChannel = std::round(clampf(p.ccChannel, 0.0f, 16.0f, 1.0f));
    return c;
}

float ccToValue(const CcTarget target, const float normalised) noexcept
{
    const float v = clampf(normalised, 0.0f, 1.0f, 0.0f);
    switch (target) {
    case kCcRate:  return kRateMin * std::pow(kRateMax / kRateMin, v);   // logarithmic
    case kCcDepth: return kDepthMin + v * (kDepthMax - kDepthMin);
    case kCcWidth: return v;
    case kCcMix:   return v;
    default:       return 0.0f;
    }
}

void releaseCc(EngineState& state, const CcTarget target) noexcept
{
    if (target >= 0 && target < kCcTargetCount) state.ccActive[static_cast<std::size_t>(target)] = false;
}

void resetState(EngineState& state) noexcept
{
    state = EngineState {};
}

void processBlock(EngineState& s, const Parameters& params, const float sampleRate, const std::uint32_t frames, const MidiEvent* events,
                  const std::uint32_t eventCount, const float* inLeft, const float* inRight, float* outLeft, float* outRight) noexcept
{
    const Parameters p = clampParameters(params);
    const double sr = std::max(8000.0, static_cast<double>(sampleRate));
    const double smooth = 1.0 - std::exp(-1.0 / (kSmoothSeconds * sr));
    const int mode = static_cast<int>(p.mode);
    const int voices = static_cast<int>(p.voices);
    const int ccChannel = static_cast<int>(p.ccChannel);

    double tga = 0.0, tgb = 0.0, tgc = 0.0;
    switch (mode) {
    case 0: tga = 1.0; break;
    case 1: tga = tgb = std::sqrt(0.5); break;
    case 2: tgb = 1.0; break;
    default: tgc = 1.0; break;
    }
    const double tfr = p.tone;

    // Targets that a controller can take over.
    double tRate = 0, tDepth = 0, tWidth = 0, tMix = 0, tDry = 0, tWet = 0;
    const auto refresh = [&]() {
        const auto value = [&](const CcTarget t, const float param) {
            const auto i = static_cast<std::size_t>(t);
            return s.ccActive[i] ? ccToValue(t, s.ccValue[i]) : param;
        };
        tRate = value(kCcRate, p.rate);
        tDepth = value(kCcDepth, p.depth);
        tWidth = value(kCcWidth, p.width);
        tMix = value(kCcMix, p.mix);
        tDry = std::sqrt(1.0 - tMix);
        tWet = std::sqrt(tMix);
    };
    refresh();

    if (!s.started) {
        s.started = true;
        s.ga = tga; s.gb = tgb; s.gc = tgc;
        s.dry = tDry; s.wet = tWet;
        s.fr = tfr;
        s.delayMs = p.delay; s.depthMs = tDepth; s.widthNow = tWidth; s.rateNow = tRate;
        setTone(s, s.fr, sr);
    }
    // Smoothed values settle exactly on their target (per sample, so the output does not depend on the block size).
    const auto glide = [smooth](double& v, const double target) {
        v += (target - v) * smooth;
        if (std::abs(target - v) < 0.00001) v = target;
    };

    std::uint32_t next = 0;
    for (std::uint32_t n = 0; n < frames; ++n) {
        bool changed = false;
        while (next < eventCount && events[next].frame <= n) {
            const MidiEvent& e = events[next++];
            if (ccChannel == 0 || (e.status & 0xF0) != 0xB0 || (e.status & 0x0F) != ccChannel - 1) continue;
            for (int t = 0; t < kCcTargetCount; ++t) {
                if (e.data1 != kCcNumber[t]) continue;
                s.ccActive[static_cast<std::size_t>(t)] = true;
                s.ccValue[static_cast<std::size_t>(t)] = static_cast<float>(e.data2) / 127.0f;
                changed = true;
            }
        }
        if (changed) refresh();

        if (s.fr != tfr) {
            s.fr += (tfr - s.fr) * smooth;
            if (std::abs(tfr - s.fr) < 0.0001) s.fr = tfr;
            ++s.coefCounter;
            if (s.coefCounter >= 16 || s.fr == tfr) {
                s.coefCounter = 0;
                setTone(s, s.fr, sr);
            }
        }
        glide(s.ga, tga); glide(s.gb, tgb); glide(s.gc, tgc); glide(s.dry, tDry); glide(s.wet, tWet);
        s.delayMs += (p.delay - s.delayMs) * smooth;
        s.depthMs += (tDepth - s.depthMs) * smooth;
        s.widthNow += (tWidth - s.widthNow) * smooth;
        s.rateNow += (tRate - s.rateNow) * smooth;

        const double inL = inLeft[n], inR = inRight[n];
        double w = runOnePole(s.pre, softLimit(inL + inR));
        if (std::abs(w) < 1e-20) w = 0.0;
        s.ring[static_cast<std::size_t>(s.widx)] = static_cast<float>(w);
        s.widx = (s.widx - 1) & kRingMask;

        s.lfo1 += 2.0 * kRate1 / sr;
        if (s.lfo1 >= 1.0) s.lfo1 -= 2.0;
        s.lfo2 += 2.0 * kRate2 / sr;
        if (s.lfo2 >= 1.0) s.lfo2 -= 2.0;
        s.phase += s.rateNow / sr;
        if (s.phase >= 1.0) s.phase -= 1.0;

        double wl = 0.0, wr = 0.0;
        if (s.ga > 0.0 || s.gb > 0.0) {
            const double p1 = std::abs(s.lfo1), p2 = std::abs(s.lfo2);
            wl += s.ga * readLine(s, clampPos((kT0Min + (kT0Max - kT0Min) * p1) * sr))
                + s.gb * readLine(s, clampPos((kT0Min + (kT0Max - kT0Min) * p2) * sr));
            wr += s.ga * readLine(s, clampPos((kT1Min + (kT1Max - kT1Min) * (1.0 - p1)) * sr))
                + s.gb * readLine(s, clampPos((kT1Min + (kT1Max - kT1Min) * (1.0 - p2)) * sr));
        }
        if (s.gc > 0.0) {
            const double depth = std::min(s.depthMs, std::max(0.0, s.delayMs - 0.05));   // the sweep never reaches negative delay
            double cl = 0.0, cr = 0.0;
            for (int v = 0; v < voices; ++v) {
                const double sweep = std::sin(2.0 * kPi * (s.phase + static_cast<double>(v) / (2.0 * voices)));
                cl += readLine(s, clampPos((s.delayMs + depth * sweep) * 0.001 * sr + 1.0));
                cr += readLine(s, clampPos((s.delayMs - depth * sweep) * 0.001 * sr + 1.0));
            }
            wl += s.gc * cl / voices;
            wr += s.gc * cr / voices;
        }

        const double mid = 0.5 * (wl + wr);
        wl = mid + s.widthNow * (wl - mid);
        wr = mid + s.widthNow * (wr - mid);
        outLeft[n] = static_cast<float>(inL * s.dry + runOnePole(s.postL, wl) * s.wet);
        outRight[n] = static_cast<float>(inR * s.dry + runOnePole(s.postR, wr) * s.wet);
    }

    for (auto& t : s.meters.tap) t = 0.0f;
    if (mode < 3) {
        s.meters.tap[0] = static_cast<float>(std::abs(s.lfo1));
        s.meters.tap[1] = static_cast<float>(std::abs(s.lfo2));
    } else {
        for (int v = 0; v < voices; ++v)
            s.meters.tap[static_cast<std::size_t>(v)] =
                static_cast<float>(0.5 + 0.5 * std::sin(2.0 * kPi * (s.phase + static_cast<double>(v) / (2.0 * voices))));
    }
    s.meters.rate = static_cast<float>(tRate);
    s.meters.depth = static_cast<float>(tDepth);
    s.meters.width = static_cast<float>(tWidth);
    s.meters.mix = static_cast<float>(tMix);
}

}  // namespace downspout::chorale
