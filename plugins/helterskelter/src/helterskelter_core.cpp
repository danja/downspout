#include "helterskelter_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

namespace downspout::helterskelter {
namespace {

using downspout::generative::absoluteQuarter;

constexpr float kMinCutoff = 80.0f;
constexpr float kMaxCutoff = 8000.0f;
constexpr float kDepthOctaves = 4.0f;

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

// exp(-1 / (seconds * sampleRate)): one-pole smoother coefficient.
float timeCoeff(double seconds, double sampleRate) noexcept
{
    if (sampleRate <= 0.0 || seconds <= 0.0)
        return 0.0f;
    return std::exp(static_cast<float>(-1.0 / (seconds * sampleRate)));
}

// RBJ lowpass coefficients; returns false when the cutoff is not usable.
bool lowpassRBJ(float cutoffHz, float q, double sampleRate,
                float& b0, float& b1, float& b2, float& a1, float& a2) noexcept
{
    if (sampleRate <= 0.0 || cutoffHz <= 0.0 || q <= 0.0)
        return false;
    const float nyquist = static_cast<float>(sampleRate) * 0.5f;
    const float fc = std::clamp(cutoffHz, 1.0f, nyquist * 0.95f);
    const float w0 = 2.0f * 3.14159265f * fc / static_cast<float>(sampleRate);
    const float cosW0 = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * q);
    const float a0 = 1.0f + alpha;
    b0 = (1.0f - cosW0) * 0.5f / a0;
    b1 = (1.0f - cosW0) / a0;
    b2 = b0;
    a1 = -2.0f * cosW0 / a0;
    a2 = (1.0f - alpha) / a0;
    return std::isfinite(b0 + b1 + b2 + a1 + a2);
}

}  // namespace

Parameters clampParameters(const Parameters& p) noexcept
{
    Parameters out = p;
    out.mode        = std::round(safe(out.mode, 0.0f, 2.0f, 0.0f));
    out.sensitivity = safe(out.sensitivity, 0.0f, 1.0f, 0.6f);
    out.depth       = safe(out.depth, 0.0f, 1.0f, 0.7f);
    out.resonance   = safe(out.resonance, 0.5f, 12.0f, 4.0f);
    out.baseFreq    = safe(out.baseFreq, 100.0f, 2000.0f, 400.0f);
    out.division    = std::round(safe(out.division, 0.0f, 3.0f, 2.0f));
    out.gateBeats   = safe(out.gateBeats, 0.5f, 8.0f, 2.0f);
    out.attack      = safe(out.attack, 1.0f, 500.0f, 20.0f);
    out.decay       = safe(out.decay, 5.0f, 1000.0f, 150.0f);
    out.sustain     = safe(out.sustain, 0.0f, 1.0f, 0.7f);
    out.release     = safe(out.release, 5.0f, 2000.0f, 200.0f);
    out.invert      = std::round(safe(out.invert, 0.0f, 1.0f, 0.0f));
    out.mix         = safe(out.mix, 0.0f, 100.0f, 100.0f);
    out.trim        = safe(out.trim, -12.0f, 12.0f, 0.0f);
    out.bypass      = std::round(safe(out.bypass, 0.0f, 1.0f, 0.0f));
    out.ccSensitivity = std::round(safe(out.ccSensitivity, 0.0f, 127.0f, kDefaultCCSensitivity));
    out.ccDepth       = std::round(safe(out.ccDepth,       0.0f, 127.0f, kDefaultCCDepth));
    out.ccResonance   = std::round(safe(out.ccResonance,   0.0f, 127.0f, kDefaultCCResonance));
    out.ccMix         = std::round(safe(out.ccMix,         0.0f, 127.0f, kDefaultCCMix));
    out.ccChannel     = std::round(safe(out.ccChannel, 1.0f, 16.0f, 1.0f));
    return out;
}

