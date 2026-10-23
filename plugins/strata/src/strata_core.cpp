#include "strata_core.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace downspout::strata {

double warpPhase(WarpMode mode, double amount, double phase, double* slope) noexcept
{
    double p = phase - std::floor(phase);
    double s = 1.0;
    switch (mode) {
    case WarpMode::Bend: {
        // Moves the half-way point of the cycle: 0 = unchanged, 1 = the first half takes 5 % of the cycle.
        const double split = 0.5 + 0.45 * amount;
        p = p < split ? p * 0.5 / split : 0.5 + (p - split) * 0.5 / (1.0 - split);
        s = 0.5 / (1.0 - split);
        break;
    }
    case WarpMode::Sync: {
        const double mult = 1.0 + 7.0 * amount;
        p = std::fmod(p * mult, 1.0);
        s = mult;
        break;
    }
    default: break;
    }
    if (slope) *slope = s;
    return p;
}

namespace {

constexpr double kPi = 3.14159265358979323846;

float clampf(float v, float lo, float hi) noexcept
{
    if (!std::isfinite(v)) return lo;
    return std::min(std::max(v, lo), hi);
}

inline std::uint32_t nextRandom(std::uint32_t& s) noexcept
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

inline float randomUnit(std::uint32_t& s) noexcept   // 0..1
{
    return static_cast<float>(nextRandom(s) >> 8) * (1.0f / 16777216.0f);
}

// Filter coefficients for the trapezoidal SVF: g = tan(pi fc / sr), k = 1 / Q.
using SvfCoeff = FilterCoeff;

SvfCoeff makeSvf(double cutoff, double resonance, double sr) noexcept
{
    const double fc = std::min(std::max(cutoff, 20.0), std::min(20000.0, sr * 0.45));
    SvfCoeff c;
    c.g = std::tan(kPi * fc / sr);
    // res 0 -> Q = 1/sqrt(2) (Butterworth), res 1 -> Q = 20.
    c.k = 1.41421356 - (1.41421356 - 0.05) * std::min(std::max(resonance, 0.0), 1.0);
    c.a1 = 1.0 / (1.0 + c.g * (c.g + c.k));
    c.a2 = c.g * c.a1;
    c.a3 = c.g * c.a2;
    return c;
}

struct SvfOut { double lp, bp, hp; };

inline SvfOut runSvf(Svf& s, const SvfCoeff& c, double x) noexcept
{
    const double v3 = x - s.ic2;
    const double v1 = c.a1 * s.ic1 + c.a2 * v3;
    const double v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
    s.ic1 = 2.0 * v1 - s.ic1;
    s.ic2 = 2.0 * v2 - s.ic2;
    return {v2, v1, x - c.k * v1 - v2};
}

inline double filterSample(FilterType type, Svf& s1, Svf& s2, const SvfCoeff& c, const SvfCoeff& c2, double x) noexcept
{
    const SvfOut a = runSvf(s1, c, x);
    switch (type) {
    case FilterType::LowPass12:  return a.lp;
    case FilterType::LowPass24:  return runSvf(s2, c2, a.lp).lp;
    case FilterType::HighPass12: return a.hp;
    case FilterType::BandPass:   return c.k * a.bp;
    default:                     return a.lp + a.hp;  // notch
    }
}

// One table oscillator read at `phase`, with the warp applied. The mip is chosen from the
// frequency the read will actually run at, so a sync multiplier or a steep bend does not alias.
inline float readOsc(const Bank& bk, const OscParams& o, double position, double phase, double freq, double sr,
                     float& foldPrev, float& foldCosPrev) noexcept
{
    const WarpMode mode = static_cast<WarpMode>(static_cast<int>(o.warpMode));
    const double amt = o.warpAmount;
    double slope = 1.0;
    const double p = warpPhase(mode, amt, phase, &slope);
    const double allowed = (sr * 0.5) / std::max(freq * slope, 1.0);
    const int mip = Bank::mipForHarmonics(allowed);
    float s = bk.read(static_cast<int>(o.table), position, mip, p);
    if (mode == WarpMode::Fold) {
        // sin(a s) with first-order antiderivative antialiasing: (F(s) - F(prev)) / (s - prev), F = -cos(a s) / a.
        const double a = (1.0 + amt * 6.0) * 0.5 * kPi;
        const float cosNow = static_cast<float>(std::cos(a * static_cast<double>(s)));
        const double d = static_cast<double>(s) - static_cast<double>(foldPrev);
        const float out = std::fabs(d) > 1.0e-4
                              ? static_cast<float>((static_cast<double>(foldCosPrev) - static_cast<double>(cosNow)) / (a * d))
                              : static_cast<float>(std::sin(a * 0.5 * (static_cast<double>(s) + static_cast<double>(foldPrev))));
        foldPrev = s;
        foldCosPrev = cosNow;
        s = std::min(std::max(out, -1.0f), 1.0f);
    }
    return s;
}

}  // namespace

