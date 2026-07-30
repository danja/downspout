#include "guardian_core.hpp"
#include <cassert>
#include <limits>
using namespace downspout::guardian;
int main(){std::array<float,kParameterCount>p{};for(std::size_t i=0;i<p.size();++i)p[i]=kParameterSpecs[i].defaultValue;assert(lookaheadFrames(p,48000)==240);State s;prepare(s,48000);std::array<float,4096>in{},l{},r{};in.fill(3);process(s,p,in.size(),48000,in.data(),in.data(),l.data(),r.data());assert(s.overload&&s.reductionDb>0);for(float v:l)assert(std::isfinite(v)&&std::fabs(v)<=1);in.fill(0);in[0]=std::numeric_limits<float>::quiet_NaN();process(s,p,in.size(),48000,in.data(),in.data(),l.data(),r.data());assert(s.faults>0);for(float v:l)assert(std::isfinite(v));p[kReset]=1;process(s,p,in.size(),48000,in.data(),in.data(),l.data(),r.data());assert(s.faults<=1&&!s.overload);in.fill(0.25f);for(int i=0;i<30;++i)process(s,p,in.size(),48000,in.data(),in.data(),l.data(),r.data());assert(std::fabs(l.back())<0.01f);return 0;}
