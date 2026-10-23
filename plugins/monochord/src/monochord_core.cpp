#include "monochord_core.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace downspout::monochord {

namespace {

constexpr double kPi = 3.14159265358979323846;

float clampf(float v, float lo, float hi) noexcept
{
    if (!std::isfinite(v)) return lo;
    return std::min(std::max(v, lo), hi);
}

using Cx = std::complex<double>;

// Phase delay in samples of a first-order all-pass (c + z^-1) / (1 + c z^-1) at angular frequency w.
double allpassDelay(double c, double w) noexcept
{
    const Cx z1 = std::polar(1.0, -w);
    const Cx h = (c + z1) / (1.0 + c * z1);
    double phase = std::arg(h);
    if (phase > 0.0) phase -= 2.0 * kPi;
    return -phase / w;
}

// One-pole low-pass (1 - a) / (1 - a z^-1).
Cx lowpassResponse(double a, double w) noexcept
{
    return (1.0 - a) / (1.0 - a * std::polar(1.0, -w));
}

double lowpassDelay(double a, double w) noexcept
{
    if (a == 0.0) return 0.0;
    double phase = std::arg(lowpassResponse(a, w));
    if (phase > 0.0) phase -= 2.0 * kPi;
    return -phase / w;
}

inline std::uint32_t nextRandom(std::uint32_t& s) noexcept
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

// RBJ band-pass (constant 0 dB peak gain).
Biquad bandPass(double f0, double q, double sr) noexcept
{
    const double w0 = 2.0 * kPi * std::min(f0, sr * 0.45) / sr;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad b;
    b.b0 = alpha / a0;
    b.b1 = 0.0;
    b.b2 = -alpha / a0;
    b.a1 = -2.0 * std::cos(w0) / a0;
    b.a2 = (1.0 - alpha) / a0;
    return b;
}

inline double runBiquad(Biquad& b, double x) noexcept
{
    const double y = b.b0 * x + b.z1;
    b.z1 = b.b1 * x - b.a1 * y + b.z2;
    b.z2 = b.b2 * x - b.a2 * y;
    return y;
}

void startVoice(EngineState& st, const Parameters& p, double sr, int note, int velocity, int channel) noexcept
{
    int idx = -1;
    for (int i = 0; i < kVoices; ++i)
        if (!st.voices[static_cast<size_t>(i)].active) { idx = i; break; }
    if (idx < 0) {
        double best = 1e9;
        for (int i = 0; i < kVoices; ++i) {
            const Voice& v = st.voices[static_cast<size_t>(i)];
            const double score = v.envelope * (0.05 + v.peak) + (v.held || v.sustained ? 10.0 : 0.0);
            if (score < best) { best = score; idx = i; }
        }
    }
    Voice& v = st.voices[static_cast<size_t>(idx)];
    v = Voice {};
    v.active = true;
    v.held = true;
    v.channel = channel & 15;
    v.note = note;
    // The Velocity control mixes between every note at full level (0) and the key velocity itself (1); this one value
    // scales the excitation, and nothing scales the output again.
    v.velocity = 1.0f - p.velocity + p.velocity * static_cast<float>(velocity) / 127.0f;
    v.age = ++st.counter;
    v.peak = 1.0;
    const double f0 = std::max(440.0 * std::exp2((note - 69 + st.bend[static_cast<size_t>(channel & 15)]) / 12.0), 20.0);
    v.tuning = tuneLoop(f0, p, sr);
    const Exciter ex = static_cast<Exciter>(static_cast<int>(p.exciter));
    if (ex == Exciter::Bow) {
        v.bowing = true;
        // A slow bow: at higher speeds the string settles to a constant offset (which the DC blocker removes) instead of
        // sticking and slipping. The oscillation's size is set by the friction curve, so loudness comes from the output level.
        v.bowSpeed = 0.03 + 0.12 * static_cast<double>(v.velocity);
        v.bowed = true;
    } else {
        std::vector<float> shape(static_cast<size_t>(v.tuning.delay));
        excitation(ex, v.tuning.delay, p, shape.data());
        for (int i = 0; i < v.tuning.delay; ++i) v.line[static_cast<size_t>(i)] = shape[static_cast<size_t>(i)] * v.velocity * 0.8f;
        v.writePos = v.tuning.delay;
    }
}

void noteOff(EngineState& st, int note, int channel) noexcept
{
    for (Voice& v : st.voices)
        if (v.active && v.held && v.note == note && v.channel == (channel & 15)) {
            v.held = false;
            if (st.sustainPedal) v.sustained = true;
        }
}

void handleEvent(EngineState& st, const Parameters& p, double sr, const MidiEvent& e) noexcept
{
    switch (e.type) {
    case MidiEvent::Type::NoteOn:
        if (e.data2 == 0) noteOff(st, e.data1, e.channel); else startVoice(st, p, sr, e.data1, e.data2, e.channel);
        break;
    case MidiEvent::Type::NoteOff:
        noteOff(st, e.data1, e.channel);
        break;
    case MidiEvent::Type::Controller:
        if (e.data1 == 64) {
            st.sustainPedal = e.data2 >= 64;
            if (!st.sustainPedal)
                for (Voice& v : st.voices) v.sustained = false;
        } else if (e.data1 == 120 || e.data1 == 123) {
            for (Voice& v : st.voices) {
                v.held = false;
                v.sustained = false;
                if (e.data1 == 120) v = Voice {};
            }
            if (e.data1 == 120) st.dcX = st.dcY = 0.0;
        }
        break;
    case MidiEvent::Type::PitchBend:
        st.bendTarget[e.channel & 15] = static_cast<float>(e.bend) / 8192.0f * p.bendRange;
        break;
    }
}

}  // namespace

