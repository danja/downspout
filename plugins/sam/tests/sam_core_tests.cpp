#include "downspout/test_assert.h"
#include "sam_engine.hpp"
#include "sam_params.hpp"
#include "sam_speech.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <new>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace downspout::sam;

namespace {

constexpr double kSampleRate = 48000.0;

// ── Allocation counter: the audio path must not allocate ──────────────────────

std::atomic<long> gAllocations {0};
std::atomic<bool> gCountAllocations {false};

}  // namespace

// GCC inlines std::thread and std::function storage into the test and then sees a pair of
// replaced allocation functions that wrap malloc and free as mismatched; they are not.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

void* operator new(const std::size_t size)
{
    if (gCountAllocations.load())
        gAllocations.fetch_add(1);
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr)
        throw std::bad_alloc();
    return memory;
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace {

// ── Helpers ────────────────────────────────────────────────────────────────

struct Note {
    std::size_t on = 0;
    std::size_t off = 0;
    int note = 60;
    int velocity = 100;
    int channel = 0;
};

// An engine driven the way a host would: in blocks, with a transport and notes.
struct Rig {
    Engine engine;
    double sampleRate = kSampleRate;

    explicit Rig(const double sr = kSampleRate) : sampleRate(sr) { engine.activate(sr); }

    void set(const ParamId id, const float value) { engine.setParameter(paramIndex(id), value); }

    // Keys: render with notes placed at absolute frames.
    std::vector<float> keys(const std::vector<Note>& notes, const std::size_t frames, const std::size_t block = 256)
    {
        std::vector<float> out(frames, 0.0f), right(frames, 0.0f);
        const Transport stopped;
        for (std::size_t done = 0; done < frames; done += block) {
            const std::size_t n = std::min(block, frames - done);
            std::vector<MidiEvent> events;
            for (const Note& note : notes) {
                const auto place = [&](const std::size_t at, const std::uint8_t status, const std::uint8_t velocity) {
                    if (at >= done && at < done + n) {
                        MidiEvent ev;
                        ev.frame = static_cast<std::uint32_t>(at - done);
                        ev.size = 3;
                        ev.data[0] = static_cast<std::uint8_t>(status | note.channel);
                        ev.data[1] = static_cast<std::uint8_t>(note.note);
                        ev.data[2] = velocity;
                        events.push_back(ev);
                    }
                };
                place(note.on, 0x90, static_cast<std::uint8_t>(note.velocity));
                if (note.off > 0)
                    place(note.off, 0x80, 0);
            }
            engine.process(stopped, events.data(), static_cast<std::uint32_t>(events.size()),
                           static_cast<std::uint32_t>(n), out.data() + done, right.data() + done);
        }
        return out;
    }

    // Transport: render from `startBeat` (absolute, zero-based) at a steady tempo.
    std::vector<float> transport(const double startBeat, const double bpm, const std::size_t frames,
                                 const std::size_t block = 256, const double beatsPerBar = 4.0, const bool playing = true)
    {
        std::vector<float> out(frames, 0.0f), right(frames, 0.0f);
        const double beatsPerFrame = bpm / (60.0 * sampleRate);
        for (std::size_t done = 0; done < frames; done += block) {
            const std::size_t n = std::min(block, frames - done);
            const double beat = startBeat + static_cast<double>(done) * beatsPerFrame;
            Transport t;
            t.valid = true;
            t.playing = playing;
            t.bpm = bpm;
            t.beatsPerBar = beatsPerBar;
            t.bar = std::floor(beat / beatsPerBar);
            t.barBeat = beat - t.bar * beatsPerBar;
            engine.process(t, nullptr, 0, static_cast<std::uint32_t>(n), out.data() + done, right.data() + done);
        }
        return out;
    }
};

double rms(const std::vector<float>& x, std::size_t begin, std::size_t end)
{
    end = std::min(end, x.size());
    if (begin >= end)
        return 0.0;
    double sum = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(end - begin));
}

double peak(const std::vector<float>& x, std::size_t begin, std::size_t end)
{
    end = std::min(end, x.size());
    double p = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        p = std::max(p, static_cast<double>(std::fabs(x[i])));
    return p;
}

// First index in [begin, end) whose magnitude exceeds `threshold`, or `end`.
std::size_t firstAbove(const std::vector<float>& x, std::size_t begin, std::size_t end, const float threshold)
{
    end = std::min(end, x.size());
    for (std::size_t i = begin; i < end; ++i) {
        if (std::fabs(x[i]) > threshold)
            return i;
    }
    return end;
}

double rmsDifference(const std::vector<float>& a, const std::vector<float>& b)
{
    const std::size_t n = std::min(a.size(), b.size());
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(a[i]) - b[i];
        sum += d * d;
    }
    return n ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
}

bool allFinite(const std::vector<float>& x)
{
    return std::all_of(x.begin(), x.end(), [](const float v) { return std::isfinite(v) && std::fabs(v) <= 1.0f; });
}

