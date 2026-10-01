#include "treatment_core_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace downspout::treatment {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float safeValue(float v, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

bool parseFloat(std::string_view text, float& value)
{
    std::string local(text);
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

// RBJ peaking EQ. This realises the panel's absorption dip: a negative gain at
// the resonance with the damping the model derived, so the notch tracks Q.
bool peakingRBJ(float centreHz, float q, float gainDb, double sampleRate,
                BiquadCoeffs& out) noexcept
{
    if (sampleRate <= 0.0 || centreHz <= 0.0 || q <= 0.0)
        return false;

    const float nyquist = static_cast<float>(sampleRate) * 0.5f;
    const float fc = std::clamp(centreHz, 1.0f, nyquist * 0.95f);
    const float amp = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * fc / static_cast<float>(sampleRate);
    const float cosW0 = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * q);

    const float a0 = 1.0f + alpha / amp;
    out.b0 = (1.0f + alpha * amp) / a0;
    out.b1 = (-2.0f * cosW0) / a0;
    out.b2 = (1.0f - alpha * amp) / a0;
    out.a1 = (-2.0f * cosW0) / a0;
    out.a2 = (1.0f - alpha / amp) / a0;

    return std::isfinite(out.b0 + out.b1 + out.b2 + out.a1 + out.a2);
}

// RBJ high shelf, used as the fill's diffusion shelf.
bool highShelfRBJ(float cornerHz, float q, float gainDb, double sampleRate,
                  BiquadCoeffs& out) noexcept
{
    if (sampleRate <= 0.0 || cornerHz <= 0.0 || q <= 0.0)
        return false;

    const float nyquist = static_cast<float>(sampleRate) * 0.5f;
    const float fc = std::clamp(cornerHz, 1.0f, nyquist * 0.95f);
    const float amp = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * fc / static_cast<float>(sampleRate);
    const float cosW0 = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * q);
    const float beta = 2.0f * std::sqrt(amp) * alpha;

    const float a0 = (amp + 1.0f) + (amp - 1.0f) * cosW0 + beta;
    out.b0 = ((amp + 1.0f) + (amp - 1.0f) * cosW0 + beta) / a0;
    out.b1 = (-2.0f * ((amp - 1.0f) + (amp + 1.0f) * cosW0)) / a0;
    out.b2 = ((amp + 1.0f) + (amp - 1.0f) * cosW0 - beta) / a0;
    out.a1 = (2.0f * ((amp - 1.0f) - (amp + 1.0f) * cosW0)) / a0;
    out.a2 = ((amp + 1.0f) + (amp - 1.0f) * cosW0 - beta) / a0;

    return std::isfinite(out.b0 + out.b1 + out.b2 + out.a1 + out.a2);
}

void runBiquad(BiquadState& state, const float input, float& output) noexcept
{
    const BiquadCoeffs& c = state.coeffs;
    float y = c.b0 * input + c.b1 * state.x1 + c.b2 * state.x2
        - c.a1 * state.y1 - c.a2 * state.y2;

    if (!std::isfinite(y)) {
        // A bad state must not propagate into the output bus.
        state.x1 = state.x2 = state.y1 = state.y2 = 0.0f;
        y = 0.0f;
    }
    // Flush denormals so long decays do not fall off a performance cliff.
    if (std::fabs(y) < 1e-12f)
        y = 0.0f;

    state.x2 = state.x1;
    state.x1 = input;
    state.y2 = state.y1;
    state.y1 = y;
    output = y;
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

    // Uniform in [lo, hi).
    float range(const float lo, const float hi) noexcept
    {
        return lo + (hi - lo) * (static_cast<float>(next() & 0x00ffffffu)
                                 / static_cast<float>(0x01000000u));
    }
};

}  // namespace

float smootherCoeff(const float seconds, const double sampleRate) noexcept
{
    if (sampleRate <= 0.0 || seconds <= 0.0f)
        return 0.0f;
    return static_cast<float>(std::exp(-1.0 / (static_cast<double>(seconds) * sampleRate)));
}

