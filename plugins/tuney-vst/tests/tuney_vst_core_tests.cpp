#include "tuney_vst_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace downspout::tuney_vst;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

void mappingParity()
{
    TuneyEngine e;
    require(e.mapCharacter("A") == -6, "default A mapping mismatch");
    require(e.mapCharacter("z") == 45, "default z mapping mismatch");
    e.setParameter(kParamMapperLength, 12);
    require(e.mapCharacter("A") == 14, "length mapping mismatch");
    require(e.mapCharacter("M") == 14, "length cycle mismatch");
    e.setParameter(kParamRangeLimit, 5);
    e.setParameter(kParamLimiter, static_cast<float>(Limiter::reflect));
    require(e.mapCharacter("A") >= 18 && e.mapCharacter("A") <= 22, "reflect range mismatch");
}

void tuningParity()
{
    TuneyEngine e;
    e.setParameter(kParamAudioNoteOffset, 0);
    require(std::fabs(e.frequencyForLogicalNote(69) - 440.0) < 0.001, "A440 mismatch");
    require(std::fabs(e.frequencyForLogicalNote(81) - 880.0) < 0.01, "octave mismatch");
    require(std::fabs(evaluateExpression("3/2") - 1.5) < 1e-12, "fraction parser mismatch");
    require(std::fabs(evaluateExpression("cents(1200)") - 2.0) < 1e-12, "cents parser mismatch");

    TuneyState s = e.state();
    s.ratioText = "16/15; 9/8; 6/5; 5/4; 4/3; 45/32; 3/2; 8/5; 5/3; 9/5; 15/8; 2";
    require(e.setStateText(serializeState(s)), "ratio state rejected");
    e.setParameter(kParamTuningType, static_cast<float>(TuningType::ratios));
    require(std::fabs(e.frequencyForLogicalNote(76) - 660.0) < 0.01, "ratio tuning mismatch");
}

void stateRoundTrip()
{
    TuneyState s;
    s.text = "Hello, κόσμε\n";
    s.alphabet = "ABCαβγ";
    s.ratioText = "3/2; 2";
    const std::string encoded = serializeState(s);
    TuneyState restored;
    require(deserializeState(encoded, restored), "state restore failed");
    require(restored.text == s.text && restored.alphabet == s.alphabet, "UTF-8 state mismatch");
    TuneyState unchanged = restored;
    require(!deserializeState("version=99\n", unchanged), "unknown state version accepted");
}

void scheduleAndRender()
{
    TuneyEngine e(48000);
    e.setText("abc.");
    require(e.scheduledEventCount() == 6, "mapped schedule count mismatch");
    e.startPlayback();
    std::vector<float> left(48000), right(48000);
    ProcessResult result;
    e.process(left.data(), right.data(), static_cast<std::uint32_t>(left.size()), result);
    require(std::any_of(left.begin(), left.end(), [](float x) { return std::fabs(x) > 1e-5f; }), "render is silent");
    require(std::all_of(left.begin(), left.end(), [](float x) { return std::isfinite(x) && std::fabs(x) <= 1.0f; }), "render is unbounded");
    require(result.midiCount > 0, "MIDI output missing");
}

void typedAndStopCleanup()
{
    TuneyEngine e;
    e.queueTypedCharacter("A");
    float left[256] {}, right[256] {};
    ProcessResult result;
    e.process(left, right, 256, result);
    require(result.midiCount == 1 && (result.midi[0].data[0] & 0xF0) == 0x90, "typed note-on missing");
    e.stopPlayback();
    e.process(left, right, 256, result);
    require(result.midiCount >= 1 && (result.midi[0].data[0] & 0xF0) == 0x80, "stop note-off missing");
}

TransportSnapshot transportAt(double beat, double bpm = 60.0, bool playing = true,
                              double bar = 0.0, double beatsPerBar = 4.0)
{
    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = playing;
    transport.bar = bar;
    transport.barBeat = beat;
    transport.beatsPerBar = beatsPerBar;
    transport.bpm = bpm;
    return transport;
}