// Pitch by autocorrelation: the first strong peak near `guessHz`, refined by a parabola.
double estimateF0(const std::vector<float>& x, const std::size_t begin, const std::size_t end, const double sr,
                  const double guessHz)
{
    const auto lagMin = static_cast<std::size_t>(sr / (guessHz * 1.6));
    const auto lagMax = static_cast<std::size_t>(sr / (guessHz / 1.6));
    std::vector<double> r(lagMax + 2, 0.0);
    for (std::size_t lag = lagMin; lag <= lagMax + 1; ++lag) {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (std::size_t i = begin; i + lag < end; ++i) {
            ab += static_cast<double>(x[i]) * x[i + lag];
            aa += static_cast<double>(x[i]) * x[i];
            bb += static_cast<double>(x[i + lag]) * x[i + lag];
        }
        r[lag] = ab / std::sqrt(aa * bb + 1.0e-30);
    }
    double best = 0.0;
    for (std::size_t lag = lagMin; lag <= lagMax; ++lag)
        best = std::max(best, r[lag]);
    for (std::size_t lag = lagMin + 1; lag <= lagMax; ++lag) {
        if (r[lag] >= 0.95 * best && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) {
            const double a = r[lag - 1], b = r[lag], c = r[lag + 1];
            const double denom = a - 2.0 * b + c;
            const double shift = denom != 0.0 ? 0.5 * (a - c) / denom : 0.0;
            return sr / (static_cast<double>(lag) + shift);
        }
    }
    return 0.0;
}

constexpr const char* kSustained = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";  // 46 chars of one vowel

void usePhonetic(Rig& rig, const std::string& text)
{
    rig.set(ParamId::phonetic, 1.0f);
    rig.engine.setText(text);
}

void pass(const char* name) { std::printf("PASS %s\n", name); }

// ── SAM wrapper ────────────────────────────────────────────────────────────

void testSamWrapperRendersDeterministically()
{
    std::vector<std::uint8_t> a(kMaxRenderSamples), b(kMaxRenderSamples), other(kMaxRenderSamples);
    const VoiceSettings settings;
    const std::size_t n1 = renderWord("HELLO", settings, a.data(), a.size());
    assert(n1 > 3000 && n1 < 40000);
    assert(std::any_of(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(n1), [](const std::uint8_t v) { return v != 128; }));

    // A different word in between must not change a later render of the first (the vendored
    // code kept state in a function-local static before it was patched).
    assert(renderWord("WORLD", settings, other.data(), other.size()) > 0);
    const std::size_t n2 = renderWord("HELLO", settings, b.data(), b.size());
    assert(n1 == n2);
    assert(std::equal(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(n1), b.begin()));
    pass("testSamWrapperRendersDeterministically");
}

void testSamWrapperRespectsLimits()
{
    std::vector<std::uint8_t> out(kMaxRenderSamples);
    const VoiceSettings settings;
    assert(renderWord("", settings, out.data(), out.size()) == 0);
    assert(renderWord("HELLO", settings, nullptr, 100) == 0);

    // A buffer smaller than the render gets only what fits, and nothing beyond it is touched.
    std::vector<std::uint8_t> small(64 + 16, 77);
    const std::size_t n = renderWord("HELLO", settings, small.data(), 64);
    assert(n == 64);
    for (std::size_t i = 64; i < small.size(); ++i)
        assert(small[i] == 77);

    // The longest word still renders inside SAM's buffer.
    const std::string longest(kMaxWordChars, 'A');
    VoiceSettings phonetic;
    phonetic.phonetic = true;
    const std::size_t big = renderWord(longest, phonetic, out.data(), out.size());
    assert(big > 0 && big <= kMaxRenderSamples);
    pass("testSamWrapperRespectsLimits");
}

// ── Script ─────────────────────────────────────────────────────────────────

void testScriptParsing()
{
    const Script s = parseScript("  Hello  world.\n_ foo\t");
    assert(s.size() == 4);
    assert(s.tokens[0].word == "Hello" && s.tokens[0].begin == 2 && s.tokens[0].length == 5);
    assert(s.tokens[1].word == "world.");
    assert(s.tokens[2].rest && s.tokens[2].word == "_");
    assert(s.tokens[3].word == "foo" && !s.tokens[3].rest);
    // Positions refer to the cleaned text, which is what the panel holds.
    for (const Token& t : s.tokens)
        assert(s.text.substr(t.begin, t.length).substr(0, t.word.size()) == t.word);

    assert(parseScript("").size() == 0);
    assert(parseScript("   \n  ").size() == 0);
    assert(parseScript("caf\xc3\xa9 \x01x").size() == 2);  // non-ASCII and control bytes are dropped

    const std::string huge(kMaxTextBytes * 3, 'a');
    assert(parseScript(huge).text.size() == kMaxTextBytes);

    std::string many;
    for (std::size_t i = 0; i < kMaxSlots + 50; ++i)
        many += "a ";
    assert(parseScript(many).size() == kMaxSlots);
    pass("testScriptParsing");
}

void testWordCleaning()
{
    assert(sanitizeWord("Hello!", false) == "HELLO");
    assert(sanitizeWord("don't", false) == "DON'T");
    assert(sanitizeWord("a1b2", false) == "AB");
    assert(sanitizeWord("/heh3low2", true) == "/HEH3LOW2");
    assert(sanitizeWord("a'b", true) == "AB");
    assert(sanitizeWord("????", false) == "????");
    assert(sanitizeWord(std::string(200, 'x'), false).size() == kMaxWordChars);
    pass("testWordCleaning");
}

// ── Parameters ─────────────────────────────────────────────────────────────

void testParameterTable()
{
    std::set<std::string> symbols;
    for (std::size_t i = 0; i < kParameterCount; ++i) {
        const ParamSpec& p = kParameterSpecs[i];
        assert(symbols.insert(p.symbol).second);
        assert(p.minimum < p.maximum);
        assert(p.defaultValue >= p.minimum && p.defaultValue <= p.maximum);
    }
    assert(kParameterSpecs[kInputParameterCount].output);
    for (std::size_t i = 0; i < kInputParameterCount; ++i)
        assert(!kParameterSpecs[i].output);

    for (const VoicePreset& v : kVoicePresets) {
        const auto inside = [](const ParamId id, const int value) {
            const ParamSpec& p = kParameterSpecs[paramIndex(id)];
            return value >= p.minimum && value <= p.maximum;
        };
        assert(inside(ParamId::speed, v.speed) && inside(ParamId::pitch, v.pitch));
        assert(inside(ParamId::throat, v.throat) && inside(ParamId::mouth, v.mouth));
    }
    pass("testParameterTable");
}