void processBlock(EngineState&        state,
                  const Parameters&   params,
                  const Transport&    transport,
                  std::uint32_t       frames,
                  double              sampleRate,
                  const float* const* inputs,
                  float* const*       outputs,
                  float               effectiveSensitivity,
                  float               effectiveDepth,
                  float               effectiveResonance,
                  float               effectiveMix) noexcept
{
    const Parameters cp = clampParameters(params);
    const float sens = std::clamp(effectiveSensitivity, 0.0f, 1.0f);
    const float depth = std::clamp(effectiveDepth, 0.0f, 1.0f);
    const float reso = std::clamp(effectiveResonance, 0.5f, 12.0f);
    const float mixFrac = std::clamp(effectiveMix, 0.0f, 100.0f) * 0.01f;
    const bool bypassed = cp.bypass > 0.5f;
    const double sr = (sampleRate > 0.0) ? sampleRate : 48000.0;
    const double bpm = transport.bpm > 0.0 ? transport.bpm : 120.0;
    const bool running = transport.valid && transport.playing;
    const double quartersPerFrame = (sr > 0.0) ? (bpm / 60.0 / sr) : 0.0;

    const int mode = static_cast<int>(std::llround(cp.mode));
    const double cycleBeats = kDivisionBeats[static_cast<int>(std::llround(cp.division))];
    const double gateBeats = std::min(static_cast<double>(cp.gateBeats), cycleBeats);
    const bool invert = cp.invert > 0.5f;
    const double blockStart = transport.valid ? absoluteQuarter(transport) : 0.0;

    const float envAtt = timeCoeff(0.001, sr);
    const float envRel = timeCoeff(0.060, sr);
    const float coeffSmooth = timeCoeff(0.005, sr);
    const float trimGain = std::pow(10.0f, cp.trim / 20.0f);

    // Linear ADSR increments, recomputed per block (tempo-derived gate).
    const float attackInc = (cp.attack > 0.0f)
        ? 1.0f / static_cast<float>(std::max(1.0, cp.attack * 0.001 * sr)) : 1.0f;
    const float decayRange = std::max(0.0f, 1.0f - cp.sustain);
    const float decayInc = (cp.decay > 0.0f)
        ? decayRange / static_cast<float>(std::max(1.0, cp.decay * 0.001 * sr)) : decayRange;
    const float releaseScale = (cp.release > 0.0f)
        ? 1.0f / static_cast<float>(std::max(1.0, cp.release * 0.001 * sr)) : 1.0f;

    for (std::uint32_t f = 0; f < frames; ++f) {
        float in[2] = { 0.0f, 0.0f };
        for (int c = 0; c < 2; ++c) {
            const float v = (inputs && inputs[c]) ? inputs[c][f] : 0.0f;
            in[c] = std::isfinite(v) ? v : 0.0f;
        }
        const float mono = 0.5f * (in[0] + in[1]);
        const float mag = std::fabs(mono);
        state.env += (mag - state.env) * (mag > state.env ? (1.0f - envAtt) : (1.0f - envRel));
        const float envNorm = std::clamp(state.env * sens * 4.0f, 0.0f, 1.0f);

        // BBT ADSR: gate high for the first gateBeats of each cycle.
        float adsrOut = state.adsr.level;
        if (running && quartersPerFrame > 0.0) {
            const double quarter = blockStart + quartersPerFrame * static_cast<double>(f);
            double phase = std::fmod(quarter, cycleBeats);
            if (phase < 0.0) phase += cycleBeats;
            if (state.havePhase && phase < state.prevPhase - 1e-9) {
                // Cycle wrap: retrigger.
                state.adsr.gateHigh = true;
            }
            state.prevPhase = phase;
            state.havePhase = true;
            bool gate = phase < gateBeats;
            if (invert) gate = !gate;
            if (gate && !state.adsr.gateHigh) {
                state.adsr.gateHigh = true;  // fresh attack
            } else if (!gate && state.adsr.gateHigh) {
                state.adsr.gateHigh = false;
                state.adsr.releaseStart = state.adsr.level;
            }
        } else {
            if (state.adsr.gateHigh) {
                state.adsr.gateHigh = false;
                state.adsr.releaseStart = state.adsr.level;
            }
        }
        if (state.adsr.gateHigh) {
            if (state.adsr.level < 1.0f) {
                state.adsr.level = std::min(1.0f, state.adsr.level + attackInc);
            } else {
                state.adsr.level = std::max(cp.sustain, state.adsr.level - decayInc);
            }
        } else if (state.adsr.level > 0.0f) {
            state.adsr.level = std::max(0.0f, state.adsr.level
                - state.adsr.releaseStart * releaseScale);
        }
        adsrOut = state.adsr.level;

        float mod = 0.0f;
        if (mode == 0) mod = envNorm;
        else if (mode == 1) mod = adsrOut;
        else mod = std::max(envNorm, adsrOut);

        const float cutoff = std::clamp(
            cp.baseFreq * std::pow(2.0f, mod * depth * kDepthOctaves),
            kMinCutoff, kMaxCutoff);

        float tb0, tb1, tb2, ta1, ta2;
        const bool haveTarget = lowpassRBJ(cutoff, reso, sr, tb0, tb1, tb2, ta1, ta2);

        float wet[2] = { 0.0f, 0.0f };
        for (int c = 0; c < 2; ++c) {
            FilterState& fs = state.filters[static_cast<std::size_t>(c)];
            if (haveTarget) {
                if (!fs.haveCoeffs) {
                    fs.b0 = tb0; fs.b1 = tb1; fs.b2 = tb2; fs.a1 = ta1; fs.a2 = ta2;
                    fs.haveCoeffs = true;
                } else {
                    const float k = 1.0f - coeffSmooth;
                    fs.b0 += (tb0 - fs.b0) * k;
                    fs.b1 += (tb1 - fs.b1) * k;
                    fs.b2 += (tb2 - fs.b2) * k;
                    fs.a1 += (ta1 - fs.a1) * k;
                    fs.a2 += (ta2 - fs.a2) * k;
                }
            }
            float y = fs.b0 * in[c] + fs.b1 * fs.x1 + fs.b2 * fs.x2
                - fs.a1 * fs.y1 - fs.a2 * fs.y2;
            if (!std::isfinite(y)) {
                y = 0.0f;
                fs.x1 = fs.x2 = fs.y1 = fs.y2 = 0.0f;
            }
            // Flush denormals in long quiet tails.
            if (std::fabs(y) < 1e-12f) y = 0.0f;
            fs.x2 = fs.x1; fs.x1 = in[c];
            fs.y2 = fs.y1; fs.y1 = y;
            wet[c] = y * trimGain;
        }

        for (int c = 0; c < 2; ++c) {
            const float y = bypassed ? in[c] : (in[c] * (1.0f - mixFrac) + wet[c] * mixFrac);
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
           "sensitivity=" + std::to_string(cp.sensitivity) + "\n"
           "depth=" + std::to_string(cp.depth) + "\n"
           "resonance=" + std::to_string(cp.resonance) + "\n"
           "base_freq=" + std::to_string(cp.baseFreq) + "\n"
           "division=" + std::to_string(cp.division) + "\n"
           "gate_beats=" + std::to_string(cp.gateBeats) + "\n"
           "attack=" + std::to_string(cp.attack) + "\n"
           "decay=" + std::to_string(cp.decay) + "\n"
           "sustain=" + std::to_string(cp.sustain) + "\n"
           "release=" + std::to_string(cp.release) + "\n"
           "invert=" + std::to_string(cp.invert) + "\n"
           "mix=" + std::to_string(cp.mix) + "\n"
           "trim=" + std::to_string(cp.trim) + "\n"
           "bypass=" + std::to_string(cp.bypass) + "\n"
           "cc_sensitivity=" + std::to_string(cp.ccSensitivity) + "\n"
           "cc_depth=" + std::to_string(cp.ccDepth) + "\n"
           "cc_resonance=" + std::to_string(cp.ccResonance) + "\n"
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
        else if (key == "sensitivity"     && parseFloat(value, v)) { p.sensitivity = v; }
        else if (key == "depth"           && parseFloat(value, v)) { p.depth = v; }
        else if (key == "resonance"       && parseFloat(value, v)) { p.resonance = v; }
        else if (key == "base_freq"       && parseFloat(value, v)) { p.baseFreq = v; }
        else if (key == "division"        && parseFloat(value, v)) { p.division = v; }
        else if (key == "gate_beats"      && parseFloat(value, v)) { p.gateBeats = v; }
        else if (key == "attack"          && parseFloat(value, v)) { p.attack = v; }
        else if (key == "decay"           && parseFloat(value, v)) { p.decay = v; }
        else if (key == "sustain"         && parseFloat(value, v)) { p.sustain = v; }
        else if (key == "release"         && parseFloat(value, v)) { p.release = v; }
        else if (key == "invert"          && parseFloat(value, v)) { p.invert = v; }
        else if (key == "mix"             && parseFloat(value, v)) { p.mix = v; }
        else if (key == "trim"            && parseFloat(value, v)) { p.trim = v; }
        else if (key == "bypass"          && parseFloat(value, v)) { p.bypass = v; }
        else if (key == "cc_sensitivity"  && parseFloat(value, v)) { p.ccSensitivity = v; }
        else if (key == "cc_depth"        && parseFloat(value, v)) { p.ccDepth = v; }
        else if (key == "cc_resonance"    && parseFloat(value, v)) { p.ccResonance = v; }
        else if (key == "cc_mix"          && parseFloat(value, v)) { p.ccMix = v; }
        else if (key == "cc_channel"      && parseFloat(value, v)) { p.ccChannel = v; }
        else { return std::nullopt; }
    }
    return clampParameters(p);
}

}  // namespace downspout::helterskelter
