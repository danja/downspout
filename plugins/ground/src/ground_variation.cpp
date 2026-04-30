#include "ground_variation.hpp"

#include "ground_pattern.hpp"
#include "ground_rng.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace downspout::ground {
namespace {

[[nodiscard]] float clampf(const float value, const float minValue, const float maxValue)
{
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

[[nodiscard]] int clampi(const int value, const int minValue, const int maxValue)
{
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

[[nodiscard]] int loopsBetweenMutations(const float vary)
{
    const float targetLoops = 1.0f + 4.0f * std::pow(1.0f - vary, 2.3f);
    return clampi(static_cast<int>(std::ceil(targetLoops - 1e-9f)), 1, 8);
}

[[nodiscard]] int chooseTargetPhrase(const FormState& form,
                                     const int currentPhraseIndex,
                                     const float vary,
                                     Rng& rng)
{
    if (form.phraseCount <= 1) {
        return 0;
    }

    const int firstInterior = form.phraseCount > 2 ? 1 : 0;
    const int lastInterior = form.phraseCount > 2 ? form.phraseCount - 2 : form.phraseCount - 1;
    int target = clampi(currentPhraseIndex + 1 + rng.nextInt(0, std::max(0, lastInterior - firstInterior)),
                        firstInterior,
                        lastInterior);

    if (vary > 0.82f && form.phraseCount > 1 && rng.nextFloat() < 0.18f) {
        target = form.phraseCount - 1;
    } else if (vary < 0.28f && target == form.phraseCount - 1 && form.phraseCount > 2) {
        target = form.phraseCount - 2;
    }

    return clampi(target, 0, form.phraseCount - 1);
}

}  // namespace

void resetVariationProgress(VariationState& variation)
{
    variation.version = kVariationStateVersion;
    variation.completedFormLoops = 0;
    variation.lastMutationLoop = 0;
}

bool applyLoopVariation(FormState& form,
                        VariationState& variation,
                        const Controls& rawControls,
                        const double,
                        const int currentPhraseIndex)
{
    if (variation.version != kVariationStateVersion) {
        resetVariationProgress(variation);
    }

    if (variation.completedFormLoops < std::numeric_limits<std::int64_t>::max()) {
        variation.completedFormLoops += 1;
    }

    const Controls controls = clampControls(rawControls);
    const float vary = clampf(controls.vary, 0.0f, 1.0f);
    if (vary <= 0.0001f || form.phraseCount <= 0) {
        return false;
    }

    const int intervalLoops = loopsBetweenMutations(vary);
    if ((variation.completedFormLoops - variation.lastMutationLoop) < intervalLoops) {
        return false;
    }

    variation.lastMutationLoop = variation.completedFormLoops;

    Rng rng;
    rng.seed(controls.seed ^
             (static_cast<std::uint32_t>(variation.completedFormLoops) * 2246822519u) ^
             (static_cast<std::uint32_t>(form.generationSerial) * 3266489917u));

    const int targetPhrase = chooseTargetPhrase(form, currentPhraseIndex, vary, rng);
    const float roll = rng.nextFloat();

    if (vary < 0.20f) {
        mutatePhraseCell(form, controls, targetPhrase, 0.28f + vary * 0.50f);
        return true;
    }

    if (vary < 0.45f) {
        if (roll < 0.70f) {
            mutatePhraseCell(form, controls, targetPhrase, 0.42f + vary * 0.40f);
        } else {
            refreshPhrase(form, controls, targetPhrase);
        }
        return true;
    }

    if (vary < 0.75f) {
        if (roll < 0.18f) {
            mutatePhraseCell(form, controls, targetPhrase, 0.70f);
        } else if (roll < 0.74f) {
            refreshPhrase(form, controls, targetPhrase);
        } else {
            regenerateForm(form, controls, form.meter);
        }
        return true;
    }

    if (roll < 0.14f) {
        mutatePhraseCell(form, controls, targetPhrase, 0.90f);
    } else if (roll < 0.54f) {
        refreshPhrase(form, controls, targetPhrase);
        if (form.phraseCount > 2 && rng.nextFloat() < 0.40f) {
            const int companion = clampi(targetPhrase + (rng.nextFloat() < 0.5f ? -1 : 1), 0, form.phraseCount - 1);
            if (companion != targetPhrase) {
                mutatePhraseCell(form, controls, companion, 0.58f);
            }
        }
    } else {
        regenerateForm(form, controls, form.meter);
    }

    return true;
}

}  // namespace downspout::ground
