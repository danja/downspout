// Fathom core tests.
//
// Uses a hand-rolled require() rather than assert(): the Release build adds
// -DNDEBUG, which compiles every assert() out and lets a suite pass while testing
// nothing. require() cannot be compiled away.

#include "fathom_engine.hpp"

#include "drift_core.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace downspout::fathom;

constexpr double kRate = 48000.0;
constexpr std::uint32_t kBlock = 64;

void require(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "fathom: " << message << '\n';
        std::exit(1);
    }
}

constexpr std::uint32_t id(const ParamId parameter)
{
    return static_cast<std::uint32_t>(parameter);
}

MidiMessage noteOn(const std::uint8_t note, const std::uint8_t velocity = 100, const std::uint8_t channel = 0)
{
    MidiMessage m;
    m.size = 3;
    m.data = {static_cast<std::uint8_t>(0x90 | channel), note, velocity};
    return m;
}

MidiMessage noteOff(const std::uint8_t note, const std::uint8_t channel = 0)
{
    MidiMessage m;
    m.size = 3;
    m.data = {static_cast<std::uint8_t>(0x80 | channel), note, 0};
    return m;
}

MidiMessage controller(const std::uint8_t cc, const std::uint8_t value, const std::uint8_t channel = 0)
{
    MidiMessage m;
    m.size = 3;
    m.data = {static_cast<std::uint8_t>(0xB0 | channel), cc, value};
    return m;
}

struct Render {
    std::vector<float> left;
    std::vector<float> right;
    float peak = 0.0f;
    bool finite = true;
};

// Render `blocks` blocks; `events` go into the first block at frame 0.
Render render(Engine& engine, const int blocks, const MidiMessage* events = nullptr, const std::uint32_t count = 0)
{
    Render out;
    std::vector<float> l(kBlock), r(kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        engine.process(l.data(), r.data(), kBlock, b == 0 ? events : nullptr, b == 0 ? count : 0u);
        for (std::uint32_t i = 0; i < kBlock; ++i)
        {
            out.finite = out.finite && std::isfinite(l[i]) && std::isfinite(r[i]);
            out.peak = std::max({out.peak, std::fabs(l[i]), std::fabs(r[i])});
        }
        out.left.insert(out.left.end(), l.begin(), l.end());
        out.right.insert(out.right.end(), r.begin(), r.end());
    }
    return out;
}

void set(Engine& engine, const ParamId parameter, const float value)
{
    engine.setParameter(id(parameter), value);
}

// A dry, unfiltered patch: processor mix off, filter open.
void dryPatch(Engine& engine)
{
    set(engine, ParamId::mix, 0.0f);
    set(engine, ParamId::attack, 5.0f);
    set(engine, ParamId::release, 10.0f);
    set(engine, ParamId::level, 1.0f);
}

double rms(const std::vector<float>& v, const std::size_t from = 0)
{
    double sum = 0.0;
    for (std::size_t i = from; i < v.size(); ++i)
        sum += static_cast<double>(v[i]) * v[i];
    return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, v.size() - from)));
}

// Goertzel magnitude of `hertz` over the signal from `from`.
double magnitude(const std::vector<float>& v, const double hertz, const std::size_t from = 0)
{
    const double w = 2.0 * 3.14159265358979323846 * hertz / kRate;
    const double coefficient = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = from; i < v.size(); ++i)
    {
        const double s = static_cast<double>(v[i]) + coefficient * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double real = s1 - s2 * std::cos(w);
    const double imag = s2 * std::sin(w);
    return std::sqrt(real * real + imag * imag) / static_cast<double>(std::max<std::size_t>(1, v.size() - from));
}

double difference(const std::vector<float>& a, const std::vector<float>& b)
{
    double sum = 0.0;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i)
        sum += std::fabs(static_cast<double>(a[i]) - b[i]);
    return sum / static_cast<double>(std::max<std::size_t>(1, n));
}

// ── Parameters and controllers ─────────────────────────────────────────────

void testParameterTable()
{
    require(kParameterSpecs.size() == kParameterCount, "spec count");
    for (std::size_t i = 0; i < kParameterCount; ++i)
    {
        const ParamSpec& s = kParameterSpecs[i];
        require(s.minimum < s.maximum, "ranges are ordered");
        require(s.defaultValue >= s.minimum && s.defaultValue <= s.maximum, "defaults are in range");
        require(s.output == (i >= kInputParameterCount), "only the trailing parameters are outputs");
    }
    require(clampParameter(id(ParamId::warp), 5.0f) == 1.0f, "clamps high");
    require(clampParameter(id(ParamId::warp), std::nanf("")) == kParameterSpecs[1].defaultValue, "NaN falls back");
    require(clampParameter(id(ParamId::algorithm), 2.6f) == 3.0f, "integers round");
}

