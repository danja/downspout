#include "drumgen_ui_layout.hpp"

#include "downspout/test_assert.h"
#include <cmath>

namespace {

using downspout::drumgen::ui_layout::kSelectorGap;
using downspout::drumgen::ui_layout::kSelectorItemHeight;
using downspout::drumgen::ui_layout::kSelectorMargin;
using downspout::drumgen::ui_layout::Rect;
using downspout::drumgen::ui_layout::selectorColumnCount;
using downspout::drumgen::ui_layout::selectorMenuRect;
using downspout::drumgen::ui_layout::selectorRowCount;

[[nodiscard]] bool near(const float a, const float b)
{
    return std::fabs(a - b) < 0.001f;
}

}  // namespace

int main()
{
    assert(selectorColumnCount(17) == 2);
    assert(selectorRowCount(17) == 9);
    assert(selectorColumnCount(8) == 2);
    assert(selectorRowCount(8) == 4);
    assert(selectorColumnCount(4) == 1);
    assert(selectorRowCount(4) == 4);

    // After moving the automaton selectors right, Conductor Ch sits near y=500;
    // its 17-value menu still fits below the selector in the 940 px panel.
    const Rect conductorBase {22.0f, 500.0f, 349.12f, 46.0f};
    const Rect conductorMenu = selectorMenuRect(conductorBase, 17, 940.0f);
    assert(near(conductorMenu.x, conductorBase.x));
    assert(near(conductorMenu.w, conductorBase.w));
    assert(near(conductorMenu.y, 552.0f));
    assert(near(conductorMenu.h, 9.0f * kSelectorItemHeight));
    assert(conductorMenu.y + conductorMenu.h <= 940.0f - kSelectorMargin);

    // The right-hand automaton selectors are low in the panel, so their menus flip upward.
    const Rect ruleBase {453.12f, 732.0f, 528.88f, 46.0f};
    const Rect ruleMenu = selectorMenuRect(ruleBase, 11, 940.0f);
    assert(near(ruleMenu.y, 546.0f));
    assert(near(ruleMenu.h, 6.0f * kSelectorItemHeight));
    assert(ruleMenu.y + ruleMenu.h <= ruleBase.y - kSelectorGap);

    const Rect everyBase {453.12f, 846.0f, 528.88f, 46.0f};
    const Rect everyMenu = selectorMenuRect(everyBase, 8, 940.0f);
    assert(near(everyMenu.y, 720.0f));
    assert(near(everyMenu.h, 4.0f * kSelectorItemHeight));
    assert(everyMenu.y + everyMenu.h <= everyBase.y - kSelectorGap);

    // A host window too short for either side keeps the menu at the top margin.
    const Rect shortMenu = selectorMenuRect({0.0f, 0.0f, 100.0f, 46.0f}, 17, 200.0f);
    assert(near(shortMenu.y, kSelectorMargin));

    return 0;
}
