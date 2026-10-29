#include "heavyverb_core.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::heavyverb {

namespace {

constexpr int kSub = 32;                    // samples per control update; sub-blocks are aligned to the sample count, not the host block
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr float kFlush = 1.0e-20f;

// Line j is a wall pair's two directions; the ratios keep every line a different length.
constexpr std::array<float, kMaxLines> kRatio = {1.000f, 1.137f, 0.953f, 1.211f, 1.071f, 1.283f,
                                                 0.987f, 1.149f, 1.033f, 1.231f, 0.917f, 1.097f};
// Where each dimension's early reflections sit between the ears (-1 left .. +1 right).
constexpr std::array<float, kMaxDimensions> kPan = {-0.6f, 0.6f, -0.25f, 0.25f, -0.9f, 0.9f};
// Diffusion all-pass delays in milliseconds, left and right.
constexpr std::array<float, 4> kDiffuseMsL = {4.8f, 3.6f, 12.7f, 9.3f};
constexpr std::array<float, 4> kDiffuseMsR = {5.3f, 3.1f, 11.9f, 8.1f};

float hermite(const float* buf, const uint32_t mask, const uint32_t w, const float delay) noexcept
{
    const float pos = static_cast<float>(w) - delay;
    const float fl = std::floor(pos);
    const int i0 = static_cast<int>(fl);
    const float f = pos - fl;
    const float ym = buf[static_cast<uint32_t>(i0 - 1) & mask];
    const float y0 = buf[static_cast<uint32_t>(i0) & mask];
    const float y1 = buf[static_cast<uint32_t>(i0 + 1) & mask];
    const float y2 = buf[static_cast<uint32_t>(i0 + 2) & mask];
    const float c1 = 0.5f * (y1 - ym);
    const float c2 = ym - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym) + 1.5f * (y0 - y1);
    return ((c3 * f + c2) * f + c1) * f + y0;
}

uint32_t powerOfTwoAtLeast(const double n)
{
    uint32_t size = 64;
    while (static_cast<double>(size) < n) size <<= 1;
    return size;
}

struct Delay {
    std::vector<float> buf;
    uint32_t mask = 0, w = 0;

    void init(const double minSamples)
    {
        buf.assign(powerOfTwoAtLeast(minSamples), 0.0f);
        mask = static_cast<uint32_t>(buf.size()) - 1;
        w = 0;
    }
    void clear() noexcept { std::fill(buf.begin(), buf.end(), 0.0f); w = 0; }
    void push(const float x) noexcept { buf[w] = x; w = (w + 1) & mask; }
    [[nodiscard]] float read(const float d) const noexcept { return hermite(buf.data(), mask, w, d); }
};

struct Allpass {
    std::vector<float> buf;
    uint32_t mask = 0, w = 0, delay = 1;

    void init(const uint32_t samples)
    {
        delay = std::max<uint32_t>(1, samples);
        buf.assign(powerOfTwoAtLeast(delay + 2), 0.0f);
        mask = static_cast<uint32_t>(buf.size()) - 1;
        w = 0;
    }
    void clear() noexcept { std::fill(buf.begin(), buf.end(), 0.0f); w = 0; }
    float process(const float x, const float g) noexcept
    {
        const float z = buf[(w - delay) & mask];
        const float v = x + g * z;
        buf[w] = v;
        w = (w + 1) & mask;
        return -g * v + z;
    }
};

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void set(const bool highpass, double fc, const double sr) noexcept
    {
        fc = std::clamp(fc, 5.0, sr * 0.45);
        const double w0 = kTwoPi * fc / sr, cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * 0.70710678118654752);
        const double a0 = 1.0 + alpha;
        const double k = highpass ? 1.0 + cw : 1.0 - cw;
        b0 = k * 0.5 / a0;
        b1 = (highpass ? -k : k) / a0;
        b2 = b0;
        a1 = -2.0 * cw / a0;
        a2 = (1.0 - alpha) / a0;
    }
    double process(const double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void flush() noexcept
    {
        if (std::fabs(z1) < 1e-30) z1 = 0;
        if (std::fabs(z2) < 1e-30) z2 = 0;
    }
    void clear() noexcept { z1 = z2 = 0; }
};

