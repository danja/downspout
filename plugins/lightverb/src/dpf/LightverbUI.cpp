#include "generative_panel_ui.hpp"
#include "lightverb_core.hpp"
#include <cmath>
#include <cstdio>
START_NAMESPACE_DISTRHO
class LightverbUI final:public GenerativePanelUI{
public:LightverbUI():GenerativePanelUI("Lightverb","Fast stereo space for Transmission · fixed-cost FDN · CC 32 mix / CC 33 space",downspout::lightverb::kParameterSpecs.data(),downspout::lightverb::kParameterCount,231,184,92){}
private:
 void onNanoDisplay()override{using namespace downspout::lightverb;beginPanel();
  drawSection(24,102,612,204,"SPACE","four delay lines; the same small workload at every setting");drawPercentSlider(kSpace,38,136,282,"Space","Changes the virtual room scale without adding processing cost.");drawSlider(kDecaySeconds,334,136,288,"Decay"," s","Time for the reverberant tail to fall by roughly 60 dB.",2);drawPercentSlider(kDamping,38,198,282,"Damping","Absorbs high frequencies in the tail; higher values sound darker.");drawSlider(kPreDelayMs,334,198,288,"Pre-delay"," ms","Separates the dry attack from the reverb onset.",0);
  drawSection(652,102,404,204,"BLEND","insert-friendly defaults; set Wet mix to 100% on a send");drawPercentSlider(kWidth,666,136,180,"Stereo width","Narrows or spreads the reverberant field.");drawPercentSlider(kMix,860,136,182,"Wet mix","Dry at 0%; effect-only at 100% for an auxiliary send.");drawSlider(kOutputDb,666,198,376,"Output trim"," dB","Level-match Lightverb with the next effect, normally Guardian.",1);
  drawSection(24,322,612,204,"PRODUCER CONTROL BUS","CC 19 lifecycle · MIDI passes through to the next effect");drawToggle(kMidiEnabled,38,356,176,"Accept MIDI control","Listen for the fixed Lightverb controller contract.");drawSlider(kControlChannel,226,353,190,"Control channel · 0 = Omni","","0 is Omni; 1–16 isolates this chain from other producer buses.",0);drawToggle(kRequireProducerGate,428,356,194,"Require CC 19 gate","Ignore CC 32/33 until the producer acquires this bus with CC 19.");drawReadout(38,410,282,46,"CC 32 · MIX","0–100% wet return");drawReadout(334,410,288,46,"CC 33 · SPACE","small–large space");drawLamp(38,472,136,"BUS ACTIVE",value(kStatusProducerActive)>=.5f);drawLamp(186,472,136,"MIX TAKEN",value(kStatusMixMidi)>=.5f);drawLamp(334,472,136,"SPACE TAKEN",value(kStatusSpaceMidi)>=.5f);drawAction(0,482,472,140,37,"RELEASE MIDI","Return Mix and Space to their saved panel values.");
  drawSection(652,322,404,204,"LIVE","effective values from the audio engine");char mix[20]{},space[20]{};std::snprintf(mix,sizeof(mix),"%d%%",static_cast<int>(std::lround(value(kStatusMix)*100)));std::snprintf(space,sizeof(space),"%d%%",static_cast<int>(std::lround(value(kStatusSpace)*100)));drawReadout(666,356,112,48,"MIX",mix);drawReadout(790,356,120,48,"SPACE",space);drawMeter(kStatusTail,922,356,120,"Tail");drawMeter(kStatusInputPeak,666,420,180,"Input level");drawMeter(kStatusOutputPeak,860,420,182,"Output level");drawReadout(666,472,376,38,"CHAIN STATUS",value(kStatusProducerActive)>=.5f?"Producer owns controls · MIDI thru on":"Manual controls · MIDI thru on");
  drawSection(24,542,1032,78,"PRESETS","starting points \xe2\x80\x94 adjust from here");
  drawAction(1, 38,568,190,34,"Room","Small warm room (Space 30%, Decay 0.8s).");
  drawAction(2,234,568,190,34,"Hall","Large concert hall (Space 70%, Decay 3.0s).");
  drawAction(3,430,568,190,34,"Plate","Plate reverb (Space 50%, Decay 1.5s, full width).");
  drawAction(4,626,568,190,34,"Shimmer","Long airy tail (Space 40%, Decay 5.0s, low damping).");
  drawAction(5,822,568,190,34,"Spring","Tight spring (Space 30%, Decay 0.5s, high damping).");
  endPanel();}
 void actionTriggered(int id)override{
  using namespace downspout::lightverb;
  if(id==0){commitParameter(kResetMidi,1);commitParameter(kResetMidi,0);return;}
  static constexpr float kPresets[5][7]={
   {0.30f,0.8f,0.60f, 8.f,0.70f,0.25f,0.f},
   {0.70f,3.0f,0.30f,25.f,0.90f,0.30f,0.f},
   {0.50f,1.5f,0.70f, 5.f,1.00f,0.25f,0.f},
   {0.40f,5.0f,0.10f,30.f,0.85f,0.20f,0.f},
   {0.30f,0.5f,0.85f,12.f,0.60f,0.30f,0.f},
  };
  const int p=id-1;
  if(p>=0&&p<5){
   commitParameter(kSpace,       kPresets[p][0]);
   commitParameter(kDecaySeconds,kPresets[p][1]);
   commitParameter(kDamping,     kPresets[p][2]);
   commitParameter(kPreDelayMs,  kPresets[p][3]);
   commitParameter(kWidth,       kPresets[p][4]);
   commitParameter(kMix,         kPresets[p][5]);
   commitParameter(kOutputDb,    kPresets[p][6]);
  }}
};
UI*createUI(){return new LightverbUI();}
END_NAMESPACE_DISTRHO
