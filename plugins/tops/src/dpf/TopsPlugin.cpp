#include "DistrhoPlugin.hpp"
#include "tops_core_types.hpp"
#include "tops_params.hpp"
#include "tops_serialization.hpp"
#include <cstring>
#include <string>
#include <cmath>
START_NAMESPACE_DISTRHO
namespace { constexpr const char* kStateKeyParameters="parameters"; enum StateIndex:uint32_t{kStateParameters=0,kStateCount}; enum ParamId:uint32_t{pInstrument=0,pEngine,pPreset,pPitch,pDecay,pTone,pNoise,pBrightness,pVolume,pDistortion,kParamCount}; }
class TopsPlugin : public Plugin {
public:
    TopsPlugin():Plugin(kParamCount,0,kStateCount){ params_=tops::Params(); engine_.setSampleRate((int)getSampleRate()); engine_.setParams(params_);}
protected:
    const char* getLabel() const override{return "Tops";}
    const char* getDescription() const override{return "Hi-hat and cymbal generator";}
    const char* getMaker() const override{return "danja";}
    const char* getHomePage() const override{return "https://danja.github.io/downspout/";}
    const char* getLicense() const override{return "MIT";}
    uint32_t getVersion() const override{return d_version(0,1,0);}
    int64_t getUniqueId() const override{return d_cconst('T','o','p','s');}
    void initAudioPort(const bool input,const uint32_t index,AudioPort& port) override{ Plugin::initAudioPort(input,index,port); if(index<2) port.groupId=kPortGroupStereo; port.name=String(input?"Input ":"Output ")+String((int)(index+1)); port.symbol=String(input?"in_":"out_")+String((int)(index+1));}
    void initParameter(uint32_t index,Parameter& parameter) override{
        if(index>=kParamCount) return; parameter.hints=kParameterIsAutomatable;
        switch(index){
            case pInstrument: parameter.name="Instrument";parameter.symbol="instrument";parameter.ranges.min=0;parameter.ranges.max=9;parameter.ranges.def=0;parameter.hints|=kParameterIsInteger;break;
            case pEngine: parameter.name="Engine";parameter.symbol="engine";parameter.ranges.min=0;parameter.ranges.max=5;parameter.ranges.def=0;parameter.hints|=kParameterIsInteger;break;
            case pPreset: parameter.name="Preset";parameter.symbol="preset";parameter.ranges.min=0;parameter.ranges.max=4;parameter.ranges.def=0;parameter.hints|=kParameterIsInteger;break;
            case pPitch: parameter.name="Pitch";parameter.symbol="pitch";parameter.ranges.min=1000;parameter.ranges.max=12000;parameter.ranges.def=8000;break;
            case pDecay: parameter.name="Decay";parameter.symbol="decay";parameter.ranges.min=0.01f;parameter.ranges.max=2;parameter.ranges.def=0.08f;break;
            case pTone: parameter.name="Tone";parameter.symbol="tone";parameter.ranges.min=0;parameter.ranges.max=1;parameter.ranges.def=0.6f;break;
            case pNoise: parameter.name="Noise";parameter.symbol="noise";parameter.ranges.min=0;parameter.ranges.max=1;parameter.ranges.def=0.5f;break;
            case pBrightness: parameter.name="Brightness";parameter.symbol="brightness";parameter.ranges.min=0;parameter.ranges.max=1;parameter.ranges.def=0.7f;break;
            case pVolume: parameter.name="Volume";parameter.symbol="volume";parameter.ranges.min=0;parameter.ranges.max=1;parameter.ranges.def=0.7f;break;
            case pDistortion: parameter.name="Distortion";parameter.symbol="distortion";parameter.ranges.min=0;parameter.ranges.max=1;parameter.ranges.def=0;break;
        }
    }
    void initState(uint32_t index,State& state) override{ if(index!=kStateParameters) return; state.key=kStateKeyParameters; state.label="Parameters"; state.hints=kStateIsOnlyForDSP; state.defaultValue="";}
    String getState(const char* key) const override{ if(std::strcmp(key,kStateKeyParameters)==0) return String(tops::serialize(params_).c_str()); return String();}
    void setState(const char* key,const char* value) override{ if(std::strcmp(key,kStateKeyParameters)!=0) return; std::string text=value?value:""; tops::Params p; if(tops::deserialize(text,p)){params_=p; engine_.setParams(params_);}}
    float getParameterValue(uint32_t index) const override{
        switch(index){ case pInstrument:return (float)params_.instrument; case pEngine:return (float)params_.engine; case pPreset:return (float)params_.preset; case pPitch:return params_.pitch; case pDecay:return params_.decay; case pTone:return params_.tone; case pNoise:return params_.noise; case pBrightness:return params_.brightness; case pVolume:return params_.volume; case pDistortion:return params_.distortion;}
        return 0;
    }
    void setParameterValue(uint32_t index,float value) override{
        switch(index){ case pInstrument:params_.instrument=(int)value;break; case pEngine:params_.engine=(int)value;break; case pPreset:params_.preset=(int)value; engine_.setParams(params_); engine_.applyPreset(); params_=engine_.params(); break; case pPitch:params_.pitch=value;break; case pDecay:params_.decay=value;break; case pTone:params_.tone=value;break; case pNoise:params_.noise=value;break; case pBrightness:params_.brightness=value;break; case pVolume:params_.volume=value;break; case pDistortion:params_.distortion=value;break;}
        engine_.setParams(params_);
    }
    void activate() override{ engine_.setSampleRate((int)getSampleRate()); engine_.setParams(params_);}
    void sampleRateChanged(double sr) override{ engine_.setSampleRate((int)sr);}
    void run(const float**,float** outputs,uint32_t frames,const MidiEvent* midiEvents,uint32_t midiEventCount) override{
        for(uint32_t i=0;i<midiEventCount;i++){ const MidiEvent&e=midiEvents[i]; if(e.size>=3 && (e.data[0]&0xF0)==0x90 && e.data[2]!=0) engine_.trigger(); }
        float* L=outputs[0]; float* R=outputs[1];
        for(uint32_t i=0;i<frames;i++){L[i]=0;R[i]=0;}
        engine_.render(L,R,(int)frames);
    }
private:
    tops::Params params_;
    tops::TopsEngine engine_;
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TopsPlugin)
};
Plugin* createPlugin(){return new TopsPlugin();}
END_NAMESPACE_DISTRHO