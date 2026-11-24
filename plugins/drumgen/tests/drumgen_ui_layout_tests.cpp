#include "drumgen_status_params.hpp"
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

    // The left selectors are spread over the panel with a generous lower
    // margin. Conductor Ch now sits near y=620, so its 17-value menu flips
    // above the selector.
    const Rect conductorBase {22.0f, 620.0f, 349.12f, 46.0f};
    const Rect conductorMenu = selectorMenuRect(conductorBase, 17, 940.0f);
    assert(near(conductorMenu.x, conductorBase.x));
    assert(near(conductorMenu.w, conductorBase.w));
    assert(near(conductorMenu.y, 344.0f));
    assert(near(conductorMenu.h, 9.0f * kSelectorItemHeight));
    assert(conductorMenu.y + conductorMenu.h <= conductorBase.y - kSelectorGap);

    // Eight 46 px selectors on a 77 px step, plus the conductor and Actions
    // headers and the 50 px buttons, end 64 px above the panel bottom.
    assert(near(140.0f + 8.0f * 77.0f + 18.0f + 30.0f + 50.0f, 854.0f));

    // The right-hand automaton selectors are low in the panel, so their menus flip upward.
    const Rect ruleBase {453.12f, 713.0f, 528.88f, 46.0f};
    const Rect ruleMenu = selectorMenuRect(ruleBase, 11, 940.0f);
    assert(near(ruleMenu.y, 527.0f));
    assert(near(ruleMenu.h, 6.0f * kSelectorItemHeight));
    assert(ruleMenu.y + ruleMenu.h <= ruleBase.y - kSelectorGap);

    const Rect everyBase {453.12f, 813.0f, 528.88f, 46.0f};
    const Rect everyMenu = selectorMenuRect(everyBase, 8, 940.0f);
    assert(near(everyMenu.y, 687.0f));
    assert(near(everyMenu.h, 4.0f * kSelectorItemHeight));
    assert(everyMenu.y + everyMenu.h <= everyBase.y - kSelectorGap);
    assert(near(everyBase.y + everyBase.h, 859.0f));

    // A host window too short for either side keeps the menu at the top margin.
    const Rect shortMenu = selectorMenuRect({0.0f, 0.0f, 100.0f, 46.0f}, 17, 200.0f);
    assert(near(shortMenu.y, kSelectorMargin));

    // The live status mirrors are appended after the saved parameters and draw
    // on their matching Pattern Mix sliders. An ordinary index passes through.
    namespace status = downspout::drumgen::status_params;
    assert(status::kDensity == 25 && status::kVariation == 26 && status::kVary == 27);
    assert(status::kParameterCount == 28);
    assert(status::displayControlFor(status::kDensity) == status::kDensityControl);
    assert(status::displayControlFor(status::kVariation) == status::kVariationControl);
    assert(status::displayControlFor(status::kVary) == status::kVaryControl);
    assert(status::displayControlFor(status::kDensityControl) == status::kDensityControl);

    return 0;
}