void testParameterAccess()
{
    Engine engine;
    const auto idx = [](const ParamId id) { return paramIndex(id); };
    engine.setParameter(idx(ParamId::speed), 9999.0f);
    assert(engine.getParameter(idx(ParamId::speed)) == 200.0f);
    engine.setParameter(idx(ParamId::speed), -5.0f);
    assert(engine.getParameter(idx(ParamId::speed)) == 20.0f);
    engine.setParameter(idx(ParamId::transpose), 3.6f);
    assert(engine.getParameter(idx(ParamId::transpose)) == 4.0f);       // integers round
    engine.setParameter(idx(ParamId::outWord), 5.0f);
    assert(engine.getParameter(idx(ParamId::outWord)) == 0.0f);         // output is read-only
    engine.setParameter(idx(ParamId::speak), 1.0f);
    assert(engine.getParameter(idx(ParamId::speak)) == 0.0f);           // a trigger never reads back as held
    assert(engine.getParameter(9999) == 0.0f);
    engine.setText("one two");
    assert(engine.text() == "one two");
    engine.setText(std::string(kMaxTextBytes + 500, 'z'));
    assert(engine.text().size() == kMaxTextBytes);
    pass("testParameterAccess");
}

// ── Keys mode ──────────────────────────────────────────────────────────────

void testKeysPitchIsInTune()
{
    // Whole-period pitch bytes land up to a few semitones off at the top of the range; the
    // playback rate makes up the remainder. Check across the keyboard, including the keys
    // where the nearest period is furthest from the target.
    for (const int note : {33, 45, 57, 62, 69, 74, 81, 88}) {
        Rig rig;
        usePhonetic(rig, kSustained);
        rig.set(ParamId::tone, 12000.0f);
        const std::vector<float> out = rig.keys({{2000, 0, note, 100, 0}}, 40000);
        const double target = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        const double f0 = estimateF0(out, 6000, 18000, kSampleRate, target);
        const double error = std::fabs(f0 - target) / target;
        if (error > 0.015)
            std::printf("note %d: wanted %.2f Hz, measured %.2f Hz\n", note, target, f0);
        assert(error < 0.015);
    }
    pass("testKeysPitchIsInTune");
}

void testKeysNoteStartsAtItsFrame()
{
    Rig rig;
    usePhonetic(rig, kSustained);
    const std::vector<float> out = rig.keys({{1000, 0, 60, 100, 0}}, 8000);
    for (std::size_t i = 0; i < 1000; ++i)
        assert(out[i] == 0.0f);
    const std::size_t first = firstAbove(out, 1000, 8000, 1.0e-3f);
    assert(first >= 1000 && first < 1000 + 480);
    assert(peak(out, 1000, 1000 + 2400) > 0.05);
    assert(allFinite(out));
    pass("testKeysNoteStartsAtItsFrame");
}

void testNoteOffReleasesAndReleaseTimeMatters()
{
    const std::size_t off = 12000;
    double tailShort = 0.0, tailLong = 0.0;
    for (const float releaseMs : {5.0f, 400.0f}) {
        Rig rig;
        usePhonetic(rig, kSustained);
        rig.set(ParamId::release, releaseMs);
        const std::vector<float> out = rig.keys({{0, off, 57, 100, 0}}, 60000);
        assert(rms(out, off - 2000, off) > 0.02);  // still speaking when the key comes up
        (releaseMs < 100.0f ? tailShort : tailLong) = rms(out, off + 480, off + 4800);
        if (releaseMs < 100.0f)
            assert(rms(out, off + 4800, off + 9600) < 1.0e-4);
        else
            assert(rms(out, off + 4800, off + 9600) < tailLong);  // and it does end
        assert(rms(out, off + 40000, 60000) < 1.0e-4);
    }
    assert(tailLong > 5.0 * tailShort);
    pass("testNoteOffReleasesAndReleaseTimeMatters");
}

void testNextWordCyclesThroughTheText()
{
    Rig rig;
    usePhonetic(rig, "AA IY");
    rig.set(ParamId::release, 20.0f);
    const std::size_t gap = 24000;
    const std::vector<float> out = rig.keys({{0, 6000, 60, 100, 0}, {gap, gap + 6000, 60, 100, 0}, {2 * gap, 2 * gap + 6000, 60, 100, 0}},
                                            3 * gap);
    const std::vector<float> first(out.begin(), out.begin() + 8000);
    const std::vector<float> second(out.begin() + gap, out.begin() + gap + 8000);
    const std::vector<float> third(out.begin() + 2 * gap, out.begin() + 2 * gap + 8000);
    assert(rmsDifference(first, second) > 0.01);   // the second note speaks the second word
    assert(rmsDifference(first, third) < 1.0e-5);  // and the third comes round to the first again
    pass("testNextWordCyclesThroughTheText");
}

