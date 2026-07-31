#include "DistrhoPlugin.hpp"
#include "lightverb_core.hpp"
#include "lightverb_serialization.hpp"
#include <algorithm>
#include <array>
#include <cstring>
START_NAMESPACE_DISTRHO
using namespace downspout::lightverb;
namespace { constexpr const char* kStateKey="parameters"; }
class LightverbPlugin final : public Plugin {
public: LightverbPlugin():Plugin(kParameterCount,0,1){}
protected:
 const char*getLabel()const override{return "Lightverb";}const char*getDescription()const override{return "Low-CPU stereo feedback-delay-network reverb with MIDI producer control";}const char*getMaker()const override{return "danja";}const char*getHomePage()const override{return "https://danja.github.io/downspout/";}const char*getLicense()const override{return "MIT";}std::uint32_t getVersion()const override{return d_version(0,1,0);}std::int64_t getUniqueId()const override{return d_cconst('L','t','V','b');}
 void initAudioPort(bool input,std::uint32_t index,AudioPort&port)override{Plugin::initAudioPort(input,index,port);port.groupId=kPortGroupStereo;port.name=index==0?(input?"Input Left":"Output Left"):(input?"Input Right":"Output Right");port.symbol=index==0?(input?"in_left":"out_left"):(input?"in_right":"out_right");}
 void initParameter(std::uint32_t index,Parameter&parameter)override{const auto&s=kParameterSpecs[index];parameter.name=s.name;parameter.symbol=s.symbol;parameter.hints=s.output?kParameterIsOutput:kParameterIsAutomatable;if(s.integer)parameter.hints|=kParameterIsInteger;if(index==kMidiEnabled||index==kResetMidi||index==kStatusSpaceMidi||index==kStatusMixMidi||index==kRequireProducerGate||index==kStatusProducerActive)parameter.hints|=kParameterIsBoolean;parameter.ranges={s.defaultValue,s.minimum,s.maximum};if(index==kDecaySeconds)parameter.unit="s";else if(index==kPreDelayMs)parameter.unit="ms";else if(index==kOutputDb)parameter.unit="dB";}
 void initState(std::uint32_t index,State&state)override{if(index==0){state.key=kStateKey;state.label="Parameters";state.hints=kStateIsOnlyForDSP;state.defaultValue="";}}
 float getParameterValue(std::uint32_t index)const override{switch(index){case kResetMidi:return 0;case kStatusSpace:return status_.space;case kStatusMix:return status_.mix;case kStatusSpaceMidi:return status_.spaceMidi?1:0;case kStatusMixMidi:return status_.mixMidi?1:0;case kStatusTail:return status_.tail;case kStatusInputPeak:return status_.inputPeak;case kStatusOutputPeak:return status_.outputPeak;case kStatusProducerActive:return status_.producerActive?1:0;default:return parameters_.values[index];}}
 void setParameterValue(std::uint32_t index,float value)override{if(index>=kParameterCount||kParameterSpecs[index].output)return;if(index==kResetMidi){if(value>=.5f)releaseMidiTakeover(engine_);return;}parameters_.values[index]=downspout::generative::clampParam(value,kParameterSpecs[index]);}
 String getState(const char*key)const override{return std::strcmp(key,kStateKey)==0?String(serializeParameters(parameters_).c_str()):String();}
 void setState(const char*key,const char*value)override{if(std::strcmp(key,kStateKey)!=0)return;const auto decoded=deserializeParameters(value?value:"");if(decoded)parameters_=*decoded;}
 void activate()override{prepare(engine_,getSampleRate());reset(engine_);}void sampleRateChanged(double rate)override{prepare(engine_,rate);}
 void run(const float**inputs,float**outputs,std::uint32_t frames,const MidiEvent*events,std::uint32_t eventCount)override{std::array<MidiControlEvent,512>controls{};const auto count=std::min<std::uint32_t>(eventCount,controls.size());for(std::uint32_t i=0;i<count;++i){controls[i].frame=std::min(events[i].frame,frames?frames-1:0);controls[i].size=static_cast<std::uint8_t>(std::min<std::uint32_t>(events[i].size,3));const auto*source=events[i].size>MidiEvent::kDataSize?events[i].dataExt:events[i].data;for(std::uint32_t b=0;b<controls[i].size;++b)controls[i].data[b]=source[b];}const AudioBlock audio{inputs[0],inputs[1],outputs[0],outputs[1]};status_=process(engine_,parameters_,frames,audio,controls.data(),count);for(std::uint32_t i=0;i<eventCount;++i)writeMidiEvent(events[i]);}
private:Parameters parameters_{};downspout::lightverb::State engine_{};OutputStatus status_{};DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LightverbPlugin)
};
Plugin*createPlugin(){return new LightverbPlugin();}
END_NAMESPACE_DISTRHO
