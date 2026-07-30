#include "DistrhoPlugin.hpp"
#include "guardian_core.hpp"
#include <array>
START_NAMESPACE_DISTRHO
using namespace downspout::guardian;
class GuardianPlugin:public Plugin{public:GuardianPlugin():Plugin(kParameterCount,0,0){for(std::uint32_t i=0;i<kParameterCount;++i)p_[i]=kParameterSpecs[i].defaultValue;prepare(s_,getSampleRate());setLatency(lookaheadFrames(p_,getSampleRate()));}
protected:const char*getLabel()const override{return"Guardian";}const char*getDescription()const override{return"Output safety, look-ahead limiting and diagnostics.";}const char*getMaker()const override{return"danja";}const char*getHomePage()const override{return"https://danja.github.io/downspout/";}const char*getLicense()const override{return"MIT";}std::uint32_t getVersion()const override{return d_version(0,1,0);}std::int64_t getUniqueId()const override{return d_cconst('G','r','d','n');}
void initParameter(std::uint32_t i,Parameter&q)override{auto&s=kParameterSpecs[i];q.name=s.name;q.symbol=s.symbol;q.hints=s.output?kParameterIsOutput:kParameterIsAutomatable;if(s.integer)q.hints|=kParameterIsInteger;q.ranges={s.defaultValue,s.minimum,s.maximum};}
float getParameterValue(std::uint32_t i)const override{switch(i){case kStatusReduction:return s_.reductionDb;case kStatusFaults:return static_cast<float>(s_.faults);case kStatusOverload:return s_.overload?1:0;case kStatusSilence:return s_.silence?1:0;case kStatusPeak:return s_.peak;default:return p_[i];}}
void setParameterValue(std::uint32_t i,float v)override{if(i<kParameterCount&&!kParameterSpecs[i].output){p_[i]=downspout::generative::clampParam(v,kParameterSpecs[i]);if(i==kLookaheadMs)setLatency(lookaheadFrames(p_,getSampleRate()));}}
void sampleRateChanged(double sr)override{prepare(s_,sr);setLatency(lookaheadFrames(p_,sr));}void activate()override{prepare(s_,getSampleRate());resetAudio(s_);}void run(const float**in,float**out,std::uint32_t n)override{process(s_,p_,n,getSampleRate(),in[0],in[1],out[0],out[1]);}
private:std::array<float,kParameterCount>p_{};downspout::guardian::State s_{};DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GuardianPlugin)};Plugin*createPlugin(){return new GuardianPlugin();}END_NAMESPACE_DISTRHO
