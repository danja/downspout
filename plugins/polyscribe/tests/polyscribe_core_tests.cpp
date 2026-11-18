#include "downspout/test_assert.h"

#include "polyscribe_core_types.hpp"
#include "polyscribe_params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace downspout::polyscribe;

constexpr int kSr = 44100;
constexpr double kPi = 3.14159265358979323846;

struct Event {
    std::uint64_t frame = 0;
    std::uint8_t status = 0;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
};

struct Result {
    EngineState state {};
    std::vector<Event> events {};
    std::vector<float> outL {};
    std::vector<float> outR {};
};

std::vector<float> zeros(const double seconds)
{
    return std::vector<float>(static_cast<std::size_t>(seconds * kSr), 0.0f);
}

void addTone(std::vector<float>& signal, const int note, const double from, const double seconds,
             const float amplitude, const double sampleRate = kSr)
{
    const std::size_t start = static_cast<std::size_t>(from * sampleRate);
    const std::size_t count = static_cast<std::size_t>(seconds * sampleRate);
    if (signal.size() < start + count)
        signal.resize(start + count, 0.0f);
    const double hz = 440.0 * std::exp2((static_cast<double>(note) - 69.0) / 12.0);
    for (std::size_t i = 0; i < count; ++i)
        signal[start + i] += amplitude * static_cast<float>(std::sin(2.0 * kPi * hz * i / sampleRate));
}

std::vector<float> tone(const int note, const double seconds, const float amplitude)
{
    std::vector<float> signal;
    addTone(signal, note, 0.0, seconds, amplitude);
    return signal;
}

std::vector<Event> drain(const EngineState& s, const std::uint64_t base)
{
    std::vector<Event> out;
    for (std::size_t i = 0; i < s.midiOut.count; ++i) {
        const MidiOutEvent& e = s.midiOut.events[i];
        out.push_back({base + e.frame, e.data[0], e.data[1], e.data[2]});
    }
    return out;
}

Result run(const Parameters& inParams, const std::vector<float>& signal, const std::size_t block = 512,
           const int sampleRate = kSr, const bool capture = false,
           const std::vector<float>* rightChannel = nullptr)
{
    const Parameters p = clampParameters(inParams);
    Result result;
    prepare(result.state, sampleRate);
    result.outL.assign(signal.size(), 0.0f);
    result.outR.assign(signal.size(), 0.0f);
    for (std::size_t offset = 0; offset < signal.size(); offset += block) {
        const std::size_t n = std::min(block, signal.size() - offset);
        const float* inputs[2] = {signal.data() + offset,
                                  rightChannel ? rightChannel->data() + offset : signal.data() + offset};
        float left[4096], right[4096];
        float* outputs[2] = {capture ? left : nullptr, capture ? right : nullptr};
        processBlock(result.state, p, static_cast<std::uint32_t>(n), inputs, capture ? outputs : nullptr);
        const std::vector<Event> got = drain(result.state, offset);
        result.events.insert(result.events.end(), got.begin(), got.end());
        if (capture)
            for (std::size_t i = 0; i < n; ++i) {
                result.outL[offset + i] = left[i];
                result.outR[offset + i] = right[i];
            }
    }
    return result;
}

bool isOn(const Event& e) { return (e.status & 0xF0) == 0x90 && e.velocity > 0; }
bool isOff(const Event& e) { return (e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.velocity == 0); }

std::set<int> notesOn(const std::vector<Event>& events)
{
    std::set<int> notes;
    for (const Event& e : events)
        if (isOn(e))
            notes.insert(e.note);
    return notes;
}

int countOn(const std::vector<Event>& events)
{
    int n = 0;
    for (const Event& e : events)
        n += isOn(e);
    return n;
}

int countOff(const std::vector<Event>& events)
{
    int n = 0;
    for (const Event& e : events)
        n += isOff(e);
    return n;
}

bool balanced(const std::vector<Event>& events)
{
    std::multiset<std::pair<int, int>> on, off;
    for (const Event& e : events) {
        const std::pair<int, int> key {e.status & 0x0F, e.note};
        if (isOn(e))
            on.insert(key);
        if (isOff(e))
            off.insert(key);
    }
    return on == off;
}

const Event& firstOn(const std::vector<Event>& events)
{
    for (const Event& e : events)
        if (isOn(e))
            return e;
    assert(false && "expected a note-on");
    static const Event dummy {};
    return dummy;
}

