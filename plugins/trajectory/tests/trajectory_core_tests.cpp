// Trajectory core tests.
//
// Uses a hand-rolled require() rather than assert(): the Release build adds
// -DNDEBUG, which compiles every assert() out and lets a suite pass while testing
// nothing. require() cannot be compiled away.

#include "trajectory_engine.hpp"

#include "drift_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace downspout::trajectory;

constexpr double kRate = 48000.0;
constexpr std::uint32_t kBlock = 64;

void require(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "trajectory: " << message << '\n';
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

    [[nodiscard]] bool silent(const float threshold = 1.0e-6f) const { return peak <= threshold; }
};

// Render `blocks` blocks; `events` land at the start of the first block.
Render render(Engine& engine, const std::uint32_t blocks, const std::vector<MidiMessage>& events = {})
{
    Render out;
    std::vector<float> l(kBlock);
    std::vector<float> r(kBlock);
    for (std::uint32_t b = 0; b < blocks; ++b)
    {
        engine.process(l.data(), r.data(), kBlock, b == 0 ? events.data() : nullptr,
                       b == 0 ? static_cast<std::uint32_t>(events.size()) : 0u);
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

Engine makeEngine()
{
    Engine engine;
    engine.activate(kRate);
    return engine;
}

double rms(const std::vector<float>& samples, const std::size_t from, const std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from; i < to && i < samples.size(); ++i)
        sum += static_cast<double>(samples[i]) * samples[i];
    return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, to - from)));
}

double difference(const std::vector<float>& a, const std::vector<float>& b)
{
    double sum = 0.0;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i)
        sum += std::fabs(static_cast<double>(a[i]) - b[i]);
    return sum / static_cast<double>(std::max<std::size_t>(1, n));
}

// ── Parameters and controllers ──────────────────────────────────────────────

void testParameterTable()
{
    require(kParameterSpecs.size() == kParameterCount, "spec table size");
    require(kInputParameterCount + 1 == kParameterCount, "one status parameter");
    for (std::size_t i = 0; i < kParameterCount; ++i)
    {
        const ParamSpec& s = kParameterSpecs[i];
        require(s.minimum < s.maximum, "ranges are ordered");
        require(s.defaultValue >= s.minimum && s.defaultValue <= s.maximum, "defaults are in range");
        require(s.output == (i >= kInputParameterCount), "only the trailing parameter is an output");
    }

    const Engine engine;
    for (std::uint32_t i = 0; i < kInputParameterCount; ++i)
        require(engine.getParameter(i) == kParameterSpecs[i].defaultValue, "engine starts at the spec default");

    // The six oscillator controls come first, in the Flues order.
    require(id(ParamId::sides) == 0 && id(ParamId::startPosition) == 1 && id(ParamId::launchAngle) == 2 &&
                id(ParamId::bounceJitter) == 3 && id(ParamId::mixX) == 4 && id(ParamId::mixY) == 5,
            "oscillator controls are parameters 0-5");
}

void testParameterClamping()
{
    Engine engine = makeEngine();
    engine.setParameter(id(ParamId::sides), 6.4f);
    require(engine.getParameter(id(ParamId::sides)) == 6.0f, "sides snaps to an integer");
    engine.setParameter(id(ParamId::sides), 99.0f);
    require(engine.getParameter(id(ParamId::sides)) == 12.0f, "sides clamps high");
    engine.setParameter(id(ParamId::sides), -4.0f);
    require(engine.getParameter(id(ParamId::sides)) == 3.0f, "sides clamps low");
    engine.setParameter(id(ParamId::sustain), 2.0f);
    require(engine.getParameter(id(ParamId::sustain)) == 1.0f, "sustain clamps");
    engine.setParameter(id(ParamId::attack), std::nanf(""));
    require(engine.getParameter(id(ParamId::attack)) == kParameterSpecs[id(ParamId::attack)].defaultValue,
            "NaN falls back to the default");
}

void testStatusParameterIsReadOnly()
{
    Engine engine = makeEngine();
    engine.setParameter(id(ParamId::outVoices), 5.0f);
    require(engine.getParameter(id(ParamId::outVoices)) == 0.0f, "status parameter ignores writes");
}