void testControllersAlignWithDrift()
{
    // CC 1-4 are the four generator controls; Drift's four default lanes land on them.
    constexpr std::array<ParamId, 4> expected = {{ParamId::warp, ParamId::span, ParamId::morph, ParamId::seed}};
    namespace drift = downspout::drift;
    for (int lane = 0; lane < drift::kLaneCount; ++lane)
    {
        const auto cc = static_cast<std::uint8_t>(
            drift::kParameterSpecs[drift::laneParam(lane, drift::kController)].defaultValue);
        ParamId target = ParamId::level;
        require(controllerTarget(cc, target), "a default Drift lane CC is mapped");
        require(target == expected[static_cast<std::size_t>(lane)], "Drift lane n drives generator control n");
    }
}

void testControllerMapIsConsistent()
{
    for (std::size_t i = 0; i < kControllerMap.size(); ++i)
    {
        for (std::size_t j = i + 1; j < kControllerMap.size(); ++j)
        {
            require(kControllerMap[i].controller != kControllerMap[j].controller, "CC numbers are unique");
            require(kControllerMap[i].target != kControllerMap[j].target, "each parameter has one CC");
        }
        const ParamSpec& spec = kParameterSpecs[static_cast<std::size_t>(kControllerMap[i].target)];
        require(!spec.output, "no CC targets an output");
        require(controllerToParameter(kControllerMap[i].target, 0) == spec.minimum, "CC 0 reaches the minimum");
        require(controllerToParameter(kControllerMap[i].target, 127) == spec.maximum, "CC 127 reaches the maximum");
        require(kControllerMap[i].controller != kCcHold && kControllerMap[i].controller != kCcAllNotesOff &&
                    kControllerMap[i].controller != kCcAllSoundOff,
                "no CC collides with hold or the channel-mode controllers");
    }
}

void testControllerWritesThrough()
{
    Engine engine;
    const MidiMessage events[] = {controller(2, 127), controller(13, 0), controller(14, 127)};
    render(engine, 1, events, 3);
    require(engine.getParameter(id(ParamId::span)) == 1.0f, "CC 2 sets Span");
    require(engine.getParameter(id(ParamId::shift)) == -1.0f, "CC 13 sets Shift, bipolar");
    require(engine.getParameter(id(ParamId::basis)) == 24.0f, "CC 14 sets Basis");
}

void testMidiChannelFilter()
{
    Engine engine;
    set(engine, ParamId::midiCh, 3.0f);
    const MidiMessage wrong[] = {noteOn(60, 100, 0), controller(2, 127, 0)};
    render(engine, 4, wrong, 2);
    require(engine.activeVoices() == 0, "a note on another channel is ignored");
    require(engine.getParameter(id(ParamId::span)) != 1.0f, "a CC on another channel is ignored");

    const MidiMessage right[] = {noteOn(60, 100, 2)};
    render(engine, 4, right, 1);
    require(engine.activeVoices() == 1, "the chosen channel plays");

    set(engine, ParamId::midiCh, 0.0f);
    const MidiMessage off[] = {noteOn(64, 100, 2)};
    render(engine, 4, off, 1);
    require(engine.activeVoices() == 1, "Off ignores everything");
}

// ── Frequency shifter ──────────────────────────────────────────────────────

void testShifterMovesAToneUp()
{
    // A 2 kHz sine, rotated at +300 Hz, should come out at 2.3 kHz with the other
    // sideband (1.7 kHz) strongly suppressed.
    FrequencyShifter shifter;
    std::vector<float> out;
    double phase = 0.0;
    double rotation = 0.0;
    for (int n = 0; n < 48000; ++n)
    {
        phase += 2000.0 / kRate;
        rotation += 300.0 / kRate;
        const float x = static_cast<float>(std::sin(2.0 * 3.14159265358979 * phase));
        out.push_back(shifter.process(x, static_cast<float>(std::cos(2.0 * 3.14159265358979 * rotation)),
                                      static_cast<float>(std::sin(2.0 * 3.14159265358979 * rotation))));
    }
    const double up = magnitude(out, 2300.0, 4800);
    const double down = magnitude(out, 1700.0, 4800);
    const double same = magnitude(out, 2000.0, 4800);
    require(up > 0.3, "the shifted tone is present");
    require(up > 20.0 * down, "the unwanted sideband is suppressed by 26 dB or more");
    require(up > 20.0 * same, "the original tone is gone");
}

void testShifterIsUnityWithoutRotation()
{
    FrequencyShifter shifter;
    std::vector<float> out;
    double phase = 0.0;
    for (int n = 0; n < 48000; ++n)
    {
        phase += 1000.0 / kRate;
        out.push_back(shifter.process(static_cast<float>(std::sin(2.0 * 3.14159265358979 * phase)), 1.0f, 0.0f));
    }
    const double level = magnitude(out, 1000.0, 4800) * 2.0;
    require(level > 0.95 && level < 1.05, "a zero shift passes the tone at unity gain");
}

