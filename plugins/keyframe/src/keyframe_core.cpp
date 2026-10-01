#include "keyframe_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace downspout::keyframe {
namespace {

constexpr std::int64_t kRingMask = kRingCapacity - 1;

// Quadratic B-spline kernel from the paper's derivative section. The kernel is
// what gives the derivative estimate its zero at Nyquist, so no trig or table
// lookup is needed at runtime.
constexpr float kSpline0 = 1.0f / 2.0f;

// h10(t) from the paper's zero-tangent Hermite basis (eq. 8/11): the weight on
// the *later* keyframe. h00 = 1 - h10. Both are 0 at t = 0 and 1 at t = 1, so
// the weight rises from the earlier keyframe to the later one.
[[nodiscard]] float hermiteLaterWeight(const float t) noexcept
{
    const float t2 = t * t;
    return t2 * (3.0f - 2.0f * t);
}

[[nodiscard]] float clamped(const float value, const float lo, const float hi) noexcept
{
    return std::max(lo, std::min(value, hi));
}

[[nodiscard]] double clamp01(const double value) noexcept
{
    return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] const Keyframe& ringAt(const EngineState& state, const std::int64_t index) noexcept
{
    return state.kf[static_cast<std::size_t>(index & kRingMask)];
}

// Cubic (Catmull-Rom) evaluation of the history at a fractional index.
// `base` is the history index that t = 0 sits on, so t = 0 reads h[base] and
// t = 1 reads h[base + 1], with the two outer samples taken either side.
// Index k of the history holds sample n-k. Used to read the channel value at a
// subsample extremum position (paper eq. 6).
[[nodiscard]] float interpolateHistory(const std::array<float, 6>& h, const std::size_t base,
                                       const float t) noexcept
{
    const float p0 = h[base - 1];
    const float p1 = h[base];
    const float p2 = h[base + 1];
    const float p3 = h[base + 2];
    const float a = 2.0f * p1;
    const float b = p2 - p0;
    const float c = 2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3;
    const float d = -p0 + 3.0f * p1 - 3.0f * p2 + p3;
    return 0.5f * (a + b * t + c * t * t + d * t * t * t);
}

void pushKeyframe(EngineState& state, const double pos, const float l, const float r)
{
    state.kf[static_cast<std::size_t>(state.kfWrite & kRingMask)] = Keyframe{pos, l, r};
    ++state.kfWrite;
    state.newestPos = pos;
}

// Newest analysed position, which is the ceiling every playhead is held below.
// Tracked explicitly because kfWrite is incremented on the way out of
// pushKeyframe, so re-deriving it from the write index would read a slot that
// is about to be overwritten rather than the one just written.
[[nodiscard]] double newestPosition(const EngineState& state) noexcept
{
    return state.newestPos;
}

// ── Analysis (paper Algorithm 1) ────────────────────────────────────────────
//
// The paper analyses one signal. We analyse the mid channel so both channels
// share one sparse time base and the stereo image survives the stretch; the
// keyframe values are then read per channel at the same subsample position.
//
// History index k holds sample n-k, so hist[0] is x[n-5] and hist[5] is x[n].
void analyseSample(EngineState& state, const float l, const float r, const float epsilon)
{
    const float mid = 0.5f * (l + r);

    for (std::size_t i = 0; i < 5; ++i)
    {
        state.histL[i] = state.histL[i + 1];
        state.histR[i] = state.histR[i + 1];
        state.histM[i] = state.histM[i + 1];
    }
    state.histL[5] = l;
    state.histR[5] = r;
    state.histM[5] = mid;

    const double n = state.inputIndex;
    state.inputIndex += 1.0;

    if (!state.haveDerivative)
    {
        // Anchor: the first sample is always saved, as in Algorithm 1 line 2.
        state.dPrev = 0.0f;
        state.dPrevSign = 0;
        state.vPrev = mid;
        state.haveDerivative = true;
        pushKeyframe(state, 0.0, l, r);
    }
    else
    {
        // Bandlimited derivative of §2.1: the quadratic B-spline kernel
        // [1, 2, 1]/4 is applied to the signal and the result differenced,
        // giving a three-tap FIR with a zero at Nyquist. Centred on sample n-1,
        // which is (x[n-2] - x[n])/2 = (hist[3] - hist[5])/2. Centring it on
        // n-1 rather than n keeps the zero crossing between samples n-2 and
        // n-1, which is where the position formula below places it.
        const float d = kSpline0 * (state.histM[3] - state.histM[5]);

        // The derivative passes through exactly zero at every extremum, so its
        // sign goes positive -> zero -> negative over three samples. Testing the
        // raw sign would therefore miss every crossing, and the buffer would
        // degenerate to block boundaries alone. Latch the last non-zero sign and
        // treat any change of that latch as the crossing.
        const int sign = (d > 0.0f) - (d < 0.0f);
        const bool crossed = sign != 0 && state.dPrevSign != 0 && sign != state.dPrevSign;

        if (crossed)
        {
            // Reverse linear interpolation of the derivative to find the
            // subsample index where it crosses zero (paper eq. 4/5). The crossing
            // lies between samples n-2 and n-1; alpha is the fraction of the way
            // from n-2.
            const float a = std::fabs(state.dPrev);
            const float b = std::fabs(d);
            const float sum = a + b;
            const float alpha = sum > 0.0f ? std::clamp(a / sum, 0.0f, 1.0f) : 0.5f;
            const double nm = (n - 2.0) + alpha;
            const float vl = interpolateHistory(state.histL, 3, alpha);
            const float vr = interpolateHistory(state.histR, 3, alpha);
            const float vm = 0.5f * (vl + vr);

            // The difference threshold of §2.2 is a deadband measured against
            // the amplitude of the last saved keyframe. vPrev is advanced for
            // every crossing, not only accepted ones: leaving it pointing at an
            // older keyframe would make the same candidate fail the threshold
            // and be reconsidered on every subsequent crossing.
            if (std::fabs(vm - state.vPrev) > epsilon && nm > state.newestPos)
            {
                pushKeyframe(state, nm, vl, vr);
                state.vPrev = vm;
            }
        }

        state.dPrev = d;
        // Keep the latched sign, not the instantaneous one, so the latch is only
        // updated on a genuine non-zero sample.
        if (sign != 0)
            state.dPrevSign = sign;
    }

    // Block-boundary keyframes: enforce a baseline uniform sample rate on the
    // sparse buffer so real-time analysis and playback can run one after the
    // other (paper §2.7), and so a silent passage still yields windows. Only
    // written when it is genuinely needed. If extrema already cover the instant
    // there is nothing to enforce, and forcing one anyway would interpolate
    // across the samples either side of a point where the signal is curving,
    // which is exactly where it would distort the reconstruction.
    state.blockCounter += 1;
    if (state.blockCounter >= kAnalysisBlock)
    {
        state.blockCounter = 0;
        // Only write the baseline keyframe when the signal has actually gone quiet
        // since the last one. The threshold is a quarter of the block, so a
        // forced keyframe never lands within ~128 samples of an extremum: two
        // keyframes that close together force a near-vertical interpolation
        // across whatever curvature lies between them, which is exactly where
        // the reconstruction would kink.
        if (n > state.newestPos + kAnalysisBlock * 0.25)
        {
            pushKeyframe(state, n, l, r);
            // The deadband reference has to follow the written keyframe, or the
            // next crossing is measured against a stale amplitude and gets
            // saved with a value that does not match its position.
            state.vPrev = 0.5f * (l + r);
        }
    }
}

// ── Playback (paper Algorithm 3) ────────────────────────────────────────────

// Locate the window for a continuous sparse position: the pair (m, m+1) with
// n_m <= pos < n_{m+1}. Linear scan from the last valid m, which is
// amortized O(1) because the playhead moves sequentially.
void updateWindow(const EngineState& state, std::int64_t& m, const double pos)
{
    if (state.kfWrite < 2)
    {
        m = 0;
        return;
    }

    m = std::clamp(m, std::int64_t{0}, state.kfWrite - 2);
    while (m + 2 < state.kfWrite && ringAt(state, m + 1).pos <= pos)
        ++m;
    while (m > 0 && ringAt(state, m).pos > pos)
        --m;
}

[[nodiscard]] float interpolate(const EngineState& state, std::int64_t& m, const double pos, const bool right)
{
    if (state.kfWrite < 2)
        return 0.0f;

    updateWindow(state, m, pos);
    const Keyframe& a = ringAt(state, m);
    const Keyframe& b = ringAt(state, m + 1);
    const double span = b.pos - a.pos;
    const float t = span > 1.0e-9 ? static_cast<float>((pos - a.pos) / span) : 0.0f;
    // h10 on the later keyframe, h00 on the earlier one.
    const float wLater = hermiteLaterWeight(clamped(t, 0.0f, 1.0f));
    const float wEarlier = 1.0f - wLater;
    return right ? (a.r * wEarlier + b.r * wLater) : (a.l * wEarlier + b.l * wLater);
}

}  // namespace

