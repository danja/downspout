#include "resonance_garden_core.hpp"
#include <cassert>
#include <limits>
using namespace downspout::resonance_garden;
int main(){std::array<float,kParameterCount>p{};for(std::size_t i=0;i<p.size();++i)p[i]=kParameterSpecs[i].defaultValue;State s;prepare(s,48000);reset(s);std::array<float,4096>in{},l{},r{};process(s,p,in.size(),48000,in.data(),in.data(),l.data(),r.data(),nullptr,0);for(float v:l)assert(std::isfinite(v)&&std::fabs(v)<1e-6);in[0]=1;process(s,p,in.size(),48000,in.data(),in.data(),l.data(),r.data(),nullptr,0);bool tail=false;for(float v:l){assert(std::isfinite(v)&&std::fabs(v)<=1);tail|=std::fabs(v)>1e-5;}assert(tail);MidiEvent e;e.size=3;e.data={0x90,72,100,0};process(s,p,512,48000,in.data(),in.data(),l.data(),r.data(),&e,1);assert(s.statusVoices<=kMaxVoices);in[0]=std::numeric_limits<float>::infinity();process(s,p,512,48000,in.data(),in.data(),l.data(),r.data(),nullptr,0);for(int i=0;i<512;++i)assert(std::isfinite(l[i]));return 0;}
