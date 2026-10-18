#include "qune_engine.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::qune {

namespace {

constexpr float kMaxShift = 24.0f;

[[nodiscard]] bool allowed(const std::uint16_t mask, const int note) noexcept
{
    return (mask >> (((note % 12) + 12) % 12)) & 1u;
}

[[nodiscard]] float dbToGain(const float db) noexcept
{
    return std::pow(10.0f, db / 20.0f);
}

[[nodiscard]] bool channelAccepted(const Parameters& p, const std::uint8_t status) noexcept
{
    const int setting = clampInt(p.midiCh, 0, 16);
    return setting == 0 || (status & 0x0F) + 1 == setting;
}

void removeNote(EngineState& s, const std::uint8_t note)
{
    for (std::size_t i = 0; i < s.noteCount; ++i)
    {
        if (s.noteStack[i] == note)
        {
            for (std::size_t j = i + 1; j < s.noteCount; ++j)
                s.noteStack[j - 1] = s.noteStack[j];
            --s.noteCount;
            return;
        }
    }
}

[[nodiscard]] std::uint16_t heldPitchClasses(const EngineState& s) noexcept
{
    std::uint16_t mask = 0;
    for (std::size_t i = 0; i < s.noteCount; ++i)
        mask = static_cast<std::uint16_t>(mask | (1u << (s.noteStack[i] % 12)));
    return mask;
}

void applyRange(EngineState& s, const int range)
{
    const PitchRange& r = kRanges[static_cast<std::size_t>(range)];
    s.tracker.setRange(r.minHz, r.maxHz);
    s.psola.setLowestPitch(r.minHz);
    s.activeRange = range;
    s.lastHop = s.tracker.result().hop;
}

void resetShifters(EngineState& s)
{
    s.fast.reset();
    s.psola.reset();
    s.spectral.reset();
}

// Runs once per tracker hop.
void updateControl(EngineState& s, const Parameters& p)
{
    const PitchTracker::Result& result = s.tracker.result();
    const bool wasVoiced = s.voiced;
    s.voiced = result.voiced;
    if (!result.voiced)
    {
        s.hasTarget = false;
        s.previousTarget = -1;
        s.shiftTarget = 0.0f;
        return;
    }

    s.lastPeriod = result.periodSamples;
    const float detected = hzToMidi(result.f0Hz);
    s.detectedNote = detected;

    const float hysteresis = p.stickiness / 100.0f;
    const auto source = static_cast<Source>(clampInt(p.source, 0, 2));
    const bool haveNotes = s.noteCount > 0;

    bool valid = false;
    float target = 0.0f;
    int chosen = -1;

    const auto snapToScale = [&]() {
        const NoteChoice c = nearestInMask(detected, rotatedScaleMask(static_cast<std::size_t>(clampInt(p.scale, 0, 23)),
                                                                      clampInt(p.root, 0, 11)),
                                           s.previousTarget, hysteresis);
        if (c.valid)
        {
            valid = true;
            chosen = c.note;
            target = static_cast<float>(c.note);
        }
    };

    if (source == Source::scale)
    {
        snapToScale();
    }
    else if (haveNotes)
    {
        if (source == Source::midiExact)
        {
            valid = true;
            target = static_cast<float>(s.noteStack[s.noteCount - 1]) + s.bendSemitones;
        }
        else
        {
            const NoteChoice c = nearestInMask(detected, heldPitchClasses(s), s.previousTarget, hysteresis);
            if (c.valid)
            {
                valid = true;
                chosen = c.note;
                target = static_cast<float>(c.note) + s.bendSemitones;
            }
        }
    }
    else if (clampInt(p.noNotes, 0, 1) == 1)
    {
        snapToScale();
    }

    s.hasTarget = valid;
    s.previousTarget = chosen;
    if (!valid)
    {
        s.shiftTarget = 0.0f;
        return;
    }

    s.targetNote = target + p.transpose;
    s.shiftTarget = std::clamp(p.amount * (target - detected) + p.transpose, -kMaxShift, kMaxShift);

    // A new phrase starts at its target rather than sliding in from the last one.
    if (!wasVoiced)
        s.shiftSmoothed = s.shiftTarget;
}

}  // namespace

NoteChoice nearestInMask(const float midi, const std::uint16_t absoluteMask, const int previous,
                         const float hysteresisSemitones)
{
    NoteChoice choice;
    if ((absoluteMask & 0xFFFu) == 0)
        return choice;

    int lower = static_cast<int>(std::floor(midi));
    while (!allowed(absoluteMask, lower))
        --lower;
    int upper = static_cast<int>(std::floor(midi)) + 1;
    while (!allowed(absoluteMask, upper))
        ++upper;

    const float lowerDistance = midi - static_cast<float>(lower);
    const float upperDistance = static_cast<float>(upper) - midi;
    int best = (lowerDistance <= upperDistance) ? lower : upper;

    if (previous >= 0 && previous != best && (previous == lower || previous == upper))
    {
        const float previousDistance = std::fabs(midi - static_cast<float>(previous));
        const float bestDistance = std::fabs(midi - static_cast<float>(best));
        if (previousDistance - bestDistance < hysteresisSemitones)
            best = previous;
    }

    choice.valid = true;
    choice.note = best;
    return choice;
}

