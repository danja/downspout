#include "polyscribe_core_types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace downspout::polyscribe {
namespace {

constexpr int kFirstAnalysedNote = 24;
constexpr int kLastAnalysedNote = 120;
constexpr float kVelocityReference = 0.35f;
constexpr float kDrumFloor = 0.02f;
constexpr std::uint64_t kDrumCooldownMs = 60;
constexpr double kPi = 3.14159265358979323846;

// Low/mid/high representatives for the three drum outputs. These are analysis
// pitches, not the GM output notes: 36/38/42 are all below 100 Hz and cannot
// distinguish a snare or hat from a kick.
constexpr std::array<int, kDrumBands> kDrumProbeNotes = {36, 62, 108};
constexpr std::array<int, kDrumBands> kDrumOutputNotes = {kDrumKick, kDrumSnare, kDrumHat};

[[nodiscard]] float midiHz(const int note) noexcept
{
    return 440.0f * std::exp2((static_cast<float>(note) - 69.0f) / 12.0f);
}

[[nodiscard]] bool isHarmonic(const int semitones) noexcept
{
    // Integer harmonics 2..8, rounded to the nearest equal-tempered semitone.
    constexpr std::array<int, 12> kIntervals = {12, 19, 24, 28, 31, 34, 36, 38, 40, 42, 43, 44};
    return std::find(kIntervals.begin(), kIntervals.end(), semitones) != kIntervals.end();
}

bool pushMidi(EngineState& s, const std::uint32_t frame, const std::uint8_t status, const int note, const int velocity)
{
    if (s.midiOut.count >= s.midiOut.events.size())
        return false;
    MidiOutEvent& e = s.midiOut.events[s.midiOut.count++];
    e.frame = frame;
    e.data[0] = status;
    e.data[1] = static_cast<std::uint8_t>(std::clamp(note, 0, 127));
    e.data[2] = static_cast<std::uint8_t>(std::clamp(velocity, 0, 127));
    return true;
}

void noteOff(EngineState& s, ActiveNote& a, const std::uint32_t frame)
{
    if (pushMidi(s, frame, static_cast<std::uint8_t>(0x80 | a.channel), a.note, 0))
        a.active = false;
}

void releaseAll(EngineState& s, const std::uint32_t frame)
{
    for (ActiveNote& a : s.active) {
        if (a.active)
            noteOff(s, a, frame);
    }
}

[[nodiscard]] int desiredChannel(const Parameters& p) noexcept
{
    return clampInt(p.channel, 1, 16) - 1;
}

[[nodiscard]] int velocityFor(const Parameters& p, const float magnitude) noexcept
{
    const float base = static_cast<float>(clampInt(p.velocity, 1, 127));
    const float dynamics = std::clamp(p.dynamics, 0.0f, 1.0f);
    const float level = std::clamp(magnitude / kVelocityReference, 0.0f, 1.0f);
    const float scale = 1.0f - dynamics + dynamics * (0.45f + 0.55f * level);
    return clampInt(std::lround(base * scale), 1, 127);
}

void addDesired(EngineState& s, const int note, const int velocity, const float magnitude, const int transpose)
{
    const int transposed = std::clamp(note + transpose, 0, 127);
    for (std::size_t i = 0; i < s.desiredCount; ++i) {
        if (s.desired[i].note == transposed) {
            if (magnitude > s.desired[i].magnitude) {
                s.desired[i].velocity = velocity;
                s.desired[i].magnitude = magnitude;
            }
            return;
        }
    }
    if (s.desiredCount >= s.desired.size())
        return;
    s.desired[s.desiredCount++] = {transposed, velocity, magnitude};
}

void measure(EngineState& s)
{
    double rms = 0.0;
    for (std::size_t i = 0; i < kWindow; ++i) {
        const float x = s.ring[(s.writeIndex + i) % kWindow];
        rms += static_cast<double>(x) * x;
    }
    s.level = 0.8f * s.level + 0.2f * static_cast<float>(std::sqrt(rms / static_cast<double>(kWindow)));

    for (int note = kFirstAnalysedNote; note <= kLastAnalysedNote; ++note) {
        const std::size_t n = static_cast<std::size_t>(note);
        float s1 = 0.0f, s2 = 0.0f;
        const float coeff = s.goertzelCoeff[n];
        for (std::size_t i = 0; i < kWindow; ++i) {
            const float x = s.ring[(s.writeIndex + i) % kWindow] * s.hann[i];
            const float s0 = x + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const float re = s1 - s2 * s.goertzelCos[n];
        const float im = s2 * s.goertzelSin[n];
        s.magnitude[n] = 2.0f * std::sqrt(re * re + im * im) / s.hannSum;
    }
}

void measureDrums(EngineState& s)
{
    const std::size_t start = (s.writeIndex + kWindow - kDrumWindow) % kWindow;
    for (std::size_t b = 0; b < kDrumBands; ++b) {
        float s1 = 0.0f, s2 = 0.0f;
        for (std::size_t i = 0; i < kDrumWindow; ++i) {
            const float x = s.ring[(start + i) % kWindow] * s.drumHann[i];
            const float s0 = x + s.drumCoeff[b] * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const float re = s1 - s2 * s.drumCos[b];
        const float im = s2 * s.drumSin[b];
        s.drumMagnitude[b] = 2.0f * std::sqrt(re * re + im * im) / s.drumHannSum;
    }
}

void detectDrums(EngineState& s, const Parameters& p, const std::uint64_t atSample, const int transpose)
{
    const float floor = kDrumFloor + (1.0f - std::clamp(p.sensitivity, 0.0f, 1.0f)) * 0.10f;
    for (std::size_t b = 0; b < kDrumBands; ++b) {
        const float level = s.drumMagnitude[b];
        // One hit per band until that band has clearly fallen away. This stops a
        // sustained or decaying sound from re-triggering as its window slides.
        if (!s.bandArmed[b] && level < std::max(floor * 1.2f, s.bandPeak[b] * 0.25f))
            s.bandArmed[b] = true;
        const float rise = level - s.bandEnvelope[b];
        s.bandEnvelope[b] = std::max(level, s.bandEnvelope[b] * 0.90f);
        if (!s.bandArmed[b] || level <= floor || rise <= 0.45f * level + 0.02f)
            continue;
        if (atSample - s.bandLastOnset[b] < s.drumCooldownSamples)
            continue;
        s.bandArmed[b] = false;
        s.bandPeak[b] = level;
        s.bandLastOnset[b] = atSample;
        addDesired(s, kDrumOutputNotes[b], velocityFor(p, level), level, transpose);
    }
}

void detectPitched(EngineState& s, const Parameters& p, const int type, const int transpose)
{
    const auto preferred = preferredRange(type);
    int low = std::max(preferred.first, clampInt(p.minNote, 0, 127));
    int high = std::min(preferred.second, clampInt(p.maxNote, 0, 127));
    if (low > high)
        return;

    struct Candidate {
        int note = 0;
        float magnitude = 0.0f;
    };
    std::array<Candidate, kNoteCount> candidates {};
    std::size_t count = 0;
    const float floor = 0.018f + (1.0f - std::clamp(p.sensitivity, 0.0f, 1.0f)) * 0.16f;

    for (int note = low; note <= high; ++note) {
        const float m = s.magnitude[static_cast<std::size_t>(note)];
        if (m < floor)
            continue;
        if (note > low && m < s.magnitude[static_cast<std::size_t>(note - 1)])
            continue;
        if (note < high && m < s.magnitude[static_cast<std::size_t>(note + 1)])
            continue;
        const Candidate c {note, m};
        std::size_t at = count;
        while (at > 0 && candidates[at - 1].magnitude < m) {
            candidates[at] = candidates[at - 1];
            --at;
        }
        candidates[at] = c;
        if (count < candidates.size())
            ++count;
    }

    std::array<Candidate, kMaxActive> accepted {};
    std::size_t acceptedCount = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const Candidate& c = candidates[i];
        bool harmonic = false;
        for (std::size_t j = 0; j < acceptedCount; ++j) {
            if (isHarmonic(c.note - accepted[j].note) && c.magnitude < accepted[j].magnitude * 0.62f) {
                harmonic = true;
                break;
            }
        }
        if (harmonic || acceptedCount >= accepted.size())
            continue;
        accepted[acceptedCount++] = c;
    }

    const bool monophonic = (type == static_cast<int>(InputType::Melody) ||
                             type == static_cast<int>(InputType::Bass));
    if (!monophonic) {
        const int limit = clampInt(p.maxNotes, 1, static_cast<int>(kMaxActive));
        // Keep notes that are already sounding while they remain strong enough.
        // Equal-amplitude chord tones trade places as their phases drift; without
        // this, Max Notes = 1 would hop between the chord tones and emit several
        // separate note-ons.
        for (const ActiveNote& a : s.active) {
            if (!a.active || static_cast<int>(s.desiredCount) >= limit || acceptedCount == 0)
                continue;
            for (std::size_t i = 0; i < acceptedCount; ++i) {
                const Candidate& c = accepted[i];
                if (c.note + transpose != a.note)
                    continue;
                if (c.magnitude >= accepted[0].magnitude * 0.60f)
                    addDesired(s, c.note, velocityFor(p, c.magnitude), c.magnitude, transpose);
                break;
            }
        }
        for (std::size_t i = 0; i < acceptedCount && static_cast<int>(s.desiredCount) < limit; ++i)
            addDesired(s, accepted[i].note, velocityFor(p, accepted[i].magnitude), accepted[i].magnitude, transpose);
        return;
    }

    std::size_t chosen = 0;
    if (type == static_cast<int>(InputType::Bass)) {
        for (std::size_t i = 1; i < acceptedCount; ++i)
            if (accepted[i].note < accepted[chosen].note)
                chosen = i;
    }
    // Equal-amplitude chord tones can trade places as their phases drift. Keep
    // the last note while it stays within 15 % of the best candidate so Melody
    // and Bass remain genuinely monophonic instead of re-triggering.
    if (acceptedCount > 0 && s.lastPitchedNote > 0) {
        for (std::size_t i = 0; i < acceptedCount; ++i) {
            if (accepted[i].note != s.lastPitchedNote)
                continue;
            if (accepted[i].magnitude >= accepted[chosen].magnitude * 0.85f)
                chosen = i;
            break;
        }
    }
    if (acceptedCount > 0)
        addDesired(s, accepted[chosen].note, velocityFor(p, accepted[chosen].magnitude),
                   accepted[chosen].magnitude, transpose);
    s.lastPitchedNote = acceptedCount > 0 ? accepted[chosen].note : 0;
}

void syncNotes(EngineState& s, const Parameters& p, const std::uint64_t atSample, const std::uint32_t frame)
{
    const bool midiOut = clampInt(p.midiOut, 0, 1) == 1;
    if (!midiOut)
        return;
    const int channel = desiredChannel(p);

    for (ActiveNote& a : s.active) {
        if (!a.active)
            continue;
        bool wanted = false;
        for (std::size_t j = 0; j < s.desiredCount; ++j)
            if (s.desired[j].note == a.note) {
                a.lastSeen = atSample;
                wanted = true;
                break;
            }
        if (wanted)
            continue;
        const bool pastGate = atSample - a.started >= s.gateSamples;
        const bool pastRelease = atSample - a.lastSeen >= s.releaseSamples;
        if (pastGate && pastRelease)
            noteOff(s, a, frame);
    }

    for (std::size_t j = 0; j < s.desiredCount; ++j) {
        const DesiredNote& d = s.desired[j];
        bool sounding = false;
        for (const ActiveNote& a : s.active)
            if (a.active && a.note == d.note) {
                sounding = true;
                break;
            }
        if (sounding)
            continue;
        std::size_t slot = s.desiredCount;
        for (std::size_t i = 0; i < s.active.size(); ++i)
            if (!s.active[i].active) {
                slot = i;
                break;
            }
        if (slot >= s.active.size())
            continue;
        if (!pushMidi(s, frame, static_cast<std::uint8_t>(0x90 | channel), d.note, d.velocity))
            continue;
        ActiveNote& a = s.active[slot];
        a.active = true;
        a.note = static_cast<std::uint8_t>(d.note);
        a.velocity = static_cast<std::uint8_t>(d.velocity);
        a.channel = static_cast<std::uint8_t>(channel);
        a.started = atSample;
        a.lastSeen = atSample;
        s.lastNote = d.note;
        s.lastVelocity = d.velocity;
        s.flash = 1.0f;
    }
}

}  // namespace

void prepare(EngineState& s, const double sampleRate)
{
    s.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (std::size_t i = 0; i < kWindow; ++i) {
        const double phase = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(kWindow - 1);
        s.hann[i] = static_cast<float>(0.5 - 0.5 * std::cos(phase));
    }
    s.hannSum = 0.0f;
    for (const float w : s.hann)
        s.hannSum += w;
    for (std::size_t note = 0; note < kNoteCount; ++note) {
        s.noteHz[note] = midiHz(static_cast<int>(note));
        const double omega = 2.0 * kPi * static_cast<double>(s.noteHz[note]) / s.sampleRate;
        const float cosine = static_cast<float>(std::cos(omega));
        s.goertzelCos[note] = cosine;
        s.goertzelSin[note] = static_cast<float>(std::sin(omega));
        s.goertzelCoeff[note] = 2.0f * cosine;
    }
    for (std::size_t i = 0; i < kDrumWindow; ++i) {
        const double phase = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(kDrumWindow - 1);
        s.drumHann[i] = static_cast<float>(0.5 - 0.5 * std::cos(phase));
    }
    s.drumHannSum = 0.0f;
    for (const float w : s.drumHann)
        s.drumHannSum += w;
    for (std::size_t b = 0; b < kDrumBands; ++b) {
        const double omega = 2.0 * kPi * static_cast<double>(s.noteHz[static_cast<std::size_t>(kDrumProbeNotes[b])]) /
                             s.sampleRate;
        const float cosine = static_cast<float>(std::cos(omega));
        s.drumCos[b] = cosine;
        s.drumSin[b] = static_cast<float>(std::sin(omega));
        s.drumCoeff[b] = 2.0f * cosine;
    }
    s.prepared = true;
    reset(s);
}

void reset(EngineState& s)
{
    s.ring.fill(0.0f);
    s.writeIndex = 0;
    s.sampleCounter = 0;
    s.nextAnalysis = kWindow;
    s.desired.fill({});
    s.desiredCount = 0;
    s.active.fill({});
    s.bandEnvelope.fill(0.0f);
    s.bandPeak.fill(0.0f);
    s.bandArmed.fill(true);
    s.bandLastOnset.fill(0);
    s.activeType = -1;
    s.activeChannel = -1;
    s.activeMidiOut = true;
    s.activeTranspose = 0;
    s.lastPitchedNote = 0;
    s.gateSamples = 0;
    s.releaseSamples = 0;
    s.drumCooldownSamples = 0;
    s.drumMagnitude.fill(0.0f);
    s.level = 0.0f;
    s.flash = 0.0f;
    s.lastNote = 0;
    s.lastVelocity = 0;
    s.faults = 0;
    s.midiOut = {};
}

void processBlock(EngineState& s, const Parameters& inParams, const std::uint32_t frames,
                  const float* const* inputs, float* const* outputs)
{
    const Parameters p = clampParameters(inParams);
    s.midiOut = {};
    if (!s.prepared)
        return;

    const int type = clampInt(p.inputType, 0, kInputTypeCount - 1);
    const int channel = desiredChannel(p);
    const bool midiOut = clampInt(p.midiOut, 0, 1) == 1;
    const int transpose = clampInt(p.transpose, -24, 24);
    s.gateSamples = static_cast<std::uint64_t>(p.gate * s.sampleRate / 1000.0);
    s.releaseSamples = static_cast<std::uint64_t>(p.release * s.sampleRate / 1000.0);
    s.drumCooldownSamples = static_cast<std::uint64_t>(kDrumCooldownMs * s.sampleRate / 1000.0);

    if (type != s.activeType || channel != s.activeChannel || midiOut != s.activeMidiOut ||
        transpose != s.activeTranspose) {
        releaseAll(s, 0);
        s.activeType = type;
        s.activeChannel = channel;
        s.activeMidiOut = midiOut;
        s.activeTranspose = transpose;
        s.lastPitchedNote = 0;
        s.bandEnvelope.fill(0.0f);
        s.bandPeak.fill(0.0f);
        s.bandArmed.fill(true);
        s.bandLastOnset.fill(0);
    }

    const bool thru = clampInt(p.audioThru, 0, 1) == 1;
    const std::uint64_t blockStart = s.sampleCounter;
    for (std::uint32_t f = 0; f < frames; ++f) {
        float left = inputs && inputs[0] ? inputs[0][f] : 0.0f;
        float right = inputs && inputs[1] ? inputs[1][f] : 0.0f;
        if (!std::isfinite(left)) {
            left = 0.0f;
            ++s.faults;
        }
        if (!std::isfinite(right)) {
            right = 0.0f;
            ++s.faults;
        }
        if (outputs && outputs[0])
            outputs[0][f] = thru ? left : 0.0f;
        if (outputs && outputs[1])
            outputs[1][f] = thru ? right : 0.0f;

        const float mono = 0.5f * (left + right);
        s.ring[s.writeIndex] = mono;
        s.writeIndex = (s.writeIndex + 1) % kWindow;
        ++s.sampleCounter;

        while (s.sampleCounter >= s.nextAnalysis) {
            const std::uint64_t at = s.nextAnalysis;
            const std::uint32_t frame = static_cast<std::uint32_t>(at - blockStart - 1);
            s.desiredCount = 0;
            measure(s);
            if (type == static_cast<int>(InputType::Drums)) {
                measureDrums(s);
                detectDrums(s, p, at, transpose);
            } else {
                detectPitched(s, p, type, transpose);
            }
            syncNotes(s, p, at, frame);
            s.nextAnalysis += kHop;
        }
    }

    if (frames > 0)
        s.flash *= static_cast<float>(std::pow(0.90, static_cast<double>(frames) / static_cast<double>(kHop)));
}

}  // namespace downspout::polyscribe