// ── Notes, voices, hold ────────────────────────────────────────────────────

void testSilentWithoutNotes()
{
    Engine engine;
    const Render out = render(engine, 100);
    require(out.peak == 0.0f, "silent without notes");
}

void testNoteSoundsAndReleases()
{
    Engine engine;
    dryPatch(engine);
    const MidiMessage on[] = {noteOn(57)};
    const Render sounding = render(engine, 80, on, 1);
    require(sounding.peak > 0.05f && sounding.finite, "a note sounds");
    require(engine.activeVoices() == 1, "one voice");

    const MidiMessage off[] = {noteOff(57)};
    render(engine, 60, off, 1);
    require(engine.activeVoices() == 0, "the voice releases to idle");
    require(render(engine, 10).peak < 1.0e-4f, "and goes silent");
}

void testStereoOutput()
{
    Engine engine;
    set(engine, ParamId::mix, 0.6f);
    const MidiMessage chord[] = {noteOn(48), noteOn(55), noteOn(60), noteOn(64)};
    const Render out = render(engine, 400, chord, 4);
    require(rms(out.left, 8000) > 0.01 && rms(out.right, 8000) > 0.01, "both channels carry sound");
    require(difference(out.left, out.right) > 1.0e-3, "the channels differ");
}

void testPolyphonyAndStealing()
{
    Engine engine;
    set(engine, ParamId::attack, 5.0f);
    const MidiMessage five[] = {noteOn(48), noteOn(52), noteOn(55), noteOn(59), noteOn(62)};
    const Render out = render(engine, 100, five, 5);
    require(engine.activeVoices() == 4, "four voices at most");
    require(out.finite, "stealing stays finite");
}

void testSameNoteRetriggersItsVoice()
{
    Engine engine;
    const MidiMessage twice[] = {noteOn(60), noteOn(60)};
    render(engine, 20, twice, 2);
    require(engine.activeVoices() == 1, "a repeated note reuses its voice");
}

void testHoldKeepsNotesAfterRelease()
{
    Engine engine;
    set(engine, ParamId::attack, 5.0f);
    set(engine, ParamId::release, 10.0f);
    set(engine, ParamId::hold, 1.0f);
    const MidiMessage on[] = {noteOn(60)};
    render(engine, 40, on, 1);
    const MidiMessage off[] = {noteOff(60)};
    render(engine, 200, off, 1);
    require(engine.activeVoices() == 1, "Hold keeps the voice after its key comes up");

    set(engine, ParamId::hold, 0.0f);
    render(engine, 60);
    require(engine.activeVoices() == 0, "releasing Hold lets it go");
}

void testHoldDoesNotReleaseHeldKeys()
{
    Engine engine;
    set(engine, ParamId::attack, 5.0f);
    set(engine, ParamId::release, 10.0f);
    set(engine, ParamId::hold, 1.0f);
    const MidiMessage on[] = {noteOn(60)};
    render(engine, 40, on, 1);
    set(engine, ParamId::hold, 0.0f);
    render(engine, 200);
    require(engine.activeVoices() == 1, "turning Hold off leaves a key that is still down");
}

void testSustainPedalIsHold()
{
    Engine engine;
    set(engine, ParamId::attack, 5.0f);
    set(engine, ParamId::release, 10.0f);
    const MidiMessage pedal[] = {controller(64, 127), noteOn(60)};
    render(engine, 40, pedal, 2);
    const MidiMessage up[] = {noteOff(60)};
    render(engine, 200, up, 1);
    require(engine.activeVoices() == 1, "CC 64 down holds");
    require(engine.getParameter(id(ParamId::hold)) == 1.0f, "and shows on the Hold parameter");
    const MidiMessage lift[] = {controller(64, 0)};
    render(engine, 100, lift, 1);
    require(engine.activeVoices() == 0, "CC 64 up releases");
}

void testAllNotesOffAndAllSoundOff()
{
    Engine engine;
    set(engine, ParamId::release, 10.0f);
    set(engine, ParamId::hold, 1.0f);
    const MidiMessage on[] = {noteOn(60), noteOn(64)};
    render(engine, 40, on, 2);
    const MidiMessage allOff[] = {controller(123, 0)};
    render(engine, 100, allOff, 1);
    require(engine.activeVoices() == 0, "CC 123 releases held voices too");

    const MidiMessage again[] = {noteOn(60)};
    render(engine, 20, again, 1);
    const MidiMessage kill[] = {controller(120, 0)};
    const Render silent = render(engine, 1, kill, 1);
    require(engine.activeVoices() == 0 && silent.peak == 0.0f, "CC 120 silences at once");
}