Parameters clampParameters(const Parameters& in) noexcept
{
    Parameters p = in;
    p.exciter     = std::round(clampf(in.exciter, 0.0f, 2.0f));
    p.position    = clampf(in.position, 0.05f, 0.5f);
    p.hardness    = clampf(in.hardness, 0.0f, 1.0f);
    p.bowPressure = clampf(in.bowPressure, 0.0f, 1.0f);
    p.decaySec    = clampf(in.decaySec, 0.1f, 20.0f);
    p.damping     = clampf(in.damping, 0.0f, 1.0f);
    p.stiffness   = clampf(in.stiffness, 0.0f, 1.0f);
    p.bodySize    = clampf(in.bodySize, 0.5f, 2.0f);
    p.bodyLevel   = clampf(in.bodyLevel, 0.0f, 1.0f);
    p.pickup      = clampf(in.pickup, 0.0f, 0.5f);
    p.releaseMs   = clampf(in.releaseMs, 5.0f, 2000.0f);
    p.master      = clampf(in.master, 0.0f, 1.0f);
    p.velocity    = clampf(in.velocity, 0.0f, 1.0f);
    p.bendRange   = clampf(in.bendRange, 0.0f, 24.0f);
    return p;
}

double loopPhaseDelay(const LoopTuning& t, double hz, double sampleRate) noexcept
{
    const double w = 2.0 * kPi * hz / sampleRate;
    return static_cast<double>(t.delay) + allpassDelay(t.fractionCoeff, w) + lowpassDelay(t.lowpass, w) +
           static_cast<double>(t.stiffStages) * allpassDelay(t.stiffCoeff, w);
}

LoopTuning tuneLoop(double frequencyHz, const Parameters& raw, double sampleRate) noexcept
{
    const Parameters p = clampParameters(raw);
    const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    const double f0 = std::min(std::max(frequencyHz, 20.0), sr * 0.4);
    const double w = 2.0 * kPi * f0 / sr;
    LoopTuning t;
    t.lowpass = static_cast<double>(p.damping) * 0.7;
    // Stiffness is a cascade of four identical all-passes whose low-frequency delay together is stiffness^2 x a quarter of the
    // period. A single section changes its delay far too little between the fundamental and the partials to be audible; a
    // cascade whose delay scales with the period gives an inharmonicity that is the same fraction of the cycle at any pitch.
    if (p.stiffness > 0.001f) {
        const double period = sr / f0;
        const double perStage = std::max(1.0, static_cast<double>(p.stiffness) * p.stiffness * 0.25 * period / 4.0);
        t.stiffStages = 4;
        t.stiffCoeff = (1.0 - perStage) / (1.0 + perStage);
    }
    const double fixedDelay = lowpassDelay(t.lowpass, w) + static_cast<double>(t.stiffStages) * allpassDelay(t.stiffCoeff, w);
    // The rest of one period goes to a whole delay and a fraction, which the all-pass supplies: pick the all-pass
    // coefficient whose phase delay AT f0 equals the fraction (the delay at DC would only be right at low pitch).
    const double remaining = sr / f0 - fixedDelay;
    t.delay = std::max(2, static_cast<int>(std::floor(remaining - 0.5)));
    const double target = std::min(std::max(remaining - t.delay, 0.5), 1.5);
    double lo = -0.5, hi = 0.5;   // the all-pass delay falls as the coefficient rises
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (allpassDelay(mid, w) > target) lo = mid; else hi = mid;
    }
    t.fractionCoeff = 0.5 * (lo + hi);
    t.delay = std::min(t.delay, kDelaySize - 8);
    // Loop gain so the fundamental falls 60 dB in decaySec: the low-pass already takes some of it at f0.
    const double perPass = std::pow(10.0, -3.0 / (static_cast<double>(p.decaySec) * f0));
    t.gain = std::min(perPass / std::abs(lowpassResponse(t.lowpass, w)), 0.99999);
    return t;
}

