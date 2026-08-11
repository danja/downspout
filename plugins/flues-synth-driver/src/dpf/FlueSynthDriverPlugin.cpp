#include "DistrhoPlugin.hpp"

#include "flues_synth_driver_params.hpp"
#include "flues_synth_driver_processor.hpp"

#include <algorithm>
#include <array>
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

using namespace downspout::flues_synth_driver;

ParameterEnumerationValue kAlgorithmEnumValues[] = {
    {0.0f,"Sine"},{1.0f,"Square"},{2.0f,"Tri"},{3.0f,"Saw"},{4.0f,"RevSaw"},
    {5.0f,"Pulse"},{6.0f,"Noise"},{7.0f,"FM"},{8.0f,"AM"},{9.0f,"Ring"},
    {10.0f,"Phase"},{11.0f,"Fold"},{12.0f,"Wrap"},{13.0f,"Clip"},
    {14.0f,"Crush"},{15.0f,"Shift"},{16.0f,"Comb"},{17.0f,"Karplus"},
};

ParameterEnumerationValue kInterfaceTypeEnumValues[] = {
    {0.0f,"Pluck"},{1.0f,"Hit"},{2.0f,"Reed"},{3.0f,"Flute"},{4.0f,"Brass"},
    {5.0f,"Bow"},{6.0f,"Bell"},{7.0f,"Drum"},{8.0f,"Crystal"},{9.0f,"Vapor"},
    {10.0f,"Quantum"},{11.0f,"Plasma"},
};

MidiMessage toCore(const MidiEvent& e)
{
    MidiMessage m {};
    m.frame = e.frame;
    m.size  = static_cast<std::uint8_t>(std::min<std::size_t>(e.size, 4));
    const std::uint8_t* src = e.size > MidiEvent::kDataSize ? e.dataExt : e.data;
    for (std::size_t i = 0; i < m.size; ++i)
        m.data[i] = src[i];
    return m;
}

MidiEvent toDpf(const MidiMessage& m)
{
    MidiEvent e {};
    e.frame = m.frame;
    e.size  = m.size;
    for (std::size_t i = 0; i < m.size && i < MidiEvent::kDataSize; ++i)
        e.data[i] = m.data[i];
    e.dataExt = nullptr;
    return e;
}

}  // namespace

