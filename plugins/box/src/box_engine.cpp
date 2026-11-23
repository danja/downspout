#include "box_core_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace downspout::box {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr double kSpeedOfSound = 343.0;
constexpr float kPoisson = 0.3f;
constexpr float kLn1000 = 6.907755278982137f;

struct Geometry {
    float lengthM = 0.5f;
    float widthM = 0.38f;
    float depthM = 0.1f;
    bool hasAir = true;
    bool hasCavity = true;
    float stiffnessScale = 1.0f;
    float lossScale = 1.0f;
};

[[nodiscard]] float safeValue(const float v, const float lo, const float hi, const float fallback) noexcept
{
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

[[nodiscard]] Geometry geometryFor(const Parameters& p) noexcept
{
    Geometry g;
    switch (static_cast<int>(p.body)) {
    case 1:  // Grand piano soundboard: large, rib-stiffened, no soundhole.
        g.lengthM = 1.10f * p.size;
        g.widthM = 0.75f * p.size;
        g.hasAir = false;
        g.stiffnessScale = 2.4f;
        g.lossScale = 1.4f;
        break;
    case 2:  // Loudspeaker-like cabinet: smaller, stiffer, lossier.
        g.lengthM = 0.45f * p.size;
        g.widthM = 0.30f * p.size;
        g.stiffnessScale = 1.4f;
        g.lossScale = 1.2f;
        break;
    case 3:  // Free plate: no enclosure, no air path.
        g.lengthM = 0.60f * p.size;
        g.widthM = 0.60f * p.size;
        g.hasAir = false;
        g.hasCavity = false;
        g.stiffnessScale = 0.8f;
        g.lossScale = 0.9f;
        break;
    default: // Acoustic guitar body.
        g.lengthM = 0.50f * p.size;
        g.widthM = 0.38f * p.size;
        break;
    }
    g.depthM = p.depth * 0.001f;
    return g;
}

[[nodiscard]] float smootherCoeff(const float seconds, const double sampleRate) noexcept
{
    if (sampleRate <= 0.0 || seconds <= 0.0f)
        return 0.0f;
    return static_cast<float>(std::exp(-1.0 / (static_cast<double>(seconds) * sampleRate)));
}

bool bandpassRBJ(const float centreHz, const float q, const double sampleRate,
                 BiquadCoeffs& out) noexcept
{
    const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    if (!std::isfinite(centreHz) || !std::isfinite(q) || q <= 0.0f)
        return false;

    const double nyquist = sr * 0.5;
    const double fc = std::clamp(static_cast<double>(centreHz), 8.0, nyquist * 0.48);
    const double w0 = 2.0 * kPi * fc / sr;
    const double alpha = std::sin(w0) / (2.0 * static_cast<double>(q));
    const double a0 = 1.0 + alpha;

    out.b0 = static_cast<float>(alpha / a0);
    out.b1 = 0.0f;
    out.b2 = static_cast<float>(-alpha / a0);
    out.a1 = static_cast<float>(-2.0 * std::cos(w0) / a0);
    out.a2 = static_cast<float>((1.0 - alpha) / a0);
    return std::isfinite(out.b0 + out.b2 + out.a1 + out.a2);
}

[[nodiscard]] float modeShape(const int harmonic, const float positionPercent) noexcept
{
    const float x = std::clamp(positionPercent / 100.0f, 0.01f, 0.99f);
    return std::sin(static_cast<float>(harmonic) * kPi * x);
}

// Fade modes out before they pile up at Nyquist when Body Size is taken to an
// extreme. Without this a 0.05x body produced a cluster of near-Nyquist
// resonators instead of a smaller, brighter box.
[[nodiscard]] float nyquistFade(const float hz, const double sampleRate) noexcept
{
    const float limit = static_cast<float>(sampleRate * 0.46);
    const float width = static_cast<float>(sampleRate * 0.14);
    return std::clamp((limit - hz) / std::max(width, 1.0f), 0.0f, 1.0f);
}

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

    float param(const ParamId id, const float loT, const float hiT) noexcept
    {
        return normalizedToValue(index(id), range(loT, hiT));
    }
};

}  // namespace