double lfoStep(EngineState& st, LfoShape shape, double rate, double sr) noexcept
{
    st.lfoPhase += rate / sr;
    bool wrapped = false;
    if (st.lfoPhase >= 1.0) { st.lfoPhase -= std::floor(st.lfoPhase); wrapped = true; }
    const double p = st.lfoPhase;
    switch (shape) {
    case LfoShape::Sine:     return std::sin(2.0 * kPi * p);
    case LfoShape::Triangle: return 4.0 * std::fabs(p - std::floor(p + 0.5)) - 1.0;
    case LfoShape::Saw:      return 2.0 * p - 1.0;
    case LfoShape::Square:   return p < 0.5 ? 1.0 : -1.0;
    default:
        if (wrapped) st.lfoHeld = randomUnit(st.rng) * 2.0f - 1.0f;
        return st.lfoHeld;
    }
}

namespace {

void startVoice(EngineState& st, const Parameters& p, int note, int velocity) noexcept
{
    int idx = -1;
    for (int i = 0; i < kVoices; ++i)
        if (!st.voices[static_cast<size_t>(i)].active) { idx = i; break; }
    if (idx < 0) {
        // Steal: a releasing voice first, then the quietest.
        double best = 1e9;
        for (int i = 0; i < kVoices; ++i) {
            const Voice& v = st.voices[static_cast<size_t>(i)];
            const double score = v.amp.value + (v.held || v.sustained ? 10.0 : 0.0);
            if (score < best) { best = score; idx = i; }
        }
    }
    Voice& v = st.voices[static_cast<size_t>(idx)];
    v = Voice {};
    v.active = true;
    v.held = true;
    v.note = note;
    v.velocity = static_cast<float>(velocity) / 127.0f;
    v.age = ++st.counter;
    v.amp.stage = Envelope::Stage::Attack;
    v.flt.stage = Envelope::Stage::Attack;
    // Start phases: all in step at Amount 0 (so the voices add, rather than cancel), spreading with Amount.
    const int n = std::min(std::max(static_cast<int>(std::lround(p.unisonVoices)), 1), kMaxUnison);
    std::uint32_t seed = static_cast<std::uint32_t>(0x9e3779b9u * static_cast<std::uint32_t>(v.age) + static_cast<std::uint32_t>(note) * 7919u + 1u);
    for (int o = 0; o < 2; ++o)
        for (int u = 0; u < n; ++u)
            v.phase[static_cast<size_t>(o)][static_cast<size_t>(u)] = static_cast<double>(randomUnit(seed)) * p.unisonAmount;
    for (int u = 0; u < kMaxUnison; ++u) v.noiseDetune[static_cast<size_t>(u)] = 0.0f;
    // The antialiased fold's memory must be a consistent pair: cos(0) is 1, not 0.
    for (auto& row : v.foldCosPrev) row.fill(1.0f);
}

void noteOff(EngineState& st, int note) noexcept
{
    for (Voice& v : st.voices)
        if (v.active && v.held && v.note == note) {
            v.held = false;
            if (st.sustainPedal) v.sustained = true;
        }
}

void handleEvent(EngineState& st, const Parameters& p, const MidiEvent& e) noexcept
{
    switch (e.type) {
    case MidiEvent::Type::NoteOn:
        if (e.data2 == 0) noteOff(st, e.data1); else startVoice(st, p, e.data1, e.data2);
        break;
    case MidiEvent::Type::NoteOff:
        noteOff(st, e.data1);
        break;
    case MidiEvent::Type::Controller:
        if (e.data1 == 64) {
            st.sustainPedal = e.data2 >= 64;
            if (!st.sustainPedal)
                for (Voice& v : st.voices)
                    if (v.active && v.sustained) { v.sustained = false; }
        } else if (e.data1 == 120 || e.data1 == 123) {
            for (Voice& v : st.voices) {
                v.held = false;
                v.sustained = false;
                if (e.data1 == 120) v = Voice {};
            }
            if (e.data1 == 120) { st.dcX[0] = st.dcX[1] = st.dcY[0] = st.dcY[1] = 0.0; }
        }
        break;
    case MidiEvent::Type::PitchBend:
        st.bendTarget = static_cast<float>(e.bend) / 8192.0f * p.bendRange;
        break;
    }
}

}  // namespace

