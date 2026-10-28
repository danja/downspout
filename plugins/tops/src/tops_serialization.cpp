#include "tops_serialization.hpp"
#include <sstream>
namespace tops {
std::string serialize(const Params& p){ std::ostringstream ss; ss<<p.instrument<<" "<<p.engine<<" "<<p.preset<<" "<<p.pitch<<" "<<p.decay<<" "<<p.tone<<" "<<p.noise<<" "<<p.brightness<<" "<<p.volume<<" "<<p.distortion<<" "<<p.noteMap; return ss.str(); }
static float clampf(float x,float lo,float hi){ return x<lo?lo:(x>hi?hi:x); }
bool deserialize(const std::string& s, Params& p){
    std::istringstream ss(s); Params q;
    ss>>q.instrument>>q.engine>>q.preset>>q.pitch>>q.decay>>q.tone>>q.noise>>q.brightness>>q.volume>>q.distortion;
    if(ss.fail()) return false;
    ss>>q.noteMap; if(ss.fail()) q.noteMap=1;  // older strings have no note map
    if(q.instrument<0||q.instrument>9||q.engine<0||q.engine>5||q.preset<0||q.preset>4||q.noteMap<0||q.noteMap>1) return false;
    q.pitch=clampf(q.pitch,1000,12000); q.decay=clampf(q.decay,0.01f,2); q.tone=clampf(q.tone,0,1); q.noise=clampf(q.noise,0,1);
    q.brightness=clampf(q.brightness,0,1); q.volume=clampf(q.volume,0,1); q.distortion=clampf(q.distortion,0,1);
    p=q; return true;
}
}