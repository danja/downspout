#pragma once

namespace downspout::drumgen::ui_layout {

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool contains(const float px, const float py) const noexcept
    {
        return px >= x && px <= (x + w) && py >= y && py <= (y + h);
    }
};

constexpr float kSelectorItemHeight = 30.0f;
constexpr float kSelectorGap = 6.0f;
constexpr float kSelectorMargin = 16.0f;

[[nodiscard]] inline int selectorColumnCount(const int count) noexcept
{
    return count > 6 ? 2 : 1;
}

[[nodiscard]] inline int selectorRowCount(const int count) noexcept
{
    const int columns = selectorColumnCount(count);
    return (count + columns - 1) / columns;
}

[[nodiscard]] inline Rect selectorMenuRect(const Rect& base,
                                           const int count,
                                           const float uiHeight) noexcept
{
    const float menuH = static_cast<float>(selectorRowCount(count)) * kSelectorItemHeight;
    const float belowY = base.y + base.h + kSelectorGap;
    if (belowY + menuH <= uiHeight - kSelectorMargin) {
        return {base.x, belowY, base.w, menuH};
    }

    const float aboveY = base.y - kSelectorGap - menuH;
    if (aboveY >= kSelectorMargin) {
        return {base.x, aboveY, base.w, menuH};
    }

    // Only possible on a very short host window; keep the menu on-screen.
    const float maxY = (uiHeight - kSelectorMargin - menuH > kSelectorMargin)
        ? uiHeight - kSelectorMargin - menuH
        : kSelectorMargin;
    const float fallbackY = (belowY < kSelectorMargin)
        ? kSelectorMargin
        : (belowY > maxY ? maxY : belowY);
    return {base.x, fallbackY, base.w, menuH};
}

}  // namespace downspout::drumgen::ui_layout