Parameters clampParameters(const Parameters& in) noexcept
{
    Parameters p = in;
    for (OscParams& o : p.osc) {
        o.table      = std::round(clampf(o.table, 0.0f, static_cast<float>(kCategories - 1)));
        o.position   = clampf(o.position, 0.0f, 1.0f);
        o.warpMode   = std::round(clampf(o.warpMode, 0.0f, 3.0f));
        o.warpAmount = clampf(o.warpAmount, 0.0f, 1.0f);
        o.transpose  = std::round(clampf(o.transpose, -24.0f, 24.0f));
        o.fineCents  = clampf(o.fineCents, -50.0f, 50.0f);
        o.level      = clampf(o.level, 0.0f, 1.0f);
    }
    p.subLevel     = clampf(in.subLevel, 0.0f, 1.0f);
    p.subOctave    = in.subOctave >= 0.5f ? 1.0f : 0.0f;
    p.unisonMode   = std::round(clampf(in.unisonMode, 0.0f, 2.0f));
    p.unisonVoices = std::round(clampf(in.unisonVoices, 1.0f, static_cast<float>(kMaxUnison)));
    p.unisonAmount = clampf(in.unisonAmount, 0.0f, 1.0f);
    p.filterType   = std::round(clampf(in.filterType, 0.0f, 4.0f));
    p.cutoffHz     = clampf(in.cutoffHz, 20.0f, 20000.0f);
    p.resonance    = clampf(in.resonance, 0.0f, 1.0f);
    p.keyTrack     = clampf(in.keyTrack, 0.0f, 1.0f);
    p.envAmount    = clampf(in.envAmount, -1.0f, 1.0f);
    p.envPosition  = clampf(in.envPosition, -1.0f, 1.0f);
    p.ampAttackMs  = clampf(in.ampAttackMs, 0.5f, 5000.0f);
    p.ampDecayMs   = clampf(in.ampDecayMs, 1.0f, 8000.0f);
    p.ampSustain   = clampf(in.ampSustain, 0.0f, 1.0f);
    p.ampReleaseMs = clampf(in.ampReleaseMs, 1.0f, 8000.0f);
    p.fltAttackMs  = clampf(in.fltAttackMs, 0.5f, 5000.0f);
    p.fltDecayMs   = clampf(in.fltDecayMs, 1.0f, 8000.0f);
    p.fltSustain   = clampf(in.fltSustain, 0.0f, 1.0f);
    p.fltReleaseMs = clampf(in.fltReleaseMs, 1.0f, 8000.0f);
    p.lfoRateHz    = clampf(in.lfoRateHz, 0.05f, 20.0f);
    p.lfoShape     = std::round(clampf(in.lfoShape, 0.0f, 4.0f));
    p.lfoPosition  = clampf(in.lfoPosition, 0.0f, 1.0f);
    p.lfoPitch     = clampf(in.lfoPitch, 0.0f, 1.0f);
    p.lfoCutoff    = clampf(in.lfoCutoff, 0.0f, 1.0f);
    p.master       = clampf(in.master, 0.0f, 1.0f);
    p.velocity     = clampf(in.velocity, 0.0f, 1.0f);
    p.bendRange    = clampf(in.bendRange, 0.0f, 24.0f);
    return p;
}