bool operator==(const Event& a, const Event& b)
{
    return a.frame == b.frame && a.status == b.status && a.note == b.note && a.velocity == b.velocity;
}

std::vector<float> withTail(std::vector<float> signal, const double seconds = 0.4)
{
    const std::vector<float> silence = zeros(seconds);
    signal.insert(signal.end(), silence.begin(), silence.end());
    return signal;
}

}  // namespace

int main()
{
    const Parameters base;

    // ---- Defaults and the input-type names ---------------------------------------------------------------------
    assert(kParameterSpecs.size() == kParameterCount);
    assert(kInputParameterCount == 13);
    for (std::size_t i = 0; i < kInputParameterCount; ++i) {
        const ParamSpec& spec = kParameterSpecs[i];
        assert(spec.minimum <= spec.defaultValue && spec.defaultValue <= spec.maximum);
    }
    assert(std::string(kInputTypeNames[0]) == "Melody" && std::string(kInputTypeNames[3]) == "Drums");
    assert(clampParameters(base).sensitivity == base.sensitivity);

    // ---- Melody: one strong pitch, with balanced note-on/off -------------------------------------------------------
    {
        const Result r = run(base, withTail(tone(64, 0.35f, 0.5f)));
        assert(notesOn(r.events) == std::set<int> {64});
        assert(countOn(r.events) == 1 && countOff(r.events) == 1);
        assert(balanced(r.events));
        assert(r.state.lastNote == 64 && r.state.lastVelocity > 0);
        assert(r.state.level > 0.0f && r.state.flash >= 0.0f);
        assert(std::all_of(r.state.midiOut.events.begin(), r.state.midiOut.events.end(),
                           [](const MidiOutEvent& e) { return e.frame < 512; }));  // drained per block
    }

    // ---- Chords: three simultaneous pitches; Melody deliberately keeps only one ----------------------------------
    std::vector<float> chord;
    addTone(chord, 60, 0.0, 0.35, 0.4);
    addTone(chord, 64, 0.0, 0.35, 0.4);
    addTone(chord, 67, 0.0, 0.35, 0.4);
    const std::vector<float> chordTail = withTail(chord);
    {
        Parameters p = base;
        p.inputType = static_cast<float>(InputType::Chords);
        const Result r = run(p, chordTail);
        assert(notesOn(r.events) == (std::set<int> {60, 64, 67}));
        assert(balanced(r.events));
    }
    {
        Parameters p = base;
        p.inputType = static_cast<float>(InputType::Melody);
        const Result r = run(p, chordTail);
        assert(countOn(r.events) == 1);
        assert(notesOn(r.events).size() == 1);
        assert(*notesOn(r.events).begin() >= 60 && *notesOn(r.events).begin() <= 67);
        assert(balanced(r.events));
    }

    // ---- Bass: only the low note is chosen, even with a loud high note present ----------------------------------
    {
        std::vector<float> signal;
        addTone(signal, 48, 0.0, 0.35, 0.5);
        addTone(signal, 72, 0.0, 0.35, 0.5);
        Parameters p = base;
        p.inputType = static_cast<float>(InputType::Bass);
        const Result r = run(p, withTail(signal));
        assert(notesOn(r.events) == std::set<int> {48});
        assert(balanced(r.events));
    }

    // ---- Drums: three band-limited transients map to kick / snare / hat -------------------------------------------
    {
        std::vector<float> signal(static_cast<std::size_t>(1.3 * kSr), 0.0f);
        addTone(signal, 36, 0.0, 0.20, 0.7);         // 65 Hz: low band
        addTone(signal, 62, 0.40, 0.20, 0.7);        // 294 Hz: mid band
        addTone(signal, 108, 0.80, 0.20, 0.7);       // 4.2 kHz: difference signal
        Parameters p = base;
        p.inputType = static_cast<float>(InputType::Drums);
        const Result r = run(p, signal);
        assert(notesOn(r.events) == (std::set<int> {kDrumKick, kDrumSnare, kDrumHat}));
        assert(countOn(r.events) == 3);
        assert(balanced(r.events));
    }

    // ---- Sensitivity: a quiet note is rejected at zero, accepted at the default and at full ------------------------
    {
        const std::vector<float> quiet = withTail(tone(64, 0.35, 0.12f));
        Parameters low = base; low.sensitivity = 0.0f;
        Parameters high = base; high.sensitivity = 1.0f;
        assert(notesOn(run(low, quiet).events).empty());
        assert(notesOn(run(base, quiet).events) == std::set<int> {64});
        assert(notesOn(run(high, quiet).events) == std::set<int> {64});
    }

    // ---- Low and High note filters --------------------------------------------------------------------------------
    {
        const std::vector<float> signal = withTail(tone(64, 0.35, 0.5f));
        Parameters low = base; low.minNote = 70.0f;
        Parameters high = base; high.maxNote = 58.0f;
        assert(notesOn(run(low, signal).events).empty());
        assert(notesOn(run(high, signal).events).empty());
        assert(notesOn(run(base, signal).events) == std::set<int> {64});
    }

    // ---- Max Notes bounds polyphony -------------------------------------------------------------------------------
    {
        Parameters p = base;
        p.inputType = static_cast<float>(InputType::Chords);
        p.maxNotes = 1.0f;
        const Result one = run(p, chordTail);
        assert(countOn(one.events) == 1);
        p.maxNotes = 3.0f;
        const Result three = run(p, chordTail);
        assert(notesOn(three.events) == (std::set<int> {60, 64, 67}));
        assert(balanced(one.events) && balanced(three.events));
    }

    // ---- Gate holds a note at the start even after the input stops -------------------------------------------------
    {
        std::vector<float> signal = tone(64, 0.093f, 0.5f);
        const std::vector<float> tail = zeros(0.4);
        signal.insert(signal.end(), tail.begin(), tail.end());
        Parameters p = base;
        p.release = 0.0f;
        p.gate = 0.0f;
        const Result shortGate = run(p, signal);
        p.gate = 100.0f;
        const Result longGate = run(p, signal);
        assert(countOff(shortGate.events) == 1 && countOff(longGate.events) == 1);
        // The 4096-sample pitched window keeps the tone visible for a while after
        // it stops, so Gate extends the release by roughly its own length, not by
        // the full distance from the end of the input signal.
        assert(longGate.events.back().frame > shortGate.events.back().frame + 1000);
    }

    // ---- Release delays note-off after the pitch disappears -------------------------------------------------------
    {
        const std::vector<float> signal = withTail(tone(64, 0.2f, 0.5f), 0.7);
        Parameters p = base;
        p.gate = 0.0f;
        p.release = 0.0f;
        const Result shortRelease = run(p, signal);
        p.release = 500.0f;
        const Result longRelease = run(p, signal);
        assert(shortRelease.events.back().frame < longRelease.events.back().frame);
        assert(longRelease.events.back().frame - shortRelease.events.back().frame > 15000);
        assert(balanced(shortRelease.events) && balanced(longRelease.events));
    }

    // ---- Velocity, Dynamics, Transpose and MIDI Channel ------------------------------------------------------------
    {
        const std::vector<float> signal = withTail(tone(64, 0.35f, 0.5f));
        Parameters soft = base; soft.velocity = 40.0f; soft.dynamics = 0.0f;
        Parameters loud = base; loud.velocity = 110.0f; loud.dynamics = 0.0f;
        assert(run(soft, signal).state.lastVelocity == 40);
        assert(run(loud, signal).state.lastVelocity == 110);

        Parameters fixed = base; fixed.dynamics = 0.0f;
        Parameters moving = base; moving.dynamics = 1.0f;
        const int fixedQuiet = run(fixed, withTail(tone(64, 0.35f, 0.12f))).state.lastVelocity;
        const int fixedLoud = run(fixed, signal).state.lastVelocity;
        const int movingQuiet = run(moving, withTail(tone(64, 0.35f, 0.12f))).state.lastVelocity;
        const int movingLoud = run(moving, signal).state.lastVelocity;
        assert(fixedQuiet == fixedLoud);
        assert(movingLoud > movingQuiet);

        Parameters up = base; up.transpose = 12.0f;
        assert(notesOn(run(up, signal).events) == std::set<int> {76});
        Parameters channel = base; channel.channel = 7.0f;
        const Result seven = run(channel, signal);
        assert((firstOn(seven.events).status & 0x0F) == 6);
        assert(balanced(seven.events));
    }

    // ---- MIDI Out off emits nothing; Audio Thru mirrors the input only when enabled ---------------------------------
    {
        const std::vector<float> signal = withTail(tone(64, 0.35f, 0.5f));
        Parameters off = base; off.midiOut = 0.0f;
        assert(run(off, signal).events.empty());

        const std::vector<float> rightChannel(signal.size(), 0.0f);
        Parameters silent = base; silent.audioThru = 0.0f;
        Parameters thru = base; thru.audioThru = 1.0f;
        const Result a = run(silent, signal, 512, kSr, true, &rightChannel);
        const Result b = run(thru, signal, 512, kSr, true, &rightChannel);
        for (std::size_t i = 0; i < signal.size(); ++i) {
            assert(a.outL[i] == 0.0f && a.outR[i] == 0.0f);
            assert(b.outL[i] == signal[i] && b.outR[i] == 0.0f);
        }
    }

    // ---- Changing channel releases the old notes and retriggers on the new one -------------------------------------
    {
        EngineState s;
        prepare(s, kSr);
        std::vector<Event> events;
        const std::vector<float> signal = withTail(tone(64, 0.5f, 0.5f), 0.4);
        const std::size_t switchAt = static_cast<std::size_t>(0.28 * kSr);
        for (std::size_t offset = 0; offset < signal.size(); offset += 256) {
            Parameters p = base;
            p.channel = offset >= switchAt ? 2.0f : 1.0f;
            const std::size_t n = std::min<std::size_t>(256, signal.size() - offset);
            const float* inputs[2] = {signal.data() + offset, signal.data() + offset};
            processBlock(s, p, static_cast<std::uint32_t>(n), inputs, nullptr);
            const std::vector<Event> got = drain(s, offset);
            events.insert(events.end(), got.begin(), got.end());
        }
        bool oldOff = false, newOn = false;
        for (const Event& e : events) {
            oldOff |= isOff(e) && (e.status & 0x0F) == 0;
            newOn |= isOn(e) && (e.status & 0x0F) == 1;
        }
        assert(oldOff && newOn);
        assert(balanced(events));
    }

    // ---- Activation and sample-rate changes clear runtime state but keep the patch usable --------------------------
    {
        EngineState s;
        prepare(s, kSr);
        const std::vector<float> signal = tone(64, 0.35f, 0.5f);
        const float* inputs[2] = {signal.data(), signal.data()};
        processBlock(s, base, static_cast<std::uint32_t>(signal.size()), inputs, nullptr);
        assert(activeNoteCount(s) > 0);
        const int detected = lastNote(s);
        assert(detected == 64);

        reset(s);  // activate(): runtime only
        assert(activeNoteCount(s) == 0 && lastNote(s) == 0 && faultCount(s) == 0);
        processBlock(s, base, static_cast<std::uint32_t>(signal.size()), inputs, nullptr);
        assert(activeNoteCount(s) > 0 && lastNote(s) == detected);

        prepare(s, 48000);  // sample-rate change: runtime only, same patch
        assert(activeNoteCount(s) == 0 && lastNote(s) == 0);
        std::vector<float> at48;
        addTone(at48, 64, 0.0, 0.35, 0.5, 48000);
        const float* inputs48[2] = {at48.data(), at48.data()};
        processBlock(s, base, static_cast<std::uint32_t>(at48.size()), inputs48, nullptr);
        assert(activeNoteCount(s) > 0 && lastNote(s) == 64);
    }

    // ---- Non-finite input is treated as silence ---------------------------------------------------------------------
    {
        const std::vector<float> nanSignal(static_cast<std::size_t>(0.2 * kSr), std::nanf(""));
        Parameters p = base; p.audioThru = 1.0f;
        const Result r = run(p, nanSignal, 512, kSr, true);
        assert(r.state.faults > 0);
        assert(r.events.empty());
        for (const float x : r.outL)
            assert(x == 0.0f);
    }

    // ---- Block-size invariance: analysis is driven by absolute sample counts --------------------------------------
    {
        std::vector<float> signal;
        addTone(signal, 60, 0.0, 0.30, 0.4);
        addTone(signal, 67, 0.0, 0.30, 0.4);
        const std::vector<float> tail = withTail(signal);
        Parameters p = base; p.inputType = static_cast<float>(InputType::Chords);
        const Result small = run(p, tail, 64);
        const Result large = run(p, tail, 4096);
        assert(small.events == large.events);
        assert(balanced(small.events));
    }

    std::printf("All polyscribe core tests passed.\n");
    return 0;
}