PanelState analysePanel(const Parameters& raw) noexcept
{
    const Parameters p = clampParameters(raw);

    // Metres.
    const float cavity = p.cavity * 0.001f;
    const float gap = p.gap * 0.001f;
    // The facing and the air gap are in series acoustically, so they add.
    const float depth = std::max(cavity + gap, 1.0e-4f);

    const float rhoC = kCharacteristicImpedance;

    // w0 = c * sqrt(rho0 / (m * D))
    const float w0 = kSpeedOfSound * std::sqrt(kAirDensity / (p.mass * depth));
    const float resonanceHz = w0 / (2.0f * kPi);

    // Specific flow resistance through the fill: R * cavity.
    const float r = std::max(p.resist, 1.0f) * cavity;

    // At the resonance the imaginary part of Z vanishes, so alpha reduces to
    // the resistive match term.
    const float denom = r + rhoC;
    const float peakAbsorb = std::clamp(4.0f * rhoC * r / (denom * denom), 0.0f, 1.0f);
    const float q = std::clamp(w0 * p.mass / denom, kMinQ, kMaxQ);

    // The fill's diffusion corner: where viscous and thermal depth effects
    // balance, R / (2*pi*rho0*cavity).
    const float diffusionHz = std::max(p.resist, 1.0f)
        / (2.0f * kPi * kAirDensity * std::max(cavity, 1.0e-4f));

    PanelState state;
    state.resonanceHz = std::clamp(resonanceHz, 10.0f, 2000.0f);
    state.q = q;
    state.peakAbsorb = peakAbsorb;
    state.diffusionHz = std::clamp(diffusionHz, 50.0f, 20000.0f);
    // 1.0 at the impedance optimum r = rho0*c, falling off either side. This
    // is the same expression as peakAbsorb at resonance; it is kept separate
    // because the panel shows them as different ideas.
    state.impedanceMatch = std::clamp(4.0f * r * rhoC / (denom * denom), 0.0f, 1.0f);
    return state;
}

Parameters clampParameters(const Parameters& raw) noexcept
{
    const auto& specs = kParameterSpecs;
    Parameters out;
    out.cavity = safeValue(raw.cavity, specs[index(ParamId::cavity)].minimum,
                           specs[index(ParamId::cavity)].maximum, 100.0f);
    out.gap = safeValue(raw.gap, specs[index(ParamId::gap)].minimum,
                        specs[index(ParamId::gap)].maximum, 50.0f);
    out.mass = safeValue(raw.mass, specs[index(ParamId::mass)].minimum,
                         specs[index(ParamId::mass)].maximum, 0.8f);
    out.resist = safeValue(raw.resist, specs[index(ParamId::resist)].minimum,
                           specs[index(ParamId::resist)].maximum, 8000.0f);
    out.amount = safeValue(raw.amount, 0.0f, 100.0f, 100.0f);
    out.bypass = std::round(safeValue(raw.bypass, 0.0f, 1.0f, 0.0f));
    out.seed = std::round(safeValue(raw.seed, 1.0f, 9999.0f, 1.0f));
    return out;
}

void activate(EngineState& state) noexcept
{
    state.notch.fill(BiquadState {});
    state.diffusion.fill(BiquadState {});
    state.haveAmount = false;
}