void testPitchBend()
{
    Engine engine;
    dryPatch(engine);
    set(engine, ParamId::span, 0.0f);
    set(engine, ParamId::warp, 0.0f);
    const MidiMessage events[] = {noteOn(57), {0, 3, {0xE0, 0x7F, 0x7F}}};
    const Render out = render(engine, 400, events, 2);
    // Full bend up is two semitones: 220 Hz becomes about 246.9 Hz.
    const double bent = magnitude(out.left, 246.94, 6000);
    const double unbent = magnitude(out.left, 220.0, 6000);
    require(bent > 4.0 * unbent, "pitch bend moves the pitch");
}

void testGlideSlidesBetweenNotes()
{
    const auto measure = [](const float glide) {
        Engine engine;
        dryPatch(engine);
        set(engine, ParamId::span, 0.0f);
        set(engine, ParamId::warp, 0.0f);
        set(engine, ParamId::glide, glide);
        const MidiMessage first[] = {noteOn(45)};
        render(engine, 100, first, 1);
        const MidiMessage off[] = {noteOff(45)};
        render(engine, 10, off, 1);
        const MidiMessage second[] = {noteOn(57)};
        const Render out = render(engine, 30, second, 1);  // 40 ms in
        return magnitude(out.left, 220.0, 0);
    };
    require(measure(0.0f) > 2.0 * measure(1500.0f), "with no glide the new note is at pitch at once");
}

// ── Generator ──────────────────────────────────────────────────────────────

double dominance(const int algorithm, const double hertz)
{
    Generator g;
    g.setSampleRate(kRate);
    GeneratorControls c;
    c.algorithm = algorithm;
    c.warp = 0.0f;
    c.span = 0.0f;
    c.morph = 0.0f;
    c.fuse = 0.0f;
    c.seed = 0.5f;
    g.setControls(c);
    g.snap();
    g.restart();
    std::vector<float> out;
    for (int n = 0; n < 24000; ++n)
        out.push_back(g.process(hertz));
    const double fundamental = magnitude(out, hertz, 2000);
    const double half = magnitude(out, hertz * 0.5, 2000);
    const double between = magnitude(out, hertz * 1.5, 2000);
    return fundamental / std::max({half, between, 1.0e-9});
}

void testEveryAlgorithmSoundsAtThePlayedPitch()
{
    for (int algorithm = 0; algorithm < kAlgorithmCount; ++algorithm)
    {
        for (const double hertz : {110.0, 220.0, 440.0})
        {
            require(dominance(algorithm, hertz) > 3.0, "the fundamental dominates at span 0");
        }
    }
}

void testOctaveDoublesTheRate()
{
    for (int algorithm = 0; algorithm < kAlgorithmCount; ++algorithm)
    {
        Generator g;
        g.setSampleRate(kRate);
        GeneratorControls c;
        c.algorithm = algorithm;
        c.warp = 0.0f;
        c.span = 0.0f;
        c.morph = 0.0f;
        g.setControls(c);
        g.snap();
        std::vector<float> low, high;
        for (int n = 0; n < 24000; ++n)
            low.push_back(g.process(150.0));
        g.restart();
        for (int n = 0; n < 24000; ++n)
            high.push_back(g.process(300.0));
        require(magnitude(high, 300.0, 2000) > 0.0 && magnitude(low, 150.0, 2000) > 0.0, "tones present");
        require(magnitude(high, 300.0, 2000) > 3.0 * magnitude(high, 150.0, 2000), "an octave up lands an octave up");
    }
}

void testGeneratorStaysBounded()
{
    for (int algorithm = 0; algorithm < kAlgorithmCount; ++algorithm)
    {
        for (const float setting : {0.0f, 0.5f, 1.0f})
        {
            Generator g;
            g.setSampleRate(kRate);
            GeneratorControls c;
            c.algorithm = algorithm;
            c.warp = c.span = c.morph = c.seed = c.fuse = setting;
            g.setControls(c);
            g.snap();
            float peak = 0.0f;
            for (const double hertz : {32.7, 130.0, 523.0, 2093.0, 8372.0})
            {
                for (int n = 0; n < 4000; ++n)
                {
                    const float s = g.process(hertz);
                    require(std::isfinite(s), "finite output");
                    peak = std::max(peak, std::fabs(s));
                }
            }
            require(peak < 3.0f, "bounded output");
            require(peak > 0.05f, "audible output");
        }
    }
}

// Mean absolute difference between two renders of the same note with one control moved.
double controlEffect(const int algorithm, const ParamId control, const float from, const float to)
{
    const auto run = [&](const float value) {
        Engine engine;
        dryPatch(engine);
        set(engine, ParamId::algorithm, static_cast<float>(algorithm));
        set(engine, ParamId::warp, 0.3f);
        set(engine, ParamId::span, 0.5f);
        set(engine, ParamId::morph, 0.4f);
        set(engine, ParamId::seed, 0.5f);
        set(engine, ParamId::fuse, 0.2f);
        set(engine, control, value);
        const MidiMessage on[] = {noteOn(50)};
        return render(engine, 200, on, 1).left;
    };
    return difference(run(from), run(to));
}

