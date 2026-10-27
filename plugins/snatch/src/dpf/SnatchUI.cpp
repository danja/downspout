#include "DistrhoUI.hpp"
START_NAMESPACE_DISTRHO
class SnatchUI : public UI {
public:
    SnatchUI() : UI(920,680){ createFontFromFile("sans","/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"); }
protected:
    void parameterChanged(uint32_t,float) override {}
    void onNanoDisplay() override {
        const float W=(float)getWidth(), H=(float)getHeight();
        beginPath(); rect(0,0,W,H); fillColor(34,34,40,255); fill(); closePath();
        beginPath(); rect(0,0,W,96); fillColor(46,46,54,255); fill(); closePath();
        fontFace("sans"); fillColor(240,240,240,255); fontSize(30); textAlign(ALIGN_LEFT|ALIGN_TOP); text(24,20,"Snatch",nullptr);
        fontSize(14); fillColor(180,180,190,255); text(24,56,"Snare Drum Generator",nullptr);
    }
};
UI* createUI(){return new SnatchUI();}
END_NAMESPACE_DISTRHO