class FlueSynthDriverPlugin : public Plugin
{
public:
    FlueSynthDriverPlugin()
        : Plugin(static_cast<uint32_t>(kParamCount), 0, 0)
    {
        processor_.init(getSampleRate());
    }

protected:
    const char* getLabel() const override       { return "FlueSynthDriver"; }
    const char* getDescription() const override { return "UI controller and MIDI driver for an external flues-synth instance."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }
    uint32_t    getVersion() const override     { return d_version(0, 1, 0); }
    int64_t     getUniqueId() const override    { return d_cconst('F', 'l', 'S', 'D'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (!input && index < 2)
            port.groupId = kPortGroupStereo;
        port.name   = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        p.hints = kParameterIsAutomatable;

        switch (static_cast<Param>(index)) {

        // ── Synth params ───────────────────────────────────────────────────────
        case kParamAlgorithm:
            p.name = "Algorithm"; p.symbol = "algorithm";
            p.hints |= kParameterIsInteger;
            p.ranges = {0.0f, 0.0f, 17.0f};
            p.enumValues.count = 18; p.enumValues.restrictedMode = true;
            p.enumValues.values = kAlgorithmEnumValues; p.enumValues.deleteLater = false;
            return;
        case kParamDisynP1:
            p.name = "Disyn P1"; p.symbol = "disyn_p1"; p.ranges = {0.5f, 0.0f, 1.0f}; return;
        case kParamDisynP2:
            p.name = "Disyn P2"; p.symbol = "disyn_p2"; p.ranges = {0.5f, 0.0f, 1.0f}; return;
        case kParamDisynP3:
            p.name = "Disyn P3"; p.symbol = "disyn_p3"; p.ranges = {0.5f, 0.0f, 1.0f}; return;
        case kParamNoiseLevel:
            p.name = "Noise Level"; p.symbol = "noise_level"; p.ranges = {0.15f, 0.0f, 1.0f}; return;
        case kParamDcLevel:
            p.name = "DC Level"; p.symbol = "dc_level"; p.ranges = {0.0f, 0.0f, 1.0f}; return;
        case kParamInterfaceType:
            p.name = "Interface Type"; p.symbol = "interface_type";
            p.hints |= kParameterIsInteger;
            p.ranges = {2.0f, 0.0f, 11.0f};
            p.enumValues.count = 12; p.enumValues.restrictedMode = true;
            p.enumValues.values = kInterfaceTypeEnumValues; p.enumValues.deleteLater = false;
            return;
        case kParamIntensity:
            p.name = "Intensity"; p.symbol = "intensity"; p.ranges = {0.5f, 0.0f, 1.0f}; return;
        case kParamF1:
            p.name = "F1 Jaw"; p.symbol = "f1"; p.ranges = {500.0f, 200.0f, 1000.0f};
            p.unit = "Hz"; return;
        case kParamF2:
            p.name = "F2 Tongue"; p.symbol = "f2"; p.ranges = {1500.0f, 500.0f, 3000.0f};
            p.unit = "Hz"; return;
        case kParamF3:
            p.name = "F3 Lips"; p.symbol = "f3"; p.ranges = {2500.0f, 1500.0f, 4000.0f};
            p.unit = "Hz"; return;
        case kParamF4:
            p.name = "F4 Quality"; p.symbol = "f4"; p.ranges = {3500.0f, 2500.0f, 4500.0f};
            p.unit = "Hz"; return;
        case kParamNasal:
            p.name = "Nasal"; p.symbol = "nasal";
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            p.ranges = {0.0f, 0.0f, 1.0f}; return;
        case kParamSing:
            p.name = "Sing"; p.symbol = "sing";
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            p.ranges = {0.0f, 0.0f, 1.0f}; return;
        case kParamShout:
            p.name = "Shout"; p.symbol = "shout";
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            p.ranges = {0.0f, 0.0f, 1.0f}; return;
        case kParamFry:
            p.name = "Fry"; p.symbol = "fry";
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            p.ranges = {0.0f, 0.0f, 1.0f}; return;
        case kParamAttack:
            p.name = "Attack"; p.symbol = "attack";
            p.ranges = {0.01f, 0.001f, 1.0f}; p.unit = "s"; return;
        case kParamRelease:
            p.name = "Release"; p.symbol = "release";
            p.ranges = {0.05f, 0.01f, 3.0f}; p.unit = "s"; return;
        case kParamTuning:
            p.name = "Tuning"; p.symbol = "tuning";
            p.hints |= kParameterIsInteger;
            p.ranges = {0.0f, -12.0f, 12.0f}; p.unit = "st"; return;
        case kParamDelay1Fb:
            p.name = "Delay 1 FB"; p.symbol = "delay1_fb"; p.ranges = {0.2f, 0.0f, 1.0f}; return;
        case kParamDelay2Fb:
            p.name = "Delay 2 FB"; p.symbol = "delay2_fb"; p.ranges = {0.2f, 0.0f, 1.0f}; return;
        case kParamDelayRatio:
            p.name = "Delay Ratio"; p.symbol = "delay_ratio"; p.ranges = {1.0f, 0.5f, 2.0f}; return;
        case kParamFilterFb:
            p.name = "Filter FB"; p.symbol = "filter_fb"; p.ranges = {0.1f, 0.0f, 1.0f}; return;
        case kParamFilterFreq:
            p.name = "Filter Freq"; p.symbol = "filter_freq";
            p.ranges = {2000.0f, 20.0f, 20000.0f}; p.unit = "Hz"; return;
        case kParamFilterQ:
            p.name = "Filter Q"; p.symbol = "filter_q"; p.ranges = {1.0f, 0.1f, 10.0f}; return;
        case kParamFilterShape:
            p.name = "Filter Shape"; p.symbol = "filter_shape"; p.ranges = {0.0f, 0.0f, 1.0f}; return;
        case kParamLfoFreq:
            p.name = "LFO Freq"; p.symbol = "lfo_freq";
            p.ranges = {5.0f, 0.1f, 20.0f}; p.unit = "Hz"; return;
        case kParamAmFmDepth:
            p.name = "AM/FM Depth"; p.symbol = "amfm_depth"; p.ranges = {0.0f, -1.0f, 1.0f}; return;
        case kParamGain:
            p.name = "Gain"; p.symbol = "gain"; p.ranges = {0.5f, 0.0f, 1.0f}; return;

        // ── Trajectory ─────────────────────────────────────────────────────────
        case kParamTrajSides:
            p.name = "Traj Sides"; p.symbol = "traj_sides";
            p.hints |= kParameterIsInteger;
            p.ranges = {8.0f, 3.0f, 24.0f}; return;
        case kParamTrajStartPos:
            p.name = "Traj Start Pos"; p.symbol = "traj_start_pos";
            p.ranges = {0.0f, 0.0f, 360.0f}; p.unit = "deg"; return;
        case kParamTrajStartAngle:
            p.name = "Traj Start Angle"; p.symbol = "traj_start_angle";
            p.ranges = {0.0f, 0.0f, 360.0f}; p.unit = "deg"; return;
        case kParamTrajJitter:
            p.name = "Traj Jitter"; p.symbol = "traj_jitter";
            p.ranges = {0.0f, 0.0f, 10.0f}; p.unit = "deg"; return;
        case kParamTrajClip:
            p.name = "Traj Clip"; p.symbol = "traj_clip"; p.ranges = {0.5f, 0.0f, 1.0f}; return;
        case kParamTrajMixX:
            p.name = "Traj Mix X"; p.symbol = "traj_mix_x"; p.ranges = {0.5f, 0.0f, 1.0f}; return;
        case kParamTrajMixY:
            p.name = "Traj Mix Y"; p.symbol = "traj_mix_y"; p.ranges = {0.5f, 0.0f, 1.0f}; return;

        // ── Driver routing ─────────────────────────────────────────────────────
        case kParamOutputChannel:
            p.name = "Output Channel"; p.symbol = "output_ch";
            p.hints |= kParameterIsInteger;
            p.ranges = {1.0f, 1.0f, 16.0f}; return;
        case kParamConductorCh:
            p.name = "Conductor Ch"; p.symbol = "conductor_ch";
            p.hints |= kParameterIsInteger;
            p.ranges = {0.0f, 0.0f, 16.0f}; return;
        case kParamPassInput:
            p.name = "Pass Input"; p.symbol = "pass_input";
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            p.ranges = {1.0f, 0.0f, 1.0f}; return;
        case kParamPanic:
            p.name = "Panic"; p.symbol = "panic";
            p.hints = kParameterIsAutomatable | kParameterIsBoolean
                    | kParameterIsInteger | kParameterIsTrigger;
            p.ranges = {0.0f, 0.0f, 1.0f}; return;

        // ── Status ─────────────────────────────────────────────────────────────
        case kParamMidiActivity:
            p.name = "MIDI Activity"; p.symbol = "midi_activity";
            p.hints = kParameterIsOutput;
            p.ranges = {0.0f, 0.0f, 1.0f}; return;

        default:
            p.name = "Unknown"; p.symbol = "unknown";
            p.ranges = {0.0f, 0.0f, 1.0f}; return;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        const auto param = static_cast<Param>(index);

        if (param >= kParamAlgorithm && param <= kParamTrajMixY)
            return processor_.getSynthParam(static_cast<std::size_t>(param - kParamAlgorithm));

        switch (param) {
        case kParamOutputChannel:  return static_cast<float>(outputChannel_);
        case kParamConductorCh:    return static_cast<float>(conductorCh_);
        case kParamPassInput:      return passInput_ ? 1.0f : 0.0f;
        case kParamPanic:          return 0.0f;
        case kParamMidiActivity:   return midiActivity_;
        default:                   return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        const auto param = static_cast<Param>(index);
        switch (param) {
        case kParamAlgorithm:      processor_.setAlgorithm(static_cast<int>(std::lround(value))); break;
        case kParamDisynP1:        processor_.setDisynP1(value); break;
        case kParamDisynP2:        processor_.setDisynP2(value); break;
        case kParamDisynP3:        processor_.setDisynP3(value); break;
        case kParamNoiseLevel:     processor_.setNoiseLevel(value); break;
        case kParamDcLevel:        processor_.setDcLevel(value); break;
        case kParamInterfaceType:  processor_.setInterfaceType(static_cast<int>(std::lround(value))); break;
        case kParamIntensity:      processor_.setIntensity(value); break;
        case kParamF1:             processor_.setF1(value); break;
        case kParamF2:             processor_.setF2(value); break;
        case kParamF3:             processor_.setF3(value); break;
        case kParamF4:             processor_.setF4(value); break;
        case kParamNasal:          processor_.setNasal(value >= 0.5f); break;
        case kParamSing:           processor_.setSing(value >= 0.5f); break;
        case kParamShout:          processor_.setShout(value >= 0.5f); break;
        case kParamFry:            processor_.setFry(value >= 0.5f); break;
        case kParamAttack:         processor_.setAttack(value); break;
        case kParamRelease:        processor_.setRelease(value); break;
        case kParamTuning:         processor_.setTuning(value); break;
        case kParamDelay1Fb:       processor_.setDelay1Fb(value); break;
        case kParamDelay2Fb:       processor_.setDelay2Fb(value); break;
        case kParamDelayRatio:     processor_.setDelayRatio(value); break;
        case kParamFilterFb:       processor_.setFilterFb(value); break;
        case kParamFilterFreq:     processor_.setFilterFreq(value); break;
        case kParamFilterQ:        processor_.setFilterQ(value); break;
        case kParamFilterShape:    processor_.setFilterShape(value); break;
        case kParamLfoFreq:        processor_.setLfoFreq(value); break;
        case kParamAmFmDepth:      processor_.setAmFmDepth(value); break;
        case kParamGain:           processor_.setGain(value); break;
        case kParamTrajSides:      processor_.setTrajSides(static_cast<int>(std::lround(value))); break;
        case kParamTrajStartPos:   processor_.setTrajStartPos(value); break;
        case kParamTrajStartAngle: processor_.setTrajStartAngle(value); break;
        case kParamTrajJitter:     processor_.setTrajJitter(value); break;
        case kParamTrajClip:       processor_.setTrajClip(value); break;
        case kParamTrajMixX:       processor_.setTrajMixX(value); break;
        case kParamTrajMixY:       processor_.setTrajMixY(value); break;
        case kParamOutputChannel:
            outputChannel_ = std::max(1, std::min(16, static_cast<int>(std::lround(value))));
            processor_.setOutputChannel(outputChannel_);
            break;
        case kParamConductorCh:
            conductorCh_ = std::max(0, std::min(16, static_cast<int>(std::lround(value))));
            processor_.setConductorCh(conductorCh_);
            break;
        case kParamPassInput:
            passInput_ = value >= 0.5f;
            processor_.setPassInput(passInput_);
            break;
        case kParamPanic:
            if (value >= 0.5f)
                processor_.triggerPanic();
            break;
        default: break;
        }
    }

    void activate() override
    {
        processor_.activate();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        processor_.init(newSampleRate);
    }

    void run(const float**,
             float** outputs,
             uint32_t frames,
             const MidiEvent* midiEvents,
             uint32_t midiEventCount) override
    {
        std::fill_n(outputs[0], frames, 0.0f);
        std::fill_n(outputs[1], frames, 0.0f);

        std::array<MidiMessage, 256> coreIn {};
        const std::uint32_t inCount = std::min<std::uint32_t>(midiEventCount, coreIn.size());
        for (std::uint32_t i = 0; i < inCount; ++i)
            coreIn[i] = toCore(midiEvents[i]);

        ProcessResult result = processor_.processBlock(frames, coreIn.data(), inCount);

        std::stable_sort(result.events.begin(),
                         result.events.begin() + result.eventCount,
                         [](const MidiMessage& a, const MidiMessage& b) {
                             return a.frame < b.frame;
                         });

        for (std::uint32_t i = 0; i < result.eventCount; ++i)
            writeMidiEvent(toDpf(result.events[i]));

        midiActivity_ = result.midiActivity;
    }

private:
    Processor processor_ {};
    int outputChannel_ = 1;
    int conductorCh_   = 0;
    bool passInput_    = true;
    float midiActivity_ = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlueSynthDriverPlugin)
};

Plugin* createPlugin()
{
    return new FlueSynthDriverPlugin();
}

END_NAMESPACE_DISTRHO