void testKeyEqualsWordSelectsByKey()
{
    // The same pitch reached from different keys (transposed to match) speaks the word the key selects.
    const auto speak = [](const int note, const int transpose) {
        Rig rig;
        usePhonetic(rig, "AA IY");
        rig.set(ParamId::advance, 1.0f);
        rig.set(ParamId::transpose, static_cast<float>(transpose));
        rig.set(ParamId::release, 20.0f);
        return rig.keys({{0, 6000, note, 100, 0}}, 9000);
    };
    const auto c3 = speak(48, 0);
    const auto c3Again = speak(50, -2);  // key 50 selects word 2 mod 2 = word 0, at the same pitch
    const auto other = speak(49, -1);    // key 49 selects word 1
    assert(rmsDifference(c3, c3Again) < 1.0e-5);
    assert(rmsDifference(c3, other) > 0.01);
    pass("testKeyEqualsWordSelectsByKey");
}

void testVelocityScalesLevel()
{
    const auto level = [](const int velocity) {
        Rig rig;
        usePhonetic(rig, kSustained);
        return peak(rig.keys({{0, 0, 57, velocity, 0}}, 12000), 0, 12000);
    };
    const double loud = level(127), soft = level(32);
    const double expected = 1.0 / (0.25 + 0.75 * 32.0 / 127.0);
    assert(std::fabs(loud / soft - expected) < 0.1 * expected);
    pass("testVelocityScalesLevel");
}

void testChannelFilter()
{
    const auto energy = [](const int setting, const int channel) {
        Rig rig;
        usePhonetic(rig, kSustained);
        rig.set(ParamId::midiCh, static_cast<float>(setting));
        return rms(rig.keys({{0, 0, 57, 100, channel}}, 12000), 0, 12000);
    };
    assert(energy(17, 5) > 0.02);   // All
    assert(energy(6, 5) > 0.02);    // channel 6 is status channel 5
    assert(energy(2, 5) < 1.0e-6);  // some other channel
    assert(energy(0, 5) < 1.0e-6);  // Off
    pass("testChannelFilter");
}

void testPolyphonyAndStealing()
{
    Rig rig;
    usePhonetic(rig, kSustained);
    std::vector<Note> notes;
    for (int i = 0; i < 12; ++i)
        notes.push_back({static_cast<std::size_t>(i) * 400, 0, 48 + i, 100, 0});
    const std::vector<float> out = rig.keys(notes, 20000);
    assert(allFinite(out));
    assert(rig.engine.activeVoices() <= Engine::kVoices);
    assert(rms(out, 10000, 20000) > 0.02);
    pass("testPolyphonyAndStealing");
}

void testAllNotesOffAndPitchBend()
{
    // Pitch bend at full travel is two semitones.
    const auto pitchWith = [](const int bendValue) {
        Rig rig;
        usePhonetic(rig, kSustained);
        rig.set(ParamId::tone, 12000.0f);
        std::vector<float> out(30000), right(30000);
        const Transport stopped;
        MidiEvent bend;
        bend.size = 3;
        bend.data[0] = 0xE0;
        bend.data[1] = static_cast<std::uint8_t>(bendValue & 0x7f);
        bend.data[2] = static_cast<std::uint8_t>(bendValue >> 7);
        MidiEvent on;
        on.size = 3;
        on.data[0] = 0x90;
        on.data[1] = 57;
        on.data[2] = 100;
        const MidiEvent first[2] = {on, bend};
        rig.engine.process(stopped, first, 2, 256, out.data(), right.data());
        rig.engine.process(stopped, nullptr, 0, 29744, out.data() + 256, right.data() + 256);
        return estimateF0(out, 6000, 18000, kSampleRate, 220.0 * 1.06);
    };
    const double centre = pitchWith(8192), up = pitchWith(16383);
    assert(std::fabs(up / centre - std::pow(2.0, 2.0 / 12.0)) < 0.01);

    Rig rig;
    usePhonetic(rig, kSustained);
    std::vector<float> out(24000), right(24000);
    const Transport stopped;
    MidiEvent on;
    on.size = 3;
    on.data[0] = 0x90;
    on.data[1] = 57;
    on.data[2] = 100;
    rig.engine.process(stopped, &on, 1, 4000, out.data(), right.data());
    assert(rig.engine.activeVoices() == 1);
    MidiEvent allOff;
    allOff.size = 3;
    allOff.data[0] = 0xB0;
    allOff.data[1] = 123;
    rig.engine.process(stopped, &allOff, 1, 20000, out.data() + 4000, right.data() + 4000);
    assert(rig.engine.activeVoices() == 0);
    assert(rms(out, 8000, 24000) < 1.0e-5);
    pass("testAllNotesOffAndPitchBend");
}

// ── Transport mode ─────────────────────────────────────────────────────────

constexpr std::size_t kQuarter = 24000;  // one beat at 120 bpm, 48 kHz

void testWordsLandOnTheirSteps()
{
    Rig rig;
    rig.set(ParamId::mode, 1.0f);
    usePhonetic(rig, "AA IY AA IY");
    rig.set(ParamId::tone, 12000.0f);
    const std::vector<float> out = rig.transport(0.0, 120.0, 4 * kQuarter);
    assert(allFinite(out));
    for (std::size_t slot = 0; slot < 4; ++slot) {
        const std::size_t boundary = slot * kQuarter;
        if (slot > 0)
            assert(peak(out, boundary - 2000, boundary) < 1.0e-5);  // the last word has finished
        const std::size_t first = firstAbove(out, boundary > 100 ? boundary - 100 : 0, boundary + 2400, 1.0e-4f);
        assert(first >= boundary && first <= boundary + 240);        // and the next starts on the step
        assert(peak(out, boundary, boundary + 4800) > 0.05);
    }
    pass("testWordsLandOnTheirSteps");
}