namespace {

// The envelope with its three per-sample constants worked out once per block.
struct EnvCoeffs {
    double attackInc, decayCoef, sustain, releaseCoef;
    EnvCoeffs(double attackMs, double decayMs, double sus, double releaseMs, double sr)
        : attackInc(1.0 / std::max(attackMs * 0.001 * sr, 1.0)),
          decayCoef(std::exp(-1.0 / std::max(decayMs * 0.001 * sr, 1.0))),
          sustain(sus),
          releaseCoef(std::exp(-1.0 / std::max(releaseMs * 0.001 * sr, 1.0))) {}
};

inline double stepEnvelope(Envelope& e, bool gate, const EnvCoeffs& c) noexcept
{
    if (e.stage == Envelope::Stage::Idle) return 0.0;
    if (!gate && e.stage != Envelope::Stage::Release) e.stage = Envelope::Stage::Release;
    switch (e.stage) {
    case Envelope::Stage::Attack:
        e.value += c.attackInc;
        if (e.value >= 1.0) { e.value = 1.0; e.stage = Envelope::Stage::Decay; }
        break;
    case Envelope::Stage::Decay:
        e.value = c.sustain + (e.value - c.sustain) * c.decayCoef;
        break;
    case Envelope::Stage::Release:
        e.value *= c.releaseCoef;
        if (e.value < 1.0e-4) { e.value = 0.0; e.stage = Envelope::Stage::Idle; }
        break;
    default: break;
    }
    return e.value;
}

}  // namespace

double envelopeStep(Envelope& e, bool gate, double attackMs, double decayMs, double sustain, double releaseMs, double sr) noexcept
{
    return stepEnvelope(e, gate, EnvCoeffs(attackMs, decayMs, sustain, releaseMs, sr));
}

