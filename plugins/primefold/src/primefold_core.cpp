#include "primefold_core.hpp"
#include "primefold_params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace downspout::primefold {
namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] float fastTanh(float x) noexcept
{
    return std::tanh(x);
}

// Linear-interpolated read at absolute input-time `pos` (monotonic int64
// write clock; mask handles the ring wrap; all callers keep pos >= 0).
[[nodiscard]] float readRingAt(const std::array<float, kRingSize>& ring, double pos) noexcept
{
    const auto i0 = static_cast<std::int64_t>(std::floor(pos));
    const float frac = static_cast<float>(pos - static_cast<double>(i0));
    const auto m = static_cast<std::int64_t>(kRingMask);
    const float a = ring[static_cast<std::size_t>(i0 & m)];
    const float b = ring[static_cast<std::size_t>((i0 + 1) & m)];
    return a + frac * (b - a);
}

// Normalized correlation between the pre-jump context ending at `pre` and
// the candidate region ending at `post`, averaged across channels (mid).
// Returns [-1, 1]; -2 when either side is silent.
[[nodiscard]] float matchScore(const std::array<std::array<float, kRingSize>, 2>& ring,
                              double pre, double post) noexcept
{
    double num = 0.0, denA = 0.0, denB = 0.0;
    for (int k = 0; k < kSolaContext; ++k)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const float p = readRingAt(ring[ch], pre - (kSolaContext - 1) + k);
            const float q = readRingAt(ring[ch], post - (kSolaContext - 1) + k);
            num += static_cast<double>(p) * q;
            denA += static_cast<double>(p) * p;
            denB += static_cast<double>(q) * q;
        }
    }
    if (denA < 1e-12 || denB < 1e-12)
        return -2.0f;
    return static_cast<float>(num / std::sqrt(denA * denB));
}

}  // namespace

Parameters clampParameters(const Parameters& raw) noexcept
{
    Parameters p = raw;
    p.dry = clampf(p.dry, 0.0f, 1.0f);
    p.level2 = clampf(p.level2, 0.0f, 1.0f);
    p.level3 = clampf(p.level3, 0.0f, 1.0f);
    p.level5 = clampf(p.level5, 0.0f, 1.0f);
    p.feedback = clampf(p.feedback, 0.0f, 0.90f);
    p.damp = clampf(p.damp, 0.0f, 1.0f);
    p.grain = clampf(std::round(p.grain), 0.0f, 2.0f);
    p.mix = clampf(p.mix, 0.0f, 1.0f);
    p.width = clampf(p.width, 0.0f, 1.0f);
    p.level = clampf(p.level, 0.0f, 1.0f);
    return p;
}

void activate(EngineState& state, double sampleRate, int window)
{
    state = EngineState{};
    state.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    state.window = std::clamp(window, 512, kMaxWindow);
    const float tau = 0.010f * static_cast<float>(state.sampleRate);
    state.smoothCoeff = tau > 1.0f ? 1.0f / tau : 1.0f;
    // Per-voice anti-alias low-pass at fs/2N (RBJ cookbook, Q = 1/sqrt(2)).
    for (int v = 0; v < 3; ++v)
    {
        SolaVoice& voice = state.voices[v];
        const double fc = state.sampleRate / (2.0 * kVoiceRatios[v]);
        const double w0 = 2.0 * kPi * fc / state.sampleRate;
        const double alpha = std::sin(w0) / std::sqrt(2.0);
        const double cw = std::cos(w0);
        const double a0 = 1.0 + alpha;
        voice.b0 = static_cast<float>((1.0 - cw) / 2.0 / a0);
        voice.b1 = static_cast<float>((1.0 - cw) / a0);
        voice.b2 = voice.b0;
        voice.a1 = static_cast<float>(-2.0 * cw / a0);
        voice.a2 = static_cast<float>((1.0 - alpha) / a0);
        voice.readPos = static_cast<double>(voice.writePos - dryAlignDelay(state.window));
    }
    state.initialized = true;
}