void excitation(Exciter exciter, int n, const Parameters& raw, float* out) noexcept
{
    const Parameters p = clampParameters(raw);
    n = std::max(n, 2);
    for (int i = 0; i < n; ++i) out[i] = 0.0f;
    if (exciter == Exciter::Bow) return;
    const double pos = p.position;
    std::vector<double> x(static_cast<size_t>(n), 0.0);
    if (exciter == Exciter::Pluck) {
        // A triangle with its apex at the pluck position; a soft pluck is that triangle rounded off.
        const double peak = std::max(pos * n, 1.0);
        for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] = i < peak ? i / peak : (n - i) / std::max(n - peak, 1.0);
        const int passes = static_cast<int>(std::lround((1.0 - p.hardness) * 6.0));
        for (int k = 0; k < passes; ++k) {
            std::vector<double> y(x.size());
            for (int i = 0; i < n; ++i)
                y[static_cast<size_t>(i)] = 0.25 * x[static_cast<size_t>((i + n - 1) % n)] + 0.5 * x[static_cast<size_t>(i)] + 0.25 * x[static_cast<size_t>((i + 1) % n)];
            x.swap(y);
        }
    } else {
        // A half-sine pulse centred at the strike position; a soft (wide) hammer is a wide pulse.
        const double width = std::max(n * (0.02 + 0.28 * (1.0 - p.hardness)), 3.0);
        const double centre = pos * n;
        for (int i = 0; i < n; ++i) {
            const double d = i - (centre - width * 0.5);
            if (d >= 0.0 && d <= width) x[static_cast<size_t>(i)] = std::sin(kPi * d / width);
        }
    }
    double mean = 0.0;
    for (double v : x) mean += v;
    mean /= n;
    double peak = 1e-9;
    for (double& v : x) { v -= mean; peak = std::max(peak, std::fabs(v)); }
    for (int i = 0; i < n; ++i) out[i] = static_cast<float>(x[static_cast<size_t>(i)] / peak);
}

int activeVoices(const EngineState& state) noexcept
{
    int n = 0;
    for (const Voice& v : state.voices) n += v.active ? 1 : 0;
    return n;
}

void resetState(EngineState& state) noexcept
{
    state = EngineState {};
}