void activate(EngineState& s, const double sampleRate)
{
    s.sampleRate = sampleRate;

    s.tracker.prepare(sampleRate);
    s.fast.prepare(sampleRate);
    s.psola.prepare(sampleRate);
    s.spectral.prepare(sampleRate);

    const double psolaWorst = 2.4 * sampleRate / static_cast<double>(kAbsoluteMinHz) + 256.0;
    const double spectralWorst = static_cast<double>(s.spectral.latencySamples()) + 256.0;
    s.history.prepare(static_cast<std::uint32_t>(2.0 * std::max(psolaWorst, spectralWorst)) + 8192u);

    s.prepared = true;
    s.activeRange = -1;
    reset(s);
}

void reset(EngineState& s)
{
    s.history.clear();
    s.tracker.reset();
    resetShifters(s);

    s.noteCount = 0;
    s.pendingRelease.fill(false);
    s.pedal = false;
    s.bendSemitones = 0.0f;

    s.lastHop = s.tracker.result().hop;
    s.voiced = false;
    s.hasTarget = false;
    s.previousTarget = -1;
    s.detectedNote = 0.0f;
    s.targetNote = 0.0f;
    s.shiftTarget = 0.0f;
    s.shiftSmoothed = 0.0f;
    s.lastPeriod = 0.0f;
    s.switching = false;
    s.switchGain = 1.0f;
}

void configure(EngineState& s, const Parameters& rawParameters)
{
    const Parameters p = clampParameters(rawParameters);
    applyRange(s, clampInt(p.range, 0, static_cast<int>(kRangeCount) - 1));
    s.activeQuality = clampInt(p.quality, 0, 2);
    resetShifters(s);
}

void midiEvent(EngineState& s, const Parameters& p, const std::uint8_t* data, const int size)
{
    if (size < 2 || data == nullptr)
        return;
    const std::uint8_t status = data[0];
    const std::uint8_t kind = status & 0xF0;
    if (kind < 0x80 || kind > 0xE0)
        return;
    if (!channelAccepted(p, status))
        return;

    switch (kind)
    {
    case 0x90:
        if (size >= 3 && data[2] != 0)
        {
            const std::uint8_t note = data[1] & 0x7F;
            removeNote(s, note);
            s.pendingRelease[note] = false;
            if (s.noteCount == kMaxHeldNotes)
                removeNote(s, s.noteStack[0]);
            s.noteStack[s.noteCount++] = note;
            break;
        }
        [[fallthrough]];
    case 0x80:
    {
        const std::uint8_t note = data[1] & 0x7F;
        if (s.pedal)
            s.pendingRelease[note] = true;
        else
            removeNote(s, note);
        break;
    }
    case 0xB0:
        if (size < 3)
            break;
        if (data[1] == 64)
        {
            const bool down = data[2] >= 64;
            if (s.pedal && !down)
            {
                for (std::size_t n = 0; n < 128; ++n)
                {
                    if (s.pendingRelease[n])
                    {
                        removeNote(s, static_cast<std::uint8_t>(n));
                        s.pendingRelease[n] = false;
                    }
                }
            }
            s.pedal = down;
        }
        else if (data[1] == 120 || data[1] == 123)
        {
            s.noteCount = 0;
            s.pendingRelease.fill(false);
        }
        break;
    case 0xE0:
        if (size >= 3)
        {
            const int value = (data[1] & 0x7F) | ((data[2] & 0x7F) << 7);
            s.bendSemitones = (static_cast<float>(value - 8192) / 8192.0f) * p.bendRange;
        }
        break;
    default:
        break;
    }
}

int latencySamplesFor(const EngineState& s, const int quality)
{
    switch (quality)
    {
    case 0: return s.fast.latencySamples();
    case 1: return s.psola.latencySamples();
    default: return s.spectral.latencySamples();
    }
}

int latencySamples(const EngineState& s)
{
    return latencySamplesFor(s, s.activeQuality);
}