float epsilonFromDb(const float decibels) noexcept
{
    return std::pow(10.0f, decibels / 20.0f);
}

Parameters clampParameters(const Parameters& raw) noexcept
{
    Parameters p = raw;
    p.time = clamped(p.time, 0.25f, 1.00f);
    p.pitch = clamped(p.pitch, 0.25f, 4.00f);
    p.splice = std::round(clamped(p.splice, 2.0f, 64.0f));
    p.threshold = clamped(p.threshold, -96.0f, -24.0f);
    p.maxSplice = clamped(p.maxSplice, 20.0f, 500.0f);
    p.hold = std::round(clamped(p.hold, 0.0f, 1.0f));
    p.mix = clamped(p.mix, 0.0f, 1.0f);
    p.width = clamped(p.width, 0.0f, 1.0f);
    p.level = clamped(p.level, 0.0f, 1.0f);
    return p;
}

void activate(EngineState& state, const double sampleRate)
{
    state = EngineState{};
    state.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const float tau = 0.010f * static_cast<float>(state.sampleRate);
    state.smoothCoeff = tau > 1.0f ? 1.0f / tau : 1.0f;
    state.initialized = true;
}

void processBlock(EngineState& state,
                  const Parameters& paramsIn,
                  const std::uint32_t nframes,
                  const float* const* inputs,
                  float* const* outputs) noexcept
{
    const Parameters params = clampParameters(paramsIn);
    if (!state.initialized)
        activate(state, state.sampleRate);

    const float* inL = inputs && inputs[0] ? inputs[0] : nullptr;
    const float* inR = inputs && inputs[1] ? inputs[1] : nullptr;
    float* outL = outputs ? outputs[0] : nullptr;
    float* outR = outputs ? outputs[1] : nullptr;
    if (!outL || !outR)
        return;

    const float epsilon = epsilonFromDb(params.threshold);
    state.hold = params.hold > 0.5f;
    const double tau = static_cast<double>(state.sTime);
    const double sigma = static_cast<double>(state.sPitch);
    const std::int64_t leash = static_cast<std::int64_t>(params.splice);
    const double maxSpliceSamples = static_cast<double>(params.maxSplice) * 0.001 * state.sampleRate;
    const float c = state.smoothCoeff;
    const std::int64_t kfBefore = state.kfWrite;
    bool tripped = false;

    for (std::uint32_t n = 0; n < nframes; ++n)
    {
        state.sTime += c * (params.time - state.sTime);
        state.sPitch += c * (params.pitch - state.sPitch);
        state.sMix += c * (params.mix - state.sMix);
        state.sWidth += c * (params.width - state.sWidth);
        state.sLevel += c * (params.level - state.sLevel);

        const float xL = inL ? inL[n] : 0.0f;
        const float xR = inR ? inR[n] : 0.0f;

        // Analysis first: the playheads may only read what this block produced.
        analyseSample(state, xL, xR, epsilon);

        // Dry path, delayed by the reported latency so dry and wet agree.
        state.dry[0][static_cast<std::size_t>(state.dryWrite & (kDryRing - 1))] = xL;
        state.dry[1][static_cast<std::size_t>(state.dryWrite & (kDryRing - 1))] = xR;
        const int dryRead = ((state.dryWrite - kLatencySamples) % kDryRing + kDryRing) % kDryRing;
        const float dryL = state.dry[0][static_cast<std::size_t>(dryRead)];
        const float dryR = state.dry[1][static_cast<std::size_t>(dryRead)];
        state.dryWrite = (state.dryWrite + 1) % kDryRing;

        float wetL = 0.0f;
        float wetR = 0.0f;

        // Nothing moves until the analysis holds a full depth window. The reference is
        // pinned at zero while the buffer fills, so if the playhead were allowed
        // to run during that time it would sit at the write head and open a
        // full-window drift the moment audio starts, firing a splice immediately.
        // Both playheads therefore start together at position 0, which is the
        // paper's initial state. This is the reported latency: the first
        // sounding output sample is the one that reads input sample 0.
        if (!state.primed && state.kfWrite >= 2 && state.inputIndex > kLatencySamples)
        {
            state.primed = true;
            state.refPos = 0.0;
            state.playPos = 0.0;
            state.mRef = 0;
            state.mPlay = 0;
            state.mTemp = 0;
        }

        if (state.primed && state.kfWrite >= 2)
        {
            const double newest = newestPosition(state);
            // The reference advances from position 0 at the time rate, so at
            // unity it tracks the write head exactly kLatencySamples behind it —
            // that fixed offset is the reported latency and what the dry path is
            // delayed by. Slowing down lets the gap widen, which is the whole
            // point, so the only bound is a floor: past kMaxDepthSamples the
            // reference rides along and the engine loops the material it holds
            // rather than backing up without limit.
            const double refFloor = std::max(0.0, newest - kMaxDepthSamples);

            // The leash in samples: the time span of K keyframes ahead of the
            // reference, which is the splice duration.
            updateWindow(state, state.mRef, state.refPos);
            const std::int64_t aheadIndex = std::min(state.mRef + leash, state.kfWrite - 1);
            double leashSamples = ringAt(state, aheadIndex).pos - ringAt(state, state.mRef).pos;
            leashSamples = std::clamp(leashSamples, kMinSpliceSamples,
                                      std::max(kMinSpliceSamples, maxSpliceSamples));

            if (state.hold)
            {
                // Frozen reference: the playhead keeps moving at the pitch rate
                // and every splice pulls it back to the same point, so the
                // passage sustains indefinitely.
                state.refPos = std::max(state.refPos, refFloor);
            }
            else
            {
                state.refPos = std::max(state.refPos + tau, refFloor);
            }

            // The playhead runs free at the pitch rate and is bounded only by the write
            // head. It must be allowed below the reference as well as above it:
            // at a pitch rate under unity the playhead legitimately lags, and
            // that negative drift is exactly what a splice later corrects. A
            // clamp against the reference would pin it there and silently turn
            // every pitch setting into the time setting.
            const double playLimit = std::max(newest - 1.0, 0.0);
            state.playPos = std::clamp(state.playPos, 0.0, playLimit);

            updateWindow(state, state.mPlay, state.playPos);

            const std::int64_t drift = state.mPlay - state.mRef;
            state.drift = static_cast<float>(std::clamp<double>(
                drift, kParameterSpecs[static_cast<std::size_t>(ParamId::outDrift)].minimum,
                kParameterSpecs[static_cast<std::size_t>(ParamId::outDrift)].maximum));

            const bool needSplice = !state.splicing && std::llabs(drift) > leash;
            if (needSplice)
            {
                // Init the temporary playhead at the reference and crossfade
                // into it over the span of K keyframes ahead of the reference.
                state.tempPos = state.refPos;
                updateWindow(state, state.mTemp, state.tempPos);
                double length = leashSamples;
                if (length > newest - state.tempPos)
                    length = std::max(1.0, newest - state.tempPos);
                state.tDot = 1.0 / length;
                state.tTemp = 0.0;
                state.splicing = true;
            }

            if (state.splicing && state.tTemp <= 1.0)
            {
                const float ypL = interpolate(state, state.mPlay, state.playPos, false);
                const float ypR = interpolate(state, state.mPlay, state.playPos, true);
                float ytL = 0.0f;
                float ytR = 0.0f;
                if (state.tempPos < newest)
                {
                    updateWindow(state, state.mTemp, state.tempPos);
                    ytL = interpolate(state, state.mTemp, state.tempPos, false);
                    ytR = interpolate(state, state.mTemp, state.tempPos, true);
                }
                else
                {
                    ytL = ypL;
                    ytR = ypR;
                }
                const float t = static_cast<float>(clamp01(state.tTemp));
                wetL = ytL * t + ypL * (1.0f - t);
                wetR = ytR * t + ypR * (1.0f - t);

                // Advance: every playhead moves exactly once per output sample,
                // and the splice phase advances at the pitch rate, so the
                // crossfade takes L/sigma output samples (paper §2.6).
                state.playPos = std::min(state.playPos + sigma, playLimit);
                state.tempPos = std::min(state.tempPos + sigma, std::max(newest - 1.0, state.refPos));
                state.tTemp += state.tDot * sigma;
            }
            else
            {
                if (state.splicing)
                {
                    // Splice finished: the playhead adopts the temporary
                    // position and window, and normal playback resumes.
                    state.playPos = state.tempPos;
                    state.mPlay = state.mTemp;
                    state.splicing = false;
                    ++state.spliceCount;
                    state.spliceLamp = 1.0f;
                }
                wetL = interpolate(state, state.mPlay, state.playPos, false);
                wetR = interpolate(state, state.mPlay, state.playPos, true);
                state.playPos = std::min(state.playPos + sigma, playLimit);
            }
        }

        const float mL = (1.0f - state.sMix) * dryL + state.sMix * wetL;
        const float mR = (1.0f - state.sMix) * dryR + state.sMix * wetR;
        const float mid = 0.5f * (mL + mR);
        const float side = 0.5f * (mL - mR) * (1.0f + state.sWidth);

        // Leaky integrator DC block; the estimate is kept in the state fields.
        constexpr float kDcA = 0.9995f;
        state.dcL += (1.0f - kDcA) * (mL - state.dcL);
        state.dcR += (1.0f - kDcA) * (mR - state.dcR);

        // Output is unity at Level = 1.00, so the level control is a plain
        // gain the user can reason about.
        const float oL = (mid + side - state.dcL) * state.sLevel;
        const float oR = (mid - side - state.dcR) * state.sLevel;

        if (!std::isfinite(oL) || !std::isfinite(oR) || !std::isfinite(wetL) || !std::isfinite(wetR))
            tripped = true;
        if (std::fabs(oL) > 0.98f || std::fabs(oR) > 0.98f)
            state.clipLamp = 1.0f;

        outL[n] = tripped ? 0.0f : clamped(oL, -1.2f, 1.2f);
        outR[n] = tripped ? 0.0f : clamped(oR, -1.2f, 1.2f);
    }

    // Keyframe density, keyframes per second, smoothed.
    if (nframes > 0 && state.sampleRate > 0.0)
    {
        const double added = static_cast<double>(state.kfWrite - kfBefore);
        const double rate = added * state.sampleRate / static_cast<double>(nframes);
        state.density += 0.02f * static_cast<float>(rate - state.density);
    }
    state.spliceLamp *= std::pow(0.9995f, static_cast<float>(nframes));
    state.clipLamp *= std::pow(0.9995f, static_cast<float>(nframes));

    if (tripped)
    {
        const float st = state.sTime, sp = state.sPitch, sm = state.sMix;
        const float sw = state.sWidth, sl = state.sLevel;
        activate(state, state.sampleRate);
        state.sTime = st;
        state.sPitch = sp;
        state.sMix = sm;
        state.sWidth = sw;
        state.sLevel = sl;
        state.clipLamp = 1.0f;
    }
}