Parameters clampParameters(const Parameters& raw) noexcept
{
    const auto& specs = kParameterSpecs;
    Parameters p;
    const auto field = [&](const ParamId id, const float value) {
        const ParamSpec& spec = specs[index(id)];
        return safeValue(value, spec.minimum, spec.maximum, spec.defaultValue);
    };

    p.body = std::round(field(ParamId::body, raw.body));
    p.size = field(ParamId::size, raw.size);
    p.thickness = field(ParamId::thickness, raw.thickness);
    p.stiffness = field(ParamId::stiffness, raw.stiffness);
    p.density = field(ParamId::density, raw.density);
    p.damping = field(ParamId::damping, raw.damping);
    p.hole = field(ParamId::hole, raw.hole);
    p.volume = field(ParamId::volume, raw.volume);
    p.depth = field(ParamId::depth, raw.depth);

    p.drive = field(ParamId::drive, raw.drive);
    p.nonlinear = field(ParamId::nonlinear, raw.nonlinear);
    p.excX = field(ParamId::excX, raw.excX);
    p.excY = field(ParamId::excY, raw.excY);
    p.micX = field(ParamId::micX, raw.micX);
    p.micY = field(ParamId::micY, raw.micY);
    p.width = field(ParamId::width, raw.width);
    p.mix = field(ParamId::mix, raw.mix);
    p.output = field(ParamId::output, raw.output);

    p.bypass = std::round(safeValue(raw.bypass, 0.0f, 1.0f, 0.0f));
    p.ccChannel = std::round(safeValue(raw.ccChannel, 1.0f, 16.0f, 1.0f));
    p.seed = std::round(safeValue(raw.seed, 1.0f, 9999.0f, 1.0f));
    return p;
}

