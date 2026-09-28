#include "spliff_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

namespace downspout::spliff {
namespace {

float safe(float v, float lo, float hi, float def) noexcept
{
    return std::isfinite(v) ? std::clamp(v, lo, hi) : def;
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

// One-pole lowpass coefficient for a -3 dB frequency at this sample rate.
float lowpassCoeff(float freqHz, double sampleRate) noexcept
{
    if (sampleRate <= 0.0 || freqHz <= 0.0)
        return 0.0f;
    const float omega = 2.0f * 3.14159265f * freqHz / static_cast<float>(sampleRate);
    return 1.0f - std::exp(-omega);
}

// exp(-1 / (seconds * sampleRate)): one-pole smoother coefficient.
float timeCoeff(double seconds, double sampleRate) noexcept
{
    if (sampleRate <= 0.0 || seconds <= 0.0)
        return 0.0f;
    return std::exp(static_cast<float>(-1.0 / (seconds * sampleRate)));
}

}  // namespace

Parameters clampParameters(const Parameters& p) noexcept
{
    Parameters out = p;
    out.mode        = std::round(safe(out.mode, 0.0f, 1.0f, 0.0f));
    out.depth       = safe(out.depth, 0.0f, 1.0f, 0.5f);
    out.sensitivity = safe(out.sensitivity, 0.0f, 1.0f, 0.5f);
    out.sharpness   = safe(out.sharpness, 0.0f, 1.0f, 0.3f);
    out.decay       = safe(out.decay, 0.0f, 1.0f, 0.25f);
    out.decayTilt   = safe(out.decayTilt, -1.0f, 1.0f, 0.0f);
    out.splitLow    = safe(out.splitLow, 20.0f, 2000.0f, 250.0f);
    out.splitHigh   = safe(out.splitHigh, 500.0f, 12000.0f, 4000.0f);
    if (out.splitHigh < out.splitLow * 1.1f)
        out.splitHigh = std::min(12000.0f, out.splitLow * 1.1f);
    out.mix         = safe(out.mix, 0.0f, 100.0f, 100.0f);
    out.trim        = safe(out.trim, -12.0f, 12.0f, 0.0f);
    out.bypass      = std::round(safe(out.bypass, 0.0f, 1.0f, 0.0f));
    out.delta       = std::round(safe(out.delta, 0.0f, 1.0f, 0.0f));
    out.ccDepth       = std::round(safe(out.ccDepth, 0.0f, 127.0f, kDefaultCCDepth));
    out.ccSensitivity = std::round(safe(out.ccSensitivity, 0.0f, 127.0f, kDefaultCCSensitivity));
    out.ccDecay       = std::round(safe(out.ccDecay, 0.0f, 127.0f, kDefaultCCDecay));
    out.ccMix         = std::round(safe(out.ccMix, 0.0f, 127.0f, kDefaultCCMix));
    out.ccChannel     = std::round(safe(out.ccChannel, 1.0f, 16.0f, 1.0f));
    return out;
}

void processBlock(EngineState&        state,
                  const Parameters&   params,
                  std::uint32_t       frames,
                  double              sampleRate,
                  const float* const* inputs,
                  float* const*       outputs,
                  float               effectiveDepth,
                  float               effectiveSensitivity,
                  float               effectiveDecay,
                  float               effectiveMix) noexcept
{
    const Parameters cp = clampParameters(params);
    const float depth = std::clamp(effectiveDepth, 0.0f, 1.0f);
    const float sens  = std::clamp(effectiveSensitivity, 0.0f, 1.0f);
    const float decay = std::clamp(effectiveDecay, 0.0f, 1.0f);
    const float mixFrac = std::clamp(effectiveMix, 0.0f, 100.0f) * 0.01f;
    const bool boost = cp.mode > 0.5f;
    const bool bypassed = cp.bypass > 0.5f;
    const bool delta = cp.delta > 0.5f;
    const double sr = (sampleRate > 0.0) ? sampleRate : 48000.0;

    const float aLow = lowpassCoeff(cp.splitLow, sr);
    const float aHigh = lowpassCoeff(cp.splitHigh, sr);
    // Detection threshold as a fast/slow ratio: higher sensitivity catches more.
    const float threshold = 2.0f - 1.8f * sens;
    // Recovery 5 ms .. 300 ms across the decay knob, per-band tilted.
    const double baseTau = 0.005 * std::pow(60.0, static_cast<double>(decay));
    const double tauLo = std::max(0.001, baseTau * (1.0 - cp.decayTilt));
    const double tauMid = std::max(0.001, baseTau);
    const double tauHi = std::max(0.001, baseTau * (1.0 + cp.decayTilt));
    const float relLo = timeCoeff(tauLo, sr);
    const float relMid = timeCoeff(tauMid, sr);
    const float relHi = timeCoeff(tauHi, sr);
    const float relBands[3] = { relLo, relMid, relHi };
    const float attFast = timeCoeff(0.0002, sr);   // detector attack ~0.2 ms
    const float attGain = timeCoeff(0.001, sr);    // gain clamps down in ~1 ms
    const float attSlow = timeCoeff(0.010, sr);    // reference floor attack
    const float relSlow = timeCoeff(0.250, sr);    // reference floor release
    const float trimGain = std::pow(10.0f, cp.trim / 20.0f);

    // Shared mono detector: both channels use the averaged transient weight,
    // which is the fixed-100%-link behaviour (no image shift on transients).
    for (std::uint32_t f = 0; f < frames; ++f) {
        float in[2] = { 0.0f, 0.0f };
        for (int c = 0; c < 2; ++c) {
            const float v = (inputs && inputs[c]) ? inputs[c][f] : 0.0f;
            in[c] = std::isfinite(v) ? v : 0.0f;
        }
        const float mono = 0.5f * (in[0] + in[1]);

        // Per-channel split (keeps stereo content intact through the bands).
        float band[2][3] = {};
        for (int c = 0; c < 2; ++c) {
            ChannelState& ch = state.channels[static_cast<std::size_t>(c)];
            ch.lowMem += aLow * (in[c] - ch.lowMem);
            ch.highMem += aHigh * (in[c] - ch.highMem);
            band[c][0] = ch.lowMem;
            band[c][2] = in[c] - ch.highMem;
            band[c][1] = ch.highMem - ch.lowMem;
        }

        // Mono detector on the averaged bands, with its own crossover
        // memories and per-band fast/slow envelopes.
        state.detLowMem += aLow * (mono - state.detLowMem);
        state.detHighMem += aHigh * (mono - state.detHighMem);
        const float monoBand[3] = {
            state.detLowMem,
            state.detHighMem - state.detLowMem,
            mono - state.detHighMem,
        };
        float weight[3];
        for (int b = 0; b < 3; ++b) {
            float& fast = state.detFast[static_cast<std::size_t>(b)];
            float& slow = state.detSlow[static_cast<std::size_t>(b)];
            const float mag = std::fabs(monoBand[b]);
            fast += (mag - fast) * (mag > fast ? (1.0f - attFast) : (1.0f - relBands[b]));
            slow += (mag - slow) * (mag > slow ? (1.0f - attSlow) : (1.0f - relSlow));
            const float ratio = fast / (slow + 1e-6f);
            weight[b] = std::clamp((ratio - threshold) / 1.5f, 0.0f, 1.0f);
        }
        const float wAvg = (weight[0] + weight[1] + weight[2]) / 3.0f;
        float wEff[3];
        for (int b = 0; b < 3; ++b)
            wEff[b] = cp.sharpness * weight[b] + (1.0f - cp.sharpness) * wAvg;

        float wet[2] = { 0.0f, 0.0f };
        for (int c = 0; c < 2; ++c) {
            ChannelState& ch = state.channels[static_cast<std::size_t>(c)];
            for (int b = 0; b < 3; ++b) {
                float target;
                if (boost)
                    target = 1.0f + depth * wEff[b] * 3.0f;  // up to ~+12 dB
                else
                    target = 1.0f - depth * wEff[b];
                float& g = ch.gain[static_cast<std::size_t>(b)];
                const float coeff = (target < g) ? attGain : relBands[b];
                g += (target - g) * (1.0f - coeff);
                // Flush denormals that build up in long quiet tails.
                if (std::fabs(g - 1.0f) < 1e-7f) g = 1.0f;
                wet[c] += band[c][b] * g;
            }
            wet[c] *= trimGain;
        }

        for (int c = 0; c < 2; ++c) {
            float y;
            if (bypassed) {
                y = in[c];
            } else if (delta) {
                y = (wet[c] - in[c]) * mixFrac;  // what is removed (cut) or added (boost)
            } else {
                y = in[c] * (1.0f - mixFrac) + wet[c] * mixFrac;
            }
            if (outputs && outputs[c])
                outputs[c][f] = std::isfinite(y) ? y : 0.0f;
        }
    }
}

std::string serializeParameters(const Parameters& p)
{
    const Parameters cp = clampParameters(p);
    return "version=1\n"
           "mode=" + std::to_string(cp.mode) + "\n"
           "depth=" + std::to_string(cp.depth) + "\n"
           "sensitivity=" + std::to_string(cp.sensitivity) + "\n"
           "sharpness=" + std::to_string(cp.sharpness) + "\n"
           "decay=" + std::to_string(cp.decay) + "\n"
           "decay_tilt=" + std::to_string(cp.decayTilt) + "\n"
           "split_low=" + std::to_string(cp.splitLow) + "\n"
           "split_high=" + std::to_string(cp.splitHigh) + "\n"
           "mix=" + std::to_string(cp.mix) + "\n"
           "trim=" + std::to_string(cp.trim) + "\n"
           "bypass=" + std::to_string(cp.bypass) + "\n"
           "delta=" + std::to_string(cp.delta) + "\n"
           "cc_depth=" + std::to_string(cp.ccDepth) + "\n"
           "cc_sensitivity=" + std::to_string(cp.ccSensitivity) + "\n"
           "cc_decay=" + std::to_string(cp.ccDecay) + "\n"
           "cc_mix=" + std::to_string(cp.ccMix) + "\n"
           "cc_channel=" + std::to_string(cp.ccChannel) + "\n";
}

std::optional<Parameters> deserializeParameters(const std::string& text)
{
    Parameters p;
    for (const std::string_view line : split(text, '\n')) {
        if (line.empty()) continue;
        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) return std::nullopt;
        const std::string_view key = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);
        float v = 0.0f;
        if      (key == "version") { continue; }
        else if (key == "mode"            && parseFloat(value, v)) { p.mode = v; }
        else if (key == "depth"           && parseFloat(value, v)) { p.depth = v; }
        else if (key == "sensitivity"     && parseFloat(value, v)) { p.sensitivity = v; }
        else if (key == "sharpness"       && parseFloat(value, v)) { p.sharpness = v; }
        else if (key == "decay"           && parseFloat(value, v)) { p.decay = v; }
        else if (key == "decay_tilt"      && parseFloat(value, v)) { p.decayTilt = v; }
        else if (key == "split_low"       && parseFloat(value, v)) { p.splitLow = v; }
        else if (key == "split_high"      && parseFloat(value, v)) { p.splitHigh = v; }
        else if (key == "mix"             && parseFloat(value, v)) { p.mix = v; }
        else if (key == "trim"            && parseFloat(value, v)) { p.trim = v; }
        else if (key == "bypass"          && parseFloat(value, v)) { p.bypass = v; }
        else if (key == "delta"           && parseFloat(value, v)) { p.delta = v; }
        else if (key == "cc_depth"        && parseFloat(value, v)) { p.ccDepth = v; }
        else if (key == "cc_sensitivity"  && parseFloat(value, v)) { p.ccSensitivity = v; }
        else if (key == "cc_decay"        && parseFloat(value, v)) { p.ccDecay = v; }
        else if (key == "cc_mix"          && parseFloat(value, v)) { p.ccMix = v; }
        else if (key == "cc_channel"      && parseFloat(value, v)) { p.ccChannel = v; }
        else { return std::nullopt; }
    }
    return clampParameters(p);
}

}  // namespace downspout::spliff
