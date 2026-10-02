#include "voxmod_core_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace downspout::voxmod {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr double kTwoPi = 6.283185307179586476925286766559;

float safeValue(float v, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

bool parseFloat(std::string_view text, float& value)
{
    const std::string local(text);
    char* end = nullptr;
    value = std::strtof(local.c_str(), &end);
    return end && *end == '\0';
}

std::vector<std::string_view> split(std::string_view text, char delim)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t pos = text.find(delim, start);
        if (pos == std::string_view::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

// Deterministic xorshift so Randomise is reproducible from the seed alone.
struct Rng {
    std::uint32_t state;

    explicit Rng(const std::uint32_t seed) noexcept
        : state(seed != 0u ? seed : 0x9e3779b9u)
    {
    }

    std::uint32_t next() noexcept
    {
        std::uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    float range(const float lo, const float hi) noexcept
    {
        return lo + (hi - lo) * (static_cast<float>(next() & 0x00ffffffu)
                                 / static_cast<float>(0x01000000u));
    }
};

// RBJ bandpass, constant skirt gain (peak 0 dB). A constant-skirt design is what
// a filter-bank vocoder wants: a constant-Q bank would make the resynthesis gain
// depend on how wide the bands are, which varies across the bank.
void bandpassRBJ(const float centreHz, const float q, const double sampleRate,
                 float& b0, float& b1, float& b2, float& a1, float& a2) noexcept
{
    const float nyquist = static_cast<float>(sampleRate) * 0.5f;
    const float fc = std::clamp(centreHz, 1.0f, nyquist * 0.95f);
    const float safeQ = std::max(q, 0.2f);
    const float w0 = 2.0f * kPi * fc / static_cast<float>(sampleRate);
    const float cosW0 = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * safeQ);

    const float a0 = 1.0f + alpha;
    b0 = alpha / a0;
    b1 = 0.0f;
    b2 = -alpha / a0;
    a1 = (-2.0f * cosW0) / a0;
    a2 = (1.0f - alpha) / a0;
}

float runBand(float& z1, float& z2, const float x,
              const float b0, const float b1, const float b2,
              const float a1, const float a2) noexcept
{
    const float y = b0 * x + b1 * z1 + b2 * z2 - a1 * z1 - a2 * z2;
    z2 = z1;
    z1 = x;
    // A non-finite state must not propagate into the output bus.
    return std::isfinite(y) ? y : 0.0f;
}

// polyBLEP: subtract a polynomial that replaces the step discontinuity a naive
// saw or square would contain, so the shapes stay usable at high ratios without
// spraying aliases across the modulator's spectrum.
double polyBlep(const double t, const double dt) noexcept
{
    if (t < dt) {
        const double u = t / dt;
        return u + u - u * u - 1.0;
    }
    if (t > 1.0 - dt) {
        const double u = (t - 1.0) / dt;
        return u * u + u + u + 1.0;
    }
    return 0.0;
}

double oscillatorSample(RingState& ring, const RingShapeId shape, const double freq) noexcept
{
    // dt is the normalised phase advance for this sample.
    const double dt = std::clamp(freq / 48000.0, 0.0, 0.49);
    double value = 0.0;

    switch (shape) {
    case RingShapeId::sine:
        value = std::sin(kTwoPi * ring.phase);
        break;
    case RingShapeId::triangle: {
        // Integrated square: a triangle has no discontinuity to correct.
        double sq = ring.phase < 0.5 ? 1.0 : -1.0;
        sq -= 4.0 * polyBlep(ring.phase, dt);
        if (ring.phase < 0.5)
            sq -= 4.0 * polyBlep(std::fmod(ring.phase + 0.5, 1.0), dt);
        value = 4.0 * sq - 1.0;
        // A leaky integrator would drift; fold it back into the band.
        value = std::clamp(value, -1.0, 1.0);
        break;
    }
    case RingShapeId::saw: {
        double saw = 2.0 * ring.phase - 1.0;
        saw -= polyBlep(ring.phase, dt);
        value = saw;
        break;
    }
    case RingShapeId::square: {
        double sq = ring.phase < 0.5 ? 1.0 : -1.0;
        sq += polyBlep(ring.phase, dt);
        sq -= polyBlep(std::fmod(ring.phase + 0.5, 1.0), dt);
        value = sq;
        break;
    }
    case RingShapeId::count:
    default:
        value = std::sin(kTwoPi * ring.phase);
        break;
    }

    ring.phase += dt;
    ring.phase -= std::floor(ring.phase);
    ring.lastSample = static_cast<float>(value);
    return value;
}

}  // namespace

float attackCoeff(const float milliseconds, const double sampleRate) noexcept
{
    if (sampleRate <= 0.0 || milliseconds <= 0.0f)
        return 0.0f;
    return static_cast<float>(std::exp(-1.0 / (static_cast<double>(milliseconds) * 0.001 * sampleRate)));
}

float releaseCoeff(const float milliseconds, const double sampleRate) noexcept
{
    if (sampleRate <= 0.0 || milliseconds <= 0.0f)
        return 0.0f;
    return static_cast<float>(std::exp(-1.0 / (static_cast<double>(milliseconds) * 0.001 * sampleRate)));
}

float quantiseRatio(const float ratio) noexcept
{
    const float clamped = std::clamp(
        std::isfinite(ratio) ? ratio : 1.0f,
        kParameterSpecs[index(ParamId::ringRatio)].minimum,
        kParameterSpecs[index(ParamId::ringRatio)].maximum);

    // Log scale: the useful ratios run from 0.5 to 8, and a linear track would
    // crowd everything useful against the bottom.
    const float lo = std::log(0.5f);
    const float hi = std::log(8.0f);
    const float t = (std::log(clamped) - lo) / (hi - lo);
    const float semitone = kRatioMinSemitones
        + std::clamp(t, 0.0f, 1.0f) * (kRatioMaxSemitones - kRatioMinSemitones);

    // 16 steps across the semitone range.
    const int step = std::clamp(static_cast<int>(std::lround(
        (semitone - kRatioMinSemitones) / (kRatioMaxSemitones - kRatioMinSemitones)
        * static_cast<float>(kRatioSteps - 1))), 0, kRatioSteps - 1);

    const float stepped = kRatioMinSemitones
        + (static_cast<float>(step) / static_cast<float>(kRatioSteps - 1))
            * (kRatioMaxSemitones - kRatioMinSemitones);

    return std::pow(2.0f, stepped / 12.0f);
}

std::array<float, kMaxBands> bandEdges(const Parameters& raw) noexcept
{
    const Parameters p = clampParameters(raw);
    std::array<float, kMaxBands> edges {};

    const int count = std::clamp(static_cast<int>(std::lround(p.bandCount)), 1, kMaxBands);
    const double sr = 48000.0;
    const float nyquist = static_cast<float>(sr) * 0.5f;

    // Logarithmic from kVocoderLowHz up through `spread` octaves.
    const float lowHz = kVocoderLowHz;
    const float topHz = std::min(lowHz * std::pow(2.0f, p.bandSpread), nyquist * 0.92f);

    for (int i = 0; i < count; ++i) {
        const float t = count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f;
        edges[static_cast<std::size_t>(i)] = lowHz * std::pow(topHz / lowHz, t);
    }
    for (int i = count; i < kMaxBands; ++i)
        edges[static_cast<std::size_t>(i)] = topHz;

    return edges;
}

float estimateCarrierHz(const Parameters& raw) noexcept
{
    const Parameters p = clampParameters(raw);
    // With an internal carrier the frequency is exactly what the user set.
    if (p.carrierSource >= 0.5f)
        return std::clamp(p.ringFreq, 1.0f, 20000.0f);

    // Otherwise fall back to the ring frequency as the best available estimate
    // from the parameter set; the live estimate comes from the envelope followers
    // during processing.
    return std::clamp(p.ringFreq, 1.0f, 20000.0f);
}

float sibilanceOf(const EngineState& state) noexcept
{
    // Share of the carrier envelope sitting in the top bands, which is what
    // "sibilance" means for an analyser: the part of the spectrum that dominates
    // fricatives and does not read as a formant.
    const int count = std::clamp(state.activeBands, 1, kMaxBands);
    const int topCount = std::max(1, count / 6);

    double top = 0.0;
    for (int i = count - topCount; i < count; ++i) {
        const float e = state.bands[static_cast<std::size_t>(i)].env;
        top += static_cast<double>(e) * e;
    }
    const double total = std::max(state.carrierEnv * static_cast<double>(state.carrierEnv), 1.0e-12);
    return static_cast<float>(std::clamp(top / total, 0.0, 1.0));
}

Parameters clampParameters(const Parameters& raw) noexcept
{
    const auto& specs = kParameterSpecs;
    Parameters out;

    out.mix = safeValue(raw.mix, 0.0f, 100.0f, 50.0f);
    out.bypass = std::round(safeValue(raw.bypass, 0.0f, 1.0f, 0.0f));
    out.seed = std::round(safeValue(raw.seed, 1.0f, 9999.0f, 1.0f));

    out.ringFreq = safeValue(raw.ringFreq, 1.0f, 20000.0f, 220.0f);
    out.ringRatio = safeValue(raw.ringRatio, 0.5f, 8.0f, 2.0f);
    out.ringShape = std::round(safeValue(raw.ringShape, 0.0f,
        static_cast<float>(static_cast<int>(RingShapeId::count) - 1), 0.0f));
    out.ringDepth = safeValue(raw.ringDepth, 0.0f, 100.0f, 100.0f);

    out.bandCount = std::round(safeValue(raw.bandCount, 4.0f, 64.0f, 24.0f));
    out.bandSpread = safeValue(raw.bandSpread, 1.0f, 8.0f, 5.0f);
    out.attackMs = safeValue(raw.attackMs, 1.0f, 200.0f, 12.0f);
    out.releaseMs = safeValue(raw.releaseMs, 20.0f, 1000.0f, 180.0f);
    out.formantShift = safeValue(raw.formantShift, -12.0f, 12.0f, 0.0f);
    out.tilt = safeValue(raw.tilt, -kMaxTiltDbPerOct, kMaxTiltDbPerOct, 0.0f);

    out.carrierSource = std::round(safeValue(raw.carrierSource, 0.0f, 1.0f, 0.0f));
    out.sync = std::round(safeValue(raw.sync, 0.0f, 1.0f, 0.0f));
    out.stereoWidth = safeValue(raw.stereoWidth, 0.0f, 100.0f, 50.0f);
    out.drift = safeValue(raw.drift, 0.0f, 50.0f, 4.0f);

    out.ccMix = std::round(safeValue(raw.ccMix, 0.0f, 127.0f, 1.0f));
    out.ccRingFreq = std::round(safeValue(raw.ccRingFreq, 0.0f, 127.0f, 2.0f));
    out.ccRingRatio = std::round(safeValue(raw.ccRingRatio, 0.0f, 127.0f, 3.0f));
    out.ccBandCount = std::round(safeValue(raw.ccBandCount, 0.0f, 127.0f, 4.0f));
    out.ccCarrier = std::round(safeValue(raw.ccCarrier, 0.0f, 127.0f, 5.0f));
    out.ccChannel = std::round(safeValue(raw.ccChannel, 1.0f, 16.0f, 1.0f));

    // Reference the specs so the array stays load-bearing if ranges ever move.
    (void)specs;
    return out;
}

void activate(EngineState& state) noexcept
{
    state.bands.fill(VocoderBand {});
    state.activeBands = 0;
    state.ring = RingState {};
    state.sibilance = 0.0f;
    state.reductionDb = 0.0f;
    state.carrierHz = 0.0f;
    state.carrierEnv = 0.0f;
    state.modulatorEnv = 0.0f;
}

Parameters randomiseParameters(const Parameters& raw) noexcept
{
    const Parameters p = clampParameters(raw);
    Rng rng(static_cast<std::uint32_t>(p.seed) * 2654435761u);

    Parameters out = p;

    // Draw from values that read as deliberate rather than random.
    out.ringFreq = std::round(rng.range(40.0f, 1400.0f));
    out.ringRatio = std::round(rng.range(0.5f, 6.0f) * 4.0f) * 0.25f;
    out.ringShape = std::floor(rng.range(0.0f, 3.99f));
    out.bandCount = std::round(rng.range(12.0f, 48.0f) * 2.0f) * 0.5f;
    out.bandSpread = std::round(rng.range(3.0f, 7.0f) * 2.0f) * 0.5f;
    out.formantShift = std::round(rng.range(-7.0f, 7.0f));
    out.tilt = std::round(rng.range(-6.0f, 6.0f));

    // Advance the seed so a second press differs.
    out.seed = static_cast<float>(rng.next() % 9999u) + 1.0f;

    return clampParameters(out);
}

void processBlock(EngineState& state,
                  const Parameters& rawParams,
                  const std::uint32_t frames,
                  const double sampleRate,
                  const float* const* inputs,
                  float* const* outputs) noexcept
{
    const Parameters params = clampParameters(rawParams);
    state.params = params;
    const double sr = (sampleRate > 0.0) ? sampleRate : 48000.0;
    state.sampleRate = sr;

    const int wantedBands = std::clamp(static_cast<int>(std::lround(params.bandCount)), 1, kMaxBands);

    // Rebuild the bank only when its shape changed. Coefficients depend on the
    // sample rate, the edges and Formant Shift, not on per-block level sweeps, so
    // this is cheap and keeps the envelope state intact across ordinary moves.
    if (state.activeBands != wantedBands
        || !state.bands[0].haveCoeffs
        || state.bankFormantShift != params.formantShift) {
        state.activeBands = wantedBands;
        state.bankFormantShift = params.formantShift;
        const std::array<float, kMaxBands> edges = bandEdges(params);
        const float formantMul = std::pow(2.0f, params.formantShift / 12.0f);

        for (int i = 0; i < wantedBands; ++i) {
            VocoderBand& band = state.bands[static_cast<std::size_t>(i)];
            const float lo = edges[static_cast<std::size_t>(i)];
            const float hi = edges[static_cast<std::size_t>(i < wantedBands - 1 ? i + 1 : i)];
            band.lowHz = lo;
            band.highHz = std::max(hi, lo * 1.0001f);

            // Geometric centre of the band, and a Q from its width so the bands
            // do not overlap into mush.
            const float centre = std::sqrt(lo * band.highHz);
            const float width = band.highHz - lo;
            const float q = std::clamp(centre / std::max(width, 1.0f), 0.7f, 12.0f);
            bandpassRBJ(centre, q, sr, band.cb0, band.cb1, band.cb2, band.ca1, band.ca2);
            // The modulator side is the same band transposed, which is what the
            // Formant control actually changes.
            bandpassRBJ(centre * formantMul, q, sr,
                        band.mb0, band.mb1, band.mb2, band.ma1, band.ma2);
            band.haveCoeffs = true;
        }
        // Bands beyond the new count must not keep contributing.
        for (int i = wantedBands; i < kMaxBands; ++i) {
            state.bands[static_cast<std::size_t>(i)].env = 0.0f;
            state.bands[static_cast<std::size_t>(i)].cZ1 = 0.0f;
            state.bands[static_cast<std::size_t>(i)].cZ2 = 0.0f;
            state.bands[static_cast<std::size_t>(i)].mZ1 = 0.0f;
            state.bands[static_cast<std::size_t>(i)].mZ2 = 0.0f;
            state.bands[static_cast<std::size_t>(i)].haveCoeffs = false;
        }
        ++state.bankSerial;
    }

    // Tilt is a per-band gain in dB relative to the bank's geometric centre, so
    // it reads as "lean the spectrum this way" rather than as an absolute EQ.
    const float tiltPerOct = params.tilt;
    const float centreRefHz = kVocoderLowHz * std::pow(2.0f, params.bandSpread * 0.5f);
    for (int i = 0; i < wantedBands; ++i) {
        VocoderBand& band = state.bands[static_cast<std::size_t>(i)];
        const float bandCentre = std::sqrt(band.lowHz * band.highHz);
        const float octaves = std::log2(bandCentre / std::max(centreRefHz, 1.0f));
        band.tiltGain = std::pow(10.0f, (tiltPerOct * octaves) / 20.0f);
    }

    const float envAttack = attackCoeff(params.attackMs, sr);
    const float envRelease = releaseCoeff(params.releaseMs, sr);

    const bool bypassed = params.bypass > 0.5f;
    const float vocoderAmount = bypassed ? 0.0f : std::clamp(params.mix * 0.01f, 0.0f, 1.0f);
    const float ringAmount = bypassed ? 0.0f : std::clamp(1.0f - params.mix * 0.01f, 0.0f, 1.0f);
    const float ringDepth = std::clamp(params.ringDepth * 0.01f, 0.0f, 1.0f);

    const RingShapeId shape = static_cast<RingShapeId>(
        std::clamp(static_cast<int>(std::lround(params.ringShape)), 0,
                   static_cast<int>(RingShapeId::count) - 1));

    // Ring Ratio transposes the carrier, so the oscillator runs at
    // ringFreq * ratio. Quantising the ratio to 16 musical steps means a move of
    // the Ratio knob changes the sidebands in semitones, which is repeatable,
    // rather than sweeping them continuously.
    const float ratio = quantiseRatio(params.ringRatio);
    const float width = std::clamp(params.stereoWidth * 0.01f, 0.0f, 1.0f);
    const float driftDepth = std::clamp(params.drift * 0.01f, 0.0f, 1.0f);
    const bool useInternal = params.carrierSource >= 0.5f;
    const bool sync = params.sync >= 0.5f;

    // A slow LFO for Drift: about 0.13 Hz, so it reads as a wavering oscillator
    // rather than as vibrato.
    const double driftInc = kTwoPi * 0.13 / sr;

    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const float carrierL = (inputs && inputs[0]) ? inputs[0][frame] : 0.0f;
        const float carrierR = (inputs && inputs[1]) ? inputs[1][frame] : 0.0f;
        const float modL = (inputs && inputs[2]) ? inputs[2][frame] : 0.0f;
        const float modR = (inputs && inputs[3]) ? inputs[3][frame] : 0.0f;

        const float cx = std::isfinite(0.5f * (carrierL + carrierR))
            ? 0.5f * (carrierL + carrierR) : 0.0f;
        const float mxL = std::isfinite(modL) ? modL : 0.0f;
        const float mxR = std::isfinite(modR) ? modR : 0.0f;
        const float mMono = 0.5f * (mxL + mxR);

        // Carrier for analysis: the internal oscillator when selected, otherwise
        // the input. With Sync on, the ring modulator's own output becomes the
        // carrier, which is what lets one input drive both engines.
        float carrier = useInternal ? 0.0f : cx;

        // ── Ring modulator ──────────────────────────────────────────────
        state.ring.driftPhase += driftInc;
        if (state.ring.driftPhase >= 1.0)
            state.ring.driftPhase -= 1.0;

        double targetFreq = static_cast<double>(params.ringFreq) * static_cast<double>(ratio);
        if (driftDepth > 0.0f) {
            // +/- 40 cents at full Drift, wobbling on the slow LFO.
            const double wobble = std::sin(kTwoPi * state.ring.driftPhase) * driftDepth * 0.4;
            targetFreq *= std::pow(2.0, wobble);
        }
        const double nyquist = sr * 0.5;
        targetFreq = std::clamp(targetFreq, 1.0, nyquist * 0.45);

        if (!state.ring.haveFreq) {
            state.ring.freq = targetFreq;
            state.ring.haveFreq = true;
        }
        // Smooth the frequency so a Ring Freq move glides rather than clicks.
        state.ring.freq += (targetFreq - state.ring.freq) * 0.002;

        const double osc = oscillatorSample(state.ring, shape, state.ring.freq);
        const float carrierWave = static_cast<float>(osc);

        // The ring-modulated signal: modulator * carrier. Depth scales the
        // oscillator toward unity rather than the output toward zero, so 0% is
        // a clean pass-through and 100% is full multiplication.
        const float ringOsc = 1.0f + ringDepth * (carrierWave - 1.0f);
        const float ringedL = mxL * ringOsc;
        const float ringedR = mxR * ringOsc;

        if (useInternal)
            carrier = carrierWave;
        if (sync)
            carrier = 0.5f * (ringedL + ringedR);
        if (!std::isfinite(carrier))
            carrier = 0.0f;

        // ── Vocoder ─────────────────────────────────────────────────────
        float vocoded = 0.0f;
        if (vocoderAmount > 0.0f) {
            // Analyse the carrier and resynthesise the modulator's spectrum.
            // Per band: split both signals, follow the carrier band with an
            // asymmetric follower, and apply that gain to the modulator band.
            double sum = 0.0;
            for (int i = 0; i < wantedBands; ++i) {
                VocoderBand& band = state.bands[static_cast<std::size_t>(i)];

                const float cBand = runBand(band.cZ1, band.cZ2, carrier,
                                            band.cb0, band.cb1, band.cb2, band.ca1, band.ca2);

                const float a = std::fabs(cBand);
                const float coeff = (a > band.env) ? envAttack : envRelease;
                band.env += (a - band.env) * coeff;

                const float gain = band.env * band.tiltGain;
                if (gain < kBandGate)
                    continue;

                const float mBand = runBand(band.mZ1, band.mZ2, mMono,
                                            band.mb0, band.mb1, band.mb2, band.ma1, band.ma2);
                sum += static_cast<double>(mBand) * static_cast<double>(gain);
            }
            vocoded = std::isfinite(sum) ? static_cast<float>(sum) : 0.0f;
        }

        // ── Envelope followers for the status outputs ───────────────────
        {
            const float ca = std::fabs(carrier);
            state.carrierEnv += (ca - state.carrierEnv)
                * (ca > state.carrierEnv ? envAttack : envRelease);
            const float ma = std::fabs(mMono);
            state.modulatorEnv += (ma - state.modulatorEnv)
                * (ma > state.modulatorEnv ? envAttack : envRelease);
        }

        // ── Output ──────────────────────────────────────────────────────
        // Width steers the two engines apart: the ring mod keeps the modulator's
        // own stereo image while the vocoder is inherently mono, so widening
        // pushes the ring mod out and leaves the vocoder centred.
        const float ringMid = 0.5f * (ringedL + ringedR);
        const float ringSide = 0.5f * (ringedL - ringedR) * width;
        const float outL = ringAmount * (ringMid + ringSide) + vocoderAmount * vocoded;
        const float outR = ringAmount * (ringMid - ringSide) + vocoderAmount * vocoded;

        if (outputs && outputs[0])
            outputs[0][frame] = std::isfinite(outL) ? outL : 0.0f;
        if (outputs && outputs[1])
            outputs[1][frame] = std::isfinite(outR) ? outR : 0.0f;
    }

    state.carrierHz = useInternal
        ? static_cast<float>(std::clamp(state.ring.freq, 0.0, 20000.0))
        : estimateCarrierHz(params);
    state.sibilance = sibilanceOf(state);
    // How hard the band gate is pulling the modulator down, as a dB figure.
    state.reductionDb = static_cast<float>(std::clamp(
        20.0 * std::log10(std::max(static_cast<double>(state.modulatorEnv), 1.0e-9)), -60.0, 0.0));
}

