#include "chipper_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

namespace downspout::chipper {

Parameters clampParameters(const Parameters& p) noexcept
{
    auto safe = [](float v, float lo, float hi, float def) noexcept -> float {
        return std::isfinite(v) ? std::clamp(v, lo, hi) : def;
    };
    Parameters out = p;
    out.bitDepth   = std::round(safe(out.bitDepth,   1.0f,  16.0f,  8.0f));
    out.rateDiv    = std::round(safe(out.rateDiv,    1.0f,  64.0f,  8.0f));
    out.jitter     = safe(out.jitter,     0.0f,   1.0f,  0.0f);
    out.mix        = safe(out.mix,        0.0f, 100.0f, 100.0f);
    out.outputGain = safe(out.outputGain, -12.0f, 12.0f,  0.0f);
    out.ccBitDepth = std::round(safe(out.ccBitDepth, 0.0f, 127.0f, kDefaultCCBitDepth));
    out.ccRateDiv  = std::round(safe(out.ccRateDiv,  0.0f, 127.0f, kDefaultCCRateDiv));
    out.ccChannel  = std::round(safe(out.ccChannel,  1.0f,  16.0f,  1.0f));
    return out;
}

namespace {

uint32_t xorshift32(uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float quantizeSample(float sample, int bitDepth) noexcept
{
    if (bitDepth >= 16) return sample;
    const float levels = static_cast<float>(1 << (bitDepth - 1));
    return std::clamp(std::round(sample * levels) / levels, -1.0f, 1.0f);
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

}  // namespace

void processBlock(EngineState&        state,
                  const Parameters&   params,
                  uint32_t            frames,
                  const float* const* inputs,
                  float* const*       outputs,
                  float               effectiveBitDepth,
                  float               effectiveRateDiv) noexcept
{
    const auto cp       = clampParameters(params);
    const int  bitDepth = static_cast<int>(std::round(std::clamp(effectiveBitDepth, 1.0f, 16.0f)));
    const int  rateDiv  = static_cast<int>(std::round(std::clamp(effectiveRateDiv,  1.0f, 64.0f)));
    const float mixFrac = cp.mix * 0.01f;
    const float gain    = std::pow(10.0f, cp.outputGain / 20.0f);

    const int jitterMax = (cp.jitter > 0.0f)
        ? static_cast<int>(std::floor(cp.jitter * static_cast<float>(rateDiv) * 0.5f))
        : 0;

    for (uint32_t f = 0; f < frames; ++f) {
        if (state.holdCounter <= 0) {
            for (int c = 0; c < 2; ++c)
                state.heldSample[c] = quantizeSample(inputs[c][f], bitDepth);

            int nextHold = rateDiv;
            if (jitterMax > 0) {
                const uint32_t r = xorshift32(state.randState);
                const int offset = static_cast<int>(r % static_cast<uint32_t>(2 * jitterMax + 1)) - jitterMax;
                nextHold = std::max(1, rateDiv + offset);
            }
            state.holdCounter = nextHold;
        }

        for (int c = 0; c < 2; ++c) {
            const float dry = inputs[c][f];
            const float wet = state.heldSample[c];
            outputs[c][f] = gain * (dry * (1.0f - mixFrac) + wet * mixFrac);
        }

        --state.holdCounter;
    }
}

std::string serializeParameters(const Parameters& p)
{
    return "version=1\n"
           "bit_depth="   + std::to_string(p.bitDepth)   + "\n"
           "rate_div="    + std::to_string(p.rateDiv)    + "\n"
           "jitter="      + std::to_string(p.jitter)     + "\n"
           "mix="         + std::to_string(p.mix)        + "\n"
           "output_gain=" + std::to_string(p.outputGain) + "\n"
           "cc_bit_depth=" + std::to_string(p.ccBitDepth) + "\n"
           "cc_rate_div="  + std::to_string(p.ccRateDiv)  + "\n"
           "cc_channel="  + std::to_string(p.ccChannel)  + "\n";
}

std::optional<Parameters> deserializeParameters(const std::string& text)
{
    Parameters p;
    for (const std::string_view line : split(text, '\n')) {
        if (line.empty()) continue;
        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) return std::nullopt;
        const std::string_view key   = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);
        float v = 0.0f;
        if      (key == "version")      { continue; }
        else if (key == "bit_depth"    && parseFloat(value, v)) { p.bitDepth   = v; }
        else if (key == "rate_div"     && parseFloat(value, v)) { p.rateDiv    = v; }
        else if (key == "jitter"       && parseFloat(value, v)) { p.jitter     = v; }
        else if (key == "mix"          && parseFloat(value, v)) { p.mix        = v; }
        else if (key == "output_gain"  && parseFloat(value, v)) { p.outputGain = v; }
        else if (key == "cc_bit_depth" && parseFloat(value, v)) { p.ccBitDepth = v; }
        else if (key == "cc_rate_div"  && parseFloat(value, v)) { p.ccRateDiv  = v; }
        else if (key == "cc_channel"   && parseFloat(value, v)) { p.ccChannel  = v; }
        else { return std::nullopt; }
    }
    return clampParameters(p);
}

}  // namespace downspout::chipper
