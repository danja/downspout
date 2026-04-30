#include "drumgen_variation.hpp"

#include "drumgen_pattern.hpp"
#include "drumgen_rng.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace downspout::drumgen {
namespace {

constexpr auto GENRE_ROCK = GenreId::rock;
constexpr auto GENRE_DISCO = GenreId::disco;
constexpr auto GENRE_SHUFFLE = GenreId::shuffle;
constexpr auto GENRE_ELECTRO = GenreId::electro;
constexpr auto GENRE_DUB = GenreId::dub;
constexpr auto GENRE_MOTORIK = GenreId::motorik;
constexpr auto GENRE_BOSSA = GenreId::bossa;
constexpr auto GENRE_AFRO = GenreId::afro;

[[nodiscard]] float clampf(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

[[nodiscard]] int clampi(int value, int minValue, int maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

[[nodiscard]] int loopsBetweenMutations(float vary, int barsPerLoop) {
    const float targetBars = 1.0f + 7.0f * std::pow(1.0f - vary, 2.5f);
    const int safeBarsPerLoop = barsPerLoop > 0 ? barsPerLoop : 1;
    return clampi(static_cast<int>(std::ceil(targetBars / static_cast<float>(safeBarsPerLoop) - 1e-6f)), 1, 64);
}

[[nodiscard]] int pickBarIndex(const PatternState& pattern, const Controls& controls, Rng& rng) {
    if (pattern.bars <= 1) {
        return 0;
    }
    if (controls.fill > 0.12f && rng.nextFloat() < 0.55f) {
        return pattern.bars - 1;
    }
    return rng.nextInt(0, pattern.bars - 1);
}

}  // namespace

void resetVariationProgress(VariationState& variation) {
    variation.version = kVariationStateVersion;
    variation.completedLoops = 0;
    variation.lastMutationLoop = 0;
}

bool applyLoopVariation(PatternState& pattern,
                        VariationState& variation,
                        const Controls& controls) {
    if (variation.version != kVariationStateVersion) {
        resetVariationProgress(variation);
    }

    if (variation.completedLoops < std::numeric_limits<std::int64_t>::max()) {
        variation.completedLoops += 1;
    }

    const float vary = clampf(controls.vary, 0.0f, 1.0f);
    if (vary <= 0.0001f) {
        return false;
    }

    const int intervalLoops = loopsBetweenMutations(vary, controls.bars);
    if ((variation.completedLoops - variation.lastMutationLoop) < intervalLoops) {
        return false;
    }

    variation.lastMutationLoop = variation.completedLoops;

    if (vary >= 0.999f) {
        regeneratePattern(pattern, controls, pattern.meter, false);
        return true;
    }

    Rng decisionRng;
    decisionRng.seed(controls.seed ^
                     (static_cast<std::uint32_t>(variation.completedLoops) * 2246822519u) ^
                     (static_cast<std::uint32_t>(pattern.generationSerial) * 3266489917u));

    const float roll = decisionRng.nextFloat();
    if (vary < 0.20f) {
        refreshBar(pattern, controls, pattern.meter, pickBarIndex(pattern, controls, decisionRng));
    } else if (vary < 0.45f) {
        if (roll < 0.60f) {
            refreshBar(pattern, controls, pattern.meter, pickBarIndex(pattern, controls, decisionRng));
        } else {
            regeneratePattern(pattern, controls, pattern.meter, true);
        }
    } else if (vary < 0.75f) {
        if (roll < 0.24f) {
            refreshBar(pattern, controls, pattern.meter, pickBarIndex(pattern, controls, decisionRng));
            if (pattern.bars > 1 && decisionRng.nextFloat() < 0.35f) {
                refreshBar(pattern, controls, pattern.meter, pickBarIndex(pattern, controls, decisionRng));
            }
        } else if (roll < 0.58f) {
            regeneratePattern(pattern, controls, pattern.meter, true);
        } else {
            regeneratePattern(pattern, controls, pattern.meter, false);
        }
    } else {
        if (roll < 0.14f) {
            refreshBar(pattern, controls, pattern.meter, pickBarIndex(pattern, controls, decisionRng));
        } else if (roll < 0.34f) {
            regeneratePattern(pattern, controls, pattern.meter, true);
        } else {
            regeneratePattern(pattern, controls, pattern.meter, false);
        }
    }

    return true;
}

}  // namespace downspout::drumgen