void activate(EngineState& state, double sampleRate)
{
    activate(state, sampleRate, state.window >= 512 ? state.window : 1024);
}

void processBlock(EngineState& state,
                  const Parameters& paramsIn,
                  std::uint32_t nframes,
                  const float* const* inputs,
                  float* const* outputs) noexcept
{
    const Parameters params = clampParameters(paramsIn);
    if (!state.initialized)
        activate(state, state.sampleRate, state.window);

    const int wantWindow = kGrainSamples[static_cast<std::size_t>(params.grain)];
    if (wantWindow != state.window)
    {
        const double sr = state.sampleRate;
        activate(state, sr, wantWindow);
    }

    const float* inL = inputs && inputs[0] ? inputs[0] : nullptr;
    const float* inR = inputs && inputs[1] ? inputs[1] : nullptr;
    float* outL = outputs ? outputs[0] : nullptr;
    float* outR = outputs ? outputs[1] : nullptr;
    if (!outL || !outR)
        return;

    const int dryLen = dryAlignDelay(state.window);
    const float c = state.smoothCoeff;
    bool tripped = false;

    for (std::uint32_t n = 0; n < nframes; ++n)
    {
        state.sDry += c * (params.dry - state.sDry);
        state.sL2 += c * (params.level2 - state.sL2);
        state.sL3 += c * (params.level3 - state.sL3);
        state.sL5 += c * (params.level5 - state.sL5);
        state.sFb += c * (params.feedback - state.sFb);
        state.sDamp += c * (params.damp - state.sDamp);
        state.sMix += c * (params.mix - state.sMix);
        state.sWidth += c * (params.width - state.sWidth);
        state.sLevel += c * (params.level - state.sLevel);

        const float xL = inL ? inL[n] : 0.0f;
        const float xR = inR ? inR[n] : 0.0f;
        const float in[2] = {xL, xR};

        // Dry alignment delay (nominal voice average delay): the
        // host-compensatable feedforward path.
        state.dryDelay[0][static_cast<std::size_t>(state.dryWrite) & static_cast<std::size_t>(kRingMask)] = in[0];
        state.dryDelay[1][static_cast<std::size_t>(state.dryWrite) & static_cast<std::size_t>(kRingMask)] = in[1];
        const int dryRead = state.dryWrite - dryLen;
        const float dryL = state.dryDelay[0][static_cast<std::size_t>(dryRead) & static_cast<std::size_t>(kRingMask)];
        const float dryR = state.dryDelay[1][static_cast<std::size_t>(dryRead) & static_cast<std::size_t>(kRingMask)];
        state.dryWrite = (state.dryWrite + 1) & kRingMask;

        // Feedback taps: explicit D-sample delay per voice, never compensated.
        float fbTap[3][2];
        for (int v = 0; v < 3; ++v)
            for (int ch = 0; ch < 2; ++ch)
            {
                const int rd = (state.fbWrite[v][ch] - kFeedbackDelay + kFbRing * 4) % kFbRing;
                fbTap[v][ch] = state.fbDelay[v][ch][static_cast<std::size_t>(rd)];
            }

        const float lpA = 1.0f - state.sDamp * 0.95f;
        float fbSum[2] = {0.0f, 0.0f};
        for (int v = 0; v < 3; ++v)
            for (int ch = 0; ch < 2; ++ch)
            {
                state.fbLp[v][ch] += lpA * (fbTap[v][ch] - state.fbLp[v][ch]);
                fbSum[ch] += clampf(state.fbLp[v][ch], -2.0f, 2.0f);
            }

        float yVoice[3][2];
        for (int v = 0; v < 3; ++v)
        {
            const float ratio = kVoiceRatios[v];
            SolaVoice& voice = state.voices[v];

            // Voice inputs: dry + contractive recirculation.
            float u[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                float s = in[ch] + state.sFb * kFanIn * fastTanh(fbSum[ch]);
                s = clampf(s, -2.0f, 2.0f);
                // Anti-alias LP at fs/2N before the memory write.
                const float y = voice.b0 * s + voice.b1 * voice.zx1[ch] + voice.b2 * voice.zx2[ch]
                              - voice.a1 * voice.zy1[ch] - voice.a2 * voice.zy2[ch];
                voice.zx2[ch] = voice.zx1[ch];
                voice.zx1[ch] = s;
                voice.zy2[ch] = voice.zy1[ch];
                voice.zy1[ch] = std::isfinite(y) ? y : 0.0f;
                u[ch] = voice.zy1[ch];
                voice.ring[ch][static_cast<std::size_t>(voice.writePos) & static_cast<std::size_t>(kRingMask)] = u[ch];
            }

            // Single-tap read at N samples per sample: exact ratio, no glide.
            const double delay = static_cast<double>(voice.writePos) - voice.readPos;
            const double trigger = static_cast<double>(kSolaFade) * (ratio - 1.0) + 8.0;
            if (voice.ghostLeft == 0 && delay < trigger)
            {
                // Correlation-matched jump-back shared across channels.
                const double jump = static_cast<double>(voiceJumpBack(state.window, ratio));
                double best = -1e99;
                int bestDelta = 0;
                for (int dl = -kSolaSearch; dl <= kSolaSearch; ++dl)
                {
                    const float score = matchScore(voice.ring, voice.readPos, voice.readPos - jump + dl);
                    if (score > best)
                    {
                        best = score;
                        bestDelta = dl;
                    }
                }
                voice.ghostPos = voice.readPos;
                voice.ghostLeft = kSolaFade;
                voice.readPos = voice.readPos - jump + bestDelta;
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                float out;
                if (voice.ghostLeft > 0)
                {
                    const double t01 = 1.0 - static_cast<double>(voice.ghostLeft) / kSolaFade;
                    const float wg = 0.5f + 0.5f * static_cast<float>(std::cos(kPi * std::clamp(t01, 0.0, 1.0)));
                    out = wg * readRingAt(voice.ring[ch], voice.ghostPos)
                        + (1.0f - wg) * readRingAt(voice.ring[ch], voice.readPos);
                }
                else
                {
                    out = readRingAt(voice.ring[ch], voice.readPos);
                }
                yVoice[v][ch] = std::isfinite(out) ? out : 0.0f;
            }
            if (voice.ghostLeft > 0)
            {
                voice.ghostPos += ratio;
                voice.ghostLeft--;
            }
            voice.readPos += ratio;
            voice.writePos += 1;

            for (int ch = 0; ch < 2; ++ch)
            {
                const float yc = clampf(yVoice[v][ch], -2.0f, 2.0f);
                state.fbDelay[v][ch][static_cast<std::size_t>(state.fbWrite[v][ch])] = yc;
                state.fbWrite[v][ch] = (state.fbWrite[v][ch] + 1) % kFbRing;
            }
        }

        float wetL = state.sL2 * yVoice[0][0] + state.sL3 * yVoice[1][0] + state.sL5 * yVoice[2][0];
        float wetR = state.sL2 * yVoice[0][1] + state.sL3 * yVoice[1][1] + state.sL5 * yVoice[2][1];
        wetL = fastTanh(wetL);
        wetR = fastTanh(wetR);
        float wetOutL, wetOutR, dryOutL, dryOutR;
        {
            // Leaky-integrator DC subtraction; LP estimate kept in dc fields.
            constexpr float kDcA = 0.9990f;
            state.dcWetL += (1.0f - kDcA) * (wetL - state.dcWetL);
            state.dcWetR += (1.0f - kDcA) * (wetR - state.dcWetR);
            state.dcDryL += (1.0f - kDcA) * (dryL - state.dcDryL);
            state.dcDryR += (1.0f - kDcA) * (dryR - state.dcDryR);
            wetOutL = wetL - state.dcWetL;
            wetOutR = wetR - state.dcWetR;
            dryOutL = (dryL - state.dcDryL) * state.sDry;
            dryOutR = (dryR - state.dcDryR) * state.sDry;
        }

        const float mL = (1.0f - state.sMix) * dryOutL + state.sMix * wetOutL;
        const float mR = (1.0f - state.sMix) * dryOutR + state.sMix * wetOutR;
        const float mid = 0.5f * (mL + mR);
        const float side = 0.5f * (mL - mR) * (1.0f + state.sWidth);
        float oL = (mid + side) * (state.sLevel * 1.5f);
        float oR = (mid - side) * (state.sLevel * 1.5f);
        oL = fastTanh(oL);
        oR = fastTanh(oR);

        if (!std::isfinite(oL) || !std::isfinite(oR) || !std::isfinite(wetL) || !std::isfinite(wetR))
            tripped = true;

        if (std::fabs(oL) > 0.98f || std::fabs(oR) > 0.98f)
            state.clipLamp = 1.0f;

        outL[n] = tripped ? 0.0f : clampf(oL, -1.2f, 1.2f);
        outR[n] = tripped ? 0.0f : clampf(oR, -1.2f, 1.2f);
    }

    state.clipLamp *= std::pow(0.9995f, static_cast<float>(nframes));
    if (tripped)
    {
        const double sr = state.sampleRate;
        const int w = state.window;
        const float sd = state.sDry, sl2 = state.sL2, sl3 = state.sL3, sl5 = state.sL5;
        const float sf = state.sFb, sdm = state.sDamp, sm = state.sMix, sw = state.sWidth, sl = state.sLevel;
        activate(state, sr, w);
        state.sDry = sd; state.sL2 = sl2; state.sL3 = sl3; state.sL5 = sl5;
        state.sFb = sf; state.sDamp = sdm; state.sMix = sm; state.sWidth = sw; state.sLevel = sl;
        state.clipLamp = 1.0f;
    }
}