// A value that is updated once per sub-block and ramped linearly across it.
struct Ramp {
    float from = 0, to = 0;
    void snap(const float v) noexcept { from = to = v; }
    void push(const float v) noexcept { from = to; to = v; }
    [[nodiscard]] float at(const float f) const noexcept { return from + (to - from) * f; }
};

struct Line {
    Delay delay;
    Ramp len, damp, tone, hpf, gain, act;
    float actSm = 0.0f;
    double lpDamp = 0, lpTone = 0, hpState = 0;
    double phase = 0;
};

struct Tap {
    Ramp delay, amp;
    float gL = 0.7f, gR = 0.7f, damp = 1.0f, lp = 0.0f;
};

float coefficient(const double sr, const double seconds, const int n = kSub) noexcept
{
    return static_cast<float>(1.0 - std::exp(-static_cast<double>(n) / (sr * seconds)));
}

float onePole(const double sr, const double hz) noexcept
{
    return static_cast<float>(1.0 - std::exp(-kTwoPi * std::min(hz, sr * 0.45) / sr));
}

}  // namespace

struct EngineState {
    float sr = 48000.0f;
    bool primed = false;
    uint32_t sub = 0;

    Delay preL, preR, early;
    std::array<std::array<Allpass, 4>, 2> diffuse;
    std::array<Line, kMaxLines> lines;
    std::array<Tap, kMaxLines> taps;
    std::array<Biquad, 2> highpass, lowpass;
    std::array<PitchShifter, 2> shifter;

    // Control values, smoothed at the sub-block rate.
    std::array<float, kMaxDimensions> distSm {}, dampSm {}, filtSm {};
    float decaySm = 2.5f, modSm = 0, lowCutSm = 60, highCutSm = 14000, mixSm = 0, widthSm = 1, outSm = 1;
    float earlySm = 0, diffSm = 0, ratioSm = 2, freezeSm = 0;
    float shimSm = 0, duckSm = 0, swellOn = 0, gateOn = 0, swellTime = 1, gateHold = 0.3f;
    Ramp pre, dry, wet, outGain, width, apGain, norm;

    // Per-sample effect state.
    float duckEnv = 0, gateEnv = 0, swellFast = 0, swellSlow = 0, swellGain = 1;
    bool swellFalling = false;
    uint32_t swellRefractory = 0, gateHoldLeft = 0;
    float gateGain = 1;
    float shimPrevL = 0, shimPrevR = 0;
};

void EngineDeleter::operator()(EngineState* s) const noexcept { delete s; }

void PitchShifter::prepare(const float sampleRate)
{
    windowSamples = 0.05f * sampleRate;
    buffer.assign(powerOfTwoAtLeast(0.12 * sampleRate), 0.0f);
    mask = static_cast<uint32_t>(buffer.size()) - 1;
    write = 0;
    phase = 0.0f;
}

void PitchShifter::reset() noexcept
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    write = 0;
    phase = 0.0f;
}

float PitchShifter::process(const float input, const float ratio) noexcept
{
    buffer[write] = input;
    write = (write + 1) & mask;
    phase += (1.0f - ratio) / windowSamples;
    phase -= std::floor(phase);
    float phase2 = phase + 0.5f;
    phase2 -= std::floor(phase2);
    const float w1 = 0.5f - 0.5f * std::cos(6.2831853f * phase);
    const float w2 = 0.5f - 0.5f * std::cos(6.2831853f * phase2);
    return w1 * hermite(buffer.data(), mask, write, phase * windowSamples + 3.0f) +
           w2 * hermite(buffer.data(), mask, write, phase2 * windowSamples + 3.0f);
}