void processBlock(EngineState& st, const Parameters& rawParams, float sampleRate, std::uint32_t frames,
                  const MidiEvent* events, std::uint32_t eventCount, float* outL, float* outR) noexcept
{
    const Parameters p = clampParameters(rawParams);
    const double sr = sampleRate > 0.0f ? sampleRate : 44100.0;
    if (!st.primed) {
        st.masterSmooth = p.master;
        st.primed = true;
    }
    const float smooth = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sr));
    const double releaseCoef = std::exp(-1.0 / std::max(static_cast<double>(p.releaseMs) * 0.001 * sr, 1.0));
    const Biquad body1 = bandPass(120.0 * p.bodySize, 4.0, sr), body2 = bandPass(280.0 * p.bodySize, 5.0, sr);
    const double bodyGain = static_cast<double>(p.bodyLevel) * 1.5;
    const double bowGain = 1.5 + 4.5 * p.bowPressure;
    const double bowDrive = 0.1 + 0.5 * p.bowPressure;

    std::uint32_t nextEvent = 0;
    for (std::uint32_t i = 0; i < frames; ++i) {
        while (nextEvent < eventCount && events[nextEvent].frame <= i) handleEvent(st, p, sr, events[nextEvent++]);
        st.masterSmooth += smooth * (p.master - st.masterSmooth);
        for (size_t ch = 0; ch < 16; ++ch) st.bend[ch] += smooth * (st.bendTarget[ch] - st.bend[ch]);

        double sum = 0.0;
        for (Voice& v : st.voices) {
            if (!v.active) continue;
            const bool gate = v.held || v.sustained;
            if (!gate) {
                v.bowing = false;
                v.envelope *= releaseCoef;
                if (v.envelope < 1.0e-4) { v.active = false; continue; }
            }
            if (v.controlCountdown-- <= 0) {
                v.controlCountdown = 31;
                v.tuning = tuneLoop(440.0 * std::exp2((v.note - 69 + st.bend[static_cast<size_t>(v.channel)]) / 12.0), p, sr);
                // Keep the bodies' memory across blocks: copy only the coefficients.
                v.body[0].b0 = body1.b0; v.body[0].b1 = body1.b1; v.body[0].b2 = body1.b2; v.body[0].a1 = body1.a1; v.body[0].a2 = body1.a2;
                v.body[1].b0 = body2.b0; v.body[1].b1 = body2.b1; v.body[1].b2 = body2.b2; v.body[1].a1 = body2.a1; v.body[1].a2 = body2.a2;
            }
            const LoopTuning& t = v.tuning;
            const int readIdx = (v.writePos - t.delay + kDelaySize) % kDelaySize;
            const double x = static_cast<double>(v.line[static_cast<size_t>(readIdx)]);

            double inject = 0.0;
            if (v.bowing) {
                // Friction against the string: a sticking region around zero relative speed, then falling force.
                const double rel = v.bowSpeed - x;
                inject = bowDrive * 0.5 * (2.0 * rel) / (1.0 + (bowGain * rel) * (bowGain * rel));
            }
            // Loop: damping low-pass, then the fractional-delay all-pass, then the stiffness all-pass.
            v.lowpassState = (1.0 - t.lowpass) * x + t.lowpass * v.lowpassState;
            const double frac = t.fractionCoeff * v.lowpassState + v.fracX1 - t.fractionCoeff * v.fracY1;
            v.fracX1 = v.lowpassState;
            v.fracY1 = frac;
            double stiff = frac;
            for (int k = 0; k < t.stiffStages; ++k) {
                const double in = stiff;
                stiff = t.stiffCoeff * in + v.stiffX1[static_cast<size_t>(k)] - t.stiffCoeff * v.stiffY1[static_cast<size_t>(k)];
                v.stiffX1[static_cast<size_t>(k)] = in;
                v.stiffY1[static_cast<size_t>(k)] = stiff;
            }
            v.line[static_cast<size_t>(v.writePos)] = static_cast<float>(std::min(std::max(t.gain * stiff + inject, -3.0), 3.0));
            v.writePos = (v.writePos + 1) % kDelaySize;

            // Pickup comb: the string read at two points along its length.
            double out = x;
            if (p.pickup > 0.0f) {
                const int d = std::max(1, static_cast<int>(std::lround(static_cast<double>(p.pickup) * t.delay)));
                out = 0.7 * (x - static_cast<double>(v.line[static_cast<size_t>((readIdx + d) % kDelaySize)]));
            }
            const double body = runBiquad(v.body[0], out) + runBiquad(v.body[1], out);
            const double voiceOut = (out * 0.8 + body * bodyGain) * v.envelope * (v.bowed ? static_cast<double>(v.velocity) : 1.0);
            v.peak = std::max(std::fabs(x), v.peak * 0.9998);
            if (!v.bowing && v.peak < 1.0e-6) { v.active = false; continue; }
            sum += voiceOut * st.masterSmooth;
        }
        // One-pole DC blocker at about 8 Hz (a bow or a hammer can carry an offset).
        const double dcCoeff = 1.0 - 2.0 * kPi * 8.0 / sr;
        const double y = sum - st.dcX + dcCoeff * st.dcY;
        st.dcX = sum;
        st.dcY = y;
        outL[i] = outR[i] = static_cast<float>(y);
    }
    while (nextEvent < eventCount) handleEvent(st, p, sr, events[nextEvent++]);

    // Recursive memory on a silent input decays into denormals; zero what is negligible.
    constexpr double kTiny = 1.0e-30;
    if (std::fabs(st.dcX) < kTiny) st.dcX = 0.0;
    if (std::fabs(st.dcY) < kTiny) st.dcY = 0.0;
    for (Voice& v : st.voices) {
        for (double* s : {&v.lowpassState, &v.fracX1, &v.fracY1})
            if (std::fabs(*s) < kTiny) *s = 0.0;
        for (double& s : v.stiffX1) if (std::fabs(s) < kTiny) s = 0.0;
        for (double& s : v.stiffY1) if (std::fabs(s) < kTiny) s = 0.0;
        for (Biquad& b : v.body) {
            if (std::fabs(b.z1) < kTiny) b.z1 = 0.0;
            if (std::fabs(b.z2) < kTiny) b.z2 = 0.0;
        }
        if (v.peak < kTiny) v.peak = 0.0;
    }
}

}  // namespace downspout::monochord
