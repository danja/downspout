#include "quartet_core.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace downspout::quartet {

namespace {

constexpr double kPi = 3.14159265358979323846;

float clampf(float v, float lo, float hi) noexcept
{
    if (!std::isfinite(v)) return lo;
    return std::min(std::max(v, lo), hi);
}

// ── Operator waveforms ─────────────────────────────────────────────────────

struct WaveBank {
    std::vector<float> data;   // [wave][mip][kWaveSize + kWavePad]
    WaveBank()
    {
        const size_t stride = static_cast<size_t>(kWaveSize + kWavePad);
        data.assign(3 * kWaveMips * stride, 0.0f);
        for (int w = 0; w < 3; ++w) {
            double peak = 1e-9;
            std::vector<std::vector<double>> mips(kWaveMips, std::vector<double>(kWaveSize, 0.0));
            for (int m = 0; m < kWaveMips; ++m) {
                const int limit = w == 0 ? 1 : (1024 >> m);
                for (int i = 0; i < kWaveSize; ++i) {
                    const double th = 2.0 * kPi * i / kWaveSize;
                    const double c2 = 2.0 * std::cos(th);
                    double sPrev = 0.0, sNow = std::sin(th), sum = 0.0;   // sin(0), sin(theta)
                    for (int h = 1; h <= limit; ++h) {
                        double amp;
                        if (w == 0) amp = 1.0;
                        else if (w == 1) amp = (2.0 / kPi) * ((h % 2 == 1) ? 1.0 : -1.0) / h;
                        else amp = (h % 2 == 1) ? (4.0 / kPi) / h : 0.0;
                        sum += amp * sNow;
                        const double sNext = c2 * sNow - sPrev;
                        sPrev = sNow;
                        sNow = sNext;
                    }
                    mips[static_cast<size_t>(m)][static_cast<size_t>(i)] = sum;
                    if (m == 0) peak = std::max(peak, std::fabs(sum));
                }
            }
            for (int m = 0; m < kWaveMips; ++m)
                for (int i = 0; i < kWaveSize + kWavePad; ++i)
                    data[(static_cast<size_t>(w) * kWaveMips + static_cast<size_t>(m)) * stride + static_cast<size_t>(i)] =
                        static_cast<float>(mips[static_cast<size_t>(m)][static_cast<size_t>(((i - 1) % kWaveSize + kWaveSize) % kWaveSize)] / peak);
        }
    }
};

const WaveBank& waves()
{
    static const WaveBank b;
    return b;
}

inline float cubicRead(const float* d, double phase) noexcept
{
    const double p = phase - std::floor(phase);
    const double x = p * kWaveSize;
    const int i = static_cast<int>(x);
    const float t = static_cast<float>(x - i);
    const float y0 = d[i], y1 = d[i + 1], y2 = d[i + 2], y3 = d[i + 3];
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

// ── Half-band decimator ────────────────────────────────────────────────────

double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 60; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

struct Decimator {
    std::array<float, kDecimatorTaps> taps {};
    std::array<int, kDecimatorTaps> nonzero {};
    int count = 0;
    Decimator()
    {
        const int c = (kDecimatorTaps - 1) / 2;
        const double beta = 8.0;
        double sum = 0.0;
        std::array<double, kDecimatorTaps> h {};
        for (int n = 0; n < kDecimatorTaps; ++n) {
            const double x = n - c;
            const double sinc = x == 0.0 ? 1.0 : std::sin(kPi * x / 2.0) / (kPi * x / 2.0);
            const double r = static_cast<double>(n) / (kDecimatorTaps - 1) * 2.0 - 1.0;
            const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(beta);
            h[static_cast<size_t>(n)] = 0.5 * sinc * w;
            sum += h[static_cast<size_t>(n)];
        }
        for (int n = 0; n < kDecimatorTaps; ++n) {
            taps[static_cast<size_t>(n)] = static_cast<float>(h[static_cast<size_t>(n)] / sum);
            if (std::fabs(taps[static_cast<size_t>(n)]) > 1e-9f) nonzero[static_cast<size_t>(count++)] = n;
        }
    }
};

const Decimator& decimator()
{
    static const Decimator d;
    return d;
}

// ── Filter (same trapezoidal SVF as the other synths) ───────────────────────

FilterCoeff makeSvf(double cutoff, double resonance, double sr) noexcept
{
    const double fc = std::min(std::max(cutoff, 20.0), std::min(20000.0, sr * 0.45));
    FilterCoeff c;
    c.g = std::tan(kPi * fc / sr);
    c.k = 1.41421356 - (1.41421356 - 0.05) * std::min(std::max(resonance, 0.0), 1.0);
    c.a1 = 1.0 / (1.0 + c.g * (c.g + c.k));
    c.a2 = c.g * c.a1;
    c.a3 = c.g * c.a2;
    return c;
}

struct SvfOut { double lp, bp, hp; };

inline SvfOut runSvf(Svf& s, const FilterCoeff& c, double x) noexcept
{
    const double v3 = x - s.ic2;
    const double v1 = c.a1 * s.ic1 + c.a2 * v3;
    const double v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
    s.ic1 = 2.0 * v1 - s.ic1;
    s.ic2 = 2.0 * v2 - s.ic2;
    return {v2, v1, x - c.k * v1 - v2};
}

inline double filterSample(FilterType type, Svf& s1, Svf& s2, const FilterCoeff& c, const FilterCoeff& c2, double x) noexcept
{
    const SvfOut a = runSvf(s1, c, x);
    switch (type) {
    case FilterType::LowPass12:  return a.lp;
    case FilterType::LowPass24:  return runSvf(s2, c2, a.lp).lp;
    case FilterType::HighPass12: return a.hp;
    case FilterType::BandPass:   return c.k * a.bp;
    default:                     return a.lp + a.hp;
    }
}

// ── Envelopes (constants worked out once per block) ─────────────────────────

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

// ── Voices and MIDI ────────────────────────────────────────────────────────

constexpr std::array<Algorithm, kAlgorithms> kAlgorithmTable = {{
    {{2u, 4u, 8u, 0u}, 1u},      // 1: D -> C -> B -> A
    {{2u, 12u, 0u, 0u}, 1u},     // 2: (C + D) -> B -> A
    {{6u, 0u, 8u, 0u}, 1u},      // 3: (B + (D -> C)) -> A
    {{6u, 8u, 8u, 0u}, 1u},      // 4: D -> (B + C) -> A
    {{2u, 0u, 8u, 0u}, 5u},      // 5: (B -> A) + (D -> C)
    {{14u, 0u, 0u, 0u}, 1u},     // 6: (B + C + D) -> A
    {{0u, 0u, 8u, 0u}, 7u},      // 7: A + B + (D -> C)
    {{0u, 4u, 8u, 0u}, 3u},      // 8: A + (D -> C -> B)
    {{0u, 12u, 0u, 0u}, 3u},     // 9: A + ((C + D) -> B)
    {{8u, 8u, 8u, 0u}, 7u},      // 10: D -> (A + B + C)
    {{0u, 0u, 0u, 0u}, 15u},     // 11: A + B + C + D
}};

void startVoice(EngineState& st, int note, int velocity) noexcept
{
    int idx = -1;
    for (int i = 0; i < kVoices; ++i)
        if (!st.voices[static_cast<size_t>(i)].active) { idx = i; break; }
    if (idx < 0) {
        double best = 1e9;
        for (int i = 0; i < kVoices; ++i) {
            const Voice& v = st.voices[static_cast<size_t>(i)];
            const double score = v.env[0].value + (v.held || v.sustained ? 10.0 : 0.0);
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
    for (Envelope& e : v.env) e.stage = Envelope::Stage::Attack;
    v.pitchEnv.stage = Envelope::Stage::Attack;
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
        if (e.data2 == 0) noteOff(st, e.data1); else startVoice(st, e.data1, e.data2);
        break;
    case MidiEvent::Type::NoteOff:
        noteOff(st, e.data1);
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
        st.bendTarget = static_cast<float>(e.bend) / 8192.0f * p.bendRange;
        break;
    }
}

}  // namespace

Parameters::Parameters()
{
    // Operator A is the carrier of the default algorithm; B, C and D are modulators at rising ratios.
    op[0].level = 0.8f; op[0].ratio = 1.0f;
    op[1].level = 0.5f; op[1].ratio = 2.0f;
    op[2].level = 0.3f; op[2].ratio = 3.0f;
    op[3].level = 0.2f; op[3].ratio = 4.0f;
}

Parameters clampParameters(const Parameters& in) noexcept
{
    Parameters p = in;
    for (OperatorParams& o : p.op) {
        o.wave      = std::round(clampf(o.wave, 0.0f, 2.0f));
        o.fixedMode = o.fixedMode >= 0.5f ? 1.0f : 0.0f;
        o.ratio     = clampf(o.ratio, 0.25f, 32.0f);
        o.fineCents = clampf(o.fineCents, -50.0f, 50.0f);
        o.fixedHz   = clampf(o.fixedHz, 10.0f, 20000.0f);
        o.level     = clampf(o.level, 0.0f, 1.0f);
        o.attackMs  = clampf(o.attackMs, 0.5f, 5000.0f);
        o.decayMs   = clampf(o.decayMs, 1.0f, 8000.0f);
        o.sustain   = clampf(o.sustain, 0.0f, 1.0f);
        o.releaseMs = clampf(o.releaseMs, 1.0f, 8000.0f);
    }
    p.algorithm  = std::round(clampf(in.algorithm, 0.0f, static_cast<float>(kAlgorithms - 1)));
    p.feedback   = clampf(in.feedback, 0.0f, 1.0f);
    p.depth      = clampf(in.depth, 0.0f, 4.0f);
    p.pitchEnvOn = in.pitchEnvOn >= 0.5f ? 1.0f : 0.0f;
    p.pitchEnvSemitones = clampf(in.pitchEnvSemitones, -48.0f, 48.0f);
    p.pitchAttackMs  = clampf(in.pitchAttackMs, 0.5f, 5000.0f);
    p.pitchDecayMs   = clampf(in.pitchDecayMs, 1.0f, 8000.0f);
    p.pitchSustain   = clampf(in.pitchSustain, 0.0f, 1.0f);
    p.pitchReleaseMs = clampf(in.pitchReleaseMs, 1.0f, 8000.0f);
    p.filterType = std::round(clampf(in.filterType, 0.0f, 4.0f));
    p.cutoffHz   = clampf(in.cutoffHz, 20.0f, 20000.0f);
    p.resonance  = clampf(in.resonance, 0.0f, 1.0f);
    p.keyTrack   = clampf(in.keyTrack, 0.0f, 1.0f);
    p.master     = clampf(in.master, 0.0f, 1.0f);
    p.velocity   = clampf(in.velocity, 0.0f, 1.0f);
    p.bendRange  = clampf(in.bendRange, 0.0f, 24.0f);
    p.quality    = in.quality >= 0.5f ? 1.0f : 0.0f;
    return p;
}

const Algorithm& algorithm(int index) noexcept
{
    return kAlgorithmTable[static_cast<size_t>(std::min(std::max(index, 0), kAlgorithms - 1))];
}

int carrierCount(const Algorithm& a) noexcept
{
    int n = 0;
    for (int i = 0; i < kOperators; ++i) n += (a.carriers >> i) & 1u;
    return n;
}

int operatorLayer(const Algorithm& a, int op) noexcept
{
    if ((a.carriers >> op) & 1u) return 0;
    int layer = 0;
    for (int t = 0; t < op; ++t)   // a modulator's targets always have a lower index
        if ((a.modulators[static_cast<size_t>(t)] >> op) & 1u) layer = std::max(layer, operatorLayer(a, t) + 1);
    return std::max(layer, 1);
}

int waveHarmonicLimit(int mip) noexcept { return 1024 >> std::min(std::max(mip, 0), kWaveMips - 1); }

int waveMipForHarmonics(double allowed) noexcept
{
    for (int m = 0; m < kWaveMips; ++m)
        if (waveHarmonicLimit(m) <= allowed) return m;
    return kWaveMips - 1;
}

const float* waveTable(Wave wave, int mip) noexcept
{
    const size_t stride = static_cast<size_t>(kWaveSize + kWavePad);
    return waves().data.data() + (static_cast<size_t>(wave) * kWaveMips + static_cast<size_t>(std::min(std::max(mip, 0), kWaveMips - 1))) * stride;
}

float readWave(Wave wave, double phase, double allowedHarmonics) noexcept
{
    return cubicRead(waveTable(wave, wave == Wave::Sine ? kWaveMips - 1 : waveMipForHarmonics(allowedHarmonics)), phase);
}

const std::array<float, kDecimatorTaps>& decimatorTaps() noexcept { return decimator().taps; }

double envelopeStep(Envelope& e, bool gate, double attackMs, double decayMs, double sustain, double releaseMs, double sr) noexcept
{
    return stepEnvelope(e, gate, EnvCoeffs(attackMs, decayMs, sustain, releaseMs, sr));
}

double filterResponseDb(FilterType type, double cutoffHz, double resonance, double hz, double sampleRate) noexcept
{
    const FilterCoeff c = makeSvf(cutoffHz, resonance, sampleRate);
    const FilterCoeff c2 = makeSvf(cutoffHz, 0.0, sampleRate);
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
    const int os = p.quality >= 0.5f ? 2 : 1;
    const double srOs = sr * os;
    const Algorithm& alg = algorithm(static_cast<int>(p.algorithm));
    const double carrierScale = 1.0 / carrierCount(alg);
    const FilterType ftype = static_cast<FilterType>(static_cast<int>(p.filterType));
    const Decimator& dec = decimator();
    (void)waves();
    if (!st.primed) {
        st.cutoffSmooth = p.cutoffHz;
        st.masterSmooth = p.master;
        st.primed = true;
    }
    const float smooth = 1.0f - std::exp(-1.0f / static_cast<float>(0.005 * sr));

    std::array<EnvCoeffs, kOperators> envCoeffs = {
        EnvCoeffs(p.op[0].attackMs, p.op[0].decayMs, p.op[0].sustain, p.op[0].releaseMs, sr),
        EnvCoeffs(p.op[1].attackMs, p.op[1].decayMs, p.op[1].sustain, p.op[1].releaseMs, sr),
        EnvCoeffs(p.op[2].attackMs, p.op[2].decayMs, p.op[2].sustain, p.op[2].releaseMs, sr),
        EnvCoeffs(p.op[3].attackMs, p.op[3].decayMs, p.op[3].sustain, p.op[3].releaseMs, sr)};
    const EnvCoeffs pitchCoeffs(p.pitchAttackMs, p.pitchDecayMs, p.pitchSustain, p.pitchReleaseMs, sr);
    const bool usePitchEnv = p.pitchEnvOn >= 0.5f;

    std::uint32_t nextEvent = 0;
    for (std::uint32_t i = 0; i < frames; ++i) {
        while (nextEvent < eventCount && events[nextEvent].frame <= i) handleEvent(st, p, events[nextEvent++]);
        st.cutoffSmooth += smooth * (p.cutoffHz - st.cutoffSmooth);
        st.masterSmooth += smooth * (p.master - st.masterSmooth);
        st.bend += smooth * (st.bendTarget - st.bend);

        double sum = 0.0;
        for (Voice& v : st.voices) {
            if (!v.active) continue;
            const bool gate = v.held || v.sustained;
            std::array<double, kOperators> amp {};
            bool carrierAlive = false;
            for (int o = 0; o < kOperators; ++o) {
                const double env = stepEnvelope(v.env[static_cast<size_t>(o)], gate, envCoeffs[static_cast<size_t>(o)]);
                amp[static_cast<size_t>(o)] = env * p.op[static_cast<size_t>(o)].level;
                if (((alg.carriers >> o) & 1u) && v.env[static_cast<size_t>(o)].stage != Envelope::Stage::Idle) carrierAlive = true;
            }
            const double pitchEnv = usePitchEnv ? stepEnvelope(v.pitchEnv, gate, pitchCoeffs) : 0.0;
            if (!carrierAlive) { v.active = false; continue; }

            if (v.controlCountdown-- <= 0) {
                v.controlCountdown = 31;
                const double baseHz = 440.0 * std::exp2((v.note - 69 + st.bend + pitchEnv * p.pitchEnvSemitones) / 12.0);
                for (int o = 0; o < kOperators; ++o) {
                    const OperatorParams& op = p.op[static_cast<size_t>(o)];
                    const double hz = op.fixedMode >= 0.5f ? static_cast<double>(op.fixedHz)
                                                           : baseHz * op.ratio * std::exp2(op.fineCents / 1200.0);
                    v.opHz[static_cast<size_t>(o)] = std::min(std::max(hz, 0.1), srOs * 0.48);
                }
                const double cutoff = static_cast<double>(st.cutoffSmooth) * std::exp2(p.keyTrack * (v.note - 60) / 12.0);
                v.coeff1 = makeSvf(cutoff, p.resonance, sr);
                v.coeff2 = makeSvf(cutoff, 0.0, sr);
            }

            double voiceOut = 0.0;
            for (int sub = 0; sub < os; ++sub) {
                std::array<double, kOperators> out {};
                for (int o = kOperators - 1; o >= 0; --o) {
                    const size_t oi = static_cast<size_t>(o);
                    double& ph = v.phase[oi];
                    ph += v.opHz[oi] / srOs;
                    ph -= std::floor(ph);
                    // Phase modulation in cycles; a modulator's own level and envelope set how much it moves its target.
                    double mod = 0.0, speed = 0.0;
                    for (int m = o + 1; m < kOperators; ++m)
                        if ((alg.modulators[oi] >> m) & 1u) {
                            mod += out[static_cast<size_t>(m)];
                            speed += std::fabs(amp[static_cast<size_t>(m)]) * v.opHz[static_cast<size_t>(m)];
                        }
                    mod *= p.depth;
                    speed *= p.depth * 2.0 * kPi;
                    if (o == kOperators - 1 && p.feedback > 0.0f)
                        mod += p.feedback * 0.398 * 0.5 * (v.feedbackY1 + v.feedbackY2);
                    // The read must stay under Nyquist even at the fastest the modulation moves the phase.
                    const double slope = 1.0 + speed / std::max(v.opHz[oi], 1.0);
                    const double allowed = (srOs * 0.5) / (std::max(v.opHz[oi], 1.0) * slope);
                    out[oi] = amp[oi] * readWave(static_cast<Wave>(static_cast<int>(p.op[oi].wave)), ph + mod, allowed);
                }
                v.feedbackY2 = v.feedbackY1;
                v.feedbackY1 = out[kOperators - 1];
                double mix = 0.0;
                for (int o = 0; o < kOperators; ++o)
                    if ((alg.carriers >> o) & 1u) mix += out[static_cast<size_t>(o)];
                mix *= carrierScale;
                if (os == 1) {
                    voiceOut = mix;
                } else {
                    v.history[static_cast<size_t>(v.historyPos)] = static_cast<float>(mix);
                    v.historyPos = (v.historyPos + 1) % kDecimatorTaps;
                    if (sub == 1) {
                        // Newest sample is at historyPos - 1; tap n multiplies the sample n steps back.
                        double acc = 0.0;
                        for (int k = 0; k < dec.count; ++k) {
                            const int n = dec.nonzero[static_cast<size_t>(k)];
                            acc += static_cast<double>(dec.taps[static_cast<size_t>(n)]) *
                                   static_cast<double>(v.history[static_cast<size_t>((v.historyPos - 1 - n + 2 * kDecimatorTaps) % kDecimatorTaps)]);
                        }
                        voiceOut = acc;
                    }
                }
            }
            const double filtered = filterSample(ftype, v.filter1, v.filter2, v.coeff1, v.coeff2, voiceOut);
            const double velGain = 1.0 - p.velocity + p.velocity * v.velocity;
            sum += filtered * velGain * st.masterSmooth;
        }
        // One-pole DC blocker at about 8 Hz: feedback and asymmetric waves can carry an offset. (Its onset transient
        // for a note starting at phase 0 is about 0.007 and dies away with a 20 ms time constant.)
        const double dcCoeff = 1.0 - 2.0 * kPi * 8.0 / sr;
        const double y = sum - st.dcX + dcCoeff * st.dcY;
        st.dcX = sum;
        st.dcY = y;
        outL[i] = outR[i] = static_cast<float>(y);
    }
    while (nextEvent < eventCount) handleEvent(st, p, events[nextEvent++]);

    constexpr double kTiny = 1.0e-30;
    if (std::fabs(st.dcX) < kTiny) st.dcX = 0.0;
    if (std::fabs(st.dcY) < kTiny) st.dcY = 0.0;
    for (Voice& v : st.voices) {
        for (Svf* s : {&v.filter1, &v.filter2}) {
            if (std::fabs(s->ic1) < kTiny) s->ic1 = 0.0;
            if (std::fabs(s->ic2) < kTiny) s->ic2 = 0.0;
        }
        if (std::fabs(v.feedbackY1) < kTiny) v.feedbackY1 = 0.0;
        if (std::fabs(v.feedbackY2) < kTiny) v.feedbackY2 = 0.0;
    }
}

}  // namespace downspout::quartet
