#include "downspout/test_assert.h"
#include "snatch_core_types.hpp"
#include "snatch_serialization.hpp"
#include <cmath>
#include <cstring>
int main(){
    snatch::SnatchEngine e; e.setSampleRate(44100);
    snatch::Params p; e.setParams(p); e.trigger();
    float L[64],R[64]; std::memset(L,0,sizeof(L)); std::memset(R,0,sizeof(R));
    e.render(L,R,64);
    float sum=0; for(int i=0;i<64;i++) sum+=std::fabs(L[i]);
    assert(sum>0.0f);
    std::string s=snatch::serialize(p); snatch::Params p2; assert(snatch::deserialize(s,p2));
    return 0;
}