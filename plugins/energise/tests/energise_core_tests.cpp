#include "downspout/test_assert.h"

#include "energise_analysis.hpp"
#include "energise_core.hpp"
#include "energise_demo.hpp"
#include "energise_params.hpp"
#include "energise_quantise.hpp"
#include "energise_render.hpp"
#include "energise_serialization.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using namespace downspout::energise;

constexpr int kSr = 48000;

// Expected labels are held independently of the generator in energise_demo.cpp.
struct ExpectedHit {
    double time;
    DrumClass cls;
    bool ghost = false;
};

const std::vector<ExpectedHit>& expectedHits()
{
    static const std::vector<ExpectedHit> hits = {
        {0.000, DrumClass::Kick}, {0.250, DrumClass::ClosedHat},
        {0.500, DrumClass::Snare}, {0.750, DrumClass::ClosedHat},
        {1.000, DrumClass::Kick}, {1.250, DrumClass::ClosedHat},
        {1.420, DrumClass::Snare, true}, {1.500, DrumClass::Snare},
        {1.750, DrumClass::ClosedHat},
        {2.000, DrumClass::Kick}, {2.000, DrumClass::ClosedHat},
        {2.500, DrumClass::Snare}, {2.750, DrumClass::ClosedHat},
        {3.000, DrumClass::Kick}, {3.000, DrumClass::Crash},
        {3.250, DrumClass::LowTom}, {3.500, DrumClass::Snare},
        {3.750, DrumClass::ClosedHat},
        {4.000, DrumClass::Kick}, {4.250, DrumClass::ClosedHat},
        {4.500, DrumClass::Snare}, {4.750, DrumClass::ClosedHat},
        {5.000, DrumClass::Kick}, {5.500, DrumClass::Snare},
    };
    return hits;
}

[[nodiscard]] double energy(const std::vector<float>& signal, const std::size_t from, const std::size_t to)
{
    double sum = 0.0;
    const std::size_t last = std::min(to, signal.size());
    for (std::size_t i = from; i < last; ++i)
        sum += static_cast<double>(signal[i]) * signal[i];
    return sum;
}

[[nodiscard]] double peak(const std::vector<float>& signal)
{
    double result = 0.0;
    for (const float sample : signal)
        result = std::max(result, static_cast<double>(std::fabs(sample)));
    return result;
}

[[nodiscard]] bool finite(const RenderedBuffer& buffer)
{
    for (const float sample : buffer.left)
        if (!std::isfinite(sample))
            return false;
    for (const float sample : buffer.right)
        if (!std::isfinite(sample))
            return false;
    return true;
}

[[nodiscard]] DrumEvent makeEvent(const std::uint32_t id, const double time, const DrumClass cls,
                                  const float velocity = 0.9f)
{
    DrumEvent event {};
    event.id = id;
    event.sourceTime = time;
    event.quantisedTime = time;
    event.primary = cls;
    event.velocity = velocity;
    event.confidence = 0.8f;
    return event;
}

[[nodiscard]] Processor analysedProcessor()
{
    Processor processor {};
    assert(processor.makeDemo(kSr, 1789u));
    assert(processor.analyse());
    return processor;
}

void testDefaultsAndParameterClamping()
{
    for (const ParamSpec& spec : kParameterSpecs) {
        assert(spec.minimum <= spec.maximum);
        assert(spec.defaultValue >= spec.minimum && spec.defaultValue <= spec.maximum);
        assert(spec.symbol != nullptr && spec.name != nullptr);
    }
    Processor processor {};
    assert(processor.parameter(static_cast<std::uint32_t>(ParamId::captureBars)) == 2.0f);
    assert(processor.parameter(static_cast<std::uint32_t>(ParamId::audioOut)) == 0.0f);
    assert(processor.parameter(static_cast<std::uint32_t>(ParamId::midiOut)) == 1.0f);
    assert(processor.parameter(static_cast<std::uint32_t>(ParamId::bbtLoop)) == 1.0f);
    assert(processor.parameter(static_cast<std::uint32_t>(ParamId::bbtValid)) == 0.0f);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::quantiseStrength), 4.0f);
    assert(std::fabs(processor.parameter(static_cast<std::uint32_t>(ParamId::quantiseStrength)) - 1.0f) < 1e-6f);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::quantiseStrength), -4.0f);
    assert(std::fabs(processor.parameter(static_cast<std::uint32_t>(ParamId::quantiseStrength))) < 1e-6f);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::status), 3.0f);
    assert(processor.state() == ProcessorState::Idle);
}

