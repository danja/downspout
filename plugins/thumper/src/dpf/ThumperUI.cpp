#include "DistrhoUI.hpp"

START_NAMESPACE_DISTRHO

class ThumperUI : public UI {
public:
    ThumperUI() : UI(920, 680) {
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    }
protected:
    void parameterChanged(uint32_t, float) override {}
    void onNanoDisplay() override {
        const float W = (float)getWidth();
        const float H = (float)getHeight();
        // background
        beginPath();
        rect(0.0f, 0.0f, W, H);
        fillColor(34, 34, 40, 255);
        fill();
        closePath();
        // header panel
        beginPath();
        rect(0.0f, 0.0f, W, 96.0f);
        fillColor(46, 46, 54, 255);
        fill();
        closePath();
        fontFace("sans");
        fillColor(240, 240, 240, 255);
        fontSize(30.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(24.0f, 20.0f, "Thumper", nullptr);
        fontSize(14.0f);
        fillColor(180, 180, 190, 255);
        text(24.0f, 56.0f, "Kick Drum Generator", nullptr);
        // footer note
        fontSize(11.0f);
        fillColor(120, 120, 130, 255);
        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        text(W*0.5f, H-12.0f, "Downspout", nullptr);
    }
};

UI* createUI() { return new ThumperUI(); }
END_NAMESPACE_DISTRHO