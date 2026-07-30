#include "mosaic_core.hpp"
#include <cassert>
#include <cmath>
using namespace downspout::mosaic;
int main(){std::array<float,kParameterCount>p{};for(std::size_t i=0;i<p.size();++i)p[i]=kParameterSpecs[i].defaultValue;Pool pool;pool.samples[0].channels=1;pool.samples[0].sampleRate=48000;pool.samples[0].data.resize(4800);for(std::size_t i=0;i<pool.samples[0].data.size();++i)pool.samples[0].data[i]=std::sin(i*0.04f)*0.5f;MidiEvent e;e.size=3;e.data={0x90,60,100,0};std::array<float,1024>l{},r{};State a,b;process(a,p,{},l.size(),48000,&pool,&e,1,l.data(),r.data());auto first=l;process(b,p,{},l.size(),48000,&pool,&e,1,l.data(),r.data());assert(first==l&&a.statusVoices<=kVoiceCount);for(float v:l)assert(std::isfinite(v)&&std::fabs(v)<=1);State missing;process(missing,p,{},l.size(),48000,nullptr,&e,1,l.data(),r.data());assert(missing.statusMissing);for(float v:l)assert(v==0);return 0;}