void processBlock(EngineState& s, const Parameters& rawParameters, const float* inLeft, const float* inRight,
                  float* outLeft, float* outRight, const std::uint32_t frames)
{
    if (!s.prepared)
        return;

    const Parameters p = clampParameters(rawParameters);
    const int wantedQuality = clampInt(p.quality, 0, 2);
    const int wantedRange = clampInt(p.range, 0, static_cast<int>(kRangeCount) - 1);
    const float outputGain = dbToGain(p.level);
    const float sr = static_cast<float>(s.sampleRate);

    s.tracker.setVoicing(p.voicing);

    if (s.activeRange < 0)
    {
        applyRange(s, wantedRange);
        s.activeQuality = wantedQuality;
    }

    const float glideSeconds = p.glide * 0.001f;
    const float fadeStep = 1.0f / (0.005f * sr);

    for (std::uint32_t offset = 0; offset < frames;)
    {
        // Sub-blocks sit on an absolute grid, so the result does not depend on
        // how the host happens to slice the stream.
        const int toGrid = kSubBlock - static_cast<int>(s.history.count() % kSubBlock);
        const int n = static_cast<int>(std::min<std::uint32_t>(static_cast<std::uint32_t>(toGrid), frames - offset));
        const float* left = inLeft + offset;
        const float* right = inRight + offset;

        s.history.push(left, right, n);
        for (int i = 0; i < n; ++i)
            s.mono[static_cast<std::size_t>(i)] = 0.5f * (left[i] + right[i]);
        s.tracker.process(s.mono.data(), n);
        if (s.tracker.result().hop != s.lastHop)
        {
            s.lastHop = s.tracker.result().hop;
            updateControl(s, p);
        }

        // Glide: one pole on the shift, so a note change slides and vibrato is
        // only partly removed. Unvoiced input releases quickly.
        const float seconds = s.voiced ? glideSeconds : 0.005f;
        const float coeff = (seconds < 0.0005f) ? 1.0f
                                                : 1.0f - std::exp(-static_cast<float>(n) / (seconds * sr));
        if (s.voiced && !s.hasTarget)
            s.shiftSmoothed += (0.0f - s.shiftSmoothed) * coeff;
        else
            s.shiftSmoothed += (s.shiftTarget - s.shiftSmoothed) * coeff;
        if (std::fabs(s.shiftSmoothed) < 1.0e-5f)
            s.shiftSmoothed = 0.0f;

        // Quality and range changes alter latency, so fade out around them.
        if (!s.switching && (wantedQuality != s.activeQuality || wantedRange != s.activeRange))
            s.switching = true;
        const float gainStart = s.switchGain;
        if (s.switching)
        {
            s.switchGain = std::max(0.0f, s.switchGain - fadeStep * static_cast<float>(n));
            if (s.switchGain <= 0.0f)
            {
                if (wantedRange != s.activeRange)
                    applyRange(s, wantedRange);
                s.activeQuality = wantedQuality;
                resetShifters(s);
                s.switching = false;
            }
        }
        else
        {
            s.switchGain = std::min(1.0f, s.switchGain + fadeStep * static_cast<float>(n));
        }
        const float gainEnd = s.switchGain;

        ShiftControl control;
        control.ratio = std::clamp(std::exp2(s.shiftSmoothed / 12.0f), 0.25f, 4.0f);
        const float formantOffset = std::exp2(p.formantShift / 12.0f);
        control.formantRatio = (clampInt(p.formantMode, 0, 1) == 1 ? 1.0f : control.ratio) * formantOffset;
        control.period = s.voiced ? s.lastPeriod : 0.0f;
        control.voiced = s.voiced;

        switch (s.activeQuality)
        {
        case 0: s.fast.process(s.history, control, s.wetLeft.data(), s.wetRight.data(), n); break;
        case 1: s.psola.process(s.history, control, s.wetLeft.data(), s.wetRight.data(), n); break;
        default: s.spectral.process(s.history, control, s.wetLeft.data(), s.wetRight.data(), n); break;
        }

        const int delay = latencySamples(s);
        const std::int64_t base = s.history.count() - n;
        for (int i = 0; i < n; ++i)
        {
            const float t = static_cast<float>(i + 1) / static_cast<float>(n);
            const float fade = gainStart + (gainEnd - gainStart) * t;
            const float dryLeft = s.history.sample(0, base + i - delay);
            const float dryRight = s.history.sample(1, base + i - delay);
            const auto k = static_cast<std::size_t>(i);
            // A quality or range change alters the latency of both paths, so the
            // whole output ducks around it rather than just the wet signal.
            const float gain = outputGain * fade;
            outLeft[offset + static_cast<std::uint32_t>(i)] =
                gain * (dryLeft + p.mix * (s.wetLeft[k] - dryLeft));
            outRight[offset + static_cast<std::uint32_t>(i)] =
                gain * (dryRight + p.mix * (s.wetRight[k] - dryRight));
        }

        offset += static_cast<std::uint32_t>(n);
    }
}

}  // namespace downspout::qune
