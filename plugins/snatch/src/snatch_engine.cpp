#include "snatch_core_types.hpp"
#include <cmath>
#include <cstdlib>
#include <algorithm>
namespace snatch {
static float clampf(float x,float lo,float hi){ if(x<lo)return lo; if(x>hi)return hi; return x; }
void SnatchEngine::applyPreset() {
    switch((Preset)params_.preset){
        case Preset::Rock: params_.engine=0; params_.pitch=200; params_.noise=0.7f; params_.snap=0.7f; params_.decay=0.25f; break;
        case Preset::Jazz: params_.engine=2; params_.pitch=180; params_.noise=0.5f; params_.snap=0.4f; params_.decay=0.3f; break;
        case Preset::Electronic: params_.engine=3; params_.pitch=220; params_.noise=0.8f; params_.snap=0.8f; params_.decay=0.15f; break;
        case Preset::Latin: params_.engine=1; params_.pitch=210; params_.noise=0.6f; params_.snap=0.5f; params_.decay=0.2f; break;
        case Preset::Brushed: params_.engine=2; params_.pitch=170; params_.noise=0.4f; params_.snap=0.2f; params_.decay=0.4f; break;
        default: break;
    }
}
int SnatchEngine::engineForNote(int note, int selected) {
    switch(note){
        case 37: return (int)Engine::SideStick;
        case 38: return selected;
        case 40: return (int)Engine::Electronic;
        default: return -1;
    }
}
void SnatchEngine::trigger(int engine, float velocity) {
    v_.velocity=velocity<0?0:(velocity>1?1:velocity);
    v_.engine=engine>=0?engine:params_.engine;
    // A side stick is a short click whatever the Decay control says.
    v_.decay=v_.engine==(int)Engine::SideStick?std::min(params_.decay,0.05f):params_.decay;
    v_.active=true; v_.phase=0; v_.env=1; v_.noiseEnv=1; v_.samplesLeft=v_.decay*(float)sr_;
}
float SnatchEngine::renderSample() {
    if(!v_.active) return 0;
    float total=v_.decay*(float)sr_+1;
    float t=1.0f-(v_.samplesLeft/total); t=clampf(t,0,1);
    float env=std::exp(-t*8.0f/std::max(0.05f,v_.decay));
    v_.env=env; v_.noiseEnv*=0.99f;
    float sample=0;
    int eng=v_.engine;
    v_.phase += 2.0f*(float)M_PI*params_.pitch/(float)sr_;
    if(v_.phase>2*M_PI) v_.phase-=2*M_PI;
    if(eng==0){ sample=std::sin(v_.phase)*params_.tone + ((noise())*params_.noise*env); }
    else if(eng==1){ sample=std::sin(v_.phase*1.5f)*0.5f + ((noise())*params_.noise); }
    else if(eng==2){ sample=((noise())*0.3f + std::sin(v_.phase)*0.2f)*params_.noise; }
    else if(eng==3){ float mod=std::sin(v_.phase*3.0f)*2.0f; sample=std::sin(v_.phase+mod); }
    else if(eng==5){ sample=std::sin(v_.phase*3.7f)*0.9f + ((noise())*0.15f); }  // wooden rim click
    else { sample=std::sin(v_.phase)*0.6f + ((noise())*0.4f); }
    // snap
    if(v_.noiseEnv>0.01f) sample += ((noise()))*v_.noiseEnv*params_.snap*(1-t);
    sample*=params_.body;
    if(params_.distortion>0){ float g=1+params_.distortion*10; sample=std::tanh(sample*g); }
    sample*=env*params_.volume*v_.velocity;
    v_.samplesLeft-=1;
    if(v_.samplesLeft<=0) v_.active=false;
    return sample;
}
void SnatchEngine::render(float* L,float* R,int n){
    for(int i=0;i<n;i++){ float s=renderSample(); L[i]+=s; R[i]+=s; }
}
} // namespace snatch