void testStateMachineAndRequestCounters()
{
    Processor processor {};
    assert(!processor.render());
    assert(processor.state() == ProcessorState::Error);

    Processor fresh {};
    fresh.setParameter(static_cast<std::uint32_t>(ParamId::renderRequest), 1.0f);
    const auto actions = fresh.takeRequests();
    assert(actions.render);
    const auto repeated = fresh.takeRequests();
    assert(!repeated.render);
    assert(fresh.parameter(static_cast<std::uint32_t>(ParamId::renderRequest)) == 1.0f);
}

void testCaptureClampsToEightSeconds()
{
    CaptureBuffer longCapture {};
    longCapture.sampleRate = kSr;
    longCapture.left.assign(static_cast<std::size_t>(kSr * 12), 0.001f);
    longCapture.right.assign(longCapture.left.size(), -0.001f);
    Processor processor {};
    assert(processor.capture(longCapture));
    assert(processor.captureBuffer().left.size() == static_cast<std::size_t>(kSr * kMaxCaptureSeconds));
    assert(processor.state() == ProcessorState::Captured);
}

void testBbtCaptureMath()
{
    HostPosition position {};
    position.valid = true;
    position.playing = true;
    position.bar = 1;
    position.beat = 1;
    position.tick = 0.0;
    position.beatsPerBar = 4.0f;
    position.beatType = 4.0f;
    position.bpm = 120.0;

    assert(std::fabs(beatSeconds(position) - 0.5) < 1e-12);
    assert(std::fabs(barSeconds(position) - 2.0) < 1e-12);
    assert(std::fabs(secondsToNextBar(position) - 2.0) < 1e-12);
    assert(captureFramesForBars(kSr, position, 4) == static_cast<std::size_t>(kSr * 8));
    assert(effectiveCaptureBars(position, 4) == 4);

    position.beat = 3;
    position.tick = 960.0;
    assert(std::fabs(beatInBar(position) - 2.5) < 1e-12);
    assert(std::fabs(secondsToNextBar(position) - 0.75) < 1e-12);

    position.beat = 1;
    position.tick = 0.0;
    position.bpm = 60.0;  // two whole bars fit under the eight-second cap
    assert(effectiveCaptureBars(position, 4) == 2);
    assert(captureFramesForBars(kSr, position, 4) == static_cast<std::size_t>(kSr * 8));

    position.bpm = 30.0;  // one bar is exactly 8 s; hard cap wins
    assert(effectiveCaptureBars(position, 4) == 1);
    assert(captureFramesForBars(kSr, position, 4) == static_cast<std::size_t>(kSr * 8));

    position.beatType = 8.0f;
    position.bpm = 120.0;
    assert(std::fabs(beatSeconds(position) - 0.25) < 1e-12);
}

void testHostLockedAnalysis()
{
    const CaptureBuffer demo = makeDemoBreak(kSr, 1789u);
    Processor processor {};
    processor.setHostBbtValid(true);
    processor.setHostGrid(137.5, 0.0);
    assert(processor.capture(demo));
    assert(processor.analyse());
    assert(std::fabs(processor.groove().bpm - 137.5) < 1e-9);
    assert(std::fabs(processor.groove().phaseSec) < 1e-12);
    assert(processor.groove().drift.empty());
    assert(processor.parameter(static_cast<std::uint32_t>(ParamId::bbtValid)) == 1.0f);

    assert(processor.quantise());
    assert(processor.render());
    const std::string text = processor.serialize();
    Processor reopened {};
    assert(reopened.deserialize(text));
    assert(std::fabs(reopened.groove().bpm - 137.5) < 1e-9);
}

void testDemoIsDeterministic()
{
    const CaptureBuffer a = makeDemoBreak(kSr, 1789u);
    const CaptureBuffer b = makeDemoBreak(kSr, 1789u);
    assert(a.left.size() == static_cast<std::size_t>(kSr * 6));
    assert(a.left == b.left && a.right == b.right);
    assert(peak(a.left) > 0.1);
}

