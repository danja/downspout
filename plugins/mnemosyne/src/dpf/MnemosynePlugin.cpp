#include "DistrhoPlugin.hpp"
#include "mnemosyne_core.hpp"
#include <algorithm>
#include <array>
#include <cstring>
START_NAMESPACE_DISTRHO
namespace{using namespace downspout::mnemosyne;Transport ct(const TimePosition&x){Transport t;t.valid=x.bbt.valid;t.playing=x.playing;if(t.valid){t.bar=x.bbt.bar-1;t.barBeat=x.bbt.beat-1+(x.bbt.ticksPerBeat>0?x.bbt.tick/x.bbt.ticksPerBeat:0);t.beatsPerBar=x.bbt.beatsPerBar;t.beatType=x.bbt.beatType;t.bpm=x.bbt.beatsPerMinute;}return t;}}
class MnemosynePlugin:public Plugin{public:MnemosynePlugin():Plugin(kParameterCount,0,1){for(std::uint32_t i=0;i<kParameterCount;++i)p_[i]=kParameterSpecs[i].defaultValue;resetRuntime(s_);}
protected:const char*getLabel()const override{return"Mnemosyne";}const char*getDescription()const override{return"Fixed-reservoir motif capture and recombination processor.";}const char*getMaker()const override{return"danja";}const char*getHomePage()const override{return"https://danja.github.io/downspout/";}const char*getLicense()const override{return"MIT";}std::uint32_t getVersion()const override{return d_version(0,1,0);}std::int64_t getUniqueId()const override{return d_cconst('M','n','s','y');}
void initParameter(std::uint32_t i,Parameter&q)override{auto&s=kParameterSpecs[i];q.name=s.name;q.symbol=s.symbol;q.hints=s.output?kParameterIsOutput:kParameterIsAutomatable;if(s.integer)q.hints|=kParameterIsInteger;q.ranges={s.defaultValue,s.minimum,s.maximum};}
void initState(std::uint32_t,State&q)override{q.key="reservoir";q.label="Motif reservoir";q.defaultValue="version=1\ncount=0\n";}
float getParameterValue(std::uint32_t i)const override{if(i==kStatusPhrases)return s_.phraseCount;if(i==kStatusEvents)return s_.capture.count;return p_[i];}
void setParameterValue(std::uint32_t i,float v)override{if(i<kParameterCount&&!kParameterSpecs[i].output)p_[i]=downspout::generative::clampParam(v,kParameterSpecs[i]);}
String getState(const char*k)const override{return std::strcmp(k,"reservoir")==0?String(serializeReservoir(s_).c_str()):String();}
void setState(const char*k,const char*v)override{if(std::strcmp(k,"reservoir")==0)deserializeReservoir(v?v:"",s_);}
void activate()override{resetRuntime(s_);}void run(const float**,float**out,std::uint32_t n,const MidiEvent*events,std::uint32_t count)override{std::fill_n(out[0],n,0);std::fill_n(out[1],n,0);std::array<downspout::generative::MidiEvent,512>in{};count=std::min<std::uint32_t>(count,in.size());for(std::uint32_t i=0;i<count;++i){in[i].frame=events[i].frame;in[i].size=static_cast<std::uint8_t>(std::min<std::uint32_t>(events[i].size,4));const auto*d=events[i].size>MidiEvent::kDataSize?events[i].dataExt:events[i].data;for(std::uint8_t b=0;b<in[i].size;++b)in[i].data[b]=d[b];}auto block=process(s_,p_,ct(getTimePosition()),n,getSampleRate(),in.data(),count);for(std::uint32_t i=0;i<block.count;++i){MidiEvent e{};e.frame=block.events[i].frame;e.size=block.events[i].size;for(std::uint8_t b=0;b<e.size;++b)e.data[b]=block.events[i].data[b];writeMidiEvent(e);}}
private:std::array<float,kParameterCount>p_{};downspout::mnemosyne::State s_{};DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MnemosynePlugin)};Plugin*createPlugin(){return new MnemosynePlugin();}END_NAMESPACE_DISTRHO
