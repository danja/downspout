#include "generative_panel_ui.hpp"
#include "worms_params.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

namespace {
using namespace downspout::worms;

// ── Item lists ────────────────────────────────────────────────────────────────

constexpr const char* kRootNames[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
constexpr const char* kRegNames[] = {
    "Oct -2", "Oct -1", "Oct 0", "Oct +1", "Oct +2"
};
constexpr const char* kMidiChNames[] = {
    "1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16"
};
constexpr const char* kCondChNames[] = {
    "Off",
    "Ch 1","Ch 2","Ch 3","Ch 4","Ch 5","Ch 6","Ch 7","Ch 8",
    "Ch 9","Ch 10","Ch 11","Ch 12","Ch 13","Ch 14","Ch 15","Ch 16"
};
constexpr const char* kScaleNames[] = {
    "Major","Ionian","Minor","Harm Minor","Mel Minor",
    "Dorian","Phrygian","Lydian","Mixolydian","Locrian",
    "Phryg Dom","Neo Major","Neo Minor",
    "Pent Major","Pent Minor","Blues",
    "Whole Tone","Altered","H-W Dim","W-H Dim",
    "Bebop Dom","Bebop Major","Bebop Minor"
};
constexpr const char* kStepSizeNames[] = {"1/4","1/8","1/16","1/32"};
constexpr const char* kPatLenNames[]   = {"16","32","64","128"};
constexpr const char* kTurnNames[]     = {"L120","L60","Fwd","R60","R120"};

// ── Selector descriptor ───────────────────────────────────────────────────────

struct SelDef {
    uint32_t          paramIdx;
    const char*       label;
    const char* const* items;
    int               count;
};

// Dropdowns: Root, Reg, MIDI Ch, Cond Ch, Scale
enum SelIdx { kSelRoot=0, kSelReg, kSelMidi, kSelCond, kSelScale, kSelCount };

constexpr SelDef kSels[kSelCount] = {
    {kParamRoot,   "Root",    kRootNames,   12},
    {kParamReg,    "Register",kRegNames,     5},
    {kParamMidiCh, "MIDI Ch", kMidiChNames, 16},
    {kParamCondCh, "Cond. Ch",kCondChNames, 17},
    {kParamScale,  "Scale",   kScaleNames,  23},
};

// ── Small rect helper ─────────────────────────────────────────────────────────

struct Rect {
    float x=0,y=0,w=0,h=0;
    bool contains(float px, float py) const noexcept {
        return px>=x && px<=x+w && py>=y && py<=y+h;
    }
};

inline int clampi(int v, int lo, int hi) noexcept {
    return v<lo ? lo : v>hi ? hi : v;
}

}  // namespace

// ── WormsUI ───────────────────────────────────────────────────────────────────

class WormsUI : public GenerativePanelUI
{
public:
    WormsUI()
        : GenerativePanelUI(
            "ToneWorm",
            "Paterson\xe2\x80\x99s Worm navigates a Tonnetz lattice of pitch classes",
            downspout::worms::kParamSpecs.data(),
            downspout::worms::kParameterCount,
            78, 180, 130)
    {}

private:
    // ── State ─────────────────────────────────────────────────────────────────
    Rect selRects_[kSelCount] {};
    int  openSel_ = -1;

    static constexpr float kItemH   = 24.0f;
    static constexpr int   kMaxRows = 12;

    // ── Layout helpers ────────────────────────────────────────────────────────

    int   menuCols(int si) const { return std::max(1,(kSels[si].count+kMaxRows-1)/kMaxRows); }
    int   menuRows(int si) const { int c=menuCols(si); return (kSels[si].count+c-1)/c; }

    Rect menuRect(int si) const {
        const Rect& base = selRects_[si];
        const int   cols = menuCols(si);
        const int   rows = menuRows(si);
        const float itemW = std::max(base.w, 130.0f);
        const float mw    = itemW * static_cast<float>(cols);
        const float mh    = static_cast<float>(rows) * kItemH;
        const float ww    = static_cast<float>(getWidth());
        const float wh    = static_cast<float>(getHeight());
        const float mg    = 10.0f;
        float mx = std::clamp(base.x, mg, std::max(mg, ww-mg-mw));
        float my = base.y + base.h + 4.0f;
        if (my + mh > wh - mg)
            my = base.y - mh - 4.0f;
        my = std::clamp(my, mg, std::max(mg, wh-mg-mh));
        return {mx, my, mw, mh};
    }

    // ── Custom selector drawing ───────────────────────────────────────────────