void testTransportIsBlockSizeIndependent()
{
    const auto run = [](const std::size_t block) {
        Rig rig;
        rig.set(ParamId::mode, 1.0f);
        rig.set(ParamId::step, 1.0f);  // 1/8
        rig.engine.setText("hello world this is sam");
        return rig.transport(0.0, 133.0, 90000, block);
    };
    const std::vector<float> reference = run(480);
    assert(rms(reference, 0, reference.size()) > 0.01);
    for (const std::size_t block : {1u, 7u, 64u, 255u, 4096u}) {
        const std::vector<float> other = run(block);
        assert(rmsDifference(reference, other) < 1.0e-6);
    }
    pass("testTransportIsBlockSizeIndependent");
}

void testKeysAreBlockSizeIndependent()
{
    const auto run = [](const std::size_t block) {
        Rig rig;
        rig.engine.setText("hello world this is sam");
        return rig.keys({{300, 9000, 55, 90, 0}, {5000, 20000, 62, 120, 0}, {7000, 0, 67, 40, 0}}, 40000, block);
    };
    const std::vector<float> reference = run(512);
    assert(rms(reference, 0, reference.size()) > 0.01);
    for (const std::size_t block : {1u, 33u, 100u, 1024u})
        assert(rmsDifference(reference, run(block)) < 1.0e-6);
    pass("testKeysAreBlockSizeIndependent");
}

void testStoppedAndInvalidTransportAreSilent()
{
    Rig rig;
    rig.set(ParamId::mode, 1.0f);
    rig.engine.setText("hello world");
    assert(peak(rig.transport(0.0, 120.0, 30000, 256, 4.0, false), 0, 30000) == 0.0);

    std::vector<float> out(4800), right(4800);
    Transport invalid;
    invalid.valid = false;
    invalid.playing = true;
    rig.engine.process(invalid, nullptr, 0, 4800, out.data(), right.data());
    assert(peak(out, 0, 4800) == 0.0);
    pass("testStoppedAndInvalidTransportAreSilent");
}

void testStartBarOffsetsTheText()
{
    Rig rig;
    rig.set(ParamId::mode, 1.0f);
    rig.set(ParamId::startBar, 2.0f);
    usePhonetic(rig, "AA IY");
    const std::vector<float> out = rig.transport(0.0, 120.0, 6 * kQuarter);
    assert(peak(out, 0, 4 * kQuarter - 1) == 0.0);                     // all of bar 1 is quiet
    assert(firstAbove(out, 4 * kQuarter - 100, 6 * kQuarter, 1.0e-4f) >= 4 * kQuarter);
    assert(peak(out, 4 * kQuarter, 4 * kQuarter + 4800) > 0.05);
    pass("testStartBarOffsetsTheText");
}

void testLoopRepeatsOrStops()
{
    const auto run = [](const bool loop) {
        Rig rig;
        rig.set(ParamId::mode, 1.0f);
        rig.set(ParamId::loop, loop ? 1.0f : 0.0f);
        usePhonetic(rig, "AA IY");
        return rig.transport(0.0, 120.0, 6 * kQuarter);
    };
    const auto looped = run(true), once = run(false);
    assert(rmsDifference(std::vector<float>(looped.begin(), looped.begin() + 8000),
                         std::vector<float>(looped.begin() + 2 * kQuarter, looped.begin() + 2 * kQuarter + 8000)) < 1.0e-5);
    assert(peak(once, 2 * kQuarter, 6 * kQuarter) == 0.0);
    assert(rmsDifference(looped, once) > 0.001);
    pass("testLoopRepeatsOrStops");
}

void testTempoAndStepScaleTheSlots()
{
    // The second word starts one slot in, and "AA IY" departs from "AA" alone exactly there.
    const auto secondWordStart = [](const double bpm, const float step) {
        const auto run = [&](const char* text) {
            Rig rig;
            rig.set(ParamId::mode, 1.0f);
            rig.set(ParamId::step, step);
            usePhonetic(rig, text);
            return rig.transport(0.0, bpm, 120000);
        };
        const std::vector<float> both = run("AA IY"), alone = run("AA");
        for (std::size_t i = 0; i < both.size(); ++i) {
            if (std::fabs(both[i] - alone[i]) > 1.0e-4f)
                return i;
        }
        return both.size();
    };
    const std::size_t s60 = secondWordStart(60.0, 2.0f);     // 1/4 note at 60 bpm: one second
    const std::size_t s120 = secondWordStart(120.0, 2.0f);   // half a second
    const std::size_t eighth = secondWordStart(120.0, 1.0f); // a quarter of a second
    assert(s60 >= 47900 && s60 <= 48100);
    assert(s120 >= 23900 && s120 <= 24100);
    assert(eighth >= 11900 && eighth <= 12100);
    pass("testTempoAndStepScaleTheSlots");
}

void testBarSteps()
{
    // "1 bar" follows the time signature: 3/4 at 120 bpm is 1.5 s.
    Rig rig;
    rig.set(ParamId::mode, 1.0f);
    rig.set(ParamId::step, 5.0f);
    usePhonetic(rig, "AA IY");
    const std::vector<float> out = rig.transport(0.0, 120.0, 120000, 256, 3.0);
    const std::size_t bar = 72000;
    assert(peak(out, bar - 3000, bar) < 1.0e-5);
    const std::size_t onset = firstAbove(out, bar - 100, bar + 2400, 1.0e-4f);
    assert(onset >= bar && onset <= bar + 240);
    pass("testBarSteps");
}

