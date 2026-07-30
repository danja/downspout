#include "DistrhoPlugin.hpp"
#include "conductor_core.hpp"
#include <algorithm>
#include <array>
START_NAMESPACE_DISTRHO
namespace {
using namespace downspout::conductor;
Transport coreTransport(const TimePosition& time) {
    Transport t; t.valid=time.bbt.valid; t.playing=time.playing;
    if(t.valid){t.bar=time.bbt.bar-1;t.barBeat=time.bbt.beat-1+(time.bbt.ticksPerBeat>0?time.bbt.tick/time.bbt.ticksPerBeat:0);
    t.beatsPerBar=time.bbt.beatsPerBar;t.beatType=time.bbt.beatType;t.bpm=time.bbt.beatsPerMinute;} return t;
}}
class ConductorPlugin : public Plugin {
public: ConductorPlugin():Plugin(kParameterCount,0,0){for(std::uint32_t i=0;i<kParameterCount;++i)p_[i]=kParameterSpecs[i].defaultValue;}
protected:
const char* getLabel()const override{return "Conductor";} const char* getDescription()const override{return "Long-form section and scene command generator.";}
const char* getMaker()const override{return "danja";} const char* getHomePage()const override{return "https://danja.github.io/downspout/";}
const char* getLicense()const override{return "MIT";} std::uint32_t getVersion()const override{return d_version(0,1,0);}
std::int64_t getUniqueId()const override{return d_cconst('C','n','d','c');}
void initParameter(std::uint32_t i,Parameter& q)override{const auto&s=kParameterSpecs[i];q.name=s.name;q.symbol=s.symbol;
q.hints=s.output?kParameterIsOutput:kParameterIsAutomatable;if(s.integer)q.hints|=kParameterIsInteger;q.ranges={s.defaultValue,s.minimum,s.maximum};}
float getParameterValue(std::uint32_t i)const override{if(i==kStatusSection)return static_cast<float>(std::max(0,state_.section));
if(i==kStatusBarsLeft)return static_cast<float>(std::max(0,state_.nextSectionBar-state_.sectionStartBar));return p_[i];}
void setParameterValue(std::uint32_t i,float v)override{if(i<kParameterCount&&!kParameterSpecs[i].output)p_[i]=downspout::generative::clampParam(v,kParameterSpecs[i]);}
void activate()override{reset(state_);}
void run(const float**,float** out,std::uint32_t frames)override{std::fill_n(out[0],frames,0.0f);std::fill_n(out[1],frames,0.0f);
auto block=process(state_,p_,coreTransport(getTimePosition()),frames,getSampleRate());for(std::uint32_t i=0;i<block.count;++i){MidiEvent e{};
e.frame=block.events[i].frame;e.size=3;e.data[0]=block.events[i].data[0];e.data[1]=block.events[i].data[1];e.data[2]=block.events[i].data[2];writeMidiEvent(e);}}
private:std::array<float,kParameterCount>p_{};downspout::conductor::State state_{};DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConductorPlugin)};
Plugin* createPlugin(){return new ConductorPlugin();}
END_NAMESPACE_DISTRHO
