#include "DistrhoPlugin.hpp"
#include "snatch_core_types.hpp"
#include "snatch_params.hpp"
#include <cstring>
#include <string>
#include <cmath>
START_NAMESPACE_DISTRHO
namespace { enum ParamId:uint32_t{pEngine=0,pPreset,pPitch,pTone,pNoise,pSnap,pBody,pDecay,pVolume,pDistortion,pNoteMap,kParamCount}; }
class SnatchPlugin : public Plugin {
public:
    SnatchPlugin():Plugin(kParamCount,0,0){ params_=snatch::Params(); engine_.setSampleRate((int)getSampleRate()); engine_.setParams(params_);}
protected:
    const char* getLabel() const override{return "Snatch";}
    const char* getDescription() const override{return "Snare drum generator";}
    const char* getMaker() const override{return "danja";}
    const char* getHomePage() const override{return "https://danja.github.io/downspout/";}
    const char* getLicense() const override{return "MIT";}
    uint32_t getVersion() const override{return d_version(0,1,0);}
    int64_t getUniqueId() const override{return d_cconst('S','n','c','h');}
    void initAudioPort(const bool input,const uint32_t index,AudioPort& port) override{ Plugin::initAudioPort(input,index,port); if(index<2) port.groupId=kPortGroupStereo; port.name=String(input?"Input ":"Output ")+String((int)(index+1)); port.symbol=String(input?"in_":"out_")+String((int)(index+1));}
    void initParameter(uint32_t index,Parameter& parameter) override{
        if(index>=kParamCount) return;
        parameter.hints=kParameterIsAutomatable;
        switch(index){
            case pEngine: parameter.name="Engine";parameter.symbol="engine";parameter.ranges.min=0;parameter.ranges.max=5;parameter.ranges.def=0;parameter.hints|=kParameterIsInteger;break;
            case pPreset: parameter.name="Preset";parameter.symbol="preset";parameter.ranges.min=0;parameter.ranges.max=4;parameter.ranges.def=0;parameter.hints|=kParameterIsInteger;break;
            case pPitch: parameter.name="Pitch";parameter.symbol="pitch";parameter.ranges.min=100;parameter.ranges.max=400;parameter.ranges.def=200;break;
            case pTone: parameter.name="Tone";parameter.symbol="tone";parameter.ranges.min=0;parameter.ranges.max=1;parameter.ranges.def=0.5f;break;
            case pNoise: parameter.name="Noise";parameter.symbol="noise";parameter.ranges.min=0;parameter.ranges.max=1;parameter.ranges.def=0.7f;break;
            case pSnap: parameter.name="Snap";parameter.symbol="snap";parameter.ranges.min=0;parameter.ranges.max=1;parameter.ranges.def=0.6f;break;
            case pBody: parameter.name="Body";parameter.symbol="body";parameter.ranges.min=0;parameter.ranges.max=1;parameter.ranges.def=0.6f;break;
            case pDecay: parameter.name="Decay";parameter.symbol="decay";parameter.ranges.min=0.05f;parameter.ranges.max=1;parameter.ranges.def=0.25f;break;
            case pVolume: parameter.name="Volume";parameter.symbol="volume";parameter.ranges.min=0;parameter.ranges.max=1;parameter.ranges.def=0.8f;break;
            case pDistortion: parameter.name="Distortion";parameter.symbol="distortion";parameter.ranges.min=0;parameter.ranges.max=1;parameter.ranges.def=0;break;
            case pNoteMap: parameter.name="Note Map";parameter.symbol="noteMap";parameter.ranges.min=0;parameter.ranges.max=1;parameter.ranges.def=1;parameter.hints|=kParameterIsInteger;break;
        }
    }
    float getParameterValue(uint32_t index) const override{
        switch(index){ case pEngine:return (float)params_.engine; case pPreset:return (float)params_.preset; case pPitch:return params_.pitch; case pTone:return params_.tone; case pNoise:return params_.noise; case pSnap:return params_.snap; case pBody:return params_.body; case pDecay:return params_.decay; case pVolume:return params_.volume; case pDistortion:return params_.distortion; case pNoteMap:return (float)params_.noteMap;}
        return 0;
    }
    void setParameterValue(uint32_t index,float value) override{
        switch(index){ case pEngine:params_.engine=(int)value;break; case pPreset:params_.preset=(int)value; break; case pPitch:params_.pitch=value;break; case pTone:params_.tone=value;break; case pNoise:params_.noise=value;break; case pSnap:params_.snap=value;break; case pBody:params_.body=value;break; case pDecay:params_.decay=value;break; case pVolume:params_.volume=value;break; case pDistortion:params_.distortion=value;break; case pNoteMap:params_.noteMap=(int)value;break;}
        engine_.setParams(params_);
    }
    void activate() override{ engine_.setSampleRate((int)getSampleRate()); engine_.setParams(params_);}
    void sampleRateChanged(double sr) override{ engine_.setSampleRate((int)sr);}
    void run(const float**,float** outputs,uint32_t frames,const MidiEvent* midiEvents,uint32_t midiEventCount) override{
        for(uint32_t i=0;i<midiEventCount;i++){
            const MidiEvent&e=midiEvents[i];
            if(e.size<3 || (e.data[0]&0xF0)!=0x90 || e.data[2]==0) continue;
            int engine=-1;  // -1 = the Engine parameter
            if(params_.noteMap==(int)snatch::NoteMap::GeneralMidi){ engine=snatch::SnatchEngine::engineForNote(e.data[1],params_.engine); if(engine<0) continue; }
            engine_.trigger(engine,(float)e.data[2]/127.0f);
        }
        float* L=outputs[0]; float* R=outputs[1];
        for(uint32_t i=0;i<frames;i++){L[i]=0;R[i]=0;}
        engine_.render(L,R,(int)frames);
    }
private:
    snatch::Params params_;
    snatch::SnatchEngine engine_;
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SnatchPlugin)
};
Plugin* createPlugin(){return new SnatchPlugin();}
END_NAMESPACE_DISTRHO