void hostSyncGridAndStop()
{
    TuneyEngine e(1000.0);
    e.setText("A B");
    e.setParameter(kParamTransportSync, 1.0f);
    float left[800] {}, right[800] {};
    ProcessResult result;

    e.process(left, right, 800, result, transportAt(0.0));
    require(result.midiCount == 4, "host-sync event count mismatch");
    require(result.midi[0].frame == 0 && (result.midi[0].data[0] & 0xf0) == 0x90, "host-sync first note-on mismatch");
    require(result.midi[1].frame == 250 && (result.midi[1].data[0] & 0xf0) == 0x80, "host-sync sixteenth note-off mismatch");
    require(result.midi[2].frame == 500 && (result.midi[2].data[0] & 0xf0) == 0x90, "host-sync space rest mismatch");
    require(result.midi[3].frame == 750 && (result.midi[3].data[0] & 0xf0) == 0x80, "host-sync final note-off mismatch");

    TuneyEngine stopped(1000.0);
    stopped.setText("A");
    stopped.setParameter(kParamTransportSync, 1.0f);
    stopped.process(left, right, 64, result, transportAt(0.0, 60.0, false));
    require(result.midiCount == 0 && !stopped.playing(), "stopped host transport produced events");
    stopped.process(left, right, 64, result, transportAt(0.0));
    require(result.midiCount == 1 && stopped.playing(), "host transport start did not trigger sequence");
    stopped.process(left, right, 64, result, transportAt(0.064, 60.0, false));
    require(result.midiCount >= 1 && (result.midi[0].data[0] & 0xf0) == 0x80, "host stop did not clear notes");
}

void hostSyncRewindTempoAndLoop()
{
    TuneyEngine e(1000.0);
    e.setText("AB");
    e.setParameter(kParamTransportSync, 1.0f);
    float left[300] {}, right[300] {};
    ProcessResult result;

    e.process(left, right, 100, result, transportAt(0.0, 120.0));
    require(result.midiCount == 1 && (result.midi[0].data[0] & 0xf0) == 0x90, "tempo test note-on missing");
    e.process(left, right, 100, result, transportAt(0.2, 60.0));
    require(result.midiCount >= 1 && result.midi[0].frame == 50 &&
            (result.midi[0].data[0] & 0xf0) == 0x80,
            "tempo change did not preserve beat-domain note-off");
    e.process(left, right, 64, result, transportAt(0.0, 60.0));
    require(result.midiCount >= 1 && result.midi[result.midiCount - 1].frame == 0 &&
            (result.midi[result.midiCount - 1].data[0] & 0xf0) == 0x90,
            "transport rewind did not restart sequence");

    TuneyEngine meterChange(1000.0);
    meterChange.setText("A");
    meterChange.setParameter(kParamTransportSync, 1.0f);
    meterChange.process(left, right, 100, result, transportAt(0.0));
    meterChange.process(left, right, 64, result, transportAt(0.0, 60.0, true, 1.0, 3.0));
    require(result.midiCount >= 1 && (result.midi[result.midiCount - 1].data[0] & 0xf0) == 0x90,
            "bar or meter change did not restart sequence");

    TuneyEngine looped(1000.0);
    looped.setText("A");
    looped.setParameter(kParamLoop, 1.0f);
    looped.setParameter(kParamTransportSync, 1.0f);
    looped.process(left, right, 300, result, transportAt(0.0));
    require(result.midiCount == 3 &&
            (result.midi[0].data[0] & 0xf0) == 0x90 &&
            (result.midi[1].data[0] & 0xf0) == 0x80 &&
            (result.midi[2].data[0] & 0xf0) == 0x90,
            "host-sync loop boundary mismatch");
}
} // namespace

int main()
{
    mappingParity();
    tuningParity();
    stateRoundTrip();
    scheduleAndRender();
    typedAndStopCleanup();
    hostSyncGridAndStop();
    hostSyncRewindTempoAndLoop();
    std::cout << "tuney-vst core tests passed\n";
}