void testEveryGeneratorControlIsAudibleInEveryAlgorithm()
{
    // Each shared control must do something in each algorithm (the page this is
    // modelled on gives every control a meaning per algorithm).
    for (int algorithm = 0; algorithm < kAlgorithmCount; ++algorithm)
    {
        for (const ParamId control : {ParamId::warp, ParamId::span, ParamId::morph, ParamId::fuse})
        {
            require(controlEffect(algorithm, control, 0.0f, 1.0f) > 1.0e-4, "a generator control changes the sound");
        }
    }
    // Seed rearranges the cluster, the sines and the table; the two-oscillator
    // algorithms have no random element.
    for (const int algorithm : {2, 3, 4})
        require(controlEffect(algorithm, ParamId::seed, 0.0f, 1.0f) > 1.0e-4, "Seed changes the sound");
}

void testSeedIsDeterministicAndSmooth()
{
    Engine a, b;
    for (Engine* e : {&a, &b})
    {
        dryPatch(*e);
        set(*e, ParamId::algorithm, 4.0f);
        set(*e, ParamId::span, 0.8f);
        set(*e, ParamId::seed, 0.37f);
    }
    const MidiMessage on[] = {noteOn(48)};
    require(difference(render(a, 100, on, 1).left, render(b, 100, on, 1).left) == 0.0, "same seed, same sound");

    // A small seed change moves the sound a little; a big one moves it more.
    const double small = controlEffect(4, ParamId::seed, 0.50f, 0.52f);
    const double large = controlEffect(4, ParamId::seed, 0.50f, 0.90f);
    require(small < large, "Seed scans rather than jumps");
}

void testWavetablesAreBandLimited()
{
    // A high note must not fold back: compare the energy near the fold-over image
    // of the 30th harmonic against the fundamental.
    Generator g;
    g.setSampleRate(kRate);
    GeneratorControls c;
    c.algorithm = 3;
    c.morph = 0.3f;  // saw-rich frames
    c.span = 0.0f;
    c.warp = 0.0f;
    g.setControls(c);
    g.snap();
    std::vector<float> out;
    for (int n = 0; n < 24000; ++n)
        out.push_back(g.process(3000.0));
    const double fundamental = magnitude(out, 3000.0, 2000);
    // 3 kHz harmonics land at 6, 9, 12, ... 21 kHz; their aliases would sit at
    // non-multiples. Probe a few.
    double alias = 0.0;
    for (const double f : {1000.0, 1700.0, 2300.0, 4300.0})
        alias = std::max(alias, magnitude(out, f, 2000));
    require(alias < 0.05 * fundamental, "no aliasing between the harmonics");
}

// ── Filter ─────────────────────────────────────────────────────────────────

void testFieldClosesTheFilter()
{
    const auto brightness = [](const float field) {
        Engine engine;
        dryPatch(engine);
        set(engine, ParamId::algorithm, 0.0f);
        set(engine, ParamId::morph, 0.0f);
        set(engine, ParamId::warp, 0.0f);
        set(engine, ParamId::field, field);
        const MidiMessage on[] = {noteOn(45)};
        const Render out = render(engine, 300, on, 1);
        return magnitude(out.left, 110.0 * 20.0, 4000);  // the 20th harmonic of a saw
    };
    require(brightness(1.0f) > 8.0 * brightness(0.3f), "a lower Field removes the top end");
}

void testNotchChangesTheSound()
{
    const auto run = [](const float notch) {
        Engine engine;
        dryPatch(engine);
        set(engine, ParamId::algorithm, 0.0f);
        set(engine, ParamId::morph, 0.0f);
        set(engine, ParamId::field, 0.55f);
        set(engine, ParamId::notch, notch);
        const MidiMessage on[] = {noteOn(45)};
        return render(engine, 300, on, 1).left;
    };
    require(difference(run(0.0f), run(1.0f)) > 1.0e-3, "Notch carves the spectrum");
}

// ── Processor ──────────────────────────────────────────────────────────────

Render burstThroughProcessor(const int kind, const float feed, const float shift, const float form, const int blocks)
{
    Processor p;
    p.prepare(kRate);
    ProcessorControls c;
    c.kind = kind;
    c.feed = feed;
    c.shift = shift;
    c.form = form;
    c.time = 0.3f;
    p.setControls(c);
    p.clear();
    Render out;
    for (int n = 0; n < blocks * static_cast<int>(kBlock); ++n)
    {
        float l = 0.0f, r = 0.0f;
        p.process(n < 200 ? 0.5f * std::sin(0.3f * static_cast<float>(n)) : 0.0f, l, r);
        out.left.push_back(l);
        out.right.push_back(r);
        out.finite = out.finite && std::isfinite(l) && std::isfinite(r);
        out.peak = std::max({out.peak, std::fabs(l), std::fabs(r)});
    }
    return out;
}