Parameters randomiseParameters(const Parameters& raw) noexcept
{
    const Parameters p = clampParameters(raw);
    Rng rng(static_cast<std::uint32_t>(p.seed) * 2654435761u);

    Parameters out = p;

    // Draw geometry and fill from plausible panel territory, rounded so the
    // panel reads like a value someone picked rather than a random float.
    out.cavity = std::round(rng.range(60.0f, 220.0f));
    out.gap = std::round(rng.range(0.0f, 180.0f));
    out.mass = std::round(rng.range(0.3f, 3.2f) * 10.0f) * 0.1f;
    out.resist = std::round(rng.range(3000.0f, 30000.0f) / 500.0f) * 500.0f;

    // Advance the seed so a second press differs, but stay in the 1-9999
    // range the Seed parameter allows.
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
    const PanelState panel = analysePanel(params);
    const double sr = (sampleRate > 0.0) ? sampleRate : 48000.0;
    state.sampleRate = sr;

    const bool bypassed = params.bypass > 0.5f;
    // Amount scales how much of the panel's absorption is applied. Bypass and
    // Amount 0 both mean fully transparent, but the DSP keeps running either
    // way so there is no click when toggling back.
    const float targetAmount = bypassed ? 0.0f
        : std::clamp(params.amount * 0.01f, 0.0f, 1.0f);

    // Notch depth: the dip the panel puts in at its resonance. Scaled by
    // Amount and by the impedance match, so an over- or under-damped fill
    // absorbs less.
    const float notchDepthDb = -std::clamp(
        20.0f * std::log10(std::max(1.0f - targetAmount * panel.peakAbsorb, 1.0e-3f)),
        kMaxNotchDepthDb, 0.0f);

    // Diffusion shelf: the fill keeps absorbing above its corner, where the
    // panel resonance has already closed.
    const float shelfDepthDb = std::clamp(
        -6.0f * kFillShare * targetAmount * panel.peakAbsorb,
        kMaxNotchDepthDb, 0.0f);

    BiquadCoeffs notchTarget {};
    BiquadCoeffs shelfTarget {};
    const bool haveNotch = peakingRBJ(panel.resonanceHz, panel.q, notchDepthDb, sr, notchTarget);
    const bool haveShelf = highShelfRBJ(panel.diffusionHz, kDiffusionQ, shelfDepthDb, sr, shelfTarget);

    const float coeffSmooth = smootherCoeff(0.005f, sr);

    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        // Smooth Amount so a CC sweep does not step the depth.
        if (!state.haveAmount) {
            state.amount = targetAmount;
            state.haveAmount = true;
        } else {
            state.amount += (targetAmount - state.amount) * (1.0f - coeffSmooth);
        }

        for (std::size_t channel = 0; channel < kAudioChannels; ++channel) {
            const float in = (inputs && inputs[channel])
                ? inputs[channel][frame] : 0.0f;
            const float x = std::isfinite(in) ? in : 0.0f;

            BiquadState& notch = state.notch[channel];
            if (haveNotch) {
                if (!notch.haveCoeffs) {
                    notch.coeffs = notchTarget;
                    notch.haveCoeffs = true;
                } else {
                    const float k = 1.0f - coeffSmooth;
                    notch.coeffs.b0 += (notchTarget.b0 - notch.coeffs.b0) * k;
                    notch.coeffs.b1 += (notchTarget.b1 - notch.coeffs.b1) * k;
                    notch.coeffs.b2 += (notchTarget.b2 - notch.coeffs.b2) * k;
                    notch.coeffs.a1 += (notchTarget.a1 - notch.coeffs.a1) * k;
                    notch.coeffs.a2 += (notchTarget.a2 - notch.coeffs.a2) * k;
                }
            }

            BiquadState& shelf = state.diffusion[channel];
            if (haveShelf) {
                if (!shelf.haveCoeffs) {
                    shelf.coeffs = shelfTarget;
                    shelf.haveCoeffs = true;
                } else {
                    const float k = 1.0f - coeffSmooth;
                    shelf.coeffs.b0 += (shelfTarget.b0 - shelf.coeffs.b0) * k;
                    shelf.coeffs.b1 += (shelfTarget.b1 - shelf.coeffs.b1) * k;
                    shelf.coeffs.b2 += (shelfTarget.b2 - shelf.coeffs.b2) * k;
                    shelf.coeffs.a1 += (shelfTarget.a1 - shelf.coeffs.a1) * k;
                    shelf.coeffs.a2 += (shelfTarget.a2 - shelf.coeffs.a2) * k;
                }
            }

            float notched = 0.0f;
            runBiquad(notch, x, notched);

            float diffused = 0.0f;
            runBiquad(shelf, notched, diffused);

            // Blend the treated signal with dry by the smoothed Amount, so the
            // model's depth is the audible depth.
            const float wet = std::clamp(state.amount, 0.0f, 1.0f);
            float y = x * (1.0f - wet) + diffused * wet;
            if (!std::isfinite(y))
                y = 0.0f;

            if (outputs && outputs[channel])
                outputs[channel][frame] = y;
        }
    }
}

std::string serializeParameters(const Parameters& raw)
{
    const Parameters p = clampParameters(raw);
    return "version=1\n"
           "cavity=" + std::to_string(p.cavity) + "\n"
           "gap=" + std::to_string(p.gap) + "\n"
           "mass=" + std::to_string(p.mass) + "\n"
           "resist=" + std::to_string(p.resist) + "\n"
           "amount=" + std::to_string(p.amount) + "\n"
           "bypass=" + std::to_string(p.bypass) + "\n"
           "seed=" + std::to_string(p.seed) + "\n";
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
        else if (key == "cavity" && parseFloat(value, v)) { p.cavity = v; }
        else if (key == "gap" && parseFloat(value, v)) { p.gap = v; }
        else if (key == "mass" && parseFloat(value, v)) { p.mass = v; }
        else if (key == "resist" && parseFloat(value, v)) { p.resist = v; }
        else if (key == "amount" && parseFloat(value, v)) { p.amount = v; }
        else if (key == "bypass" && parseFloat(value, v)) { p.bypass = v; }
        else if (key == "seed" && parseFloat(value, v)) { p.seed = v; }
        else { return std::nullopt; }
    }
    return clampParameters(p);
}

}  // namespace downspout::treatment
