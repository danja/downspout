#include "generative_panel_ui.hpp"
#include "lightverb_core.hpp"
#include <cmath>
#include <cstdio>
START_NAMESPACE_DISTRHO
class LightverbUI final:public GenerativePanelUI{
public:LightverbUI():GenerativePanelUI("Lightverb","Fast stereo space for Transmission · fixed-cost FDN · CC 32 mix / CC 33 space",downspout::lightverb::kParameterSpecs.data(),downspout::lightverb::kParameterCount,231,184,92){}
private:
 void onNanoDisplay()override{using namespace downspout::lightverb;beginPanel();
  drawSection(24,102,1032,66,"SIGNAL FLOW","zero latency · stereo in / stereo out · use as insert or 100% wet send");drawReadout(38,120,1004,40,"RECOMMENDED MASTER CHAIN","T-Mix  →  Loopdelay  →  Lightverb  →  Guardian");
  drawSection(24,184,612,204,"SPACE","four delay lines; the same small workload at every setting");drawPercentSlider(kSpace,38,218,282,"Space","Changes the virtual room scale without adding processing cost.");drawSlider(kDecaySeconds,334,218,288,"Decay"," s","Time for the reverberant tail to fall by roughly 60 dB.",2);drawPercentSlider(kDamping,38,280,282,"Damping","Absorbs high frequencies in the tail; higher values sound darker.");drawSlider(kPreDelayMs,334,280,288,"Pre-delay"," ms","Separates the dry attack from the reverb onset.",0);
  drawSection(652,184,404,204,"BLEND","insert-friendly defaults; set Wet mix to 100% on a send");drawPercentSlider(kWidth,666,218,180,"Stereo width","Narrows or spreads the reverberant field.");drawPercentSlider(kMix,860,218,182,"Wet mix","Dry at 0%; effect-only at 100% for an auxiliary send.");drawSlider(kOutputDb,666,280,376,"Output trim"," dB","Level-match Lightverb with the next effect, normally Guardian.",1);
  drawSection(24,404,612,204,"PRODUCER CONTROL BUS","CC 19 lifecycle · MIDI passes through to the next effect");drawToggle(kMidiEnabled,38,438,176,"Accept MIDI control","Listen for the fixed Lightverb controller contract.");drawSlider(kControlChannel,226,435,190,"Control channel · 0 = Omni","","0 is Omni; 1–16 isolates this chain from other producer buses.",0);drawToggle(kRequireProducerGate,428,438,194,"Require CC 19 gate","Ignore CC 32/33 until the producer acquires this bus with CC 19.");drawReadout(38,492,282,46,"CC 32 · MIX","0–100% wet return");drawReadout(334,492,288,46,"CC 33 · SPACE","small–large space");drawLamp(38,554,136,"BUS ACTIVE",value(kStatusProducerActive)>=.5f);drawLamp(186,554,136,"MIX TAKEN",value(kStatusMixMidi)>=.5f);drawLamp(334,554,136,"SPACE TAKEN",value(kStatusSpaceMidi)>=.5f);drawAction(0,482,554,140,37,"RELEASE MIDI","Return Mix and Space to their saved panel values.");
  drawSection(652,404,404,204,"LIVE","effective values from the audio engine");char mix[20]{},space[20]{};std::snprintf(mix,sizeof(mix),"%d%%",static_cast<int>(std::lround(value(kStatusMix)*100)));std::snprintf(space,sizeof(space),"%d%%",static_cast<int>(std::lround(value(kStatusSpace)*100)));drawReadout(666,438,112,48,"MIX",mix);drawReadout(790,438,120,48,"SPACE",space);drawMeter(kStatusTail,922,438,120,"Tail");drawMeter(kStatusInputPeak,666,502,180,"Input level");drawMeter(kStatusOutputPeak,860,502,182,"Output level");drawReadout(666,554,376,38,"CHAIN STATUS",value(kStatusProducerActive)>=.5f?"Producer owns controls · MIDI thru on":"Manual controls · MIDI thru on");endPanel();}
 void actionTriggered(int)override{using namespace downspout::lightverb;commitParameter(kResetMidi,1);commitParameter(kResetMidi,0);}
};
UI*createUI(){return new LightverbUI();}
END_NAMESPACE_DISTRHO