BodyState analyseBody(const Parameters& raw, const double sampleRate, const float level) noexcept
{
    const Parameters p = clampParameters(raw);
    const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const Geometry g = geometryFor(p);

    const float h = p.thickness * 0.001f;
    const float young = p.stiffness * 1.0e9f * g.stiffnessScale;
    const float rigidity = young * h * h * h / (12.0f * (1.0f - kPoisson * kPoisson));
    const float plateSpeed = std::sqrt(rigidity / (std::max(p.density, 1.0f) * h));

    // Wood is not a perfectly linear spring: a hard drive stiffens the panel and
    // lifts every mode. This is an effect control, not a calibrated material law.
    const float driveEnergy = std::clamp(level * level * 0.10f, 0.0f, 1.5f);
    const float freqScale = std::clamp(
        1.0f + p.nonlinear * 0.01f * driveEnergy * 0.28f, 0.8f, 2.2f);

    BodyState out;
    out.valid = true;

    const float widthT = p.width / 100.0f;
    const float micXL = std::clamp(p.micX - widthT * 20.0f, 1.0f, 99.0f);
    const float micXR = std::clamp(p.micX + widthT * 20.0f, 1.0f, 99.0f);
    const float detune = widthT * 0.015f;

    float sumAbs = 0.0f;
    std::array<float, kModeCount> rawGain[2] {};
    std::array<float, kModeCount> frequencies {};

    for (std::size_t k = 0; k < kModeCount; ++k) {
        const ModeIndex mi = kModeIndices[k];
        const float m = static_cast<float>(mi.m);
        const float n = static_cast<float>(mi.n);
        const float shape = m * m / (g.lengthM * g.lengthM)
                          + n * n / (g.widthM * g.widthM);
        const float hz = (kPi * 0.5f) * plateSpeed * shape * freqScale;
        frequencies[k] = hz;
        const float fade = nyquistFade(hz, sr);
        const float radiation = 0.30f + 0.70f * hz / (hz + 900.0f);
        const float driveWeight = modeShape(mi.m, p.excX) * modeShape(mi.n, p.excY);
        for (std::size_t ch = 0; ch < kAudioChannels; ++ch) {
            const float mic = ch == 0 ? micXL : micXR;
            const float readWeight = modeShape(mi.m, mic) * modeShape(mi.n, p.micY);
            rawGain[ch][k] = driveWeight * readWeight * radiation * fade;
            sumAbs += std::fabs(rawGain[ch][k]);
        }
    }
    // Normalise only large banks; a small sumAbs is a genuine weak coupling
    // from the chosen excitation and mic positions, not something to cancel.
    const float normalise = 5.5f / std::max(sumAbs, 2.0f);

    for (std::size_t k = 0; k < kModeCount; ++k) {
        const float hz = frequencies[k];
        const float t60 = std::clamp(
            0.55f * g.lossScale / std::max(p.damping, 0.001f)
                * std::sqrt(200.0f / std::max(hz, 20.0f)),
            0.015f, 12.0f);
        const float q = std::clamp(kPi * hz * t60 / kLn1000, 0.7f, 60.0f);
        if (k == 0) {
            out.bodyHz = hz;
            out.decaySec = t60;
            out.q = q;
        }
        for (std::size_t ch = 0; ch < kAudioChannels; ++ch) {
            const float detunedHz = hz * (ch == 0 ? 1.0f - detune : 1.0f + detune);
            ModeResponse& mode = out.modes[ch][k];
            bandpassRBJ(detunedHz, q, sr, mode.filter);
            mode.gain = rawGain[ch][k] * normalise;
        }
    }

    // Helmholtz air resonance. A zero-diameter soundhole is a sealed box: no
    // air mode, not a resonance at zero hertz.
    if (g.hasAir && p.hole > 0.5f) {
        const double radius = static_cast<double>(p.hole) * 0.0005;
        const double area = kPi * radius * radius;
        const double volume = std::max(static_cast<double>(p.volume) * 0.001, 1.0e-5);
        const double neck = std::max(static_cast<double>(p.thickness) * 0.001 + 1.7 * radius, 1.0e-4);
        const double airHz = kSpeedOfSound / (2.0 * kPi)
                           * std::sqrt(area / (volume * neck));
        const float airF = static_cast<float>(airHz);
        const float fade = nyquistFade(airF, sr);
        const float q = std::clamp(8.0f / std::max(p.damping, 0.01f)
                                   * std::sqrt(p.volume / 15.0f), 1.2f, 30.0f);
        out.airHz = airF;
        for (std::size_t ch = 0; ch < kAudioChannels; ++ch) {
            bandpassRBJ(airF, q, sr, out.air[ch].filter);
            out.air[ch].gain = 1.20f * std::min(p.hole / 90.0f, 2.0f) * fade;
        }
    }

    if (g.hasCavity) {
        // The two axial cavity modes follow the box's dimensions. Body Depth
        // joins the longer and shorter spans so a deep cabinet really does
        // behave differently from a shallow soundboard.
        const float cavity[2] = {
            static_cast<float>(kSpeedOfSound / (2.0 * std::sqrt(g.lengthM * g.depthM))),
            static_cast<float>(kSpeedOfSound / (2.0 * std::sqrt(g.widthM * g.depthM))),
        };
        const float q = std::clamp(4.0f / std::max(p.damping, 0.01f), 1.0f, 20.0f);
        for (std::size_t i = 0; i < 2; ++i) {
            ModeResponse* target = i == 0 ? out.lowCavity.data() : out.highCavity.data();
            const float gain = 0.50f / (1.0f + cavity[i] / 400.0f) * nyquistFade(cavity[i], sr);
            for (std::size_t ch = 0; ch < kAudioChannels; ++ch) {
                bandpassRBJ(cavity[i], q, sr, target[ch].filter);
                target[ch].gain = gain;
            }
        }
    }

    out.directGain = 0.30f * std::clamp(std::sqrt(3.0f / p.thickness), 0.2f, 2.0f);
    return out;
}