void testControllersAlignWithDrift()
{
    // CC 1-6 are the six oscillator controls in order.
    for (std::size_t i = 0; i < 6; ++i)
    {
        ParamId target = ParamId::level;
        require(controllerTarget(static_cast<std::uint8_t>(i + 1), target), "CC 1-6 are mapped");
        require(static_cast<std::size_t>(target) == i, "CC n drives oscillator control n");
    }

    // Drift's four lanes default to CC 1-4; each must land on an oscillator control.
    namespace drift = downspout::drift;
    for (int lane = 0; lane < drift::kLaneCount; ++lane)
    {
        const auto cc = static_cast<std::uint8_t>(
            drift::kParameterSpecs[drift::laneParam(lane, drift::kController)].defaultValue);
        ParamId target = ParamId::level;
        require(controllerTarget(cc, target), "a default Drift lane CC is mapped");
        require(static_cast<std::size_t>(target) == static_cast<std::size_t>(lane),
                "Drift lane n drives oscillator control n by default");
    }
}

void testControllersSpanTheFullRange()
{
    for (const auto& mapping : kControllerMap)
    {
        const ParamSpec& spec = kParameterSpecs[static_cast<std::size_t>(mapping.target)];
        require(controllerToParameter(mapping.target, 0) == spec.minimum, "CC 0 reaches the minimum");
        require(controllerToParameter(mapping.target, 127) == spec.maximum, "CC 127 reaches the maximum");
    }
}

void testControllerWritesThrough()
{
    Engine engine = makeEngine();
    render(engine, 1, {controller(1, 127), controller(2, 127), controller(3, 0), controller(4, 127),
                       controller(5, 127), controller(6, 0), controller(7, 0)});
    require(engine.getParameter(id(ParamId::sides)) == 12.0f, "CC 1 sets sides");
    require(engine.getParameter(id(ParamId::startPosition)) == 360.0f, "CC 2 sets start position");
    require(engine.getParameter(id(ParamId::launchAngle)) == 0.0f, "CC 3 sets launch angle");
    require(engine.getParameter(id(ParamId::bounceJitter)) == 10.0f, "CC 4 sets bounce jitter");
    require(engine.getParameter(id(ParamId::mixX)) == 1.0f, "CC 5 sets X level");
    require(engine.getParameter(id(ParamId::mixY)) == 0.0f, "CC 6 sets Y level");
    require(engine.getParameter(id(ParamId::level)) == 0.0f, "CC 7 sets level");
}

void testMidiChannelFilter()
{
    {
        Engine engine = makeEngine();
        engine.setParameter(id(ParamId::midiCh), 3.0f);  // channel 3 only
        require(render(engine, 8, {noteOn(60, 100, 0)}).silent(), "a note on another channel is ignored");
        render(engine, 1, {controller(5, 127, 0)});
        require(engine.getParameter(id(ParamId::mixX)) == 0.0f, "a CC on another channel is ignored");
        require(!render(engine, 8, {noteOn(60, 100, 2)}).silent(), "a note on the chosen channel sounds");
    }
    {
        Engine engine = makeEngine();
        engine.setParameter(id(ParamId::midiCh), 0.0f);  // off
        require(render(engine, 8, {noteOn(60)}).silent(), "MIDI Off ignores notes");
    }
    {
        Engine engine = makeEngine();  // default: all channels
        require(!render(engine, 8, {noteOn(60, 100, 9)}).silent(), "All accepts channel 10");
    }
}

// ── Oscillator ──────────────────────────────────────────────────────────────

void testPointStaysInsideEveryPolygon()
{
    for (int sides = kMinSides; sides <= kMaxSides; ++sides)
    {
        for (const double hertz : {55.0, 440.0, 3520.0, 12000.0})
        {
            for (const double angle : {0.0, 0.3, 1.1, 2.7, 4.5, 6.0})
            {
                Oscillator osc;
                osc.setSampleRate(kRate);
                osc.setSides(sides);
                osc.setFrequency(hertz);
                osc.start(angle * 0.7, angle);
                for (int n = 0; n < 20000; ++n)
                {
                    const float out = osc.process(0.5f, 0.5f);
                    require(std::isfinite(out), "oscillator output is finite");
                    require(osc.inside(osc.position()), "the point never leaves the polygon");
                    require(std::fabs(out) <= 1.5f, "output stays within the polygon's extent");
                }
            }
        }
    }
}