void testOnsetsMatchSyntheticBreak()
{
    const CaptureBuffer capture = makeDemoBreak(kSr, 1789u);
    const auto onsets = detectOnsets(capture);
    std::size_t matched = 0;
    for (const ExpectedHit& expected : expectedHits()) {
        const bool found = std::any_of(onsets.begin(), onsets.end(), [&](const DetectedOnset& onset) {
            return std::fabs(onset.timeSec - expected.time) <= 0.030;
        });
        if (found)
            ++matched;
    }
    const double recall = static_cast<double>(matched) / static_cast<double>(expectedHits().size());
    std::size_t useful = 0;
    for (const DetectedOnset& onset : onsets) {
        if (std::any_of(expectedHits().begin(), expectedHits().end(), [&](const ExpectedHit& expected) {
                return std::fabs(onset.timeSec - expected.time) <= 0.030;
            }))
            ++useful;
    }
    const double precision = onsets.empty() ? 0.0 : static_cast<double>(useful) / static_cast<double>(onsets.size());
    std::cout << "onsets=" << onsets.size() << " recall=" << recall << " precision=" << precision << '\n';
    assert(recall >= 0.85);
    assert(precision >= 0.85);
}

void testSimultaneousHitStaysOneMultiLabelEvent()
{
    Processor processor = analysedProcessor();
    const auto& events = processor.events();
    std::size_t overlaps = 0;
    for (const DrumEvent& event : events) {
        if (std::fabs(event.sourceTime - 2.0) <= 0.030) {
            ++overlaps;
            assert(event.scores[static_cast<std::size_t>(DrumClass::Kick)] > 0.5f);
            assert(event.scores[static_cast<std::size_t>(DrumClass::ClosedHat)] > 0.3f);
        }
    }
    assert(overlaps == 1);
}

void testTempoAndGridFit()
{
    Processor processor = analysedProcessor();
    assert(std::fabs(processor.groove().bpm - 120.0) <= 2.4);
    const double step = gridStepSeconds(processor.groove());
    const double phaseError = std::fabs(std::fmod(processor.groove().phaseSec + step * 0.5, step) - step * 0.5);
    std::cout << "bpm=" << processor.groove().bpm << " phase=" << processor.groove().phaseSec
              << " step=" << step << '\n';
    assert(phaseError <= 0.020);
    assert(processor.groove().confidence > 0.4f);
}

void testClassificationAndGhostFlag()
{
    Processor processor = analysedProcessor();
    const auto& events = processor.events();
    std::size_t correct = 0;
    std::size_t compared = 0;
    for (const DrumEvent& event : events) {
        const ExpectedHit* nearest = nullptr;
        double bestDistance = 0.030;
        for (const ExpectedHit& expected : expectedHits()) {
            const double distance = std::fabs(event.sourceTime - expected.time);
            if (distance < bestDistance) {
                bestDistance = distance;
                nearest = &expected;
            }
        }
        if (nearest == nullptr)
            continue;
        ++compared;
        if (event.primary == nearest->cls)
            ++correct;
    }
    const double accuracy = compared == 0 ? 0.0 : static_cast<double>(correct) / static_cast<double>(compared);
    std::cout << "class accuracy=" << accuracy << " over " << compared << " events\n";
    assert(accuracy >= 0.80);

    bool ghostFound = false;
    for (const DrumEvent& event : events)
        if (event.ghost && std::fabs(event.sourceTime - 1.42) < 0.030)
            ghostFound = true;
    assert(ghostFound);
}

void testSilentCaptureProducesNoEvents()
{
    CaptureBuffer silent {};
    silent.sampleRate = kSr;
    silent.left.assign(static_cast<std::size_t>(kSr), 0.0f);
    silent.right.assign(silent.left.size(), 0.0f);
    GrooveMap groove {};
    std::vector<DrumEvent> events {};
    assert(analyseCapture(silent, 4, 0.25f, groove, events));
    assert(events.empty());
    assert(std::isfinite(groove.confidence));
}