void testProcessorTailsDecay()
{
    for (const int kind : {0, 1})
    {
        for (const float shift : {0.0f, 0.6f, -0.6f})
        {
            const Render out = burstThroughProcessor(kind, 1.0f, shift, 0.8f, 3000);  // 4 s
            require(out.finite, "finite with full feedback");
            require(out.peak < 2.5f, "bounded with full feedback");
            const std::size_t third = out.left.size() / 3;
            const std::vector<float> early(out.left.begin() + 5000, out.left.begin() + 5000 + static_cast<long>(third) / 4);
            const std::vector<float> late(out.left.end() - static_cast<long>(third) / 4, out.left.end());
            require(rms(late) < rms(early), "the tail decays rather than grows");
        }
    }
}

void testProcessorProducesStereo()
{
    for (const int kind : {0, 1})
    {
        const Render out = burstThroughProcessor(kind, 0.7f, 0.0f, 0.6f, 600);
        require(rms(out.left) > 1.0e-3 && rms(out.right) > 1.0e-3, "both sides ring");
        require(difference(out.left, out.right) > 1.0e-4, "the sides differ");
    }
}

void testFeedLengthensTheTail()
{
    for (const int kind : {0, 1})
    {
        const auto tail = [&](const float feed) {
            const Render out = burstThroughProcessor(kind, feed, 0.0f, 0.6f, 1500);
            return rms(out.left, out.left.size() / 2);
        };
        require(tail(0.9f) > 3.0 * tail(0.2f), "Feed sets how long it rings");
    }
}

void testShiftBendsTheRepeats()
{
    for (const int kind : {0, 1})
    {
        const Render still = burstThroughProcessor(kind, 0.8f, 0.0f, 0.4f, 800);
        const Render shifted = burstThroughProcessor(kind, 0.8f, 0.5f, 0.4f, 800);
        require(difference(still.left, shifted.left) > 1.0e-4, "Shift changes the repeats");
    }
}

void testFormChangesTheStructure()
{
    for (const int kind : {0, 1})
    {
        const Render a = burstThroughProcessor(kind, 0.7f, 0.0f, 0.0f, 600);
        const Render b = burstThroughProcessor(kind, 0.7f, 0.0f, 1.0f, 600);
        require(difference(a.left, b.left) > 1.0e-4, "Form changes the structure");
    }
}

void testProcessorKindsDiffer()
{
    const Render a = burstThroughProcessor(0, 0.7f, 0.0f, 0.6f, 600);
    const Render b = burstThroughProcessor(1, 0.7f, 0.0f, 0.6f, 600);
    require(difference(a.left, b.left) > 1.0e-4, "the two structures sound different");
}

void testTimeScalesTheDelay()
{
    // The first echo of a click arrives later with a longer Time.
    const auto firstEcho = [](const float time) {
        Processor p;
        p.prepare(kRate);
        ProcessorControls c;
        c.kind = 1;
        c.time = time;
        c.feed = 0.5f;
        c.form = 0.0f;
        p.setControls(c);
        p.clear();
        for (int n = 0; n < 60000; ++n)
        {
            float l = 0.0f, r = 0.0f;
            p.process(n == 0 ? 1.0f : 0.0f, l, r);
            if (n > 20 && std::fabs(l) > 0.05f)
                return n;
        }
        return 60000;
    };
    require(firstEcho(0.5f) > 3 * firstEcho(0.2f), "a longer Time delays the first echo");
}

void testMixBlendsDryAndWet()
{
    const auto run = [](const float mix) {
        Engine engine;
        set(engine, ParamId::mix, mix);
        set(engine, ParamId::attack, 5.0f);
        const MidiMessage on[] = {noteOn(50)};
        return render(engine, 300, on, 1).left;
    };
    require(difference(run(0.0f), run(1.0f)) > 1.0e-3, "Mix changes the sound");
    // With Mix at 0 the processor is out of the path entirely.
    Engine a, b;
    for (Engine* e : {&a, &b})
    {
        set(*e, ParamId::mix, 0.0f);
        set(*e, ParamId::attack, 5.0f);
    }
    set(b, ParamId::feed, 1.0f);
    set(b, ParamId::shift, 1.0f);
    set(b, ParamId::time, 0.9f);
    const MidiMessage on[] = {noteOn(50)};
    const std::vector<float> settledA = render(a, 200, on, 1).left;
    const std::vector<float> settledB = render(b, 200, on, 1).left;
    // Mix glides from its default over a few milliseconds; compare once it has.
    const std::vector<float> tailA(settledA.begin() + 4000, settledA.end());
    const std::vector<float> tailB(settledB.begin() + 4000, settledB.end());
    require(difference(tailA, tailB) < 1.0e-6, "processor settings have no effect at Mix 0");
}

// ── Whole-engine behaviour ─────────────────────────────────────────────────

