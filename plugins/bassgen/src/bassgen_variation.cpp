#include "bassgen_variation.hpp"

#include "bassgen_pattern.hpp"
#include "bassgen_rng.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace downspout::bassgen {
namespace {

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

[[nodiscard]] int loopsBetweenMutations(float vary, double barsPerLoop) {
    const float targetBars = 1.0f + 7.0f * std::pow(1.0f - vary, 2.5f);
    const double safeBarsPerLoop = barsPerLoop > 0.0 ? barsPerLoop : 1.0;
    return clampi(static_cast<int>(std::ceil(targetBars / safeBarsPerLoop - 1e-9)), 1, 64);
}

[[nodiscard]] float partialNoteStrength(float vary) {
    return clampf(0.16f + vary * 0.84f, 0.16f, 1.0f);
}

}  // namespace

void resetVariationProgress(VariationState& variation) {
    variation.version = kVariationStateVersion;
    variation.completedLoops = 0;
    variation.lastMutationLoop = 0;
}

bool applyLoopVariation(PatternState& pattern,
                        VariationState& variation,
                        const Controls& controls,
                        double beatsPerBar) {
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

    const double safeBeatsPerBar = beatsPerBar > 0.0 ? beatsPerBar : 4.0;
    const double barsPerLoop = static_cast<double>(controls.lengthBeats) / safeBeatsPerBar;
    const int intervalLoops = loopsBetweenMutations(vary, barsPerLoop);
    if ((variation.completedLoops - variation.lastMutationLoop) < intervalLoops) {
        return false;
    }

    variation.lastMutationLoop = variation.completedLoops;

    if (vary >= 0.999f) {
        regeneratePattern(pattern, controls, true, true);
        return true;
    }

    Rng decisionRng;
    decisionRng.seed(controls.seed ^
                     (static_cast<std::uint32_t>(variation.completedLoops) * 2246822519u) ^
                     (static_cast<std::uint32_t>(pattern.generationSerial) * 3266489917u));

    const float roll = decisionRng.nextFloat();
    if (vary < 0.20f) {
        partialNoteMutation(pattern, controls, partialNoteStrength(vary) * 0.45f);
    } else if (vary < 0.45f) {
        if (roll < 0.68f) {
            partialNoteMutation(pattern, controls, partialNoteStrength(vary) * 0.70f);
        } else {
            regeneratePattern(pattern, controls, false, true);
        }
    } else if (vary < 0.75f) {
        if (roll < 0.30f) {
            partialNoteMutation(pattern, controls, partialNoteStrength(vary));
        } else if (roll < 0.74f) {
            regeneratePattern(pattern, controls, false, true);
        } else {
            regeneratePattern(pattern, controls, true, false);
        }
    } else {
        if (roll < 0.16f) {
            partialNoteMutation(pattern, controls, partialNoteStrength(vary));
        } else if (roll < 0.38f) {
            regeneratePattern(pattern, controls, false, true);
        } else if (roll < 0.70f) {
            regeneratePattern(pattern, controls, true, false);
        } else {
            regeneratePattern(pattern, controls, true, true);
        }
    }

    return true;
}

}  // namespace downspout::bassgen
