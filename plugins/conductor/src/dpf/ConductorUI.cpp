#include "generative_panel_ui.hpp"
#include "downspout/look_and_feel.hpp"
#include "conductor_core.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;
namespace protocol = downspout::conductor_protocol;

class ConductorUI : public GenerativePanelUI {
public:
    ConductorUI()
        : GenerativePanelUI(
            "Conductor",
            "Edit up to eight blocks; scene commands leave graph routing to the host",
            downspout::conductor::kParameterSpecs.data(),
            downspout::conductor::kParameterCount, 220, 154, 78) {}

private:
    const laf::Theme& t_ { laf::defaultTheme() };
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }

    int selectedBlock_ = 0;
    int pendingBlock_ = -1;

    [[nodiscard]] int blockCount() const noexcept
    {
        return std::clamp(static_cast<int>(std::lround(value(downspout::conductor::kBlockCount))),
                          1, protocol::kMaxBlocks);
    }

    [[nodiscard]] Box blockBox(const int index) const noexcept
    {
        return {38.0f + static_cast<float>(index) * 110.0f, 140.0f, 104.0f, 58.0f};
    }

    void parameterChanged(uint32_t index, float v) override
    {
        GenerativePanelUI::parameterChanged(index, v);
        using namespace downspout::conductor;
        if (index == kBlockCount) {
            selectedBlock_ = std::clamp(selectedBlock_, 0, blockCount() - 1);
            if (pendingBlock_ >= blockCount())
                pendingBlock_ = -1;
        }
        if (index == kStatusBlock && pendingBlock_ >= 0 &&
            static_cast<int>(std::lround(v)) == pendingBlock_)
            pendingBlock_ = -1;
        repaint();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button == 1 && ev.press) {
            const float x = static_cast<float>(ev.pos.getX());
            const float y = static_cast<float>(ev.pos.getY());
            const int count = blockCount();
            for (int i = 0; i < count; ++i) {
                const Box box = blockBox(i);
                if (box.contains(x, y)) {
                    if (selectedBlock_ != i || pendingBlock_ != -1) {
                        selectedBlock_ = i;
                        repaint();
                    }
                    return true;
                }
            }
        }
        return handleMouse(ev);
    }

    void onNanoDisplay() override
    {
        using namespace downspout::conductor;
        static constexpr const char* modes[] {"Fixed form", "Weighted form"};

        beginPanel();
        drawBlockStrip();
        drawStructure(modes);
        drawMidiCommands();

        endPanel();
    }

    void drawBlockStrip()
    {
        using namespace downspout::conductor;
        const int count = blockCount();
        const int current = std::clamp(static_cast<int>(std::lround(value(kStatusBlock))), -1, 7);
        const int barsLeft = std::max(0, static_cast<int>(std::lround(value(kStatusBarsLeft))));
        const int currentType = std::clamp(static_cast<int>(std::lround(value(kStatusSection))), 0, 6);

        drawSection(24, 104, 912, 132, "FORM BLOCKS",
                    "click to edit · Jump here queues a change at the next bar");
        for (int i = 0; i < count; ++i) {
            const Box box = blockBox(i);
            const bool isCurrent = (i == current);
            const bool isPending = (i == pendingBlock_);
            const bool isSelected = (i == selectedBlock_);
            const BlockType type = protocol::fromInt(static_cast<int>(
                std::lround(value(blockTypeParam(i)))));

            beginPath();
            if (isCurrent)
                fillColor(accentR(), accentG(), accentB(), 255);
            else if (isPending)
                fillColor(200, 148, 36, 255);
            else if (isSelected)
                fillColor(56, 68, 78, 255);
            else
                fillColor(34, 41, 49, 255);
            roundedRect(box.x, box.y, box.w, box.h, 5.0f);
            fill();
            if (isSelected) {
                sc(t_.textPrimary);
                strokeWidth(1.5f);
                stroke();
            }

            fillColor(isCurrent || isPending ? 18 : 150,
                      isCurrent || isPending ? 22 : 162,
                      isCurrent || isPending ? 24 : 170, 255);
            fontSize(9.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            char number[8] {};
            std::snprintf(number, sizeof(number), "%d", i + 1);
            text(box.x + 7.0f, box.y + 5.0f, number, nullptr);

            fillColor(isCurrent || isPending ? 22 : 232,
                      isCurrent || isPending ? 26 : 238,
                      isCurrent || isPending ? 28 : 234, 255);
            fontSize(12.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(box.x + box.w * 0.5f, box.y + 34.0f, protocol::shortName(type), nullptr);

            if (isCurrent) {
                fc(t_.textPrimary);
                fontSize(8.5f);
                textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
                text(box.x + box.w * 0.5f, box.y + box.h - 3.0f, "NOW", nullptr);
            } else if (isPending) {
                fc(t_.textPrimary);
                fontSize(8.5f);
                textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
                text(box.x + box.w * 0.5f, box.y + box.h - 3.0f, "NEXT", nullptr);
            }
        }

        char status[168] {};
        if (pendingBlock_ >= 0 && pendingBlock_ < count) {
            const BlockType type = protocol::fromInt(static_cast<int>(
                std::lround(value(blockTypeParam(pendingBlock_)))));
            std::snprintf(status, sizeof(status), "Queued: block %d → %s at next bar",
                          pendingBlock_ + 1, protocol::name(type));
        } else if (current >= 0) {
            std::snprintf(status, sizeof(status), "Now: block %d · %s · %d bars remaining",
                          current + 1, protocol::name(protocol::fromInt(currentType)), barsLeft);
        } else {
            std::snprintf(status, sizeof(status), "Transport stopped · select a block to edit");
        }
        fc(t_.textDim);
        fontSize(10.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(38.0f, 206.0f, status, nullptr);
        drawAction(778, 812.0f, 204.0f, 110.0f, 27.0f, "Jump here",
                   "Queue the selected block for the next bar boundary.");
    }

    void drawStructure(const char* const* modes)
    {
        using namespace downspout::conductor;
        static constexpr const char* typeChoices[] {
            "Null", "Intro", "Theme", "Dev", "Break", "Rep", "Coda",
        };
        drawSection(24, 248, 574, 440, "STRUCTURE", "weighted form uses the block types as a palette");
        drawSlider(kBlockCount, 38, 280, 264, "Block count", "",
                   "Number of editable blocks in the repeating form.", 0);

        char typeLabel[32] {};
        std::snprintf(typeLabel, sizeof(typeLabel), "Block %d type", selectedBlock_ + 1);
        drawChoice(blockTypeParam(selectedBlock_), 310, 280, 274, typeLabel,
                   typeChoices, protocol::kBlockTypeCount,
                   "Type emitted when this block is reached (Dev = Develop, Rep = Reprise).", true);

        drawSlider(kMinBars, 38, 339, 264, "Minimum section", " bars", "Shortest block duration.", 0);
        drawSlider(kMaxBars, 310, 339, 274, "Maximum section", " bars", "Longest block duration.", 0);

        drawWeight(kNullWeight, 38, 398, 264, "Null");
        drawWeight(kIntroWeight, 38, 455, 264, "Intro");
        drawWeight(kThemeWeight, 38, 512, 264, "Theme");
        drawWeight(kDevelopWeight, 38, 569, 264, "Develop");
        drawWeight(kBreakWeight, 310, 398, 274, "Break");
        drawWeight(kRepriseWeight, 310, 455, 274, "Reprise");
        drawWeight(kCodaWeight, 310, 512, 274, "Coda");
        drawChoice(kMode, 38, 624, 546, "Form generation", modes, 2,
                   "Fixed follows the edited block order; Weighted picks from the active block types.");
    }

    void drawMidiCommands()
    {
        using namespace downspout::conductor;
        drawSection(612, 248, 324, 440, "MIDI COMMANDS", "advanced routing");
        drawNoteSlider(kBaseNote, 626, 280, 143, "Section note", "Base note used for block commands.");
        drawChannelSlider(kChannel, 777, 280, 145, "Output", "MIDI command channel.");
        drawCcSlider(kSceneCc, 626, 336, 143, "Scene", "CC carrying the current block type.");
        drawCcSlider(kDensityCc, 777, 336, 145, "Density", "CC carrying the block density.");
        drawCcSlider(kEnergyCc, 626, 392, 143, "Energy", "CC carrying the block energy.");
        drawCcSlider(kMutationCc, 777, 392, 145, "Mutation", "CC requesting generative mutation.");
        drawCcSlider(kResetCc, 626, 448, 296, "Reset", "CC requesting downstream reset.");
        drawSlider(kSeed, 626, 504, 180, "Form seed", "", "Deterministic form identity.", 0);
        drawAction(777, 814, 504, 108, 48, "Reroll", "Pick a new form seed.");

        fc(t_.textDim);
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(626, 568, "Scene: Null 1 · Intro 0 · Theme 16 · Devel 32", nullptr);
        text(626, 584, "Break 64 · Reprise 96 · Coda 127", nullptr);
        text(626, 606, "CC 21–24 drive the matching receiver masters.", nullptr);
    }

    void actionTriggered(const int action) override
    {
        using namespace downspout::conductor;
        if (action == 778) {
            if (selectedBlock_ >= blockCount())
                selectedBlock_ = blockCount() - 1;
            commitParameter(kPendingSection, static_cast<float>(selectedBlock_ + 1));
            pendingBlock_ = selectedBlock_;
            repaint();
            return;
        }
        if (action != 777) return;
        using Clock = std::chrono::high_resolution_clock;
        const auto ticks = static_cast<std::uint64_t>(Clock::now().time_since_epoch().count());
        const auto s = static_cast<std::uint32_t>(ticks ^ (ticks >> 17) ^ (ticks >> 33));
        const float current = value(kSeed);
        float next = 1.0f + static_cast<float>(s % 65535u);
        if (next == current) next = 1.0f + static_cast<float>((s + 1u) % 65535u);
        commitParameter(kSeed, next);
    }

    void drawWeight(const std::uint32_t parameter,
                    const float x,
                    const float y,
                    const float w,
                    const char* label)
    {
        using namespace downspout::conductor;
        drawPercentSlider(parameter, x, y, w, label, "Relative chance of selecting this block.",
                          value(kMode) >= 0.5f);
    }
};

UI* createUI() { return new ConductorUI(); }

END_NAMESPACE_DISTRHO