Parameters clampParameters(const Parameters& p) noexcept
{
    Parameters q = p;
    q.dimensions = std::clamp(p.dimensions, 1.0f, static_cast<float>(kMaxDimensions));
    for (int i = 0; i < kMaxDimensions; ++i) {
        const size_t u = static_cast<size_t>(i);
        q.distance[u] = std::clamp(p.distance[u], kMinDistance, kMaxDistance);
        q.damping[u] = std::clamp(p.damping[u], 0.0f, 100.0f);
        q.filter[u] = std::clamp(p.filter[u], -100.0f, 100.0f);
    }
    q.decaySeconds = std::clamp(p.decaySeconds, 0.1f, 60.0f);
    q.preDelayMs = std::clamp(p.preDelayMs, 0.0f, 250.0f);
    q.mixPercent = std::clamp(p.mixPercent, 0.0f, 100.0f);
    q.widthPercent = std::clamp(p.widthPercent, 0.0f, 150.0f);
    q.modulation = std::clamp(p.modulation, 0.0f, 100.0f);
    q.lowCutHz = std::clamp(p.lowCutHz, 20.0f, 500.0f);
    q.highCutHz = std::clamp(p.highCutHz, 1000.0f, 20000.0f);
    q.diffusion = std::clamp(p.diffusion, 0.0f, 100.0f);
    q.earlyLevel = std::clamp(p.earlyLevel, 0.0f, 100.0f);
    q.outputDb = std::clamp(p.outputDb, -24.0f, 12.0f);
    q.effect = std::clamp(p.effect, 0.0f, static_cast<float>(kEffectCount - 1));
    q.effectAmount = std::clamp(p.effectAmount, 0.0f, 100.0f);
    q.shiftSemitones = std::clamp(p.shiftSemitones, -12.0f, 24.0f);
    q.freeze = std::clamp(p.freeze, 0.0f, 1.0f);
    q.preset = std::clamp(p.preset, 0.0f, 15.0f);
    return q;
}

int dimensionCount(const Parameters& p) noexcept
{
    return std::clamp(static_cast<int>(std::lround(p.dimensions)), 1, kMaxDimensions);
}

float lineLengthSeconds(const Parameters& p, const int line) noexcept
{
    const int j = std::clamp(line, 0, kMaxLines - 1);
    return std::clamp(p.distance[static_cast<size_t>(j / 2)], kMinDistance, kMaxDistance) / kSpeedOfSound * kRatio[static_cast<size_t>(j)];
}

EngineState* createEngine(const float sampleRate)
{
    auto* s = new EngineState();
    prepareEngine(*s, sampleRate);
    return s;
}

void destroyEngine(EngineState* state) noexcept { delete state; }

void prepareEngine(EngineState& s, const float sampleRate)
{
    s.sr = std::clamp(sampleRate, 8000.0f, 384000.0f);
    const double sr = s.sr;
    for (Line& line : s.lines) line.delay.init(0.45 * sr + 16.0);
    s.preL.init(0.26 * sr);
    s.preR.init(0.26 * sr);
    s.early.init(0.40 * sr);
    for (int c = 0; c < 2; ++c)
        for (size_t k = 0; k < 4; ++k)
            s.diffuse[static_cast<size_t>(c)][k].init(static_cast<uint32_t>(std::lround((c == 0 ? kDiffuseMsL[k] : kDiffuseMsR[k]) * 0.001 * sr)));
    for (PitchShifter& shifter : s.shifter) shifter.prepare(s.sr);
    resetEngine(s);
}

void resetEngine(EngineState& s) noexcept
{
    for (Line& line : s.lines) {
        line.delay.clear();
        line.lpDamp = line.lpTone = line.hpState = 0;
    }
    s.preL.clear();
    s.preR.clear();
    s.early.clear();
    for (auto& channel : s.diffuse)
        for (Allpass& a : channel) a.clear();
    for (Tap& t : s.taps) t.lp = 0.0f;
    for (Biquad& b : s.highpass) b.clear();
    for (Biquad& b : s.lowpass) b.clear();
    for (PitchShifter& shifter : s.shifter) shifter.reset();
    s.duckEnv = s.gateEnv = s.swellFast = s.swellSlow = 0.0f;
    s.swellGain = 1.0f;
    s.swellFalling = false;
    s.swellRefractory = s.gateHoldLeft = 0;
    s.gateGain = 1.0f;
    s.shimPrevL = s.shimPrevR = 0.0f;
    s.sub = 0;
    s.primed = false;   // the next block snaps its controls to the patch instead of gliding from old values
}