void testPolygonGeometry()
{
    // Unit circumradius: the point can reach close to 1 on a vertex but never beyond.
    Oscillator osc;
    osc.setSampleRate(kRate);
    osc.setSides(3);
    osc.setFrequency(220.0);
    osc.start(0.5, 0.9);
    double extent = 0.0;
    for (int n = 0; n < 48000; ++n)
    {
        static_cast<void>(osc.process(1.0f, 0.0f));
        extent = std::max(extent, std::hypot(osc.position().x, osc.position().y));
    }
    require(extent <= 1.0001, "triangle stays inside the unit circle");
    require(extent > 0.4, "triangle orbit actually covers the shape");
}

void testStartPositionPlacesThePointOnTheBoundary()
{
    Oscillator osc;
    osc.setSampleRate(kRate);
    osc.setSides(4);
    osc.setFrequency(100.0);
    osc.start(0.0, 1.0);
    // Square with a horizontal side: the ray at angle 0 meets a vertical-ish edge
    // at the apothem 1 * cos(pi/4); the point sits at 0.995 of that.
    require(std::fabs(osc.position().x - 0.995 * std::cos(3.14159265358979 / 4.0)) < 1.0e-6,
            "start position lands on the boundary");
    require(std::fabs(osc.position().y) < 1.0e-6, "start position at 0 is on the x axis");
}

void testSidesChangeKeepsThePointInside()
{
    Oscillator osc;
    osc.setSampleRate(kRate);
    osc.setSides(12);
    osc.setFrequency(300.0);
    osc.start(0.3, 0.8);
    for (int n = 0; n < 2000; ++n)
        static_cast<void>(osc.process(1.0f, 1.0f));
    for (int sides = 12; sides >= 3; --sides)
    {
        osc.setSides(sides);
        require(osc.inside(osc.position()), "shrinking the polygon re-seats the point");
        for (int n = 0; n < 500; ++n)
            require(std::isfinite(osc.process(1.0f, 1.0f)), "output stays finite across side changes");
    }
}

void testJitterIsDeterministicAndAudible()
{
    const auto run = [](const float jitter) {
        Engine engine = makeEngine();
        engine.setParameter(id(ParamId::bounceJitter), jitter);
        return render(engine, 400, {noteOn(57)});
    };
    const Render a = run(6.0f);
    const Render b = run(6.0f);
    require(a.left == b.left, "the same jitter renders identically (seeded noise)");
    const Render clean = run(0.0f);
    require(difference(a.left, clean.left) > 1.0e-3, "bounce jitter changes the sound");
    require(a.finite && a.peak <= 1.0f, "jitter output stays bounded");
}

void testPitchFollowsTheNote()
{
    // Doubling the note frequency doubles the point's speed, so the waveform
    // repeats twice as fast. Zero crossings of the DC-free output track that.
    const auto crossings = [](const int note) {
        Engine engine = makeEngine();
        engine.setParameter(id(ParamId::attack), 1.0f);
        engine.setParameter(id(ParamId::launchAngle), 70.0f);
        const Render r = render(engine, 750, {noteOn(static_cast<std::uint8_t>(note))});
        int count = 0;
        for (std::size_t i = 2000; i + 1 < r.left.size(); ++i)
            count += (r.left[i] < 0.0f) != (r.left[i + 1] < 0.0f) ? 1 : 0;
        return count;
    };
    const int low = crossings(48);
    const int high = crossings(60);
    require(low > 50, "the low note produces a periodic signal");
    const double ratio = static_cast<double>(high) / static_cast<double>(low);
    require(ratio > 1.8 && ratio < 2.2, "an octave up roughly doubles the rate");
}

// ── Every control is audible ────────────────────────────────────────────────

void testEveryOscillatorControlChangesTheSound()
{
    const auto base = []() {
        Engine engine = makeEngine();
        engine.setParameter(id(ParamId::mixX), 0.5f);  // so X and Y both contribute
        return engine;
    };
    const auto sound = [](Engine engine) { return render(engine, 300, {noteOn(55)}); };

    const Render reference = sound(base());
    require(!reference.silent(), "the reference patch sounds");

    const std::array<std::pair<ParamId, float>, 6> changes = {{
        {ParamId::sides, 3.0f},
        {ParamId::startPosition, 137.0f},
        {ParamId::launchAngle, 200.0f},
        {ParamId::bounceJitter, 8.0f},
        {ParamId::mixX, 1.0f},
        {ParamId::mixY, 0.2f},
    }};
    for (const auto& [parameter, value] : changes)
    {
        Engine engine = base();
        engine.setParameter(id(parameter), value);
        const Render changed = sound(engine);
        require(difference(reference.left, changed.left) > 1.0e-3, "each oscillator control changes the sound");
    }
}