float keyframesPerSecond(const EngineState& state) noexcept { return state.density; }
float playheadDrift(const EngineState& state) noexcept { return state.drift; }
float spliceLamp(const EngineState& state) noexcept { return state.spliceLamp; }
std::uint64_t spliceCount(const EngineState& state) noexcept { return state.spliceCount; }
float clipLamp(const EngineState& state) noexcept { return state.clipLamp; }

std::int64_t keyframeCount(const EngineState& state) noexcept { return state.kfWrite; }

bool keyframeAt(const EngineState& state, const std::int64_t index, double& pos, float& l, float& r) noexcept
{
    if (index < 0 || index >= state.kfWrite)
        return false;
    const Keyframe& k = ringAt(state, index);
    pos = k.pos;
    l = k.l;
    r = k.r;
    return true;
}

// ── Text state (version=1 key=value, magneto/primefold shape) ───────────────

std::string serializeParameters(const Parameters& params)
{
    std::string text = "version=1\n";
    auto line = [&](const char* k, const float v) {
        text += k;
        text += '=';
        text += std::to_string(v);
        text += '\n';
    };
    line("time", params.time);
    line("pitch", params.pitch);
    line("splice", params.splice);
    line("threshold", params.threshold);
    line("max_splice", params.maxSplice);
    line("hold", params.hold);
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
            if (nl == std::string::npos)
                break;
            continue;
        }
        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos)
            return std::nullopt;
        const std::string_view key = line.substr(0, sep);
        const std::string_view val = line.substr(sep + 1);
        if (key == "version")
            continue;
        std::string buf(val);
        char* e = nullptr;
        const float v = std::strtof(buf.c_str(), &e);
        if (e == nullptr || *e != '\0')
            return std::nullopt;
        if (key == "time") p.time = v;
        else if (key == "pitch") p.pitch = v;
        else if (key == "splice") p.splice = v;
        else if (key == "threshold") p.threshold = v;
        else if (key == "max_splice") p.maxSplice = v;
        else if (key == "hold") p.hold = v;
        else if (key == "mix") p.mix = v;
        else if (key == "width") p.width = v;
        else if (key == "level") p.level = v;
        else return std::nullopt;
        if (nl == std::string::npos)
            break;
    }
    return clampParameters(p);
}

}  // namespace downspout::keyframe