void activate(EngineState& state) noexcept
{
    for (auto& channel : state.modes)
        for (auto& filter : channel)
            filter = BiquadState {};
    for (auto& filter : state.air)
        filter = BiquadState {};
    for (auto& filter : state.lowCavity)
        filter = BiquadState {};
    for (auto& filter : state.highCavity)
        filter = BiquadState {};
    state.controlCounter = 0;
    state.envelope = 0.0f;
    state.initialised = false;
    // Wet mix is not cleared: activate() must not change the user's settings.
}

Parameters randomiseParameters(const Parameters& raw) noexcept
{
    const Parameters p = clampParameters(raw);
    Rng rng(static_cast<std::uint32_t>(p.seed) * 2654435761u);
    Parameters out = p;

    out.body = std::round(rng.range(0.0f, 3.0f));
    out.size = rng.param(ParamId::size, 0.18f, 0.82f);
    out.thickness = rng.param(ParamId::thickness, 0.12f, 0.78f);
    out.stiffness = rng.param(ParamId::stiffness, 0.10f, 0.90f);
    out.density = rng.param(ParamId::density, 0.10f, 0.85f);
    out.damping = rng.param(ParamId::damping, 0.18f, 0.85f);
    out.hole = rng.param(ParamId::hole, 0.0f, 0.75f);
    out.volume = rng.param(ParamId::volume, 0.12f, 0.85f);
    out.depth = rng.param(ParamId::depth, 0.10f, 0.85f);
    out.drive = rng.param(ParamId::drive, 0.05f, 0.75f);
    out.nonlinear = rng.param(ParamId::nonlinear, 0.0f, 0.9f);
    out.excX = rng.param(ParamId::excX, 0.15f, 0.85f);
    out.excY = rng.param(ParamId::excY, 0.15f, 0.85f);
    out.micX = rng.param(ParamId::micX, 0.15f, 0.85f);
    out.micY = rng.param(ParamId::micY, 0.15f, 0.85f);
    out.width = rng.param(ParamId::width, 0.0f, 1.0f);

    out.seed = static_cast<float>(rng.next() % 9999u) + 1.0f;
    return clampParameters(out);
}

bool applyController(Parameters& p, const std::uint8_t controller, const std::uint8_t value) noexcept
{
    const auto target = controllerTarget(controller);
    if (!target.has_value())
        return false;
    const float next = controllerToValue(*target, value);
    switch (*target) {
    case ParamId::size: p.size = next; break;
    case ParamId::damping: p.damping = next; break;
    case ParamId::hole: p.hole = next; break;
    case ParamId::drive: p.drive = next; break;
    default: return false;
    }
    return true;
}

namespace {

void glide(BiquadState& state, const BiquadCoeffs& target, const float smooth) noexcept
{
    if (!state.haveCoeffs) {
        state.coeffs = target;
        state.haveCoeffs = true;
        return;
    }
    const float k = 1.0f - smooth;
    state.coeffs.b0 += (target.b0 - state.coeffs.b0) * k;
    state.coeffs.b1 += (target.b1 - state.coeffs.b1) * k;
    state.coeffs.b2 += (target.b2 - state.coeffs.b2) * k;
    state.coeffs.a1 += (target.a1 - state.coeffs.a1) * k;
    state.coeffs.a2 += (target.a2 - state.coeffs.a2) * k;
}

void runBiquad(BiquadState& state, const float input, float& output) noexcept
{
    const BiquadCoeffs& c = state.coeffs;
    float y = c.b0 * input + c.b1 * state.x1 + c.b2 * state.x2
            - c.a1 * state.y1 - c.a2 * state.y2;
    if (!std::isfinite(y)) {
        state.x1 = state.x2 = state.y1 = state.y2 = 0.0f;
        y = 0.0f;
    }
    if (std::fabs(y) < 1.0e-12f)
        y = 0.0f;
    state.x2 = state.x1;
    state.x1 = input;
    state.y2 = state.y1;
    state.y1 = y;
    output = y;
}

[[nodiscard]] float softClip(const float x) noexcept
{
    // tanh(kx)/k tends to 1/k, so a 36 dB drive cannot produce an unbounded
    // excitation. The normal drive of 6 dB stays close to linear.
    constexpr float k = 0.35f;
    return std::tanh(x * k) / k;
}

constexpr std::uint32_t kControlGrid = 32;

}  // namespace