std::string serializeParameters(const Parameters& raw)
{
    const Parameters p = clampParameters(raw);
    return "version=1\n"
           "mix=" + std::to_string(p.mix) + "\n"
           "bypass=" + std::to_string(p.bypass) + "\n"
           "seed=" + std::to_string(p.seed) + "\n"
           "ring_freq=" + std::to_string(p.ringFreq) + "\n"
           "ring_ratio=" + std::to_string(p.ringRatio) + "\n"
           "ring_shape=" + std::to_string(p.ringShape) + "\n"
           "ring_depth=" + std::to_string(p.ringDepth) + "\n"
           "band_count=" + std::to_string(p.bandCount) + "\n"
           "band_spread=" + std::to_string(p.bandSpread) + "\n"
           "attack_ms=" + std::to_string(p.attackMs) + "\n"
           "release_ms=" + std::to_string(p.releaseMs) + "\n"
           "formant_shift=" + std::to_string(p.formantShift) + "\n"
           "tilt=" + std::to_string(p.tilt) + "\n"
           "carrier_source=" + std::to_string(p.carrierSource) + "\n"
           "sync=" + std::to_string(p.sync) + "\n"
           "stereo_width=" + std::to_string(p.stereoWidth) + "\n"
           "drift=" + std::to_string(p.drift) + "\n"
           "cc_mix=" + std::to_string(p.ccMix) + "\n"
           "cc_ring_freq=" + std::to_string(p.ccRingFreq) + "\n"
           "cc_ring_ratio=" + std::to_string(p.ccRingRatio) + "\n"
           "cc_band_count=" + std::to_string(p.ccBandCount) + "\n"
           "cc_carrier=" + std::to_string(p.ccCarrier) + "\n"
           "cc_channel=" + std::to_string(p.ccChannel) + "\n";
}