void testQuantisationStrengthAndGhostStrength()
{
    GrooveMap groove {};
    groove.bpm = 120.0;
    groove.phaseSec = 0.0;
    groove.gridDivisionsPerBeat = 4;
    RenderSettings settings {};

    std::vector<DrumEvent> events = {makeEvent(1, 0.060, DrumClass::Kick)};
    settings.quantiseStrength = 0.0f;
    quantiseEvents(events, groove, settings, 1u, 4.0);
    assert(std::fabs(events[0].quantisedTime - 0.060) < 1e-9);

    settings.quantiseStrength = 1.0f;
    quantiseEvents(events, groove, settings, 1u, 4.0);
    assert(std::fabs(events[0].quantisedTime) < 1e-9);

    settings.quantiseStrength = 0.8f;
    settings.ghostStrength = 0.25f;
    events = {makeEvent(2, 0.060, DrumClass::Kick), makeEvent(3, 0.060, DrumClass::Snare)};
    events[1].ghost = true;
    quantiseEvents(events, groove, settings, 1u, 4.0);
    assert(std::fabs(events[0].quantisedTime - 0.012) < 1e-6);
    assert(std::fabs(events[1].quantisedTime - 0.045) < 1e-6);
    assert(std::fabs(events[0].sourceTime - 0.060) < 1e-9);
}

void testSwingOffsetsAndPerClassOffsets()
{
    GrooveMap groove {};
    groove.bpm = 120.0;
    groove.gridDivisionsPerBeat = 4;
    RenderSettings settings {};
    settings.quantiseStrength = 1.0f;
    settings.swing = 0.5f;
    std::vector<DrumEvent> events = {makeEvent(1, 0.0, DrumClass::Kick), makeEvent(2, 0.130, DrumClass::Snare)};
    quantiseEvents(events, groove, settings, 1u, 4.0);
    assert(std::fabs(events[0].quantisedTime) < 1e-9);
    assert(std::fabs(events[1].quantisedTime - 0.15625) < 1e-6);

    settings.swing = 0.0f;
    settings.classOffsetsMs[static_cast<std::size_t>(DrumClass::Snare)] = 20.0f;
    quantiseEvents(events, groove, settings, 1u, 4.0);
    assert(std::fabs(events[0].quantisedTime) < 1e-9);
    assert(std::fabs(events[1].quantisedTime - 0.145) < 1e-6);

    settings.humanise = 1.0f;
    for (int seed = 1; seed <= 16; ++seed) {
        quantiseEvents(events, groove, settings, static_cast<std::uint32_t>(seed), 4.0);
        assert(events[1].quantisedTime >= 0.0 && events[1].quantisedTime <= 4.0);
    }
}

void testMidiExport()
{
    Processor processor = analysedProcessor();
    const auto messages = processor.exportMidi();
    assert(!messages.empty());
    for (const MidiMessage& message : messages) {
        assert(message.status == 0x99);
        assert(message.note != 0);
        assert(message.velocity >= 1 && message.velocity <= 127);
    }
    bool kick = false;
    bool snare = false;
    bool hat = false;
    for (const MidiMessage& message : messages) {
        kick |= message.note == 36;
        snare |= message.note == 38;
        hat |= message.note == 42;
    }
    assert(kick && snare && hat);
}

void testRendererIsFiniteDeterministicAndVelocitySensitive()
{
    RenderSettings settings {};
    DrumEvent quiet = makeEvent(1, 0.0, DrumClass::Kick, 0.2f);
    DrumEvent loud = makeEvent(1, 0.0, DrumClass::Kick, 0.95f);
    const auto a = renderVoice(loud, settings, 17u, kSr);
    const auto b = renderVoice(loud, settings, 17u, kSr);
    const auto soft = renderVoice(quiet, settings, 17u, kSr);
    assert(finite(a) && finite(soft));
    assert(a.left == b.left && a.right == b.right);
    assert(peak(a.left) > peak(soft.left) * 1.5);

    for (std::size_t cls = 0; cls < kClassCount; ++cls) {
        DrumEvent event = makeEvent(static_cast<std::uint32_t>(cls + 1), 0.0, classFromIndex(static_cast<int>(cls)));
        const auto voice = renderVoice(event, settings, 7u, kSr);
        assert(finite(voice));
        const double tail = tailSeconds(event.primary);
        const std::size_t window = static_cast<std::size_t>(tail * kSr * 0.10);
        const std::size_t from = voice.left.size() > window ? voice.left.size() - window : voice.left.size() - 1;
        const double tailEnergy = energy(voice.left, from, voice.left.size());
        const double attackEnergy = energy(voice.left, 0, window);
        std::cout << "tail " << className(event.primary) << " ratio="
                  << (attackEnergy > 0.0 ? tailEnergy / attackEnergy : 0.0) << '\n';
        assert(tailEnergy < attackEnergy * 0.01);
    }
}