namespace {

// Once per sub-block: smooth the controls toward the patch and turn them into per-line coefficients.
void controlUpdate(EngineState& s, const Parameters& p) noexcept
{
    const double sr = s.sr;
    const bool snap = !s.primed;
    const float a30 = snap ? 1.0f : coefficient(sr, 0.03), a50 = snap ? 1.0f : coefficient(sr, 0.05), a80 = snap ? 1.0f : coefficient(sr, 0.08);
    const auto track = [](float& v, const float target, const float a) { v += (target - v) * a; };
    const auto push = [snap](Ramp& r, const float v) { if (snap) r.snap(v); else r.push(v); };

    const int dims = dimensionCount(p);
    const int effect = static_cast<int>(std::lround(p.effect));
    const float amount = p.effectAmount * 0.01f;

    track(s.decaySm, p.decaySeconds, a50);
    track(s.modSm, p.modulation, a50);
    track(s.lowCutSm, p.lowCutHz, a50);
    track(s.highCutSm, p.highCutHz, a50);
    track(s.mixSm, p.mixPercent, a30);
    track(s.widthSm, p.widthPercent, a30);
    track(s.outSm, p.outputDb, a30);
    track(s.earlySm, p.earlyLevel, a30);
    track(s.diffSm, p.diffusion, a50);
    track(s.freezeSm, p.freeze, a50);
    track(s.ratioSm, std::pow(2.0f, p.shiftSemitones / 12.0f), a80);
    track(s.shimSm, effect == kEffectShimmer ? amount : 0.0f, a50);
    track(s.swellOn, effect == kEffectSwell ? 1.0f : 0.0f, a50);
    track(s.duckSm, effect == kEffectDuck ? amount : 0.0f, a50);
    track(s.gateOn, effect == kEffectGate ? 1.0f : 0.0f, a50);
    s.swellTime = 0.1f + 2.4f * amount;
    s.gateHold = 0.05f + 0.95f * amount;

    float activeSum = 0.0f;
    for (int j = 0; j < kMaxLines; ++j) {
        Line& line = s.lines[static_cast<size_t>(j)];
        track(line.actSm, (j / 2) < dims ? 1.0f : 0.0f, a50);
        activeSum += line.actSm;
    }
    const float nEff = std::max(activeSum, 1.0f);

    for (int i = 0; i < kMaxDimensions; ++i) {
        const size_t u = static_cast<size_t>(i);
        track(s.distSm[u], p.distance[u], a80);
        track(s.dampSm[u], p.damping[u], a50);
        track(s.filtSm[u], p.filter[u], a50);
    }

    const float fz = s.freezeSm;
    const float maxLen = static_cast<float>(s.lines[0].delay.buf.size()) - 8.0f;
    for (int j = 0; j < kMaxLines; ++j) {
        const size_t u = static_cast<size_t>(j), i = u / 2;
        Line& line = s.lines[u];
        const float samples = std::clamp(s.distSm[i] / kSpeedOfSound * kRatio[u] * s.sr, 6.0f, maxLen);
        line.phase += kTwoPi * (0.061 + 0.0173 * j) * kSub / sr;
        const float depth = s.modSm * 0.01f * std::min(0.0025f * s.sr, 0.04f * samples);
        push(line.len, std::clamp(samples + depth * static_cast<float>(std::sin(line.phase + 0.7 * j)), 4.0f, maxLen));

        const float g = std::pow(10.0f, -3.0f * samples / (s.sr * s.decaySm));
        push(line.gain, g + (1.0f - g) * fz);

        const float a = onePole(sr, 20000.0 * std::pow(10.0, -1.65 * s.dampSm[i] * 0.01));
        push(line.damp, a + (1.0f - a) * fz);

        const float f = s.filtSm[i] * 0.01f;
        const float lowpass = f < 0.0f ? onePole(sr, 20000.0 * std::pow(10.0, -1.4 * -f)) : 1.0f;
        const float highpass = f > 0.0f ? onePole(sr, 20.0 * std::pow(10.0, 1.9 * f)) : 0.0f;
        push(line.tone, lowpass + (1.0f - lowpass) * fz);
        push(line.hpf, highpass * (1.0f - fz));
        push(line.act, line.actSm);
    }

    // Early reflections: each dimension reflects the source back from its two walls (the source
    // sits at 0.4 of the way across), so the taps land at 0.8 and 1.2 wall spacings of travel.
    const float earlyGain = s.earlySm * 0.01f * 0.7f / std::sqrt(std::max(1.0f, nEff * 0.5f));
    for (int t = 0; t < kMaxLines; ++t) {
        const size_t u = static_cast<size_t>(t), i = u / 2;
        const bool second = (t & 1) != 0;
        Tap& tap = s.taps[u];
        const float path = (second ? 1.2f : 0.8f) * s.distSm[i];
        push(tap.delay, std::clamp(path / kSpeedOfSound * s.sr, 4.0f, static_cast<float>(s.early.buf.size()) - 8.0f));
        push(tap.amp, (second ? -1.0f : 1.0f) * earlyGain * s.lines[2 * i].actSm / (1.0f + path * 0.125f));
        const float pan = kPan[i] * (second ? -1.0f : 1.0f);
        const float angle = (pan + 1.0f) * 0.78539816f;
        tap.gL = std::cos(angle);
        tap.gR = std::sin(angle);
        const float a = onePole(sr, 20000.0 * std::pow(10.0, -1.65 * s.dampSm[i] * 0.01));
        tap.damp = a + (1.0f - a) * fz;
    }

    push(s.pre, std::max(3.0f, p.preDelayMs * 0.001f * s.sr));
    const float mix = s.mixSm * 0.01f * 1.57079633f;
    push(s.dry, std::cos(mix));
    push(s.wet, std::sin(mix));
    push(s.outGain, std::pow(10.0f, s.outSm * 0.05f));
    push(s.width, s.widthSm * 0.01f);
    push(s.apGain, 0.7f * s.diffSm * 0.01f);
    push(s.norm, std::pow(2.0f / nEff, 0.25f));

    for (Biquad& b : s.highpass) b.set(true, s.lowCutSm, sr);
    for (Biquad& b : s.lowpass) b.set(false, s.highCutSm, sr);

    s.primed = true;
}

}  // namespace