void testRelocationSpeaksTheRestOfTheWord()
{
    // Starting mid-slot (or after a locate) speaks the word from the matching point, not from nothing.
    const auto run = [](const double startBeat, const std::size_t frames) {
        Rig rig;
        rig.set(ParamId::mode, 1.0f);
        rig.set(ParamId::step, 4.0f);  // 1/1: the whole word has room
        rig.set(ParamId::tone, 12000.0f);
        usePhonetic(rig, kSustained);
        return rig.transport(startBeat, 120.0, frames);
    };
    const std::vector<float> whole = run(0.0, 40000);
    const std::vector<float> late = run(0.25, 20000);  // 0.25 beat at 120 bpm is 6000 frames in
    assert(rms(late, 0, 2000) > 0.02);                 // speaking at once
    std::vector<float> a(whole.begin() + 6000 + 600, whole.begin() + 6000 + 4000);
    std::vector<float> b(late.begin() + 600, late.begin() + 4000);
    assert(rmsDifference(a, b) < 1.0e-3);
    pass("testRelocationSpeaksTheRestOfTheWord");
}

void testLoopWrapRestartsTheText()
{
    // The host loops from beat 8 back to beat 0: the first word starts again at the wrap.
    Rig rig;
    rig.set(ParamId::mode, 1.0f);
    rig.set(ParamId::loop, 0.0f);
    usePhonetic(rig, "AA IY AA IY");
    std::vector<float> out(4800), right(4800);
    Transport t;
    t.valid = t.playing = true;
    t.bpm = 120.0;
    t.bar = 3.0;
    t.barBeat = 1.0;  // beat 13, long after the text has finished
    rig.engine.process(t, nullptr, 0, 480, out.data(), right.data());
    t.bar = 0.0;
    t.barBeat = 0.0;  // wrapped to the start
    rig.engine.process(t, nullptr, 0, 4800, out.data(), right.data());
    assert(peak(out, 0, 4800) > 0.05);
    assert(firstAbove(out, 0, 4800, 1.0e-4f) < 240);
    pass("testLoopWrapRestartsTheText");
}

void testStoppingTheTransportSilencesTheWord()
{
    Rig rig;
    rig.set(ParamId::mode, 1.0f);
    rig.set(ParamId::step, 4.0f);
    usePhonetic(rig, kSustained);
    std::vector<float> out = rig.transport(0.0, 120.0, 12000);
    assert(rms(out, 6000, 12000) > 0.02);
    std::vector<float> after(12000), right(12000);
    Transport stopped;
    stopped.valid = true;
    stopped.playing = false;
    rig.engine.process(stopped, nullptr, 0, 12000, after.data(), right.data());
    assert(rms(after, 2000, 12000) < 1.0e-5);
    pass("testStoppingTheTransportSilencesTheWord");
}

void testSpeakAuditionsWithoutTheTransport()
{
    Rig rig;
    rig.set(ParamId::mode, 1.0f);
    usePhonetic(rig, "AA IY AA");
    rig.set(ParamId::speak, 1.0f);
    const std::vector<float> out = rig.transport(0.0, 120.0, 4 * kQuarter, 256, 4.0, false);
    assert(rms(out, 0, 4800) > 0.02);                  // speaks from the first word
    assert(peak(out, kQuarter + 100, kQuarter + 2000) > 0.0);
    assert(rig.engine.getParameter(paramIndex(ParamId::outWord)) == 0.0f || rig.engine.activeVoices() > 0);
    // One pass only: well after the last word it is quiet again, and a second press repeats it.
    const std::vector<float> later = rig.transport(0.0, 120.0, 4 * kQuarter, 256, 4.0, false);
    assert(peak(later, 0, 4 * kQuarter) == 0.0);
    rig.set(ParamId::speak, 1.0f);
    const std::vector<float> again = rig.transport(0.0, 120.0, 4 * kQuarter, 256, 4.0, false);
    assert(rmsDifference(out, again) < 1.0e-5);
    pass("testSpeakAuditionsWithoutTheTransport");
}

void testCurrentWordStatus()
{
    Rig rig;
    rig.set(ParamId::mode, 1.0f);
    usePhonetic(rig, "AA IY UW");
    const auto status = [&] { return static_cast<int>(rig.engine.getParameter(paramIndex(ParamId::outWord))); };
    assert(status() == 0);
    (void)rig.transport(0.0, 120.0, 100);
    assert(status() == 1);
    (void)rig.transport(1.0, 120.0, 100);
    assert(status() == 2);
    (void)rig.transport(2.0, 120.0, 100);
    assert(status() == 3);
    (void)rig.transport(2.0, 120.0, 100, 256, 4.0, false);
    assert(status() == 0);

    Rig keys;
    keys.set(ParamId::phonetic, 1.0f);
    keys.engine.setText("AA IY");
    (void)keys.keys({{0, 0, 60, 100, 0}}, 200);
    assert(static_cast<int>(keys.engine.getParameter(paramIndex(ParamId::outWord))) == 1);
    pass("testCurrentWordStatus");
}

// ── Whole-plugin behaviour ─────────────────────────────────────────────────

