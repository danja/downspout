#include "DistrhoPlugin.hpp"
#include "drift_core.hpp"
#include <algorithm>
#include <array>
#include <cmath>
START_NAMESPACE_DISTRHO
namespace {using namespace downspout::drift;Transport ct(const TimePosition&x){Transport t;t.valid=x.bbt.valid;t.playing=x.playing;if(t.valid){t.bar=x.bbt.bar-1;t.barBeat=x.bbt.beat-1+(x.bbt.ticksPerBeat>0?x.bbt.tick/x.bbt.ticksPerBeat:0);t.beatsPerBar=x.bbt.beatsPerBar;t.beatType=x.bbt.beatType;t.bpm=x.bbt.beatsPerMinute;}return t;}}
class DriftPlugin:public Plugin{public:DriftPlugin():Plugin(kParameterCount,0,0){for(std::uint32_t i=0;i<kParameterCount;++i)p_[i]=kParameterSpecs[i].defaultValue;}
protected:const char*getLabel()const override{return"Drift";}const char*getDescription()const override{return"Four-lane transport-synced generative MIDI CC modulator.";}
const char*getMaker()const override{return"danja";}const char*getHomePage()const override{return"https://danja.github.io/downspout/";}const char*getLicense()const override{return"MIT";}
std::uint32_t getVersion()const override{return d_version(0,1,0);}std::int64_t getUniqueId()const override{return d_cconst('D','r','f','t');}
void initParameter(std::uint32_t i,Parameter&q)override{auto&s=kParameterSpecs[i];q.name=s.name;q.symbol=s.symbol;q.hints=s.output?kParameterIsOutput:kParameterIsAutomatable;if(s.integer)q.hints|=kParameterIsInteger;q.ranges={s.defaultValue,s.minimum,s.maximum};}
float getParameterValue(std::uint32_t i)const override{return i==kStatusEvents?static_cast<float>(state_.statusEvents):p_[i];}
void setParameterValue(std::uint32_t i,float v)override{if(i<kParameterCount&&!kParameterSpecs[i].output)p_[i]=downspout::generative::clampParam(v,kParameterSpecs[i]);}
void activate()override{reset(state_);}void run(const float**in,float**out,std::uint32_t n)override{double sum=0;for(std::uint32_t i=0;i<n;++i){out[0][i]=in[0][i];out[1][i]=in[1][i];sum+=0.5*(std::fabs(in[0][i])+std::fabs(in[1][i]));}
auto b=process(state_,p_,ct(getTimePosition()),n,getSampleRate(),static_cast<float>(sum/std::max(1u,n)));for(std::uint32_t i=0;i<b.count;++i){MidiEvent e{};e.frame=b.events[i].frame;e.size=3;e.data[0]=b.events[i].data[0];e.data[1]=b.events[i].data[1];e.data[2]=b.events[i].data[2];writeMidiEvent(e);}}
private:std::array<float,kParameterCount>p_{};downspout::drift::State state_{};DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DriftPlugin)};Plugin*createPlugin(){return new DriftPlugin();}END_NAMESPACE_DISTRHO