void processBlock(EngineState&      s,
                  const Parameters& params,
                  const uint32_t    frames,
                  const float* const* inputs,
                  float* const*       outputs) noexcept
{
    const Parameters p = clampParameters(params);
    const float* const inL = inputs[0];
    const float* const inR = inputs[1] != nullptr ? inputs[1] : inputs[0];
    const float sr = s.sr;

    // Detector time constants.
    const float duckAttack = coefficient(sr, 0.005, 1), duckRelease = coefficient(sr, 0.25, 1);
    const float gateFollow = coefficient(sr, 0.003, 1), gateOpen = coefficient(sr, 0.002, 1), gateClose = coefficient(sr, 0.02, 1);
    const float swellFastCoef = coefficient(sr, 0.003, 1), swellSlowCoef = coefficient(sr, 0.1, 1);
    const uint32_t swellRefractory = static_cast<uint32_t>(0.12f * sr);
    const float swellFall = 1.0f / (0.003f * sr);

    for (uint32_t n = 0; n < frames; ++n) {
        if (s.sub == 0) controlUpdate(s, p);
        const float f = static_cast<float>(s.sub + 1) * (1.0f / kSub);
        const float dryL = inL[n], dryR = inR[n];
        const float level = std::max(std::fabs(dryL), std::fabs(dryR));

        // ---- Effect detectors on the dry input ------------------------------------------------
        s.duckEnv += (level - s.duckEnv) * (level > s.duckEnv ? duckAttack : duckRelease);
        s.gateEnv += (level - s.gateEnv) * gateFollow;
        s.swellFast += (level - s.swellFast) * swellFastCoef;
        s.swellSlow += (level - s.swellSlow) * swellSlowCoef;

        if (s.swellRefractory > 0) --s.swellRefractory;
        if (s.swellRefractory == 0 && s.swellFast > 2.2f * s.swellSlow + 0.004f) {
            s.swellFalling = true;
            s.swellRefractory = swellRefractory;
        }
        if (s.swellFalling) {
            s.swellGain -= swellFall;
            if (s.swellGain <= 0.0f) { s.swellGain = 0.0f; s.swellFalling = false; }
        } else {
            s.swellGain = std::min(1.0f, s.swellGain + 1.0f / (s.swellTime * sr));
        }
        const float swell = 1.0f - s.swellOn + s.swellOn * s.swellGain;

        // ---- Pre-delay, then the reverb input -------------------------------------------------
        s.preL.push(dryL);
        s.preR.push(dryR);
        const float pd = s.pre.at(f);
        const float inGain = (1.0f - s.freezeSm) * swell;
        const float xL = s.preL.read(pd) * inGain, xR = s.preR.read(pd) * inGain;

        // ---- Early reflections ----------------------------------------------------------------
        s.early.push(0.5f * (xL + xR));
        float erL = 0.0f, erR = 0.0f;
        for (int t = 0; t < kMaxLines; ++t) {
            Tap& tap = s.taps[static_cast<size_t>(t)];
            const float amp = tap.amp.at(f);
            if (amp == 0.0f && tap.amp.from == 0.0f) continue;
            tap.lp += tap.damp * (s.early.read(tap.delay.at(f)) * amp - tap.lp);
            erL += tap.gL * tap.lp;
            erR += tap.gR * tap.lp;
        }

        // ---- Input diffusion, and the shimmer feedback ----------------------------------------
        const float gAp = s.apGain.at(f);
        float dL = xL, dR = xR;
        for (size_t k = 0; k < 4; ++k) {
            dL = s.diffuse[0][k].process(dL, gAp);
            dR = s.diffuse[1][k].process(dR, gAp);
        }
        const float feedback = 0.4f * s.shimSm * (1.0f - s.freezeSm);
        dL += feedback * s.shimPrevL;
        dR += feedback * s.shimPrevR;

        // ---- The feedback delay network -------------------------------------------------------
        std::array<float, kMaxLines> out {}, x {};
        std::array<float, kMaxLines> act {};
        float sum = 0.0f, count = 0.0f;
        for (int j = 0; j < kMaxLines; ++j) {
            const size_t u = static_cast<size_t>(j);
            Line& line = s.lines[u];
            const float a = line.act.at(f);
            act[u] = a;
            if (a <= 0.0f && line.act.from <= 0.0f) continue;
            const float y = line.delay.read(line.len.at(f));
            line.lpDamp += line.damp.at(f) * (y - line.lpDamp);
            line.lpTone += line.tone.at(f) * (line.lpDamp - line.lpTone);
            line.hpState += line.hpf.at(f) * (line.lpTone - line.hpState);
            out[u] = y * a;
            x[u] = static_cast<float>(line.lpTone - line.hpState) * line.gain.at(f) * a;
            sum += x[u];
            count += a;
        }
        const float reflect = 2.0f * sum / std::max(count, 1.0f);
        const float norm = s.norm.at(f);
        float lateL = 0.0f, lateR = 0.0f;
        for (int j = 0; j < kMaxLines; ++j) {
            const size_t u = static_cast<size_t>(j);
            Line& line = s.lines[u];
            if (act[u] <= 0.0f && line.act.from <= 0.0f) { line.delay.push(0.0f); continue; }
            const float injected = norm * ((j & 1) ? dR : dL) * (((j >> 1) & 1) ? -1.0f : 1.0f);
            float v = injected * act[u] + x[u] - reflect * act[u];
            if (std::fabs(v) < kFlush) v = 0.0f;
            line.delay.push(v);
            const float tapped = out[u] * norm * (((j >> 1) % 2) ? -1.0f : 1.0f);
            if (j & 1) lateR += tapped; else lateL += tapped;
        }

        // ---- Shimmer: the late tail, shifted, back into the input and a little straight out ----
        float shimL = 0.0f, shimR = 0.0f;
        if (s.shimSm > 1e-4f) {
            shimL = std::tanh(s.shifter[0].process(lateL, s.ratioSm));
            shimR = std::tanh(s.shifter[1].process(lateR, s.ratioSm));
        }
        s.shimPrevL = shimL;
        s.shimPrevR = shimR;

        double wetL = static_cast<double>(lateL) + erL + 0.5f * s.shimSm * shimL;
        double wetR = static_cast<double>(lateR) + erR + 0.5f * s.shimSm * shimR;

        // ---- Wet filtering and width ----------------------------------------------------------
        wetL = s.lowpass[0].process(s.highpass[0].process(wetL));
        wetR = s.lowpass[1].process(s.highpass[1].process(wetR));
        const double mid = 0.5 * (wetL + wetR), side = 0.5 * (wetL - wetR) * s.width.at(f);
        wetL = mid + side;
        wetR = mid - side;

        // ---- Duck and gate act on the wet signal only ------------------------------------------
        float wetGain = s.wet.at(f) * s.outGain.at(f);
        if (s.duckSm > 1e-4f) {
            const float db = 20.0f * std::log10(s.duckEnv + 1e-9f);
            const float reduction = s.duckSm * 24.0f * std::clamp((db + 40.0f) * (1.0f / 30.0f), 0.0f, 1.0f);
            wetGain *= std::exp(-reduction * 0.11512925f);
        }
        if (s.gateOn > 1e-4f) {
            if (s.gateEnv > 0.01f) s.gateHoldLeft = static_cast<uint32_t>(s.gateHold * sr);
            const bool open = s.gateEnv > 0.01f || s.gateHoldLeft > 0;
            if (s.gateHoldLeft > 0 && s.gateEnv <= 0.01f) --s.gateHoldLeft;
            s.gateGain += ((open ? 1.0f : 0.0f) - s.gateGain) * (open ? gateOpen : gateClose);
            wetGain *= 1.0f - s.gateOn + s.gateOn * s.gateGain;
        }

        const float dryGain = s.dry.at(f);
        const float outL = dryL * dryGain + static_cast<float>(wetL) * wetGain;
        const float outR = dryR * dryGain + static_cast<float>(wetR) * wetGain;
        outputs[0][n] = outL;
        outputs[1][n] = outR;
        s.sub = (s.sub + 1) & (kSub - 1);
    }

    // Idle recursive filters decay into denormals, which are slow; snap them to zero.
    for (Line& line : s.lines) {
        if (std::fabs(line.lpDamp) < 1e-30) line.lpDamp = 0;
        if (std::fabs(line.lpTone) < 1e-30) line.lpTone = 0;
        if (std::fabs(line.hpState) < 1e-30) line.hpState = 0;
    }
    for (Tap& t : s.taps) if (std::fabs(t.lp) < kFlush) t.lp = 0.0f;
    for (Biquad& b : s.highpass) b.flush();
    for (Biquad& b : s.lowpass) b.flush();
    if (std::fabs(s.shimPrevL) < kFlush) s.shimPrevL = 0.0f;
    if (std::fabs(s.shimPrevR) < kFlush) s.shimPrevR = 0.0f;
}

}  // namespace downspout::heavyverb
