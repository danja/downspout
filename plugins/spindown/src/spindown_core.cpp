#include "spindown_core.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::spindown {

namespace {

float clampf(const float v, const float lo, const float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Four-point Hermite between y0 and y1 for t in [0,1].
float hermite(const float ym1, const float y0, const float y1, const float y2, const float t) noexcept
{
    const float c0 = y0;
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

// The ring holds the input at writePos and older samples behind it. `back` is how far behind writePos to read (samples).
float readBack(const std::vector<float>& ring, const std::uint32_t mask, const std::uint32_t writePos, const double back) noexcept
{
    const auto n = static_cast<std::uint32_t>(back);
    const float f = static_cast<float>(back - static_cast<double>(n));
    const auto at = [&](const std::uint32_t k) { return ring[(writePos - k) & mask]; };
    const float ym1 = at(n > 0 ? n - 1 : 0);   // nothing is newer than the sample just written
    return hermite(ym1, at(n), at(n + 1), at(n + 2), f);
}

// Fade the tape out as it nears standstill so a held sample never leaves a DC step.
float speedGain(const float speed) noexcept
{
    const float x = clampf(speed / 0.04f, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

double logMs(const int cc) noexcept
{
    return static_cast<double>(kMinStopMs) * std::pow(static_cast<double>(kMaxStopMs / kMinStopMs), cc / 127.0);
}

}  // namespace

Parameters clampParameters(const Parameters& p) noexcept
{
    Parameters r = p;
    r.trigger = std::round(clampf(r.trigger, 0.0f, static_cast<float>(kTriggerCount - 1)));
    r.stop = r.stop >= 0.5f ? 1.0f : 0.0f;
    r.stopMs = clampf(r.stopMs, kMinStopMs, kMaxStopMs);
    r.startMs = clampf(r.startMs, kMinStopMs, kMaxStopMs);
    r.curve = clampf(r.curve, kMinCurve, kMaxCurve);
    r.cycle = std::round(clampf(r.cycle, 0.0f, static_cast<float>(kCycleCount - 1)));
    r.length = clampf(r.length, kMinLengthBeats, kMaxLengthBeats);
    r.ccChannel = std::round(clampf(r.ccChannel, 0.0f, 16.0f));
    return r;
}

int cycleBars(const float cycleIndex) noexcept
{
    static constexpr int kBars[kCycleCount] = {1, 2, 4, 8};
    return kBars[std::clamp(static_cast<int>(std::lround(cycleIndex)), 0, kCycleCount - 1)];
}

float speedFor(const double progress, const float curve) noexcept
{
    const double rest = std::clamp(1.0 - progress, 0.0, 1.0);
    const double s = std::pow(rest, static_cast<double>(curve));
    return s < 1.0e-6 ? 0.0f : static_cast<float>(s);
}

void prepare(EngineState& state, const float sampleRate)
{
    std::uint32_t size = 1024;
    const auto wanted = static_cast<std::uint32_t>(std::max(8000.0f, sampleRate) * (kBufferSeconds + 0.5f));
    while (size < wanted) size <<= 1;
    for (auto& r : state.ring) r.assign(size, 0.0f);
    state.mask = size - 1;
    resetState(state);
}

void resetState(EngineState& state) noexcept
{
    for (auto& r : state.ring) std::fill(r.begin(), r.end(), 0.0f);
    state.writePos = 0;
    state.delay = 0.0;
    state.progress = 0.0;
    state.live = 1.0f;
    state.heldNotes = 0;
    state.overrideActive.fill(false);
    state.meters = {};
}

void releaseOverride(EngineState& state, const int control) noexcept
{
    if (control >= 0 && control < kOverrideCount) state.overrideActive[static_cast<std::size_t>(control)] = false;
}

bool cycleStopped(const Parameters& p, const double beatPosition, const double beatsPerBar) noexcept
{
    const double bar = beatsPerBar > 0.0 ? beatsPerBar : kFallbackBeatsPerBar;
    const double cycleLen = bar * cycleBars(p.cycle);
    const double length = std::min(static_cast<double>(p.length), cycleLen);
    double pos = std::fmod(beatPosition, cycleLen);
    if (pos < 0.0) pos += cycleLen;
    return pos >= cycleLen - length;
}

void processBlock(EngineState& state, const Parameters& rawParams, const float sampleRate, const std::uint32_t frames,
                  const Transport& transport, const MidiEvent* events, const std::uint32_t eventCount,
                  const float* const* inputs, float* const* outputs) noexcept
{
    if (state.ring[0].empty() || state.ring[1].empty() || sampleRate <= 0.0f) {
        // Not prepared: pass through rather than guess.
        for (int c = 0; c < 2; ++c)
            if (outputs[c] != inputs[c]) std::copy(inputs[c], inputs[c] + frames, outputs[c]);
        return;
    }
    const Parameters params = clampParameters(rawParams);
    const auto trigger = static_cast<Trigger>(static_cast<int>(params.trigger));
    const double sr = static_cast<double>(sampleRate);
    const double bpm = transport.bpm > 0.0 ? transport.bpm : kFallbackBpm;
    const double beatsPerSample = bpm / (60.0 * sr);
    const bool timeKnown = transport.playing && transport.valid;
    const double delayCap = static_cast<double>(kBufferSeconds) * sr;
    const float crossfadeStep = static_cast<float>(1.0 / (static_cast<double>(kCrossfadeMs) * 0.001 * sr));

    float stopMs = state.overrideActive[0] ? state.overrideValue[0] : params.stopMs;
    float startMs = state.overrideActive[1] ? state.overrideValue[1] : params.startMs;
    float curve = state.overrideActive[2] ? state.overrideValue[2] : params.curve;
    float manualStop = state.overrideActive[3] ? state.overrideValue[3] : params.stop;

    std::uint32_t next = 0;
    for (std::uint32_t i = 0; i < frames; ++i) {
        while (next < eventCount && events[next].frame <= i) {
            const MidiEvent& e = events[next++];
            const std::uint8_t kind = e.status & 0xF0;
            const int channel = e.status & 0x0F;
            if (kind == 0x90 && e.data2 > 0) {
                ++state.heldNotes;
            } else if (kind == 0x80 || (kind == 0x90 && e.data2 == 0)) {
                state.heldNotes = std::max(0, state.heldNotes - 1);
            } else if (kind == 0xB0 && (e.data1 == 120 || e.data1 == 123)) {
                state.heldNotes = 0;
            } else if (kind == 0xB0 && params.ccChannel >= 1.0f && channel == static_cast<int>(params.ccChannel) - 1) {
                switch (e.data1) {
                case kCcStopTime:
                    state.overrideValue[0] = static_cast<float>(logMs(e.data2)); state.overrideActive[0] = true; stopMs = state.overrideValue[0]; break;
                case kCcStartTime:
                    state.overrideValue[1] = static_cast<float>(logMs(e.data2)); state.overrideActive[1] = true; startMs = state.overrideValue[1]; break;
                case kCcCurve:
                    state.overrideValue[2] = kMinCurve + (kMaxCurve - kMinCurve) * static_cast<float>(e.data2) / 127.0f;
                    state.overrideActive[2] = true; curve = state.overrideValue[2]; break;
                case kCcStop:
                    state.overrideValue[3] = e.data2 >= 64 ? 1.0f : 0.0f; state.overrideActive[3] = true; manualStop = state.overrideValue[3]; break;
                default: break;
                }
            }
        }

        bool engaged = false;
        switch (trigger) {
        case Trigger::Manual: engaged = manualStop >= 0.5f; break;
        case Trigger::Midi:   engaged = state.heldNotes > 0; break;
        case Trigger::Cycle:
            engaged = timeKnown && cycleStopped(params, transport.beatPosition + beatsPerSample * i, transport.beatsPerBar);
            break;
        }

        float in[2];
        for (int c = 0; c < 2; ++c) {
            in[c] = inputs[c][i];
            state.ring[c][state.writePos & state.mask] = in[c];
        }

        if (engaged) state.progress = std::min(1.0, state.progress + 1.0 / (static_cast<double>(clampf(stopMs, kMinStopMs, kMaxStopMs)) * 0.001 * sr));
        else         state.progress = std::max(0.0, state.progress - 1.0 / (static_cast<double>(clampf(startMs, kMinStopMs, kMaxStopMs)) * 0.001 * sr));
        const float speed = speedFor(state.progress, clampf(curve, kMinCurve, kMaxCurve));
        state.delay = std::clamp(state.delay + (1.0 - static_cast<double>(speed)), 0.0, delayCap);

        // `live` is 1 when the output is the live input. It drops as soon as the tape slows, and returns once it is up to
        // speed again, by a short crossfade from the lagging head.
        if (engaged || state.progress > 0.0) {
            state.live = state.delay < 1.0 ? 0.0f : std::max(0.0f, state.live - crossfadeStep);
        } else {
            state.live = std::min(1.0f, state.live + crossfadeStep);
            if (state.live >= 1.0f) state.delay = 0.0;
        }

        for (int c = 0; c < 2; ++c) {
            if (state.live >= 1.0f) {
                outputs[c][i] = in[c];
            } else {
                const float wet = readBack(state.ring[c], state.mask, state.writePos, state.delay) * speedGain(speed);
                outputs[c][i] = state.live * in[c] + (1.0f - state.live) * wet;
            }
        }
        ++state.writePos;
        // readBack counts back from the newest sample, so keep writePos pointing at it for the next frame.
    }

    const float lastSpeed = speedFor(state.progress, clampf(curve, kMinCurve, kMaxCurve));
    state.meters.speed = lastSpeed;
    state.meters.delayMs = static_cast<float>(state.delay * 1000.0 / sr);
}

}  // namespace downspout::spindown