void testResidualAndSaturation()
{
    CaptureBuffer capture {};
    capture.sampleRate = kSr;
    capture.left.assign(static_cast<std::size_t>(kSr), 0.0f);
    capture.right.assign(capture.left.size(), 0.0f);
    for (std::size_t i = 0; i < capture.left.size(); ++i)
        capture.left[i] = capture.right[i] = std::sin(static_cast<double>(i) * 0.03) * 0.4f;

    RenderSettings settings {};
    settings.bodyLevel = 0.0f;
    settings.transientLevel = 0.0f;
    settings.saturation = 0.0f;
    settings.residualLevel = 0.0f;
    const std::vector<DrumEvent> events = {makeEvent(1, 0.1, DrumClass::Kick)};
    const RenderedBuffer dry = renderBreak(capture, events, settings, 3u, 0u, 0u, 0.5f);
    settings.residualLevel = 1.0f;
    const RenderedBuffer wet = renderBreak(capture, events, settings, 3u, 0u, 0u, 0.5f);
    assert(finite(dry) && finite(wet));
    assert(energy(dry.left, static_cast<std::size_t>(0.1 * kSr), static_cast<std::size_t>(0.13 * kSr)) < 1e-9);
    assert(energy(wet.left, static_cast<std::size_t>(0.1 * kSr), static_cast<std::size_t>(0.13 * kSr)) > 0.01);

    settings.bodyLevel = 0.8f;
    settings.transientLevel = 0.6f;
    settings.saturation = 1.0f;
    settings.residualLevel = 0.35f;
    const RenderedBuffer hot = renderBreak(capture, events, settings, 3u, 0u, 0u, 1.0f);
    assert(finite(hot));
    assert(peak(hot.left) <= 1.0001);
    const std::size_t eventFrame = static_cast<std::size_t>(0.1 * kSr);
    const std::size_t window = static_cast<std::size_t>(0.05 * kSr);
    const std::size_t from = hot.left.size() - window;
    const double hotTail = energy(hot.left, from, hot.left.size());
    const double hotAttack = energy(hot.left, eventFrame, eventFrame + window);
    assert(hotAttack > 0.0);
    assert(hotTail < hotAttack * 0.1);
}

void testRenderBreakDeterminismAndMute()
{
    Processor processor = analysedProcessor();
    assert(processor.render());
    const RenderedBuffer first = processor.renderBuffer();
    assert(processor.render());
    assert(first.left == processor.renderBuffer().left);
    assert(first.right == processor.renderBuffer().right);

    Processor muted = analysedProcessor();
    muted.setParameter(static_cast<std::uint32_t>(ParamId::muteMask), 8191.0f);
    assert(muted.render());
    assert(peak(muted.renderBuffer().left) < 1e-6);
}

void testSerializationAndHostLifecycle()
{
    Processor processor = analysedProcessor();
    assert(processor.setEvent(processor.events()[0].id, makeEvent(processor.events()[0].id, 0.02, DrumClass::Clap)));
    assert(processor.render());
    const std::string text = processor.serialize();
    Processor reopened {};
    assert(reopened.deserialize(text));
    assert(reopened.events().size() == processor.events().size());
    assert(reopened.events()[0].id == processor.events()[0].id);
    assert(reopened.events()[0].primary == DrumClass::Clap);
    assert(reopened.renderBuffer().left == processor.renderBuffer().left);
    assert(reopened.state() == ProcessorState::Rendered);

    reopened.activate(44100);
    assert(reopened.events().size() == processor.events().size());
    assert(reopened.state() == ProcessorState::Rendered);

    assert(!Processor{}.deserialize("version=99\nstate=0\n"));
}