std::optional<Parameters> deserializeParameters(const std::string& text)
{
    Parameters p;
    for (const std::string_view line : split(text, '\n')) {
        if (line.empty())
            continue;
        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos)
            return std::nullopt;
        const std::string_view key = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);
        float v = 0.0f;
        if (key == "version") {
            continue;
        }
        else if (key == "mix" && parseFloat(value, v)) p.mix = v;
        else if (key == "bypass" && parseFloat(value, v)) p.bypass = v;
        else if (key == "seed" && parseFloat(value, v)) p.seed = v;
        else if (key == "ring_freq" && parseFloat(value, v)) p.ringFreq = v;
        else if (key == "ring_ratio" && parseFloat(value, v)) p.ringRatio = v;
        else if (key == "ring_shape" && parseFloat(value, v)) p.ringShape = v;
        else if (key == "ring_depth" && parseFloat(value, v)) p.ringDepth = v;
        else if (key == "band_count" && parseFloat(value, v)) p.bandCount = v;
        else if (key == "band_spread" && parseFloat(value, v)) p.bandSpread = v;
        else if (key == "attack_ms" && parseFloat(value, v)) p.attackMs = v;
        else if (key == "release_ms" && parseFloat(value, v)) p.releaseMs = v;
        else if (key == "formant_shift" && parseFloat(value, v)) p.formantShift = v;
        else if (key == "tilt" && parseFloat(value, v)) p.tilt = v;
        else if (key == "carrier_source" && parseFloat(value, v)) p.carrierSource = v;
        else if (key == "sync" && parseFloat(value, v)) p.sync = v;
        else if (key == "stereo_width" && parseFloat(value, v)) p.stereoWidth = v;
        else if (key == "drift" && parseFloat(value, v)) p.drift = v;
        else if (key == "cc_mix" && parseFloat(value, v)) p.ccMix = v;
        else if (key == "cc_ring_freq" && parseFloat(value, v)) p.ccRingFreq = v;
        else if (key == "cc_ring_ratio" && parseFloat(value, v)) p.ccRingRatio = v;
        else if (key == "cc_band_count" && parseFloat(value, v)) p.ccBandCount = v;
        else if (key == "cc_carrier" && parseFloat(value, v)) p.ccCarrier = v;
        else if (key == "cc_channel" && parseFloat(value, v)) p.ccChannel = v;
        else return std::nullopt;
    }
    return clampParameters(p);
}

}  // namespace downspout::voxmod