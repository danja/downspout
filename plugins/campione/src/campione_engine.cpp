#include "campione_engine.hpp"
#include "campione_pitch_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace downspout::campione {
namespace {

static void computeBiquad(Voice& v, const SampleZone& zone, double sr) noexcept
{
    // RBJ Audio EQ Cookbook
    const double f0    = std::clamp(static_cast<double>(zone.filterCutoffHz), 20.0, sr * 0.499);
    const double Q     = std::max(static_cast<double>(zone.filterQ), 0.01);
    const double w0    = 2.0 * M_PI * f0 / sr;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double alpha = sinw0 / (2.0 * Q);
    double b0, b1, b2, a0, a1, a2;
    switch (zone.filterType) {
    case 1: // Band-pass
        b0 =  sinw0 / 2.0; b1 = 0.0;       b2 = -sinw0 / 2.0;
        a0 =  1.0 + alpha;  a1 = -2.0*cosw0; a2 =  1.0 - alpha; break;
    case 2: // High-pass
        b0 =  (1.0 + cosw0) / 2.0; b1 = -(1.0 + cosw0); b2 = (1.0 + cosw0) / 2.0;
        a0 =  1.0 + alpha;          a1 = -2.0 * cosw0;   a2 = 1.0 - alpha; break;
    case 3: // Notch
        b0 = 1.0; b1 = -2.0*cosw0; b2 = 1.0;
        a0 = 1.0 + alpha; a1 = -2.0*cosw0; a2 = 1.0 - alpha; break;
    default: // Low-pass (0)
        b0 = (1.0 - cosw0) / 2.0; b1 = 1.0 - cosw0; b2 = (1.0 - cosw0) / 2.0;
        a0 = 1.0 + alpha;          a1 = -2.0 * cosw0; a2 = 1.0 - alpha; break;
    }
    v.bqB0 = static_cast<float>(b0 / a0);
    v.bqB1 = static_cast<float>(b1 / a0);
    v.bqB2 = static_cast<float>(b2 / a0);
    v.bqA1 = static_cast<float>(a1 / a0);
    v.bqA2 = static_cast<float>(a2 / a0);
}

static float applyBiquad(Voice& v, float x, int ch) noexcept
{
    const float y = v.bqB0 * x + v.bqZ1[ch];
    v.bqZ1[ch]   = v.bqB1 * x - v.bqA1 * y + v.bqZ2[ch];
    v.bqZ2[ch]   = v.bqB2 * x - v.bqA2 * y;
    return y;
}

inline float readSample(const SampleZone& zone, std::uint32_t frame, std::uint32_t channel) noexcept
{
    if (zone.data.empty()) return 0.0f;
    const auto total = static_cast<std::uint32_t>(zone.data.size() / zone.channelCount);
    if (frame >= total) return 0.0f;
    const std::uint32_t ch = channel < static_cast<std::uint32_t>(zone.channelCount)
                             ? channel
                             : static_cast<std::uint32_t>(zone.channelCount - 1);
    return zone.data[static_cast<std::size_t>(frame) * zone.channelCount + ch];
}

inline float interpolate(const SampleZone& zone, double position, std::uint32_t channel) noexcept
{
    const auto frame0 = static_cast<std::uint32_t>(position);
    const float s0 = readSample(zone, frame0, channel);
    const float s1 = readSample(zone, frame0 + 1, channel);
    return s0 + static_cast<float>(position - frame0) * (s1 - s0);
}

int findZone(const std::vector<SampleZone>& zones, int midiNote, int midiChannel)
{
    if (zones.empty()) return -1;

    // Exact range match first (first matching zone wins)
    for (int i = 0; i < static_cast<int>(zones.size()); ++i) {
        const SampleZone& z = zones[i];
        const bool chMatch = (z.midiChannel == 0) || (midiChannel == 0) || (z.midiChannel == midiChannel);
        if (chMatch && midiNote >= z.rangeLow && midiNote <= z.rangeHigh)
            return i;
    }

    // Gap-filling: nearest rootNote, ignoring range
    int bestIdx = 0;
    int bestDist = std::numeric_limits<int>::max();
    for (int i = 0; i < static_cast<int>(zones.size()); ++i) {
        const int dist = std::abs(midiNote - zones[i].rootNote);
        if (dist < bestDist) { bestDist = dist; bestIdx = i; }
    }
    return bestIdx;
}

int allocateVoice(std::array<Voice, kMaxVoices>& voices, int midiNote, int midiChannel, std::uint64_t now)
{
    // Reuse same note+channel voice (retrigger)
    for (int i = 0; i < kMaxVoices; ++i)
        if (voices[i].active && voices[i].midiNote == midiNote && voices[i].midiChannel == midiChannel)
            return i;
    // Free voice
    for (int i = 0; i < kMaxVoices; ++i)
        if (!voices[i].active) return i;
    // Steal oldest
    int oldest = 0;
    std::uint64_t minAge = voices[0].ageFrames;
    for (int i = 1; i < kMaxVoices; ++i)
        if (voices[i].ageFrames < minAge) { minAge = voices[i].ageFrames; oldest = i; }
    (void)now;
    return oldest;
}

}  // namespace

