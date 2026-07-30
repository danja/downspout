#include "DistrhoPlugin.hpp"
#include "resonance_garden_core.hpp"
#include <array>
START_NAMESPACE_DISTRHO
using namespace downspout::resonance_garden;
class ResonanceGardenPlugin:public Plugin{public:ResonanceGardenPlugin():Plugin(kParameterCount,0,0){for(std::uint32_t i=0;i<kParameterCount;++i)p_[i]=kParameterSpecs[i].defaultValue;prepare(s_,getSampleRate());}
protected:const char*getLabel()const override{return"ResonanceGarden";}const char*getDescription()const override{return"MIDI-tuned bounded resonator bank.";}const char*getMaker()const override{return"danja";}const char*getHomePage()const override{return"https://danja.github.io/downspout/";}const char*getLicense()const override{return"MIT";}std::uint32_t getVersion()const override{return d_version(0,1,0);}std::int64_t getUniqueId()const override{return d_cconst('R','s','G','r');}
void initParameter(std::uint32_t i,Parameter&q)override{auto&s=kParameterSpecs[i];q.name=s.name;q.symbol=s.symbol;q.hints=s.output?kParameterIsOutput:kParameterIsAutomatable;if(s.integer)q.hints|=kParameterIsInteger;q.ranges={s.defaultValue,s.minimum,s.maximum};}
float getParameterValue(std::uint32_t i)const override{if(i==kStatusVoices)return s_.statusVoices;if(i==kStatusPeak)return s_.statusPeak;return p_[i];}
void setParameterValue(std::uint32_t i,float v)override{if(i<kParameterCount&&!kParameterSpecs[i].output)p_[i]=downspout::generative::clampParam(v,kParameterSpecs[i]);}
void sampleRateChanged(double sr)override{prepare(s_,sr);}void activate()override{prepare(s_,getSampleRate());reset(s_);}
void run(const float**in,float**out,std::uint32_t n,const MidiEvent*events,std::uint32_t count)override{std::array<downspout::generative::MidiEvent,512>mi{};count=std::min<std::uint32_t>(count,mi.size());for(std::uint32_t i=0;i<count;++i){mi[i].frame=events[i].frame;mi[i].size=static_cast<std::uint8_t>(std::min<std::uint32_t>(events[i].size,4));auto*d=events[i].size>MidiEvent::kDataSize?events[i].dataExt:events[i].data;for(std::uint8_t b=0;b<mi[i].size;++b)mi[i].data[b]=d[b];}process(s_,p_,n,getSampleRate(),in[0],in[1],out[0],out[1],mi.data(),count);}
private:std::array<float,kParameterCount>p_{};downspout::resonance_garden::State s_{};DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResonanceGardenPlugin)};Plugin*createPlugin(){return new ResonanceGardenPlugin();}END_NAMESPACE_DISTRHO