void testEveryControlHasAnEffect()
{
    enum class Scene { keys, transport };
    struct Case {
        ParamId id;
        float value;
        Scene scene;
    };
    const std::vector<Case> cases = {
        {ParamId::mode, 1.0f, Scene::keys},
        {ParamId::speed, 120.0f, Scene::transport},
        {ParamId::speed, 120.0f, Scene::keys},
        {ParamId::pitch, 110.0f, Scene::transport},
        {ParamId::mouth, 220.0f, Scene::transport},
        {ParamId::mouth, 220.0f, Scene::keys},
        {ParamId::throat, 220.0f, Scene::transport},
        {ParamId::throat, 220.0f, Scene::keys},
        {ParamId::monotone, 1.0f, Scene::transport},
        {ParamId::phonetic, 1.0f, Scene::transport},
        {ParamId::phonetic, 1.0f, Scene::keys},
        {ParamId::midiCh, 3.0f, Scene::keys},
        {ParamId::advance, 1.0f, Scene::keys},
        {ParamId::transpose, 7.0f, Scene::keys},
        {ParamId::release, 400.0f, Scene::keys},
        {ParamId::step, 1.0f, Scene::transport},
        {ParamId::startBar, 2.0f, Scene::transport},
        {ParamId::loop, 0.0f, Scene::transport},
        {ParamId::tone, 1500.0f, Scene::transport},
        {ParamId::tone, 1500.0f, Scene::keys},
        {ParamId::level, 0.3f, Scene::transport},
        {ParamId::level, 0.3f, Scene::keys},
    };

    const auto render = [](const Scene scene, const ParamId id, const float value, const bool change) {
        Rig rig;
        rig.engine.setText("hello world, this is sam speaking to you again");
        rig.set(ParamId::mode, scene == Scene::transport ? 1.0f : 0.0f);
        if (change)
            rig.set(id, value);
        if (id == ParamId::mode && change)
            rig.set(ParamId::mode, value);
        if (scene == Scene::transport)
            return rig.transport(0.0, 120.0, 12 * kQuarter);  // past the end of the nine-word text
        // Three notes, the second released well before its word ends; channel 1 on the wire (status 1).
        return rig.keys({{0, 5000, 52, 100, 1}, {12000, 14000, 62, 100, 1}, {26000, 0, 57, 100, 1}}, 60000);
    };

    for (const Case& c : cases) {
        const auto base = render(c.scene, c.id, c.value, false);
        const auto changed = render(c.scene, c.id, c.value, true);
        const double difference = rmsDifference(base, changed);
        if (difference < 1.0e-3)
            std::printf("%s has no audible effect in the %s scene (diff %.6g)\n", kParameterSpecs[paramIndex(c.id)].symbol,
                        c.scene == Scene::keys ? "keys" : "transport", difference);
        assert(difference >= 1.0e-3);
        assert(allFinite(changed));
    }
    pass("testEveryControlHasAnEffect");
}

void testSettingsDoNotLeakBetweenInstances()
{
    // SAM keeps its state in globals; two engines with different voices must not disturb each other.
    Rig a, b;
    a.engine.setText("hello world");
    b.engine.setText("hello world");
    b.set(ParamId::speed, 150.0f);
    b.set(ParamId::throat, 40.0f);
    Rig solo;
    solo.engine.setText("hello world");
    const auto expected = solo.keys({{0, 8000, 60, 100, 0}}, 20000);
    (void)b.keys({{0, 8000, 60, 100, 0}}, 20000);
    const auto got = a.keys({{0, 8000, 60, 100, 0}}, 20000);
    assert(rmsDifference(expected, got) < 1.0e-6);
    pass("testSettingsDoNotLeakBetweenInstances");
}

void testActivationKeepsThePatch()
{
    Rig rig;
    rig.engine.setText("one two three");
    rig.set(ParamId::speed, 90.0f);
    rig.set(ParamId::mode, 1.0f);
    (void)rig.transport(0.0, 120.0, 6000);
    assert(rig.engine.activeVoices() > 0);

    rig.engine.activate(44100.0);
    assert(rig.engine.activeVoices() == 0);
    assert(rig.engine.text() == "one two three");
    assert(rig.engine.getParameter(paramIndex(ParamId::speed)) == 90.0f);
    assert(rig.engine.getParameter(paramIndex(ParamId::mode)) == 1.0f);

    rig.sampleRate = 44100.0;
    const std::vector<float> out = rig.transport(0.0, 120.0, 22050);
    assert(rms(out, 0, 22050) > 0.01);  // and it still speaks the same text
    pass("testActivationKeepsThePatch");
}

void testSampleRates()
{
    for (const double sr : {22050.0, 44100.0, 96000.0, 192000.0}) {
        Rig rig(sr);
        usePhonetic(rig, kSustained);
        rig.set(ParamId::tone, 12000.0f);
        std::vector<float> out(static_cast<std::size_t>(sr * 0.6)), right(out.size());
        MidiEvent on;
        on.size = 3;
        on.data[0] = 0x90;
        on.data[1] = 57;
        on.data[2] = 100;
        const Transport stopped;
        rig.engine.process(stopped, &on, 1, static_cast<std::uint32_t>(out.size()), out.data(), right.data());
        assert(allFinite(out));
        const double f0 = estimateF0(out, static_cast<std::size_t>(sr * 0.1), static_cast<std::size_t>(sr * 0.3), sr, 220.0);
        assert(std::fabs(f0 - 220.0) / 220.0 < 0.015);
    }
    pass("testSampleRates");
}