Parameters clampParameters(const Parameters& p)
{
    Parameters out = p;
    out.masterVolume       = std::clamp(out.masterVolume,       0.0f,   1.0f);
    out.midiChannel        = std::clamp(out.midiChannel,        0.0f,  16.0f);
    out.crossfadeDurationMs = std::clamp(out.crossfadeDurationMs, 0.0f, 500.0f);
    out.pitchBendRange     = std::clamp(out.pitchBendRange,     0.0f,  24.0f);
    return out;
}

void activate(EngineState& state)
{
    for (Voice& v : state.voices) v = {};
    state.frameCounter = 0;
}

void processBlock(EngineState& state,
                  const Parameters& params,
                  const std::vector<SampleZone>& zones,
                  const MidiInputEvent* midiEvents,
                  std::uint32_t midiCount,
                  std::uint32_t frames,
                  double hostSampleRate,
                  AudioBlock audio)
{
    if (audio.channelCount < 1) return;
    const bool hasStereo = audio.channelCount >= 2;

    std::uint32_t midiIdx = 0;

    for (std::uint32_t f = 0; f < frames; ++f) {
        // Dispatch MIDI events for this frame
        while (midiIdx < midiCount && midiEvents[midiIdx].frame <= f) {
            const MidiInputEvent& ev = midiEvents[midiIdx++];
            if (ev.size < 2) continue;
            const std::uint8_t status  = ev.data[0] & 0xF0u;
            const std::uint8_t channel = static_cast<std::uint8_t>((ev.data[0] & 0x0Fu) + 1);
            const std::uint8_t note    = ev.data[1];
            const std::uint8_t vel     = ev.size >= 3 ? ev.data[2] : 0u;

            // Filter by global MIDI channel (0 = all)
            const int globalCh = static_cast<int>(std::lround(params.midiChannel));
            if (globalCh > 0 && static_cast<int>(channel) != globalCh) continue;

            if (status == 0x90u && vel > 0u) {  // note-on
                const int zoneIdx = findZone(zones, note, channel);
                if (zoneIdx >= 0 && !zones[static_cast<std::size_t>(zoneIdx)].muted) {
                    const std::uint64_t now = state.frameCounter + f;
                    const int vi = allocateVoice(state.voices, note, channel, now);
                    Voice& v = state.voices[vi];
                    const SampleZone& zone = zones[zoneIdx];
                    v.active            = true;
                    v.zoneIndex         = zoneIdx;
                    v.midiNote          = note;
                    v.midiChannel       = channel;
                    v.velocity          = vel / 127.0f;
                    v.position          = 0.0;
                    v.playbackRate      = computePlaybackRate(zone, note, hostSampleRate);
                    v.inCrossfade       = false;
                    v.crossfadePosition = 0.0;
                    v.ageFrames         = now;
                    v.adsrPhase = Voice::AdsrPhase::Attack;
                    v.adsrValue = 0.0f;
                    v.bqZ1[0] = v.bqZ1[1] = 0.0f;
                    v.bqZ2[0] = v.bqZ2[1] = 0.0f;
                    v.lastFilterCutoff = -1.0f;  // force recompute on first use
                    v.lastFilterQ      = -1.0f;
                    v.lastFilterType   = -1;
                }
            } else if (status == 0x80u || (status == 0x90u && vel == 0u)) {  // note-off
                for (Voice& v : state.voices) {
                    if (!v.active || v.midiNote != note || v.midiChannel != channel) continue;
                    const bool hasRelease = (v.zoneIndex >= 0 &&
                                             v.zoneIndex < static_cast<int>(zones.size()) &&
                                             zones[static_cast<std::size_t>(v.zoneIndex)].releaseMs > 0.0f);
                    if (hasRelease) {
                        v.adsrPhase = Voice::AdsrPhase::Release;
                    } else {
                        v.active    = false;
                        v.adsrPhase = Voice::AdsrPhase::Idle;
                    }
                }
            }
        }

        // Clear output this frame
        if (audio.outputs[0]) audio.outputs[0][f] = 0.0f;
        if (hasStereo && audio.outputs[1]) audio.outputs[1][f] = 0.0f;

        // Accumulate active voices
        for (Voice& v : state.voices) {
            if (!v.active) continue;
            if (v.zoneIndex < 0 || v.zoneIndex >= static_cast<int>(zones.size())) {
                v.active = false; continue;
            }
            const SampleZone& zone = zones[v.zoneIndex];
            const auto totalFrames = static_cast<std::uint32_t>(
                zone.data.empty() ? 0u : zone.data.size() / zone.channelCount);
            if (totalFrames == 0) { v.active = false; continue; }

            const bool loopValid = zone.loopEnabled && zone.loopEnd > zone.loopStart;

            // Crossfade entry check (before reading)
            if (loopValid && !v.inCrossfade && zone.crossfadeFrames > 0 &&
                zone.loopEnd >= zone.crossfadeFrames)
            {
                const double threshold = static_cast<double>(zone.loopEnd) - zone.crossfadeFrames;
                if (v.position >= threshold) {
                    v.inCrossfade = true;
                    v.crossfadePosition = static_cast<double>(zone.loopStart) +
                                         (v.position - threshold);
                }
            }

            // Read tail samples
            const float tailL = interpolate(zone, v.position, 0);
            const float tailR = interpolate(zone, v.position,
                                            zone.channelCount > 1 ? 1u : 0u);

            float outL = tailL, outR = tailR;

            // Blend with loop head during crossfade
            if (v.inCrossfade && zone.crossfadeFrames > 0) {
                const float framesLeft = static_cast<float>(
                    static_cast<double>(zone.loopEnd) - v.position);
                const float wTail = std::clamp(
                    framesLeft / static_cast<float>(zone.crossfadeFrames), 0.0f, 1.0f);
                const float wHead = 1.0f - wTail;
                const float headL = interpolate(zone, v.crossfadePosition, 0);
                const float headR = interpolate(zone, v.crossfadePosition,
                                                zone.channelCount > 1 ? 1u : 0u);
                outL = wTail * tailL + wHead * headL;
                outR = wTail * tailR + wHead * headR;
            }

            // Advance ADSR
            switch (v.adsrPhase) {
            case Voice::AdsrPhase::Attack: {
                const float inc = (zone.attackMs > 0.0f)
                    ? (1.0f / static_cast<float>(zone.attackMs * 0.001 * hostSampleRate))
                    : 1.0f;
                v.adsrValue += inc;
                if (v.adsrValue >= 1.0f) { v.adsrValue = 1.0f; v.adsrPhase = Voice::AdsrPhase::Decay; }
                break; }
            case Voice::AdsrPhase::Decay: {
                const float tgt = zone.sustainLevel;
                const float inc = (zone.decayMs > 0.0f)
                    ? ((1.0f - tgt) / static_cast<float>(zone.decayMs * 0.001 * hostSampleRate))
                    : (1.0f - tgt);
                v.adsrValue -= inc;
                if (v.adsrValue <= tgt) { v.adsrValue = tgt; v.adsrPhase = Voice::AdsrPhase::Sustain; }
                break; }
            case Voice::AdsrPhase::Sustain:
                v.adsrValue = zone.sustainLevel;
                break;
            case Voice::AdsrPhase::Release: {
                // Linear rate: 1.0 → 0 in releaseMs (constant per frame)
                const float releaseFrames = zone.releaseMs > 0.0f
                    ? static_cast<float>(zone.releaseMs * 0.001 * hostSampleRate) : 1.0f;
                v.adsrValue -= 1.0f / releaseFrames;
                if (v.adsrValue <= 0.0f) { v.active = false; v.adsrPhase = Voice::AdsrPhase::Idle; continue; }
                break; }
            default:
                break;
            }

            if (zone.filterEnabled) {
                // Recompute coefficients when filter params change (live update, same as ADSR)
                if (zone.filterCutoffHz != v.lastFilterCutoff ||
                    zone.filterQ        != v.lastFilterQ      ||
                    zone.filterType     != v.lastFilterType) {
                    computeBiquad(v, zone, hostSampleRate);
                    v.lastFilterCutoff = zone.filterCutoffHz;
                    v.lastFilterQ      = zone.filterQ;
                    v.lastFilterType   = zone.filterType;
                }
                outL = applyBiquad(v, outL, 0);
                // Stereo: independent filter state per channel.
                // Mono: reuse the already-filtered L rather than running the same biquad state twice.
                outR = (zone.channelCount > 1) ? applyBiquad(v, outR, 1) : outL;
            }

            const float gain = v.velocity * params.masterVolume * v.adsrValue;
            const float panL = std::clamp(1.0f - zone.pan, 0.0f, 1.0f);
            const float panR = std::clamp(1.0f + zone.pan, 0.0f, 1.0f);
            if (audio.outputs[0]) audio.outputs[0][f] += outL * gain * panL;
            if (hasStereo && audio.outputs[1]) audio.outputs[1][f] += outR * gain * panR;

            // Advance positions
            v.position += v.playbackRate;
            if (v.inCrossfade) v.crossfadePosition += v.playbackRate;

            // Loop wrap
            if (loopValid && v.position >= static_cast<double>(zone.loopEnd)) {
                v.position = v.inCrossfade ? v.crossfadePosition
                                           : static_cast<double>(zone.loopStart);
                v.inCrossfade = false;
            }

            // One-shot end
            if (!zone.loopEnabled && v.position >= static_cast<double>(totalFrames))
                v.active = false;
        }
    }

    state.frameCounter += frames;
}

}  // namespace downspout::campione