void testLevelScalesTheOutput()
{
    const auto peak = [](const float level) {
        Engine engine;
        set(engine, ParamId::level, level);
        const MidiMessage on[] = {noteOn(50)};
        return render(engine, 300, on, 1).peak;
    };
    require(peak(0.8f) > 1.5f * peak(0.3f), "Level scales the output");
    require(peak(0.0f) < 1.0e-3f, "Level 0 is silent");
}

void testAttackAndReleaseShapeTheEnvelope()
{
    const auto early = [](const float attack) {
        Engine engine;
        dryPatch(engine);
        set(engine, ParamId::attack, attack);
        const MidiMessage on[] = {noteOn(50)};
        return rms(render(engine, 20, on, 1).left);  // first 27 ms
    };
    require(early(5.0f) > 4.0 * early(2000.0f), "a slow attack fades in");

    Engine engine;
    set(engine, ParamId::mix, 0.0f);
    set(engine, ParamId::attack, 5.0f);
    set(engine, ParamId::release, 2000.0f);
    const MidiMessage on[] = {noteOn(50)};
    render(engine, 50, on, 1);
    const MidiMessage off[] = {noteOff(50)};
    render(engine, 20, off, 1);
    require(engine.activeVoices() == 1, "a long release is still sounding 27 ms later");
}

void testBasisTransposes()
{
    Engine engine;
    dryPatch(engine);
    set(engine, ParamId::span, 0.0f);
    set(engine, ParamId::warp, 0.0f);
    set(engine, ParamId::basis, 12.0f);
    const MidiMessage on[] = {noteOn(45)};  // 110 Hz, an octave up is 220 Hz
    const Render out = render(engine, 300, on, 1);
    require(magnitude(out.left, 220.0, 4000) > 4.0 * magnitude(out.left, 110.0, 4000), "Basis shifts the pitch");
}

void testVelocityShadesTheLevelGently()
{
    const auto peak = [](const std::uint8_t velocity) {
        Engine engine;
        dryPatch(engine);
        const MidiMessage on[] = {noteOn(50, velocity)};
        return render(engine, 200, on, 1).peak;
    };
    require(peak(127) > peak(20), "harder is louder");
    require(peak(20) > 0.5f * peak(127), "but not by much");
}

void testActivationKeepsThePatch()
{
    Engine engine;
    set(engine, ParamId::algorithm, 4.0f);
    set(engine, ParamId::feed, 0.77f);
    set(engine, ParamId::hold, 1.0f);
    engine.activate(44100.0);
    engine.activate(96000.0);
    require(engine.getParameter(id(ParamId::algorithm)) == 4.0f, "activation keeps the algorithm");
    require(engine.getParameter(id(ParamId::feed)) == 0.77f, "and the sliders");
    require(engine.getParameter(id(ParamId::hold)) == 1.0f, "and Hold");
}

void testStatusParameterIsReadOnly()
{
    Engine engine;
    engine.setParameter(id(ParamId::outVoices), 3.0f);
    require(engine.getParameter(id(ParamId::outVoices)) == 0.0f, "the status parameter cannot be written");
    const MidiMessage on[] = {noteOn(50), noteOn(55)};
    render(engine, 10, on, 2);
    require(engine.getParameter(id(ParamId::outVoices)) == 2.0f, "and reports the voice count");
}

void testDeterministic()
{
    Engine a, b;
    const MidiMessage on[] = {noteOn(48), noteOn(55)};
    require(difference(render(a, 200, on, 2).left, render(b, 200, on, 2).left) == 0.0, "same input, same output");
}

void testSampleRates()
{
    for (const double rate : {44100.0, 48000.0, 96000.0})
    {
        Engine engine;
        engine.activate(rate);
        set(engine, ParamId::mix, 0.0f);
        set(engine, ParamId::span, 0.0f);
        set(engine, ParamId::warp, 0.0f);
        set(engine, ParamId::attack, 5.0f);
        const MidiMessage on[] = {noteOn(57)};
        std::vector<float> l(kBlock), r(kBlock), all;
        for (int b = 0; b < 600; ++b)
        {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            engine.process(l.data(), r.data(), kBlock, b == 0 ? on : nullptr, b == 0 ? 1u : 0u);
            all.insert(all.end(), l.begin(), l.end());
        }
        // 220 Hz regardless of rate: measure with the matching rate.
        const double w = 2.0 * 3.14159265358979 * 220.0 / rate;
        const double c = 2.0 * std::cos(w);
        double s1 = 0.0, s2 = 0.0;
        for (std::size_t i = 4000; i < all.size(); ++i)
        {
            const double s = all[i] + c * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        const double at220 = std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2);
        double t1 = 0.0, t2 = 0.0;
        const double w2 = 2.0 * 3.14159265358979 * 330.0 / rate;
        const double c2 = 2.0 * std::cos(w2);
        for (std::size_t i = 4000; i < all.size(); ++i)
        {
            const double s = all[i] + c2 * t1 - t2;
            t2 = t1;
            t1 = s;
        }
        const double at330 = std::sqrt(t1 * t1 + t2 * t2 - c2 * t1 * t2);
        require(at220 > 3.0 * at330, "the pitch holds at every sample rate");
    }
}