double filterResponseDb(FilterType type, double cutoffHz, double resonance, double hz, double sampleRate) noexcept
{
    const SvfCoeff c = makeSvf(cutoffHz, resonance, sampleRate);
    const SvfCoeff c2 = makeSvf(cutoffHz, 0.0, sampleRate);
    // Analog-prototype shape through the same g, k: H(s) with s = j tan(pi f / sr) / g.
    const double w = std::tan(kPi * std::min(hz, sampleRate * 0.499) / sampleRate) / c.g;
    const std::complex<double> s(0.0, w);
    const std::complex<double> d = s * s + c.k * s + 1.0;
    const std::complex<double> lp = 1.0 / d, hp = s * s / d, bp = c.k * s / d;
    std::complex<double> h;
    switch (type) {
    case FilterType::LowPass12:  h = lp; break;
    case FilterType::LowPass24: {
        const double w2 = std::tan(kPi * std::min(hz, sampleRate * 0.499) / sampleRate) / c2.g;
        const std::complex<double> s2(0.0, w2);
        h = lp * (1.0 / (s2 * s2 + c2.k * s2 + 1.0));
        break;
    }
    case FilterType::HighPass12: h = hp; break;
    case FilterType::BandPass:   h = bp; break;
    default:                     h = lp + hp; break;
    }
    return 20.0 * std::log10(std::max(std::abs(h), 1e-9));
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
    const Bank& bk = bank();
    if (!st.primed) {
        st.posSmooth[0] = p.osc[0].position; st.posSmooth[1] = p.osc[1].position;
        st.levelSmooth[0] = p.osc[0].level; st.levelSmooth[1] = p.osc[1].level;
        st.subSmooth = p.subLevel; st.cutoffSmooth = p.cutoffHz; st.masterSmooth = p.master;
        st.primed = true;
    }
    const float smooth = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sr));
    const int nUni = static_cast<int>(p.unisonVoices);
    const UnisonMode umode = static_cast<UnisonMode>(static_cast<int>(p.unisonMode));
    const FilterType ftype = static_cast<FilterType>(static_cast<int>(p.filterType));
    // Constant-power for decorrelated voices (1/sqrt n) at full Amount, and 1/n when they are still
    // coherent at Amount 0, so the level does not jump with the number of voices.
    const float uniWeight = std::pow(static_cast<float>(nUni), -(1.0f - 0.5f * p.unisonAmount));

    const EnvCoeffs ampCoeffs(p.ampAttackMs, p.ampDecayMs, p.ampSustain, p.ampReleaseMs, sr);
    const EnvCoeffs fltCoeffs(p.fltAttackMs, p.fltDecayMs, p.fltSustain, p.fltReleaseMs, sr);

    std::uint32_t nextEvent = 0;
    for (std::uint32_t i = 0; i < frames; ++i) {
        while (nextEvent < eventCount && events[nextEvent].frame <= i) handleEvent(st, p, events[nextEvent++]);

        st.posSmooth[0] += smooth * (p.osc[0].position - st.posSmooth[0]);
        st.posSmooth[1] += smooth * (p.osc[1].position - st.posSmooth[1]);
        st.levelSmooth[0] += smooth * (p.osc[0].level - st.levelSmooth[0]);
        st.levelSmooth[1] += smooth * (p.osc[1].level - st.levelSmooth[1]);
        st.subSmooth += smooth * (p.subLevel - st.subSmooth);
        st.cutoffSmooth += smooth * (p.cutoffHz - st.cutoffSmooth);
        st.masterSmooth += smooth * (p.master - st.masterSmooth);
        st.bend += smooth * (st.bendTarget - st.bend);

        const double lfo = lfoStep(st, static_cast<LfoShape>(static_cast<int>(p.lfoShape)), p.lfoRateHz, sr);
        double sumL = 0.0, sumR = 0.0;

        for (Voice& v : st.voices) {
            if (!v.active) continue;
            const bool gate = v.held || v.sustained;
            const double ampEnv = stepEnvelope(v.amp, gate, ampCoeffs);
            const double fltEnv = stepEnvelope(v.flt, gate, fltCoeffs);
            if (v.amp.stage == Envelope::Stage::Idle) { v.active = false; continue; }

            const double posMod = fltEnv * p.envPosition + lfo * p.lfoPosition * 0.5;

            // Unison detune and pan change slowly, so they are worked out every 32 samples, not every sample.
            if (v.controlCountdown-- <= 0) {
                v.controlCountdown = 31;
                // Pitch and filter move slowly too (bend, LFO and the filter envelope are all well under 1 kHz).
                const double baseSemi = (v.note - 69) + st.bend + lfo * p.lfoPitch;
                const double noteHz = 440.0 * std::exp2(baseSemi / 12.0);
                for (int o = 0; o < 2; ++o)
                    v.oscHz[static_cast<size_t>(o)] = noteHz * std::exp2((p.osc[static_cast<size_t>(o)].transpose + p.osc[static_cast<size_t>(o)].fineCents * 0.01) / 12.0);
                v.subHz = std::min(noteHz * (p.subOctave >= 0.5f ? 0.25 : 0.5), sr * 0.48);
                const double oct = p.keyTrack * (v.note - 60) / 12.0 + p.envAmount * 4.0 * fltEnv + p.lfoCutoff * 2.0 * lfo;
                const double cutoff = static_cast<double>(st.cutoffSmooth) * std::exp2(oct);
                v.coeff1 = makeSvf(cutoff, p.resonance, sr);
                v.coeff2 = makeSvf(cutoff, 0.0, sr);
                for (int u = 0; u < nUni; ++u) {
                    const double spread = nUni > 1 ? 2.0 * u / (nUni - 1) - 1.0 : 0.0;
                    double detune = 0.0, pan = 0.0;
                    switch (umode) {
                    case UnisonMode::Classic:
                        detune = spread * p.unisonAmount * 0.5;
                        pan = spread * p.unisonAmount * 0.8;
                        break;
                    case UnisonMode::Shimmer:
                        detune = spread * p.unisonAmount * 0.2 + ((u % 2 == 1) ? 12.0 * p.unisonAmount * u / nUni : 0.0);
                        pan = spread * p.unisonAmount * 0.8;
                        break;
                    default: {
                        float& nd = v.noiseDetune[static_cast<size_t>(u)];
                        nd += 0.016f * ((randomUnit(st.rng) - 0.5f) * 2.0f - nd);   // slow random drift
                        detune = spread * p.unisonAmount * 0.3 + nd * p.unisonAmount * 0.5;
                        pan = (spread + 0.2 * nd) * p.unisonAmount * 0.8;
                        break;
                    }
                    }
                    v.detuneRatio[static_cast<size_t>(u)] = std::exp2(detune / 12.0);
                    const double angle = (std::min(std::max(pan, -1.0), 1.0) + 1.0) * 0.25 * kPi;
                    v.panL[static_cast<size_t>(u)] = static_cast<float>(std::cos(angle));
                    v.panR[static_cast<size_t>(u)] = static_cast<float>(std::sin(angle));
                }
            }

            double oscL[2] = {0.0, 0.0}, oscR[2] = {0.0, 0.0};
            for (int o = 0; o < 2; ++o) {
                const OscParams& op = p.osc[static_cast<size_t>(o)];
                if (op.level <= 0.0f && st.levelSmooth[o] < 1e-4f) continue;
                const double position = std::min(std::max(st.posSmooth[o] + posMod, 0.0), 1.0);
                const double oscHz = v.oscHz[static_cast<size_t>(o)];
                for (int u = 0; u < nUni; ++u) {
                    const double freq = std::min(oscHz * v.detuneRatio[static_cast<size_t>(u)], sr * 0.48);
                    double& ph = v.phase[static_cast<size_t>(o)][static_cast<size_t>(u)];
                    ph += freq / sr;
                    ph -= std::floor(ph);
                    const float s = readOsc(bk, op, position, ph, freq, sr, v.foldPrev[static_cast<size_t>(o)][static_cast<size_t>(u)],
                                            v.foldCosPrev[static_cast<size_t>(o)][static_cast<size_t>(u)]) * uniWeight;
                    oscL[o] += s * v.panL[static_cast<size_t>(u)];
                    oscR[o] += s * v.panR[static_cast<size_t>(u)];
                }
            }
            double subL = 0.0, subR = 0.0;
            if (p.subLevel > 0.0f || st.subSmooth > 1e-4f) {
                v.subPhase += v.subHz / sr;
                v.subPhase -= std::floor(v.subPhase);
                const double sub = std::sin(2.0 * kPi * v.subPhase) * st.subSmooth;
                subL = subR = sub * 0.7071;
            }

            // Filter: cutoff follows the key, the filter envelope and the LFO, all in octaves.
            const SvfCoeff& c1 = v.coeff1;
            const SvfCoeff& c2 = v.coeff2;
            const double inL = oscL[0] * st.levelSmooth[0] + oscL[1] * st.levelSmooth[1] + subL;
            const double inR = oscR[0] * st.levelSmooth[0] + oscR[1] * st.levelSmooth[1] + subR;
            const double fl = filterSample(ftype, v.filter1[0], v.filter2[0], c1, c2, inL);
            const double fr = filterSample(ftype, v.filter1[1], v.filter2[1], c1, c2, inR);

            const double velGain = 1.0 - p.velocity + p.velocity * v.velocity;
            const double gain = ampEnv * velGain * st.masterSmooth;
            sumL += fl * gain;
            sumR += fr * gain;
        }
        // A warp can leave a DC offset that moves with the envelope; a one-pole high-pass at about 4 Hz removes it.
        const double dcCoeff = 1.0 - 2.0 * kPi * 4.0 / sr;
        const double inLR[2] = {sumL, sumR};
        for (int ch = 0; ch < 2; ++ch) {
            const double y = inLR[ch] - st.dcX[ch] + dcCoeff * st.dcY[ch];
            st.dcX[ch] = inLR[ch];
            st.dcY[ch] = y;
        }
        outL[i] = static_cast<float>(st.dcY[0]);
        outR[i] = static_cast<float>(st.dcY[1]);
    }
    while (nextEvent < eventCount) handleEvent(st, p, events[nextEvent++]);

    // Recursive filter memory on a silent voice decays into denormals; inactive voices are cleared above,
    // and a releasing voice is cut when its envelope reaches the floor. Flush what is left.
    constexpr double kTiny = 1.0e-30;
    for (int ch = 0; ch < 2; ++ch) {
        if (std::fabs(st.dcX[ch]) < kTiny) st.dcX[ch] = 0.0;
        if (std::fabs(st.dcY[ch]) < kTiny) st.dcY[ch] = 0.0;
    }
    for (Voice& v : st.voices)
        for (int c = 0; c < 2; ++c)
            for (Svf* s : {&v.filter1[static_cast<size_t>(c)], &v.filter2[static_cast<size_t>(c)]}) {
                if (std::fabs(s->ic1) < kTiny) s->ic1 = 0.0;
                if (std::fabs(s->ic2) < kTiny) s->ic2 = 0.0;
            }
}

}  // namespace downspout::strata