void processBlock(EngineState& state,
                  const Parameters& rawParams,
                  const std::uint32_t frames,
                  const double sampleRate,
                  const float* const* inputs,
                  float* const* outputs) noexcept
{
    const Parameters p = clampParameters(rawParams);
    const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    state.sampleRate = sr;

    const float driveGain = std::pow(10.0f, p.drive / 20.0f);
    const float outputGain = std::pow(10.0f, p.output / 20.0f);
    const float wetTarget = p.bypass > 0.5f ? 0.0f : p.mix / 100.0f;
    const float coeffSmooth = smootherCoeff(0.005f, sr);
    const float envelopeCoeff = smootherCoeff(0.008f, sr);
    const float wetSmooth = smootherCoeff(0.005f, sr);

    // Start at the current mix so the very first impulse excites the body
    // immediately; only later changes crossfade over 5 ms.
    if (!state.initialised) {
        state.wetMix = wetTarget;
        state.initialised = true;
    }

    BodyState targets = analyseBody(p, sr, state.envelope);

    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        float driven[2] = {0.0f, 0.0f};
        float dry[2] = {0.0f, 0.0f};
        float envelopePeak = 0.0f;

        for (std::size_t ch = 0; ch < kAudioChannels; ++ch) {
            const float raw = (inputs && inputs[ch]) ? inputs[ch][frame] : 0.0f;
            dry[ch] = std::isfinite(raw) ? raw : 0.0f;
            driven[ch] = softClip(dry[ch] * driveGain);
            envelopePeak = std::max(envelopePeak, std::fabs(driven[ch]));
        }
        state.envelope += (envelopePeak - state.envelope) * (1.0f - envelopeCoeff);

        if (state.controlCounter == 0)
            targets = analyseBody(p, sr, state.envelope);
        state.controlCounter = (state.controlCounter + 1) % kControlGrid;

        state.wetMix += (wetTarget - state.wetMix) * (1.0f - wetSmooth);

        float wet[2] = {0.0f, 0.0f};
        for (std::size_t ch = 0; ch < kAudioChannels; ++ch) {
            for (std::size_t k = 0; k < kModeCount; ++k) {
                BiquadState& filter = state.modes[ch][k];
                const ModeResponse& response = targets.modes[ch][k];
                glide(filter, response.filter, coeffSmooth);
                float ring = 0.0f;
                runBiquad(filter, driven[ch], ring);
                wet[ch] += ring * response.gain;
            }
            BiquadState* extra[3] = {
                &state.air[ch], &state.lowCavity[ch], &state.highCavity[ch],
            };
            const ModeResponse* responses[3] = {
                &targets.air[ch], &targets.lowCavity[ch], &targets.highCavity[ch],
            };
            for (int i = 0; i < 3; ++i) {
                glide(*extra[i], responses[i]->filter, coeffSmooth);
                float ring = 0.0f;
                runBiquad(*extra[i], driven[ch], ring);
                wet[ch] += ring * responses[i]->gain;
            }
            wet[ch] += driven[ch] * targets.directGain;
        }

        for (std::size_t ch = 0; ch < kAudioChannels; ++ch) {
            float y = (dry[ch] * (1.0f - state.wetMix) + wet[ch] * state.wetMix) * outputGain;
            if (!std::isfinite(y))
                y = 0.0f;
            if (std::fabs(y) < 1.0e-12f)
                y = 0.0f;
            if (outputs && outputs[ch])
                outputs[ch][frame] = y;
        }
    }
}

}  // namespace downspout::box
