#include "DistrhoPlugin.hpp"
#include "thumper_core_types.hpp"
#include "thumper_params.hpp"
#include "thumper_serialization.hpp"

#include <cstring>
#include <string>
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {
constexpr const char* kStateKeyParameters = "parameters";
enum StateIndex : uint32_t { kStateParameters = 0, kStateCount };

enum ParamId : uint32_t {
    pEngine = 0, pPreset, pPitch, pPitchDrop, pDecay, pClick, pBody, pDrive, pTone, pVolume,
    pFmRatio, pFmIndex, pModalDecay, pSubMix, pDistortion,
    pPitchEnvDecay, pFilterEnvDecay, pAmpEnvDecay,
    pEngine2, pLayerMix,
    kParamCount
};
}

class ThumperPlugin : public Plugin {
public:
    ThumperPlugin()
        : Plugin(kParamCount, 0, kStateCount)
    {
        params_ = thumper::Params();
        engine_.setSampleRate((int)getSampleRate());
        engine_.setParams(params_);
    }
protected:
    const char* getLabel() const override { return "Thumper"; }
    const char* getDescription() const override { return "Kick drum generator"; }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0,1,0); }
    int64_t getUniqueId() const override { return d_cconst('T','h','m','p'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override {
        Plugin::initAudioPort(input, index, port);
        if (index < 2) port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String((int)(index+1));
        port.symbol = String(input ? "in_" : "out_") + String((int)(index+1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override {
        if (index >= kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        switch (index) {
            case pEngine: parameter.name="Engine"; parameter.symbol="engine"; parameter.ranges.min=0; parameter.ranges.max=9; parameter.ranges.def=0; parameter.hints|=kParameterIsInteger; break;
            case pPreset: parameter.name="Preset"; parameter.symbol="preset"; parameter.ranges.min=0; parameter.ranges.max=7; parameter.ranges.def=0; parameter.hints|=kParameterIsInteger; break;
            case pPitch: parameter.name="Pitch"; parameter.symbol="pitch"; parameter.unit="Hz"; parameter.ranges.min=30; parameter.ranges.max=120; parameter.ranges.def=55; break;
            case pPitchDrop: parameter.name="Pitch Drop"; parameter.symbol="pitchDrop"; parameter.unit="st"; parameter.ranges.min=0; parameter.ranges.max=24; parameter.ranges.def=12; break;
            case pDecay: parameter.name="Decay"; parameter.symbol="decay"; parameter.unit="s"; parameter.ranges.min=0.05f; parameter.ranges.max=2; parameter.ranges.def=0.5f; break;
            case pClick: parameter.name="Click"; parameter.symbol="click"; parameter.ranges.min=0; parameter.ranges.max=1; parameter.ranges.def=0.5f; break;
            case pBody: parameter.name="Body"; parameter.symbol="body"; parameter.ranges.min=0; parameter.ranges.max=1; parameter.ranges.def=0.7f; break;
            case pDrive: parameter.name="Drive"; parameter.symbol="drive"; parameter.ranges.min=0; parameter.ranges.max=1; parameter.ranges.def=0.3f; break;
            case pTone: parameter.name="Tone"; parameter.symbol="tone"; parameter.ranges.min=0; parameter.ranges.max=1; parameter.ranges.def=0.5f; break;
            case pVolume: parameter.name="Volume"; parameter.symbol="volume"; parameter.ranges.min=0; parameter.ranges.max=1; parameter.ranges.def=0.8f; break;
            case pFmRatio: parameter.name="FM Ratio"; parameter.symbol="fmRatio"; parameter.ranges.min=0.5f; parameter.ranges.max=8; parameter.ranges.def=2; break;
            case pFmIndex: parameter.name="FM Index"; parameter.symbol="fmIndex"; parameter.ranges.min=0; parameter.ranges.max=10; parameter.ranges.def=3; break;
            case pModalDecay: parameter.name="Modal Decay"; parameter.symbol="modalDecay"; parameter.ranges.min=0.05f; parameter.ranges.max=2; parameter.ranges.def=0.4f; break;
            case pSubMix: parameter.name="Sub Mix"; parameter.symbol="subMix"; parameter.ranges.min=0; parameter.ranges.max=1; parameter.ranges.def=0.5f; break;
            case pDistortion: parameter.name="Distortion"; parameter.symbol="distortion"; parameter.ranges.min=0; parameter.ranges.max=1; parameter.ranges.def=0.0f; break;
            case pPitchEnvDecay: parameter.name="Pitch Env"; parameter.symbol="pitchEnvDecay"; parameter.ranges.min=0.001f; parameter.ranges.max=2; parameter.ranges.def=0.1f; break;
            case pFilterEnvDecay: parameter.name="Filter Env"; parameter.symbol="filterEnvDecay"; parameter.ranges.min=0.001f; parameter.ranges.max=2; parameter.ranges.def=0.3f; break;
            case pAmpEnvDecay: parameter.name="Amp Env"; parameter.symbol="ampEnvDecay"; parameter.ranges.min=0.001f; parameter.ranges.max=2; parameter.ranges.def=0.5f; break;
            case pEngine2: parameter.name="Engine 2"; parameter.symbol="engine2"; parameter.ranges.min=0; parameter.ranges.max=9; parameter.ranges.def=0; parameter.hints|=kParameterIsInteger; break;
            case pLayerMix: parameter.name="Layer Mix"; parameter.symbol="layerMix"; parameter.ranges.min=0; parameter.ranges.max=1; parameter.ranges.def=0.0f; break;
        }
    }

    void initState(uint32_t index, State& state) override {
        if (index != kStateParameters) return;
        state.key = kStateKeyParameters; state.label = "Parameters"; state.hints = kStateIsOnlyForDSP; state.defaultValue = "";
    }

    String getState(const char* key) const override {
        if (std::strcmp(key, kStateKeyParameters)==0) return String(thumper::serialize(params_).c_str());
        return String();
    }
    void setState(const char* key, const char* value) override {
        if (std::strcmp(key, kStateKeyParameters)!=0) return;
        std::string text = value?value:"";
        thumper::Params p;
        if (thumper::deserialize(text, p)) { params_ = p; engine_.setParams(params_); }
    }

    float getParameterValue(uint32_t index) const override {
        switch(index) {
            case pEngine: return (float)params_.engine;
            case pPreset: return (float)params_.preset;
            case pPitch: return params_.pitch;
            case pPitchDrop: return params_.pitchDrop;
            case pDecay: return params_.decay;
            case pClick: return params_.click;
            case pBody: return params_.body;
            case pDrive: return params_.drive;
            case pTone: return params_.tone;
            case pVolume: return params_.volume;
            case pFmRatio: return params_.fmRatio;
            case pFmIndex: return params_.fmIndex;
            case pModalDecay: return params_.modalDecay;
            case pSubMix: return params_.subMix;
            case pDistortion: return params_.distortion;
            case pPitchEnvDecay: return params_.pitchEnvDecay;
            case pFilterEnvDecay: return params_.filterEnvDecay;
            case pAmpEnvDecay: return params_.ampEnvDecay;
            case pEngine2: return (float)params_.engine2;
            case pLayerMix: return params_.layerMix;
        }
        return 0.0f;
    }
    void setParameterValue(uint32_t index, float value) override {
        switch(index) {
            case pEngine: params_.engine = (int)value; break;
            case pPreset: params_.preset = (int)value; engine_.setParams(params_); engine_.applyPreset(); params_ = engine_.params(); break;
            case pPitch: params_.pitch = value; break;
            case pPitchDrop: params_.pitchDrop = value; break;
            case pDecay: params_.decay = value; break;
            case pClick: params_.click = value; break;
            case pBody: params_.body = value; break;
            case pDrive: params_.drive = value; break;
            case pTone: params_.tone = value; break;
            case pVolume: params_.volume = value; break;
            case pFmRatio: params_.fmRatio = value; break;
            case pFmIndex: params_.fmIndex = value; break;
            case pModalDecay: params_.modalDecay = value; break;
            case pSubMix: params_.subMix = value; break;
            case pDistortion: params_.distortion = value; break;
            case pPitchEnvDecay: params_.pitchEnvDecay = value; break;
            case pFilterEnvDecay: params_.filterEnvDecay = value; break;
            case pAmpEnvDecay: params_.ampEnvDecay = value; break;
            case pEngine2: params_.engine2 = (int)value; break;
            case pLayerMix: params_.layerMix = value; break;
        }
        engine_.setParams(params_);
    }

    void activate() override { engine_.setSampleRate((int)getSampleRate()); engine_.setParams(params_); }
    void sampleRateChanged(double newSampleRate) override { engine_.setSampleRate((int)newSampleRate); }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override {
        for (uint32_t i=0;i<midiEventCount;i++) {
            const MidiEvent& e = midiEvents[i];
            if (e.size<3) continue;
            if ((e.data[0]&0xF0)!=0x90 || e.data[2]==0) continue;
            int channel = (e.data[0]&0x0F)+1;
            int note = e.data[1];
            if (channel != 10) {
                // pitch follows MIDI note
                float freq = 440.0f * std::pow(2.0f, (note-69)/12.0f);
                params_.pitch = freq;
                engine_.setParams(params_);
            }
            engine_.trigger();
        }
        float* L = outputs[0]; float* R = outputs[1];
        // clear
        for (uint32_t i=0;i<frames;i++){ L[i]=0; R[i]=0; }
        engine_.render(L,R,(int)frames);
    }

private:
    thumper::Params params_;
    thumper::ThumperEngine engine_;
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThumperPlugin)
};

Plugin* createPlugin() { return new ThumperPlugin(); }
END_NAMESPACE_DISTRHO