void testMixControlsSelectTheAxes()
{
    const auto run = [](const float x, const float y) {
        Engine engine = makeEngine();
        engine.setParameter(id(ParamId::mixX), x);
        engine.setParameter(id(ParamId::mixY), y);
        engine.activate(kRate);  // snap the smoothers to the new values
        return render(engine, 200, {noteOn(52)});
    };
    require(run(0.0f, 0.0f).silent(), "X and Y both at zero is silent");
    require(!run(1.0f, 0.0f).silent(), "X alone sounds");
    require(!run(0.0f, 1.0f).silent(), "Y alone sounds");
    require(difference(run(1.0f, 0.0f).left, run(0.0f, 1.0f).left) > 1.0e-3, "X and Y are different signals");
}

void testLevelAndVelocity()
{
    const auto peakAt = [](const float level, const float sensitivity, const std::uint8_t velocity) {
        Engine engine = makeEngine();
        engine.setParameter(id(ParamId::level), level);
        engine.setParameter(id(ParamId::velocity), sensitivity);
        engine.activate(kRate);  // snap the smoothers to the new values
        return render(engine, 300, {noteOn(52, velocity)}).peak;
    };
    require(peakAt(0.0f, 0.5f, 100) < 1.0e-4f, "level zero is silent");
    require(peakAt(0.7f, 0.5f, 100) > peakAt(0.35f, 0.5f, 100) * 1.5f, "level scales the output");
    require(peakAt(0.7f, 1.0f, 127) > peakAt(0.7f, 1.0f, 30) * 2.0f, "velocity sensitivity scales the output");
    const float flatA = peakAt(0.7f, 0.0f, 127);
    const float flatB = peakAt(0.7f, 0.0f, 20);
    require(std::fabs(flatA - flatB) < 1.0e-3f, "velocity sensitivity zero ignores velocity");
}

// ── Envelope ────────────────────────────────────────────────────────────────

void testAdsrStages()
{
    Adsr envelope;
    AdsrSteps steps;
    steps.attack = 0.1f;
    steps.decay = 0.1f;
    steps.release = 0.05f;
    steps.sustain = 0.5f;

    require(envelope.process(steps) == 0.0f, "idle envelope is silent");
    envelope.gateOn();
    float level = 0.0f;
    for (int i = 0; i < 10; ++i)
        level = envelope.process(steps);
    require(level >= 0.999f, "attack reaches full scale");
    for (int i = 0; i < 20; ++i)
        level = envelope.process(steps);
    require(std::fabs(level - 0.5f) < 1.0e-6f, "decay settles on the sustain level");
    require(envelope.stage() == Adsr::Stage::sustain, "envelope holds in sustain");
    for (int i = 0; i < 100; ++i)
        level = envelope.process(steps);
    require(std::fabs(level - 0.5f) < 1.0e-6f, "sustain holds while the gate is open");
    envelope.gateOff();
    for (int i = 0; i < 20; ++i)
        level = envelope.process(steps);
    require(level == 0.0f && !envelope.active(), "release falls to idle");
}

void testSustainLevelIsAudible()
{
    // After the decay has finished, a held note sits at `sustain` of its peak.
    const auto held = [](const float sustain) {
        Engine engine = makeEngine();
        engine.setParameter(id(ParamId::attack), 1.0f);
        engine.setParameter(id(ParamId::decay), 20.0f);
        engine.setParameter(id(ParamId::sustain), sustain);
        const Render r = render(engine, 600, {noteOn(50)});
        return rms(r.left, r.left.size() - 8000, r.left.size());
    };
    const double full = held(1.0f);
    const double half = held(0.5f);
    const double none = held(0.0f);
    require(full > 1.0e-3, "a full sustain holds the note");
    require(half > full * 0.4 && half < full * 0.6, "half sustain is about half the level");
    require(none < full * 0.01, "zero sustain decays to silence while the key is held");
}