void testUndoRedoAndABSwap()
{
    Processor processor = analysedProcessor();
    const auto original = processor.events()[0];
    DrumEvent edited = original;
    edited.primary = DrumClass::Rim;
    assert(processor.setEvent(original.id, edited));
    assert(processor.events()[0].primary == DrumClass::Rim);
    assert(processor.undo());
    assert(processor.events()[0].primary == original.primary);
    assert(processor.redo());
    assert(processor.events()[0].primary == DrumClass::Rim);

    assert(processor.render());
    assert(!processor.showingRender());
    processor.setAbSide(true);
    assert(processor.showingRender());
    assert(processor.parameter(static_cast<std::uint32_t>(ParamId::abSide)) == 1.0f);
    assert(processor.events()[0].primary == DrumClass::Rim);
}

void testEventEditsThroughRequestParameters()
{
    Processor processor = analysedProcessor();
    processor.setParameter(static_cast<std::uint32_t>(ParamId::selectedEvent), 1.0f);
    assert(processor.selectedEvent() == 1);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::selectedEvent), 0.0f);
    assert(processor.selectedEvent() == 0);
    assert(processor.parameter(static_cast<std::uint32_t>(ParamId::selectedEvent)) == 0.0f);
    const DrumEvent before = processor.events()[0];

    processor.setParameter(static_cast<std::uint32_t>(ParamId::editClass),
                           static_cast<float>(static_cast<int>(DrumClass::Snare)));
    processor.setParameter(static_cast<std::uint32_t>(ParamId::editTime), 0.031f);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::editVelocity), 0.42f);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::editGhost), 1.0f);
    processor.setParameter(static_cast<std::uint32_t>(ParamId::eventEditRequest), 1.0f);
    assert(processor.takeRequests().eventEdit);
    assert(!processor.takeRequests().eventEdit);

    const DrumEvent& edited = processor.events()[0];
    assert(before.id == edited.id);
    assert(std::fabs(edited.sourceTime - before.sourceTime) < 1e-12);
    assert(edited.primary == DrumClass::Snare);
    assert(std::fabs(edited.quantisedTime - 0.031) < 1e-6);
    assert(std::fabs(edited.velocity - 0.42f) < 1e-6f);
    assert(edited.ghost);

    assert(processor.undo());
    assert(processor.events()[0].primary == before.primary);
    assert(processor.events()[0].ghost == before.ghost);
}

void testOutputSwitchSemantics()
{
    assert(audioOutMode(0.0f) == AudioOutMode::Off);
    assert(audioOutMode(0.4f) == AudioOutMode::Off);
    assert(audioOutMode(1.0f) == AudioOutMode::Pass);
    assert(audioOutMode(1.4f) == AudioOutMode::Pass);
    assert(audioOutMode(1.6f) == AudioOutMode::Render);
    assert(audioOutMode(2.0f) == AudioOutMode::Render);
    assert(!midiOutEnabled(0.0f));
    assert(midiOutEnabled(1.0f));
    assert(!bbtLoopEnabled(0.0f));
    assert(bbtLoopEnabled(1.0f));

    Processor processor {};
    processor.setHostBbtValid(true);
    assert(processor.parameter(static_cast<std::uint32_t>(ParamId::bbtValid)) == 1.0f);
    processor.setHostGrid(96.0, 0.0);
    assert(std::fabs(processor.parameter(static_cast<std::uint32_t>(ParamId::tempoBpm)) - 96.0f) < 1e-6f);
    processor.setHostBbtValid(false);
    assert(std::fabs(processor.parameter(static_cast<std::uint32_t>(ParamId::tempoBpm)) - 120.0f) < 1e-6f);
}

}  // namespace

int main()
{
    testDefaultsAndParameterClamping();
    testStateMachineAndRequestCounters();
    testCaptureClampsToEightSeconds();
    testBbtCaptureMath();
    testHostLockedAnalysis();
    testOutputSwitchSemantics();
    testDemoIsDeterministic();
    testOnsetsMatchSyntheticBreak();
    testSimultaneousHitStaysOneMultiLabelEvent();
    testTempoAndGridFit();
    testClassificationAndGhostFlag();
    testSilentCaptureProducesNoEvents();
    testQuantisationStrengthAndGhostStrength();
    testSwingOffsetsAndPerClassOffsets();
    testMidiExport();
    testRendererIsFiniteDeterministicAndVelocitySensitive();
    testResidualAndSaturation();
    testRenderBreakDeterminismAndMute();
    testSerializationAndHostLifecycle();
    testUndoRedoAndABSwap();
    testEventEditsThroughRequestParameters();
    std::cout << "energise core tests passed\n";
    return 0;
}