float currentLatencySamples(const EngineState& state) noexcept
{
    return static_cast<float>(worstVoiceDelay(state.window));
}

float clipLamp(const EngineState& state) noexcept
{
    return state.clipLamp;
}

// ── Text state (version=1 key=value, magneto shape) ─────────────────────────

std::string serializeParameters(const Parameters& params)
{
    std::string text = "version=1\n";
    auto line = [&](const char* k, float v) {
        text += k;
        text += '=';
        text += std::to_string(v);
        text += '\n';
    };
    line("dry", params.dry);
    line("lvl2", params.level2);
    line("lvl3", params.level3);
    line("lvl5", params.level5);
    line("feedback", params.feedback);
    line("damp", params.damp);
    line("grain", params.grain);
    line("mix", params.mix);
    line("width", params.width);
    line("level", params.level);
    return text;
}

std::optional<Parameters> deserializeParameters(const std::string& text)
{
    Parameters p;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t nl = text.find('\n', start);
        const std::size_t end = nl == std::string::npos ? text.size() : nl;
        const std::string_view line(text.data() + start, end - start);
        start = end + 1;
        if (line.empty())
        {
            if (nl == std::string::npos) break;
            continue;
        }
        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) return std::nullopt;
        const std::string_view key = line.substr(0, sep);
        const std::string_view val = line.substr(sep + 1);
        if (key == "version") continue;
        std::string buf(val);
        char* e = nullptr;
        const float v = std::strtof(buf.c_str(), &e);
        if (e == nullptr || *e != '\0') return std::nullopt;
        if (key == "dry") p.dry = v;
        else if (key == "lvl2") p.level2 = v;
        else if (key == "lvl3") p.level3 = v;
        else if (key == "lvl5") p.level5 = v;
        else if (key == "feedback") p.feedback = v;
        else if (key == "damp") p.damp = v;
        else if (key == "grain") p.grain = v;
        else if (key == "mix") p.mix = v;
        else if (key == "width") p.width = v;
        else if (key == "level") p.level = v;
        else return std::nullopt;
        if (nl == std::string::npos) break;
    }
    return clampParameters(p);
}

}  // namespace downspout::primefold