void testAttackTimeShapesTheOnset()
{
    const auto early = [](const float attackMs) {
        Engine engine = makeEngine();
        engine.setParameter(id(ParamId::attack), attackMs);
        const Render r = render(engine, 100, {noteOn(50)});
        return rms(r.left, 0, 1200);  // first 25 ms
    };
    require(early(1.0f) > early(200.0f) * 4.0, "a slow attack is quieter at the onset");
}

void testDecayTimeShapesTheFall()
{
    const auto late = [](const float decayMs) {
        Engine engine = makeEngine();
        engine.setParameter(id(ParamId::attack), 1.0f);
        engine.setParameter(id(ParamId::sustain), 0.0f);
        engine.setParameter(id(ParamId::decay), decayMs);
        const Render r = render(engine, 200, {noteOn(50)});
        return rms(r.left, 4000, 8000);  // 80-160 ms in
    };
    require(late(2000.0f) > late(20.0f) * 4.0, "a long decay keeps the note louder");
}

void testReleaseTimeShapesTheTail()
{
    const auto tail = [](const float releaseMs) {
        Engine engine = makeEngine();
        engine.setParameter(id(ParamId::attack), 1.0f);
        engine.setParameter(id(ParamId::release), releaseMs);
        render(engine, 100, {noteOn(50)});
        const Render r = render(engine, 100, {noteOff(50)});
        return rms(r.left, 1000, 4000);
    };
    require(tail(1000.0f) > tail(10.0f) * 4.0, "a long release rings on");

    Engine engine = makeEngine();
    engine.setParameter(id(ParamId::release), 50.0f);
    render(engine, 100, {noteOn(50)});
    require(engine.activeVoices() == 1, "a held note is one active voice");
    render(engine, 200, {noteOff(50)});  // 12800 samples >> 50 ms
    require(engine.activeVoices() == 0, "the voice frees itself once the release ends");
}

// ── Voices ──────────────────────────────────────────────────────────────────

void testSilenceWithoutNotes()
{
    Engine engine = makeEngine();
    const Render r = render(engine, 50);
    require(r.peak == 0.0f, "no notes, no output");
    require(engine.activeVoices() == 0, "no notes, no voices");
}

void testNoteOffEndsTheNote()
{
    Engine engine = makeEngine();
    engine.setParameter(id(ParamId::release), 20.0f);
    require(!render(engine, 50, {noteOn(60)}).silent(), "note on sounds");
    render(engine, 100, {noteOff(60)});
    require(render(engine, 20).silent(1.0e-5f), "note off ends the sound");

    // Note-on with velocity zero is a note-off.
    Engine other = makeEngine();
    other.setParameter(id(ParamId::release), 20.0f);
    render(other, 50, {noteOn(60)});
    render(other, 100, {noteOn(60, 0)});
    require(other.activeVoices() == 0, "velocity-zero note-on releases the note");
}

void testPolyphony()
{
    Engine engine = makeEngine();
    std::vector<MidiMessage> chord;
    for (int i = 0; i < 8; ++i)
        chord.push_back(noteOn(static_cast<std::uint8_t>(48 + i * 3)));
    render(engine, 20, chord);
    require(engine.activeVoices() == 8, "eight notes make eight voices");

    // A ninth steals a voice instead of growing or going silent.
    const Render r = render(engine, 20, {noteOn(90)});
    require(engine.activeVoices() == 8 && !r.silent() && r.finite, "a ninth note steals a voice");
}

void testRepeatedNoteReusesItsVoice()
{
    Engine engine = makeEngine();
    render(engine, 10, {noteOn(60)});
    render(engine, 10, {noteOn(60)});
    require(engine.activeVoices() == 1, "the same note retriggers one voice");
    require(!render(engine, 10).silent(), "the retriggered note keeps sounding");
}

void testAllNotesOff()
{
    Engine engine = makeEngine();
    engine.setParameter(id(ParamId::release), 10.0f);
    render(engine, 10, {noteOn(48), noteOn(52), noteOn(55)});
    require(engine.activeVoices() == 3, "three held notes");
    render(engine, 100, {controller(123, 0)});
    require(engine.activeVoices() == 0, "CC 123 releases every note");

    Engine other = makeEngine();
    other.setParameter(id(ParamId::release), 4000.0f);
    render(other, 10, {noteOn(48), noteOn(52)});
    render(other, 1, {controller(120, 0)});
    require(other.activeVoices() == 0, "CC 120 silences immediately, ignoring release");
}