void testAudioPathDoesNotAllocate()
{
    Rig rig;
    rig.engine.setText("hello world this is a long enough sentence for the test, with punctuation.");
    std::vector<float> out(512), right(512);
    Transport t;
    t.valid = t.playing = true;
    t.bpm = 140.0;
    MidiEvent on;
    on.size = 3;
    on.data[0] = 0x90;
    on.data[1] = 60;
    on.data[2] = 100;
    MidiEvent off = on;
    off.data[0] = 0x80;

    // Warm up anything lazily created, then count.
    rig.engine.process(Transport {}, &on, 1, 512, out.data(), right.data());
    gAllocations = 0;
    gCountAllocations = true;
    for (int block = 0; block < 400; ++block) {
        rig.set(ParamId::mode, block % 200 < 100 ? 0.0f : 1.0f);
        t.barBeat = std::fmod(block * 0.04, 4.0);
        MidiEvent events[2] = {on, off};
        events[0].data[1] = static_cast<std::uint8_t>(40 + block % 30);
        events[1].data[1] = static_cast<std::uint8_t>(40 + (block + 29) % 30);
        events[1].frame = 300;
        rig.engine.process(t, events, 2, 512, out.data(), right.data());
    }
    gCountAllocations = false;
    if (gAllocations.load() != 0)
        std::printf("%ld allocations in process()\n", gAllocations.load());
    assert(gAllocations.load() == 0);
    pass("testAudioPathDoesNotAllocate");
}

void testRandomTextNeverBreaksTheRenderer()
{
    // SAM was written for a C64 and trusts its input. Throw printable junk at both readers.
    std::uint32_t seed = 12345;
    const auto next = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    std::vector<std::uint8_t> out(kMaxRenderSamples);
    double slowest = 0.0;
    for (int i = 0; i < 4000; ++i) {
        std::string word;
        const std::size_t length = 1 + next() % kMaxWordChars;
        for (std::size_t k = 0; k < length; ++k)
            word.push_back(static_cast<char>(0x21 + next() % 94));
        VoiceSettings v;
        v.phonetic = (i & 1) != 0;
        v.sing = (i & 2) != 0;
        v.speed = 1 + static_cast<int>(next() % 255);
        v.pitch = static_cast<int>(next() % 256);
        v.mouth = static_cast<int>(next() % 256);
        v.throat = static_cast<int>(next() % 256);
        const std::string cleaned = sanitizeWord(word, v.phonetic);
        const auto begin = std::chrono::steady_clock::now();
        const std::size_t n = renderWord(cleaned, v, out.data(), out.size());
        slowest = std::max(slowest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
        assert(n <= kMaxRenderSamples);
    }
    std::printf("slowest of 4000 junk renders: %.2f ms\n", slowest);
    assert(slowest < 100.0);
    pass("testRandomTextNeverBreaksTheRenderer");
}

void testTextCanChangeWhileTheAudioThreadRuns()
{
    Rig rig;
    rig.set(ParamId::mode, 1.0f);
    std::atomic<bool> stop {false};
    std::thread editor([&] {
        int i = 0;
        while (!stop.load()) {
            rig.engine.setText(i % 3 == 0 ? "hello world" : (i % 3 == 1 ? "a b c d e f g h" : ""));
            ++i;
        }
    });
    bool finite = true;
    for (int block = 0; block < 600; ++block) {
        const auto out = rig.transport(block * 0.04, 120.0, 256, 256);
        finite = finite && allFinite(out);
    }
    stop = true;
    editor.join();
    assert(finite);
    pass("testTextCanChangeWhileTheAudioThreadRuns");
}

void testEmptyTextIsSilent()
{
    Rig rig;
    rig.engine.setText("");
    assert(peak(rig.keys({{0, 0, 60, 100, 0}}, 5000), 0, 5000) == 0.0);
    rig.set(ParamId::mode, 1.0f);
    assert(peak(rig.transport(0.0, 120.0, 40000), 0, 40000) == 0.0);

    // A rest slot is silent, and still takes its turn.
    Rig rests;
    rests.set(ParamId::mode, 1.0f);
    usePhonetic(rests, "_ AA");
    const auto out = rests.transport(0.0, 120.0, 2 * kQuarter);
    assert(peak(out, 0, kQuarter) == 0.0);
    assert(peak(out, kQuarter, kQuarter + 4800) > 0.05);
    pass("testEmptyTextIsSilent");
}

}  // namespace

int main()
{
    testSamWrapperRendersDeterministically();
    testSamWrapperRespectsLimits();
    testScriptParsing();
    testWordCleaning();
    testParameterTable();
    testParameterAccess();
    testKeysPitchIsInTune();
    testKeysNoteStartsAtItsFrame();
    testNoteOffReleasesAndReleaseTimeMatters();
    testNextWordCyclesThroughTheText();
    testKeyEqualsWordSelectsByKey();
    testVelocityScalesLevel();
    testChannelFilter();
    testPolyphonyAndStealing();
    testAllNotesOffAndPitchBend();
    testWordsLandOnTheirSteps();
    testTransportIsBlockSizeIndependent();
    testKeysAreBlockSizeIndependent();
    testStoppedAndInvalidTransportAreSilent();
    testStartBarOffsetsTheText();
    testLoopRepeatsOrStops();
    testTempoAndStepScaleTheSlots();
    testBarSteps();
    testRelocationSpeaksTheRestOfTheWord();
    testLoopWrapRestartsTheText();
    testStoppingTheTransportSilencesTheWord();
    testSpeakAuditionsWithoutTheTransport();
    testCurrentWordStatus();
    testEveryControlHasAnEffect();
    testSettingsDoNotLeakBetweenInstances();
    testActivationKeepsThePatch();
    testSampleRates();
    testAudioPathDoesNotAllocate();
    testRandomTextNeverBreaksTheRenderer();
    testTextCanChangeWhileTheAudioThreadRuns();
    testEmptyTextIsSilent();
    std::printf("All SAM core tests passed.\n");
    return 0;
}