void testExtremeSettingsStayFinite()
{
    for (int algorithm = 0; algorithm < kAlgorithmCount; ++algorithm)
    {
        for (const int kind : {0, 1})
        {
            Engine engine;
            set(engine, ParamId::algorithm, static_cast<float>(algorithm));
            set(engine, ParamId::processor, static_cast<float>(kind));
            for (const ParamId p : {ParamId::warp, ParamId::span, ParamId::morph, ParamId::seed, ParamId::fuse,
                                    ParamId::notch, ParamId::form, ParamId::feed, ParamId::mix})
                set(engine, p, 1.0f);
            set(engine, ParamId::shift, 1.0f);
            set(engine, ParamId::time, 1.0f);
            set(engine, ParamId::field, 0.2f);
            set(engine, ParamId::attack, 5.0f);
            const MidiMessage chord[] = {noteOn(24), noteOn(48), noteOn(72), noteOn(108)};
            const Render out = render(engine, 1500, chord, 4);
            require(out.finite, "finite at extreme settings");
            require(out.peak <= 1.0f, "never beyond full scale");
        }
    }
}

void testRandomisedControlsStayFinite()
{
    std::uint32_t state = 12345u;
    const auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / static_cast<float>(1 << 24);
    };
    Engine engine;
    set(engine, ParamId::attack, 5.0f);
    const MidiMessage chord[] = {noteOn(40), noteOn(52), noteOn(64)};
    render(engine, 2, chord, 3);
    for (int step = 0; step < 300; ++step)
    {
        for (std::uint32_t p = 0; p < kInputParameterCount; ++p)
        {
            const ParamSpec& s = kParameterSpecs[p];
            if (p == id(ParamId::midiCh))
                continue;
            engine.setParameter(p, s.minimum + next() * (s.maximum - s.minimum));
        }
        const Render out = render(engine, 5);
        require(out.finite && out.peak <= 1.0f, "stays finite under random control changes");
    }
}

void testIdleCostIsLow()
{
    // Denormals in the delay network would multiply the cost of an idle tail.
    Engine engine;
    set(engine, ParamId::feed, 0.9f);
    set(engine, ParamId::mix, 1.0f);
    set(engine, ParamId::release, 10.0f);
    set(engine, ParamId::attack, 5.0f);
    const MidiMessage on[] = {noteOn(40)};
    render(engine, 100, on, 1);
    const MidiMessage off[] = {noteOff(40)};
    render(engine, 10, off, 1);

    const auto time = [&](const int blocks) {
        const auto start = std::chrono::steady_clock::now();
        render(engine, blocks);
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    time(2000);                      // let the tail die away
    const double seconds = time(4000);  // 5.3 s of audio
    require(seconds < 0.5, "an idle engine renders far faster than real time");
}

}  // namespace

int main()
{
    testParameterTable();
    testControllersAlignWithDrift();
    testControllerMapIsConsistent();
    testControllerWritesThrough();
    testMidiChannelFilter();

    testShifterMovesAToneUp();
    testShifterIsUnityWithoutRotation();

    testSilentWithoutNotes();
    testNoteSoundsAndReleases();
    testStereoOutput();
    testPolyphonyAndStealing();
    testSameNoteRetriggersItsVoice();
    testHoldKeepsNotesAfterRelease();
    testHoldDoesNotReleaseHeldKeys();
    testSustainPedalIsHold();
    testAllNotesOffAndAllSoundOff();
    testPitchBend();
    testGlideSlidesBetweenNotes();

    testEveryAlgorithmSoundsAtThePlayedPitch();
    testOctaveDoublesTheRate();
    testGeneratorStaysBounded();
    testEveryGeneratorControlIsAudibleInEveryAlgorithm();
    testSeedIsDeterministicAndSmooth();
    testWavetablesAreBandLimited();

    testFieldClosesTheFilter();
    testNotchChangesTheSound();

    testProcessorTailsDecay();
    testProcessorProducesStereo();
    testFeedLengthensTheTail();
    testShiftBendsTheRepeats();
    testFormChangesTheStructure();
    testProcessorKindsDiffer();
    testTimeScalesTheDelay();
    testMixBlendsDryAndWet();

    testLevelScalesTheOutput();
    testAttackAndReleaseShapeTheEnvelope();
    testBasisTransposes();
    testVelocityShadesTheLevelGently();
    testActivationKeepsThePatch();
    testStatusParameterIsReadOnly();
    testDeterministic();
    testSampleRates();
    testExtremeSettingsStayFinite();
    testRandomisedControlsStayFinite();
    testIdleCostIsLow();

    std::cout << "fathom: all tests passed\n";
    return 0;
}