void testNoteOnIsSampleAccurate()
{
    Engine engine = makeEngine();
    engine.setParameter(id(ParamId::attack), 1.0f);
    MidiMessage late = noteOn(57);
    late.frame = 40;
    std::vector<float> l(kBlock);
    std::vector<float> r(kBlock);
    engine.process(l.data(), r.data(), kBlock, &late, 1);
    for (std::uint32_t i = 0; i < 40; ++i)
        require(l[i] == 0.0f, "nothing sounds before the note's frame");
    float after = 0.0f;
    for (std::uint32_t i = 41; i < kBlock; ++i)
        after = std::max(after, std::fabs(l[i]));
    require(after > 0.0f, "the note starts at its frame");
}

// ── Robustness and lifecycle ────────────────────────────────────────────────

void testOutputStaysFiniteAndBounded()
{
    Engine engine = makeEngine();
    std::uint32_t seed = 12345u;
    const auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / 16777216.0f;
    };

    std::vector<float> l(kBlock);
    std::vector<float> r(kBlock);
    for (int block = 0; block < 4000; ++block)
    {
        std::vector<MidiMessage> events;
        if (block % 5 == 0)
            events.push_back(noteOn(static_cast<std::uint8_t>(24 + next() * 90.0f), 1 + next() * 126.0f));
        if (block % 7 == 0)
            events.push_back(noteOff(static_cast<std::uint8_t>(24 + next() * 90.0f)));
        for (std::uint32_t p = 0; p < kInputParameterCount; ++p)
        {
            if (p == id(ParamId::midiCh))
                continue;
            const ParamSpec& s = kParameterSpecs[p];
            if (next() < 0.1f)
                engine.setParameter(p, s.minimum + next() * (s.maximum - s.minimum));
        }
        engine.process(l.data(), r.data(), kBlock, events.data(), static_cast<std::uint32_t>(events.size()));
        for (std::uint32_t i = 0; i < kBlock; ++i)
            require(std::isfinite(l[i]) && std::fabs(l[i]) <= 1.0f && l[i] == r[i], "output is finite and bounded");
    }
}

void testHighNotesAreStable()
{
    // The oscillator clamps its step so the very top of the keyboard cannot escape the polygon.
    for (int sides = kMinSides; sides <= kMaxSides; ++sides)
    {
        Engine engine = makeEngine();
        engine.setParameter(id(ParamId::sides), static_cast<float>(sides));
        const Render r = render(engine, 100, {noteOn(127)});
        require(r.finite && r.peak <= 1.0f, "note 127 is finite and bounded for every polygon");
    }
}

void testActivationKeepsThePatch()
{
    Engine engine = makeEngine();
    engine.setParameter(id(ParamId::sides), 9.0f);
    engine.setParameter(id(ParamId::launchAngle), 123.0f);
    engine.setParameter(id(ParamId::release), 777.0f);
    render(engine, 10, {noteOn(60)});

    engine.activate(44100.0);
    require(engine.getParameter(id(ParamId::sides)) == 9.0f, "activation keeps sides");
    require(engine.getParameter(id(ParamId::launchAngle)) == 123.0f, "activation keeps launch angle");
    require(engine.getParameter(id(ParamId::release)) == 777.0f, "activation keeps release");
    require(engine.activeVoices() == 0, "activation clears runtime voices");
    require(render(engine, 10).peak == 0.0f, "activation clears the output tail");
}

// Repeat period of the y coordinate, by autocorrelation, as a frequency in Hz.
double estimateFundamental(const int sides, const double launchDegrees, const double hertz, const double rate)
{
    Oscillator osc;
    osc.setSampleRate(rate);
    osc.setSides(sides);
    osc.setFrequency(hertz);
    osc.start(0.0, launchDegrees * 3.14159265358979 / 180.0);

    std::vector<double> x;
    x.reserve(static_cast<std::size_t>(rate * 0.5));
    for (std::size_t n = 0; n < static_cast<std::size_t>(rate * 0.5); ++n)
        x.push_back(static_cast<double>(osc.process(0.0f, 1.0f)));
    double mean = 0.0;
    for (const double v : x)
        mean += v;
    mean /= static_cast<double>(x.size());
    for (double& v : x)
        v -= mean;

    const int maxLag = static_cast<int>(rate / hertz * 2.5);
    const int minLag = static_cast<int>(rate / hertz * 0.3);
    std::vector<double> ac(static_cast<std::size_t>(maxLag) + 1u, 0.0);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        for (std::size_t i = 500; i < 20500; ++i)
            sum += x[i] * x[i + static_cast<std::size_t>(lag)];
        ac[static_cast<std::size_t>(lag)] = sum;
    }
    int lag = 1;
    while (lag < maxLag && ac[static_cast<std::size_t>(lag)] > 0.0)
        ++lag;
    double best = -1.0e30;
    int bestLag = lag;
    for (int l = std::max(lag, minLag); l <= maxLag; ++l)
    {
        if (ac[static_cast<std::size_t>(l)] > best)
        {
            best = ac[static_cast<std::size_t>(l)];
            bestLag = l;
        }
    }
    return rate / static_cast<double>(bestLag);
}

