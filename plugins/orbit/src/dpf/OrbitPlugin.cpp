#include "DistrhoPlugin.hpp"
#include "orbit_core.hpp"
#include <array>
START_NAMESPACE_DISTRHO
namespace{using namespace downspout::orbit;Transport ct(const TimePosition&x){Transport t;t.valid=x.bbt.valid;t.playing=x.playing;if(t.valid){t.bar=x.bbt.bar-1;t.barBeat=x.bbt.beat-1+(x.bbt.ticksPerBeat>0?x.bbt.tick/x.bbt.ticksPerBeat:0);t.beatsPerBar=x.bbt.beatsPerBar;t.beatType=x.bbt.beatType;t.bpm=x.bbt.beatsPerMinute;}return t;}}
class OrbitPlugin:public Plugin{public:OrbitPlugin():Plugin(kParameterCount,0,0){for(std::uint32_t i=0;i<kParameterCount;++i)p_[i]=kParameterSpecs[i].defaultValue;prepare(s_,getSampleRate());}
protected:const char*getLabel()const override{return"Orbit";}const char*getDescription()const override{return"Seeded transport-aware stereo spatial motion effect.";}const char*getMaker()const override{return"danja";}const char*getHomePage()const override{return"https://danja.github.io/downspout/";}const char*getLicense()const override{return"MIT";}std::uint32_t getVersion()const override{return d_version(0,1,0);}std::int64_t getUniqueId()const override{return d_cconst('O','r','b','t');}
void initParameter(std::uint32_t i,Parameter&q)override{auto&s=kParameterSpecs[i];q.name=s.name;q.symbol=s.symbol;q.hints=s.output?kParameterIsOutput:kParameterIsAutomatable;if(s.integer)q.hints|=kParameterIsInteger;q.ranges={s.defaultValue,s.minimum,s.maximum};}
float getParameterValue(std::uint32_t i)const override{if(i==kStatusPan)return s_.statusPan;if(i==kStatusDistance)return s_.statusDistance;return p_[i];}
void setParameterValue(std::uint32_t i,float v)override{if(i<kParameterCount&&!kParameterSpecs[i].output)p_[i]=downspout::generative::clampParam(v,kParameterSpecs[i]);}
void sampleRateChanged(double sr)override{prepare(s_,sr);}void activate()override{prepare(s_,getSampleRate());reset(s_);}void run(const float**in,float**out,std::uint32_t n)override{process(s_,p_,ct(getTimePosition()),n,getSampleRate(),in[0],in[1],out[0],out[1]);}
private:std::array<float,kParameterCount>p_{};downspout::orbit::State s_{};DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OrbitPlugin)};Plugin*createPlugin(){return new OrbitPlugin();}END_NAMESPACE_DISTRHO