    void drawSelector(int si, float x, float y, float w, float h)
    {
        selRects_[si] = {x, y, w, h};
        const SelDef& def  = kSels[si];
        const int     cur  = clampi(static_cast<int>(std::lround(value(def.paramIdx))), 0, def.count-1);
        const bool    open = (openSel_ == si);

        beginPath();
        fillColor(30, 37, 45, 255);
        roundedRect(x, y, w, h, 8.0f);
        fill();
        if (open) {
            beginPath();
            strokeColor(78, 180, 130, 200);
            strokeWidth(1.5f);
            roundedRect(x+0.75f, y+0.75f, w-1.5f, h-1.5f, 8.0f);
            stroke();
        }

        fillColor(140, 152, 163, 255);
        fontSize(10.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x+10.0f, y+7.0f, def.label, nullptr);

        fillColor(228, 234, 230, 255);
        fontSize(13.5f);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        text(x+10.0f, y+h-8.0f, def.items[cur], nullptr);

        fillColor(110, 124, 136, 255);
        fontSize(14.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(x+w-10.0f, y+h*0.5f+1.0f, open ? "\xe2\x86\x91" : "\xe2\x86\x93", nullptr);
    }

    void drawOpenMenu(int si)
    {
        const SelDef& def   = kSels[si];
        const int     cur   = clampi(static_cast<int>(std::lround(value(def.paramIdx))), 0, def.count-1);
        const Rect    mr    = menuRect(si);
        const int     cols  = menuCols(si);
        const int     rows  = menuRows(si);
        const float   itemW = mr.w / static_cast<float>(cols);

        beginPath();
        fillColor(22, 28, 37, 250);
        roundedRect(mr.x, mr.y, mr.w, mr.h, 10.0f);
        fill();
        beginPath();
        strokeColor(88, 104, 122, 220);
        strokeWidth(1.0f);
        roundedRect(mr.x+0.5f, mr.y+0.5f, mr.w-1.0f, mr.h-1.0f, 10.0f);
        stroke();

        for (int i = 0; i < def.count; ++i) {
            const int   col  = i / rows;
            const int   row  = i % rows;
            const float ix   = mr.x + static_cast<float>(col) * itemW;
            const float iy   = mr.y + static_cast<float>(row) * kItemH;

            if (i == cur) {
                beginPath();
                fillColor(52, 130, 94, 255);
                roundedRect(ix+3.0f, iy+3.0f, itemW-6.0f, kItemH-6.0f, 6.0f);
                fill();
            }
            fontSize(cols > 1 ? 10.5f : 12.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(232, 238, 234, 255);
            text(ix+11.0f, iy+kItemH*0.5f+1.0f, def.items[i], nullptr);
        }
    }

    // ── Rule row helper (label on left, 5-button grid on right) ───────────────
    // Uses GenerativePanelUI::drawChoice which already registers hit boxes.

    static const char* ruleLabel(int d) {
        static const char* const kL[] = {
            "E \xe2\x86\x92", "NE \xe2\x86\x92", "NW \xe2\x86\x92",
            "W \xe2\x86\x92", "SW \xe2\x86\x92", "SE \xe2\x86\x92",
        };
        return kL[d];
    }

    // ── Display ───────────────────────────────────────────────────────────────

    void onNanoDisplay() override
    {
        const float W = static_cast<float>(getWidth());
        beginPanel();

        // ── Navigation ──────────────────────────────────────────────────
        // Dropdowns for Root, Reg, Midi Ch, Cond Ch; choices for Step, Length
        const float navY = 100.0f;
        const float selH = 48.0f;
        const float cpad = 30.0f;   // section title → first control row
        drawSection(24, navY, W-48, cpad + selH + 10.0f, "NAVIGATION");

        // Row 1: Root, Reg, Step, Length
        const float r1y = navY + cpad;
        drawSelector(kSelRoot, 38,  r1y, 130, selH);
        drawSelector(kSelReg,  176, r1y, 130, selH);
        drawChoice(kParamStepSize, 314, r1y, 160, "Step", kStepSizeNames, 4,
                   "Note grid resolution.");
        drawChoice(kParamPatLen,   482, r1y, 130, "Length", kPatLenNames, 4,
                   "Pattern steps before looping.");
        drawSelector(kSelMidi,  620, r1y, 110, selH);
        drawSelector(kSelCond,  738, r1y, 124, selH);

        // ── Worm Rules (2 columns of 3) ─────────────────────────────────
        const float rulesY   = navY + cpad + selH + 18.0f;
        const float ruleRowH = 53.0f;
        const float ruleRows = 3.0f;
        const float actH     = 38.0f;
        const float ruleSecH = cpad + ruleRows * ruleRowH + actH + 8.0f;
        drawSection(24, rulesY, W-48, ruleSecH, "WORM RULES",
                    "Given incoming direction, choose turn: L120 / L60 / Fwd / R60 / R120");

        const float colW = (W - 76.0f) * 0.5f - 4.0f;
        for (int d = 0; d < 6; ++d) {
            const int   col = d / 3;
            const int   row = d % 3;
            const float rx  = 38.0f + static_cast<float>(col) * (colW + 8.0f);
            const float ry  = rulesY + cpad + static_cast<float>(row) * ruleRowH;
            drawChoice(static_cast<uint32_t>(kParamRule0 + d),
                       rx, ry, colW, ruleLabel(d), kTurnNames, 5,
                       "Turn rule for this incoming direction.");
        }

        // Action buttons below rules
        const float actY = rulesY + cpad + ruleRows * ruleRowH + 8.0f;
        drawAction(kParamActionRandomize, 38,  actY, 140, 30,
                   "Randomize", "Randomize all six worm rules.");
        drawAction(kParamActionMutate,    186, actY, 140, 30,
                   "Mutate",    "Mutate one random worm rule.");

        // ── Generation + Quantize ───────────────────────────────────────
        const float genY  = rulesY + ruleSecH + 8.0f;
        const float genH  = cpad + selH + 8.0f;
        const float genW  = W - 48.0f - (value(kParamQuantize) >= 0.5f ? 230.0f : 160.0f);
        drawSection(24, genY, genW, genH, "GENERATION");

        const float ctrlY = genY + cpad;
        const float ctrlW = (genW - 28.0f) * 0.25f;
        drawPercentSlider(kParamDensity,  38,                  ctrlY, ctrlW, "Density",
                          "Note vs rest probability at each step.");
        drawPercentSlider(kParamVelocity, 38 + ctrlW + 4,      ctrlY, ctrlW, "Velocity",
                          "Base MIDI velocity.");
        drawPercentSlider(kParamVary,     38 + (ctrlW+4)*2.0f, ctrlY, ctrlW, "Vary",
                          "Mutation rate at loop boundaries.");
        drawSlider(kParamSeed,            38 + (ctrlW+4)*3.0f, ctrlY, ctrlW, "Seed",
                   nullptr, "RNG seed (0 = deterministic base).");

        // Quantize section on the right
        const float qx = 24.0f + genW + 8.0f;
        const float qw = W - qx - 24.0f;
        drawSection(qx, genY, qw, genH, "QUANTIZE");
        drawToggle(kParamQuantize, qx+8, ctrlY, qw-16, "Scale quantize",
                   "Snap Tonnetz pitch classes to the selected scale.");
        if (value(kParamQuantize) >= 0.5f) {
            const float sy = ctrlY + 32.0f;
            drawSelector(kSelScale, qx+8, sy, qw-16, selH - 10.0f);
        }

        endPanel();

        // Draw open dropdown on top of everything (after endPanel)
        if (openSel_ >= 0)
            drawOpenMenu(openSel_);
    }

    // ── Input ─────────────────────────────────────────────────────────────────

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());

        if (!ev.press) return false;

        // Close / select from open menu
        if (openSel_ >= 0) {
            const Rect& base = selRects_[openSel_];
            if (base.contains(mx, my)) {
                openSel_ = -1;
                repaint();
                return true;
            }
            const Rect mr = menuRect(openSel_);
            if (mr.contains(mx, my)) {
                const SelDef& def  = kSels[openSel_];
                const int     cols = menuCols(openSel_);
                const int     rows = menuRows(openSel_);
                const float   itemW = mr.w / static_cast<float>(cols);
                const int     col  = clampi(static_cast<int>((mx-mr.x)/itemW), 0, cols-1);
                const int     row  = clampi(static_cast<int>((my-mr.y)/kItemH), 0, rows-1);
                const int     item = col*rows + row;
                if (item < def.count)
                    commitParameter(def.paramIdx, static_cast<float>(item));
                openSel_ = -1;
                repaint();
                return true;
            }
            openSel_ = -1;
            repaint();
        }

        // Open a selector
        for (int i = 0; i < kSelCount; ++i) {
            if (i == kSelScale && value(kParamQuantize) < 0.5f) continue;
            if (selRects_[i].contains(mx, my)) {
                openSel_ = i;
                repaint();
                return true;
            }
        }

        return handleMouse(ev);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WormsUI)
};

UI* createUI() { return new WormsUI(); }

END_NAMESPACE_DISTRHO