void testPitchIsGeometryDependent()
{
    // The Flues original drives the point at frequency * 4 / sampleRate and makes
    // no attempt to tune each polygon: the pitch that comes out is a property of the
    // shape. A square launched at 45 degrees repeats at the note's frequency; other
    // shapes sit flat of it, a dodecagon by more than a fourth. Doubling the note
    // doubles the pitch whatever the shape. This pins that behaviour down so it is a
    // documented property, not a surprise (see docs/design.md).
    for (const double hertz : {110.0, 220.0})
    {
        const double square = estimateFundamental(4, 45.0, hertz, kRate);
        require(std::fabs(square / hertz - 1.0) < 0.03, "a square at 45 degrees repeats at the note frequency");
    }

    const double hexagon = estimateFundamental(6, 45.0, 220.0, kRate) / 220.0;
    const double dodecagon = estimateFundamental(12, 45.0, 220.0, kRate) / 220.0;
    require(hexagon > 0.88 && hexagon < 0.98, "a hexagon sits a little flat of the note");
    require(dodecagon > 0.65 && dodecagon < 0.80, "a dodecagon sits well flat of the note");

    for (const int sides : {4, 6, 8, 12})
    {
        const double low = estimateFundamental(sides, 45.0, 110.0, kRate);
        const double high = estimateFundamental(sides, 45.0, 220.0, kRate);
        require(std::fabs(high / low - 2.0) < 0.1, "an octave up doubles the pitch for every shape");
    }
}

void testSampleRateScalesTheOscillator()
{
    // The same note at different sample rates keeps its pitch: speed scales with 1/rate.
    for (const int sides : {4, 6})
    {
        const double at48 = estimateFundamental(sides, 45.0, 220.0, 48000.0);
        const double at96 = estimateFundamental(sides, 45.0, 220.0, 96000.0);
        const double at44 = estimateFundamental(sides, 45.0, 220.0, 44100.0);
        require(std::fabs(at96 / at48 - 1.0) < 0.03, "pitch is independent of the sample rate (96k)");
        require(std::fabs(at44 / at48 - 1.0) < 0.03, "pitch is independent of the sample rate (44.1k)");
    }
}

}  // namespace

int main()
{
    testParameterTable();
    testParameterClamping();
    testStatusParameterIsReadOnly();
    testControllersAlignWithDrift();
    testControllersSpanTheFullRange();
    testControllerWritesThrough();
    testMidiChannelFilter();

    testPointStaysInsideEveryPolygon();
    testPolygonGeometry();
    testStartPositionPlacesThePointOnTheBoundary();
    testSidesChangeKeepsThePointInside();
    testJitterIsDeterministicAndAudible();
    testPitchFollowsTheNote();

    testEveryOscillatorControlChangesTheSound();
    testMixControlsSelectTheAxes();
    testLevelAndVelocity();

    testAdsrStages();
    testSustainLevelIsAudible();
    testAttackTimeShapesTheOnset();
    testDecayTimeShapesTheFall();
    testReleaseTimeShapesTheTail();

    testSilenceWithoutNotes();
    testNoteOffEndsTheNote();
    testPolyphony();
    testRepeatedNoteReusesItsVoice();
    testAllNotesOff();
    testNoteOnIsSampleAccurate();

    testOutputStaysFiniteAndBounded();
    testHighNotesAreStable();
    testActivationKeepsThePatch();
    testPitchIsGeometryDependent();
    testSampleRateScalesTheOscillator();

    std::cout << "trajectory core tests passed\n";
    return 0;
}
