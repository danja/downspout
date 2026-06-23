#include "drumgen_pattern.hpp"

#include "drumgen_rng.hpp"

#include <climits>
#include <cmath>
#include <cstring>

namespace downspout::drumgen {
namespace {

constexpr int kFluesDrumkitNotes[kLaneCount] = {36, 39, 40, 41, 42, 45, 46, 50, 51, 52, 53};
constexpr int kGMNotes[kLaneCount] = {36, 39, 38, 49, 42, 45, 46, 50, 57, 56, 75};

constexpr auto GENRE_ROCK = GenreId::rock;
constexpr auto GENRE_DISCO = GenreId::disco;
constexpr auto GENRE_SHUFFLE = GenreId::shuffle;
constexpr auto GENRE_ELECTRO = GenreId::electro;
constexpr auto GENRE_DUB = GenreId::dub;
constexpr auto GENRE_MOTORIK = GenreId::motorik;
constexpr auto GENRE_BOSSA = GenreId::bossa;
constexpr auto GENRE_AFRO = GenreId::afro;
constexpr auto GENRE_BREAKBEAT = GenreId::breakbeat;
constexpr auto GENRE_AMEN = GenreId::amen;
constexpr auto GENRE_JUNGLE = GenreId::jungle;
constexpr auto GENRE_HIPHOP = GenreId::hipHop;
constexpr auto GENRE_JAZZ = GenreId::jazz;
constexpr auto GENRE_FUGUE = GenreId::fugue;

constexpr auto RESOLUTION_8TH = ResolutionId::eighth;
constexpr auto RESOLUTION_16TH = ResolutionId::sixteenth;
constexpr auto RESOLUTION_16T = ResolutionId::sixteenthTriplet;
constexpr auto RESOLUTION_4TH = ResolutionId::quarter;

constexpr auto KIT_MAP_GM = KitMapId::gm;

constexpr int LANE_KICK = static_cast<int>(LaneId::kick);
constexpr int LANE_CLAP = static_cast<int>(LaneId::clap);
constexpr int LANE_SNARE = static_cast<int>(LaneId::snare);
constexpr int LANE_CRASH = static_cast<int>(LaneId::crash);
constexpr int LANE_CLOSED_HAT = static_cast<int>(LaneId::closedHat);
constexpr int LANE_LOW_TOM = static_cast<int>(LaneId::lowTom);
constexpr int LANE_OPEN_HAT = static_cast<int>(LaneId::openHat);
constexpr int LANE_HIGH_TOM = static_cast<int>(LaneId::highTom);
constexpr int LANE_BASH = static_cast<int>(LaneId::bash);
constexpr int LANE_COWBELL = static_cast<int>(LaneId::cowbell);
constexpr int LANE_CLAVE = static_cast<int>(LaneId::clave);

constexpr std::uint8_t STEP_FLAG_ACCENT = kStepFlagAccent;
constexpr std::uint8_t STEP_FLAG_FILL = kStepFlagFill;

enum class StepRole : std::uint8_t {
    weak = 0,
    pickup,
    secondary,
    primary
};

struct StylePulseInfo {
    int pulseCount = 1;
    int pulseIndex = 0;
    bool pulseStart = false;
    bool pickup = false;
};

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

[[nodiscard]] int mappedQuarterBeatIndex(const ::downspout::Meter& meter, const int quarterSlot) {
    const int beatCount = ::downspout::sanitizeMeter(meter).numerator;
    if (quarterSlot <= 0) {
        return 0;
    }
    return clampi(static_cast<int>(std::lround((static_cast<double>(quarterSlot) * beatCount) / 4.0)),
                  0,
                  beatCount - 1);
}

[[nodiscard]] int beatForFraction(const ::downspout::Meter& meter, const int numerator, const int denominator) {
    const ::downspout::Meter normalized = ::downspout::sanitizeMeter(meter);
    if (normalized.numerator <= 1 || denominator <= 0) {
        return 0;
    }

    return clampi((normalized.numerator * numerator) / denominator, 0, normalized.numerator - 1);
}

[[nodiscard]] bool isBackbeatBeat(const ::downspout::Meter& meter, const int beatIndex) {
    const ::downspout::Meter normalized = ::downspout::sanitizeMeter(meter);
    if (!::downspout::meterIsPulseStart(normalized, beatIndex)) {
        return false;
    }

    return (::downspout::meterPulseIndexForBeat(normalized, beatIndex) % 2) == 1;
}

[[nodiscard]] bool isBreakbeatFamily(const GenreId genre) {
    return genre == GENRE_BREAKBEAT || genre == GENRE_AMEN || genre == GENRE_JUNGLE;
}

[[nodiscard]] int quarterBeatStartStep(const PatternState& pattern, const int quarterSlot) {
    return mappedQuarterBeatIndex(pattern.meter, quarterSlot) * pattern.stepsPerBeat;
}

[[nodiscard]] int collectBackbeatBeats(const ::downspout::Meter& meter, std::array<int, ::downspout::kMaxMeterGroups>& beats) {
    const ::downspout::Meter normalized = ::downspout::sanitizeMeter(meter);
    int count = 0;

    for (int beatIndex = 0; beatIndex < normalized.numerator && count < static_cast<int>(beats.size()); ++beatIndex) {
        if (isBackbeatBeat(normalized, beatIndex)) {
            beats[static_cast<std::size_t>(count++)] = beatIndex;
        }
    }

    if (count == 0 && normalized.numerator > 1) {
        beats[0] = mappedQuarterBeatIndex(normalized, 1);
        count = 1;
    }

    return count;
}

[[nodiscard]] int collectAccentBeatsForStyle(const StyleModeId styleMode,
                                             const ::downspout::Meter& meter,
                                             std::array<int, ::downspout::kMaxMeterGroups>& beats) {
    const ::downspout::Meter normalized = ::downspout::sanitizeMeter(meter);

    if (styleMode == StyleModeId::autoMode) {
        return collectBackbeatBeats(normalized, beats);
    }

    auto pushBeat = [&beats, &normalized](int& count, const int beat) {
        const int clampedBeat = clampi(beat, 0, normalized.numerator - 1);
        for (int index = 0; index < count; ++index) {
            if (beats[static_cast<std::size_t>(index)] == clampedBeat) {
                return;
            }
        }
        if (count < static_cast<int>(beats.size())) {
            beats[static_cast<std::size_t>(count++)] = clampedBeat;
        }
    };

    int count = 0;
    switch (styleMode) {
    case StyleModeId::straight:
        for (int beat = 1; beat < normalized.numerator && count < static_cast<int>(beats.size()); beat += 2) {
            pushBeat(count, beat);
        }
        break;
    case StyleModeId::reel:
        pushBeat(count, beatForFraction(normalized, 1, 4));
        pushBeat(count, beatForFraction(normalized, 3, 4));
        break;
    case StyleModeId::waltz:
        pushBeat(count, beatForFraction(normalized, 1, 3));
        break;
    case StyleModeId::jig:
        pushBeat(count, beatForFraction(normalized, 1, 2));
        break;
    case StyleModeId::slipJig:
        pushBeat(count, beatForFraction(normalized, 1, 3));
        break;
    case StyleModeId::diddley:
        pushBeat(count, beatForFraction(normalized, 3, 4));
        break;
    case StyleModeId::autoMode:
    case StyleModeId::count:
    default:
        break;
    }

    if (count == 0 && normalized.numerator > 1) {
        pushBeat(count, beatForFraction(normalized, 1, 2));
    }

    return count;
}

[[nodiscard]] StylePulseInfo stylePulseInfo(const StyleModeId styleMode,
                                           const ::downspout::Meter& meter,
                                           const int beatIndex,
                                           const int subIndex,
                                           const int stepsPerBeat) {
    const ::downspout::Meter normalized = ::downspout::sanitizeMeter(meter);
    const int clampedBeat = clampi(beatIndex, 0, normalized.numerator - 1);
    const bool beatStart = subIndex == 0;
    const bool lateSub = stepsPerBeat > 0 && subIndex == (stepsPerBeat - 1);
    StylePulseInfo info;

    switch (styleMode) {
    case StyleModeId::straight:
        info.pulseCount = normalized.numerator;
        info.pulseIndex = clampedBeat;
        info.pulseStart = beatStart;
        info.pickup = lateSub;
        return info;

    case StyleModeId::reel: {
        const int halfBeat = beatForFraction(normalized, 1, 2);
        info.pulseCount = normalized.numerator >= 2 ? 2 : 1;
        info.pulseIndex = clampedBeat >= halfBeat ? std::min(1, info.pulseCount - 1) : 0;
        info.pulseStart = beatStart && (clampedBeat == 0 || clampedBeat == halfBeat);
        info.pickup = lateSub && (((clampedBeat + 1) % normalized.numerator) == halfBeat ||
                                  ((clampedBeat + 1) % normalized.numerator) == 0);
        return info;
    }

    case StyleModeId::waltz: {
        const int second = beatForFraction(normalized, 1, 3);
        const int third = beatForFraction(normalized, 2, 3);
        info.pulseCount = 3;
        info.pulseIndex = clampedBeat >= third ? 2 : (clampedBeat >= second ? 1 : 0);
        info.pulseStart = beatStart && (clampedBeat == 0 || clampedBeat == second || clampedBeat == third);
        info.pickup = lateSub && (((clampedBeat + 1) % normalized.numerator) == second ||
                                  ((clampedBeat + 1) % normalized.numerator) == third ||
                                  ((clampedBeat + 1) % normalized.numerator) == 0);
        return info;
    }

    case StyleModeId::jig: {
        const int second = beatForFraction(normalized, 1, 2);
        info.pulseCount = 2;
        info.pulseIndex = clampedBeat >= second ? 1 : 0;
        info.pulseStart = beatStart && (clampedBeat == 0 || clampedBeat == second);
        info.pickup = lateSub && (((clampedBeat + 1) % normalized.numerator) == second ||
                                  ((clampedBeat + 1) % normalized.numerator) == 0);
        return info;
    }

    case StyleModeId::slipJig: {
        const int second = beatForFraction(normalized, 1, 3);
        const int third = beatForFraction(normalized, 2, 3);
        info.pulseCount = 3;
        info.pulseIndex = clampedBeat >= third ? 2 : (clampedBeat >= second ? 1 : 0);
        info.pulseStart = beatStart && (clampedBeat == 0 || clampedBeat == second || clampedBeat == third);
        info.pickup = lateSub && (((clampedBeat + 1) % normalized.numerator) == second ||
                                  ((clampedBeat + 1) % normalized.numerator) == third ||
                                  ((clampedBeat + 1) % normalized.numerator) == 0);
        return info;
    }

    case StyleModeId::diddley:
        info.pulseCount = 5;
        info.pulseIndex = beatForFraction(normalized, 3, 4) <= clampedBeat ? 2
            : (beatForFraction(normalized, 1, 2) <= clampedBeat ? 1 : 0);
        info.pulseStart = beatStart && (clampedBeat == 0 ||
                                        clampedBeat == beatForFraction(normalized, 1, 2) ||
                                        clampedBeat == beatForFraction(normalized, 3, 4));
        info.pickup = lateSub;
        return info;

    case StyleModeId::autoMode:
    case StyleModeId::count:
    default:
        info.pulseCount = ::downspout::meterPulseCount(normalized);
        info.pulseIndex = ::downspout::meterPulseIndexForBeat(normalized, clampedBeat);
        info.pulseStart = beatStart && ::downspout::meterIsPulseStart(normalized, clampedBeat);
        info.pickup = lateSub && ::downspout::meterIsPulseStart(normalized, (clampedBeat + 1) % normalized.numerator);
        return info;
    }
}

[[nodiscard]] StepRole styleStepRole(const StyleModeId styleMode,
                                     const ::downspout::Meter& meter,
                                     const int beatIndex,
                                     const int subIndex,
                                     const int stepsPerBeat) {
    const StylePulseInfo info = stylePulseInfo(styleMode, meter, beatIndex, subIndex, stepsPerBeat);
    if (subIndex == 0 && info.pulseStart) {
        return StepRole::primary;
    }
    if (subIndex == 0) {
        return StepRole::secondary;
    }
    if (info.pickup) {
        return StepRole::pickup;
    }
    return StepRole::weak;
}

[[nodiscard]] bool isTripleMeter(const ::downspout::Meter& meter) {
    const ::downspout::Meter normalized = ::downspout::sanitizeMeter(meter);
    return normalized.numerator == 3 && !::downspout::meterHasCompoundFeel(normalized);
}

[[nodiscard]] int lastPulseIndex(const ::downspout::Meter& meter) {
    return std::max(0, ::downspout::meterPulseCount(meter) - 1);
}

[[nodiscard]] bool isPulseStartBeat(const ::downspout::Meter& meter, const int beatIndex) {
    return ::downspout::meterIsPulseStart(::downspout::sanitizeMeter(meter), beatIndex);
}

[[nodiscard]] int preferredFillBeats(const ::downspout::Meter& meter, const float fillAmount) {
    const ::downspout::Meter normalized = ::downspout::sanitizeMeter(meter);
    if (::downspout::meterHasCompoundFeel(normalized)) {
        return ::downspout::meterGroupSize(normalized, lastPulseIndex(normalized));
    }
    if (isTripleMeter(normalized)) {
        return fillAmount > 0.62f ? 2 : 1;
    }
    return fillAmount > 0.62f ? 2 : 1;
}

[[nodiscard]] bool isFinalPulseBeat(const ::downspout::Meter& meter, const int beatIndex) {
    const ::downspout::Meter normalized = ::downspout::sanitizeMeter(meter);
    return ::downspout::meterPulseIndexForBeat(normalized, beatIndex) == lastPulseIndex(normalized);
}

[[nodiscard]] bool isPulsePickupSub(const ::downspout::Meter& meter,
                                    const int beatIndex,
                                    const int subIndex,
                                    const int stepsPerBeat) {
    if (stepsPerBeat <= 0 || subIndex != (stepsPerBeat - 1)) {
        return false;
    }
    const ::downspout::Meter normalized = ::downspout::sanitizeMeter(meter);
    const int nextBeat = (beatIndex + 1) % normalized.numerator;
    return isPulseStartBeat(normalized, nextBeat);
}

[[nodiscard]] int nextGenerationSerial(std::int32_t currentSerial) {
    return currentSerial >= INT32_MAX ? 1 : currentSerial + 1;
}

[[nodiscard]] Controls controlsForManualFill(const Controls& rawControls) {
    Controls controls = clampControls(rawControls);
    controls.fill = clampf(std::max(controls.fill, 0.72f), 0.0f, 1.0f);
    return controls;
}

[[nodiscard]] std::uint32_t baseSeedForSerial(const Controls& controls, std::int32_t generationSerial) {
    return controls.seed ^
           (static_cast<std::uint32_t>(controls.genre) * 0x45D9F3Bu) ^
           (static_cast<std::uint32_t>(static_cast<int>(controls.styleMode) + 1) * 0x119DE1F3u) ^
           (static_cast<std::uint32_t>(generationSerial) * 0x9E3779B9u);
}

[[nodiscard]] std::uint32_t fillSeedForSerial(const Controls& controls, std::int32_t generationSerial) {
    return controls.seed ^
           (static_cast<std::uint32_t>(generationSerial) * 0x85EBCA6Bu) ^
           0xA511E9B3u;
}

[[nodiscard]] std::uint32_t barSeedForSerial(const Controls& controls,
                                             std::int32_t generationSerial,
                                             int barIndex,
                                             bool fillBar) {
    const std::uint32_t baseSeed = baseSeedForSerial(controls, generationSerial);
    const std::uint32_t fillSeed = fillSeedForSerial(controls, generationSerial);
    return baseSeed ^
           (static_cast<std::uint32_t>(barIndex + 1) * 0x27D4EB2Du) ^
           (fillBar ? fillSeed : 0u);
}

[[nodiscard]] int noteForLane(KitMapId kitMap, int lane) {
    const int* table = (kitMap == KIT_MAP_GM) ? kGMNotes : kFluesDrumkitNotes;
    return table[clampi(lane, 0, kLaneCount - 1)];
}

[[nodiscard]] float laneMacro(const Controls& controls, int lane) {
    switch (lane) {
    case LANE_KICK:
        return controls.kickAmt;
    case LANE_CLAP:
    case LANE_SNARE:
        return controls.backbeatAmt;
    case LANE_CLOSED_HAT:
    case LANE_OPEN_HAT:
        return controls.hatAmt;
    case LANE_LOW_TOM:
    case LANE_HIGH_TOM:
        return controls.tomAmt;
    case LANE_CRASH:
    case LANE_BASH:
        return controls.metalAmt;
    case LANE_COWBELL:
    case LANE_CLAVE:
    default:
        return controls.auxAmt;
    }
}

[[nodiscard]] bool isOffbeatStep(int stepInBar, int stepsPerBeat) {
    if (stepsPerBeat <= 0) {
        return false;
    }
    if (stepsPerBeat == 3) {
        return (stepInBar % stepsPerBeat) == 2;
    }
    return (stepInBar % stepsPerBeat) == (stepsPerBeat / 2);
}

[[nodiscard]] bool isFillZoneStep(int stepInBar,
                                  int stepsPerBar,
                                  int stepsPerBeat,
                                  const ::downspout::Meter& meter,
                                  float fill) {
    const int fillBeats = preferredFillBeats(meter, fill);
    const int fillSteps = clampi(fillBeats * stepsPerBeat, stepsPerBeat, stepsPerBar);
    return stepInBar >= stepsPerBar - fillSteps;
}

[[nodiscard]] bool euclidHit(int step, int pulses, int offset, int length) {
    if (length <= 0 || pulses <= 0) {
        return false;
    }
    if (pulses >= length) {
        return true;
    }

    const int baseStep = (step - offset + length) % length;
    return ((baseStep * pulses) % length) < pulses;
}

[[nodiscard]] float compoundAnchorProbability(const Controls& controls,
                                              const ::downspout::Meter& meter,
                                              int lane,
                                              int beatIndex,
                                              int subIndex,
                                              int stepsPerBeat,
                                              bool fillBar) {
    const ::downspout::Meter normalized = ::downspout::sanitizeMeter(meter);
    const bool beatStart = subIndex == 0;
    const bool pulseStart = beatStart && isPulseStartBeat(normalized, beatIndex);
    const int pulseIndex = ::downspout::meterPulseIndexForBeat(normalized, beatIndex);
    const bool secondaryPulse = pulseStart && pulseIndex == std::min(1, lastPulseIndex(normalized));
    const bool finalPulse = isFinalPulseBeat(normalized, beatIndex);
    const bool pulsePickup = isPulsePickupSub(normalized, beatIndex, subIndex, stepsPerBeat);
    const bool lateSub = subIndex == stepsPerBeat - 1;
    const float kick = controls.kickAmt;
    const float backbeat = controls.backbeatAmt;
    const float hat = controls.hatAmt;
    const float tom = controls.tomAmt;
    const float metal = controls.metalAmt;
    const float perc = controls.auxAmt;
    const float fill = controls.fill;

    switch (lane) {
    case LANE_KICK:
        if (pulseStart && pulseIndex == 0) return 0.96f;
        if (pulseStart) {
            if (controls.genre == GENRE_DISCO || controls.genre == GENRE_MOTORIK) return 0.72f + 0.20f * kick;
            if (controls.genre == GENRE_ELECTRO) return 0.54f + 0.24f * kick;
            return 0.46f + 0.20f * kick;
        }
        if (pulsePickup && (controls.genre == GENRE_SHUFFLE || controls.genre == GENRE_DUB || controls.genre == GENRE_AFRO)) {
            return 0.12f + 0.14f * controls.variation;
        }
        break;

    case LANE_SNARE:
        if (secondaryPulse) return 0.78f + 0.18f * backbeat;
        if (fillBar && finalPulse && lateSub) return 0.10f + 0.24f * fill * backbeat;
        break;

    case LANE_CLAP:
        if (secondaryPulse) {
            if (controls.genre == GENRE_DISCO) return 0.68f + 0.18f * backbeat;
            if (controls.genre == GENRE_ELECTRO) return 0.42f + 0.22f * backbeat;
            return 0.12f + 0.24f * backbeat;
        }
        if (fillBar && finalPulse && !beatStart) return 0.08f + 0.18f * fill * backbeat;
        break;

    case LANE_CRASH:
        if (beatIndex == 0 && beatStart) return 0.06f + 0.08f * metal;
        if (fillBar && finalPulse && beatStart) return 0.04f + 0.10f * fill * metal;
        break;

    case LANE_CLOSED_HAT:
        if (beatStart) return pulseStart ? 0.82f + 0.16f * hat : 0.66f + 0.18f * hat;
        if (pulsePickup) return 0.30f + 0.18f * hat;
        return 0.10f + 0.16f * hat * controls.variation;

    case LANE_OPEN_HAT:
        if (pulsePickup) return 0.24f + 0.18f * hat;
        if (pulseStart && pulseIndex > 0) return 0.14f + 0.16f * hat;
        if (fillBar && finalPulse && lateSub) return 0.16f + 0.16f * fill;
        break;

    case LANE_LOW_TOM:
        if (fillBar && finalPulse && (pulsePickup || lateSub)) return 0.10f + 0.30f * fill + 0.12f * tom;
        break;

    case LANE_HIGH_TOM:
        if (fillBar && finalPulse && !beatStart) return 0.10f + 0.28f * fill + 0.12f * tom;
        break;

    case LANE_BASH:
        if (fillBar && finalPulse && (beatStart || lateSub)) return 0.10f + 0.22f * fill * metal;
        if ((controls.genre == GENRE_DUB || controls.genre == GENRE_ELECTRO) && pulsePickup) return 0.08f + 0.16f * metal;
        break;

    case LANE_COWBELL:
        if (pulseStart && pulseIndex > 0) return 0.12f + 0.20f * perc;
        if (pulsePickup) return 0.16f + 0.18f * perc;
        break;

    case LANE_CLAVE:
        if (pulseStart && (secondaryPulse || finalPulse)) return 0.14f + 0.18f * perc;
        if (pulsePickup) return 0.16f + 0.18f * perc;
        break;

    default:
        break;
    }

    return 0.0f;
}

[[nodiscard]] float tripleAnchorProbability(const Controls& controls,
                                            int lane,
                                            int beatIndex,
                                            int subIndex,
                                            int stepsPerBeat,
                                            bool fillBar) {
    const bool beatStart = subIndex == 0;
    const bool offbeat = isOffbeatStep(beatIndex * stepsPerBeat + subIndex, stepsPerBeat);
    const bool lateSub = subIndex == stepsPerBeat - 1;
    const bool beatOne = beatIndex == 0;
    const bool beatTwo = beatIndex == 1;
    const bool beatThree = beatIndex == 2;
    const float kick = controls.kickAmt;
    const float backbeat = controls.backbeatAmt;
    const float hat = controls.hatAmt;
    const float tom = controls.tomAmt;
    const float metal = controls.metalAmt;
    const float perc = controls.auxAmt;
    const float fill = controls.fill;

    switch (lane) {
    case LANE_KICK:
        if (beatOne && beatStart) return 0.96f;
        if (beatThree && beatStart) return 0.46f + 0.20f * kick;
        if (beatTwo && lateSub && controls.genre == GENRE_SHUFFLE) return 0.10f + 0.12f * controls.variation;
        break;

    case LANE_SNARE:
        if (beatTwo && beatStart) return 0.72f + 0.18f * backbeat;
        if (fillBar && beatThree && lateSub) return 0.08f + 0.22f * fill * backbeat;
        break;

    case LANE_CLAP:
        if (beatTwo && beatStart) return 0.16f + 0.22f * backbeat;
        if (controls.genre == GENRE_DISCO && beatThree && offbeat) return 0.08f + 0.12f * controls.variation;
        break;

    case LANE_CRASH:
        if (beatOne && beatStart) return 0.06f + 0.08f * metal;
        if (fillBar && beatThree && beatStart) return 0.04f + 0.10f * fill * metal;
        break;

    case LANE_CLOSED_HAT:
        if (beatStart) return beatOne ? 0.82f + 0.16f * hat : 0.68f + 0.18f * hat;
        if (offbeat) return 0.16f + 0.14f * hat;
        return 0.08f + 0.10f * hat * controls.variation;

    case LANE_OPEN_HAT:
        if (beatThree && offbeat) return 0.20f + 0.18f * hat;
        if (fillBar && beatThree && lateSub) return 0.14f + 0.16f * fill;
        break;

    case LANE_LOW_TOM:
        if (fillBar && beatThree && (offbeat || lateSub)) return 0.10f + 0.28f * fill + 0.12f * tom;
        break;

    case LANE_HIGH_TOM:
        if (fillBar && beatThree && !beatStart) return 0.10f + 0.26f * fill + 0.12f * tom;
        break;

    case LANE_BASH:
        if (fillBar && beatThree && (beatStart || lateSub)) return 0.10f + 0.20f * fill * metal;
        break;

    case LANE_COWBELL:
        if (beatTwo && beatStart) return 0.12f + 0.18f * perc;
        if (beatThree && offbeat) return 0.14f + 0.18f * perc;
        break;

    case LANE_CLAVE:
        if (beatOne && beatStart) return 0.16f + 0.16f * perc;
        if (beatTwo && lateSub) return 0.14f + 0.18f * perc;
        if (beatThree && beatStart) return 0.16f + 0.16f * perc;
        break;

    default:
        break;
    }

    return 0.0f;
}

[[nodiscard]] float styleAnchorProbability(const Controls& controls,
                                           int lane,
                                           int beatIndex,
                                           int subIndex,
                                           int stepsPerBeat,
                                           bool fillBar,
                                           const ::downspout::Meter& meter) {
    const StylePulseInfo pulse = stylePulseInfo(controls.styleMode, meter, beatIndex, subIndex, stepsPerBeat);
    const StepRole role = styleStepRole(controls.styleMode, meter, beatIndex, subIndex, stepsPerBeat);
    const bool beatStart = subIndex == 0;
    const bool offbeat = isOffbeatStep(beatIndex * stepsPerBeat + subIndex, stepsPerBeat);
    const bool lateSub = subIndex == stepsPerBeat - 1;
    const bool primaryPulse = pulse.pulseIndex == 0;
    const bool secondPulse = pulse.pulseIndex == std::min(1, pulse.pulseCount - 1);
    const bool finalPulse = pulse.pulseIndex == (pulse.pulseCount - 1);
    const float kick = controls.kickAmt;
    const float backbeat = controls.backbeatAmt;
    const float hat = controls.hatAmt;
    const float tom = controls.tomAmt;
    const float metal = controls.metalAmt;
    const float perc = controls.auxAmt;
    const float fill = controls.fill;

    switch (lane) {
    case LANE_KICK:
        if (controls.styleMode == StyleModeId::diddley) {
            if (beatStart && primaryPulse) return 0.94f;
            if (role == StepRole::pickup) return 0.18f + 0.22f * kick;
        }
        if (pulse.pulseStart && primaryPulse) return 0.96f;
        if (controls.styleMode == StyleModeId::reel && beatStart && role == StepRole::secondary) return 0.18f + 0.16f * kick;
        if ((controls.styleMode == StyleModeId::waltz ||
             controls.styleMode == StyleModeId::jig ||
             controls.styleMode == StyleModeId::slipJig) && pulse.pulseStart) {
            return (secondPulse ? 0.44f : 0.34f) + 0.18f * kick;
        }
        if (role == StepRole::pickup && (controls.styleMode == StyleModeId::reel ||
                                         controls.styleMode == StyleModeId::jig ||
                                         controls.styleMode == StyleModeId::slipJig)) {
            return 0.10f + 0.14f * controls.variation;
        }
        break;

    case LANE_SNARE:
        if (controls.styleMode == StyleModeId::diddley) {
            if (pulse.pulseStart && finalPulse) return 0.62f + 0.22f * backbeat;
            if (role == StepRole::pickup) return 0.10f + 0.16f * controls.variation;
        }
        if (controls.styleMode == StyleModeId::reel && beatStart && role == StepRole::secondary) {
            return 0.76f + 0.18f * backbeat;
        }
        if (controls.styleMode == StyleModeId::straight && beatStart && (beatIndex % 2) == 1) {
            return 0.72f + 0.18f * backbeat;
        }
        if (controls.styleMode == StyleModeId::waltz && beatStart && secondPulse) {
            return 0.74f + 0.18f * backbeat;
        }
        if ((controls.styleMode == StyleModeId::jig || controls.styleMode == StyleModeId::slipJig) &&
            pulse.pulseStart && secondPulse) {
            return 0.78f + 0.18f * backbeat;
        }
        if (fillBar && finalPulse && lateSub) {
            return 0.10f + 0.24f * fill * backbeat;
        }
        break;

    case LANE_CLAP:
        if (controls.styleMode == StyleModeId::diddley) {
            if (pulse.pulseStart && finalPulse) return 0.18f + 0.20f * backbeat;
        }
        if (controls.styleMode == StyleModeId::reel && beatStart && role == StepRole::secondary) {
            return 0.20f + 0.22f * backbeat;
        }
        if (controls.styleMode == StyleModeId::straight && beatStart && (beatIndex % 2) == 1) {
            return 0.16f + 0.22f * backbeat;
        }
        if (controls.styleMode == StyleModeId::waltz && beatStart && secondPulse) {
            return 0.12f + 0.18f * backbeat;
        }
        if ((controls.styleMode == StyleModeId::jig || controls.styleMode == StyleModeId::slipJig) &&
            pulse.pulseStart && secondPulse) {
            return 0.10f + 0.18f * backbeat;
        }
        if (fillBar && finalPulse && !beatStart) {
            return 0.08f + 0.18f * fill * backbeat;
        }
        break;

    case LANE_CRASH:
        if (beatStart && primaryPulse) return 0.06f + 0.08f * metal;
        if (fillBar && finalPulse && beatStart) return 0.04f + 0.10f * fill * metal;
        break;

    case LANE_CLOSED_HAT:
        if (controls.styleMode == StyleModeId::diddley) {
            if (beatStart || offbeat) return 0.56f + 0.22f * hat;
            return 0.08f + 0.12f * hat * controls.variation;
        }
        if (beatStart) return pulse.pulseStart ? 0.82f + 0.16f * hat : 0.66f + 0.18f * hat;
        if (role == StepRole::pickup || offbeat) return 0.18f + 0.18f * hat;
        return 0.08f + 0.14f * hat * controls.variation;

    case LANE_OPEN_HAT:
        if (role == StepRole::pickup) return 0.22f + 0.18f * hat;
        if (pulse.pulseStart && !primaryPulse) return 0.14f + 0.16f * hat;
        if (fillBar && finalPulse && lateSub) return 0.14f + 0.16f * fill;
        break;

    case LANE_LOW_TOM:
        if (fillBar && finalPulse && (role == StepRole::pickup || lateSub)) return 0.10f + 0.30f * fill + 0.12f * tom;
        break;

    case LANE_HIGH_TOM:
        if (fillBar && finalPulse && !beatStart) return 0.10f + 0.28f * fill + 0.12f * tom;
        break;

    case LANE_BASH:
        if (fillBar && finalPulse && (beatStart || lateSub)) return 0.10f + 0.22f * fill * metal;
        if ((controls.styleMode == StyleModeId::jig || controls.styleMode == StyleModeId::slipJig) && role == StepRole::pickup) {
            return 0.08f + 0.16f * metal;
        }
        break;

    case LANE_COWBELL:
        if (controls.styleMode == StyleModeId::diddley) {
            if (pulse.pulseStart || role == StepRole::pickup) return 0.24f + 0.32f * perc;
        }
        if (pulse.pulseStart && !primaryPulse) return 0.14f + 0.20f * perc;
        if (role == StepRole::pickup) return 0.16f + 0.18f * perc;
        break;

    case LANE_CLAVE:
        if (controls.styleMode == StyleModeId::diddley) {
            if (pulse.pulseStart || role == StepRole::pickup) return 0.34f + 0.36f * perc;
        }
        if (pulse.pulseStart && (secondPulse || finalPulse)) return 0.14f + 0.18f * perc;
        if (role == StepRole::pickup) return 0.16f + 0.18f * perc;
        break;

    default:
        break;
    }

    return 0.0f;
}

[[nodiscard]] float anchorProbability(const Controls& controls,
                                      const ::downspout::Meter& meter,
                                      int lane,
                                      int barIndex,
                                      int beatIndex,
                                      int subIndex,
                                      int stepsPerBeat,
                                      bool fillBar) {
    if (controls.styleMode != StyleModeId::autoMode) {
        return styleAnchorProbability(controls, lane, beatIndex, subIndex, stepsPerBeat, fillBar, meter);
    }
    if (::downspout::meterHasCompoundFeel(meter)) {
        return compoundAnchorProbability(controls, meter, lane, beatIndex, subIndex, stepsPerBeat, fillBar);
    }
    if (isTripleMeter(meter)) {
        return tripleAnchorProbability(controls, lane, beatIndex, subIndex, stepsPerBeat, fillBar);
    }

    const bool beatStart = subIndex == 0;
    const bool offbeat = isOffbeatStep(beatIndex * stepsPerBeat + subIndex, stepsPerBeat);
    const bool lateSub = subIndex == stepsPerBeat - 1;
    const int q1Beat = mappedQuarterBeatIndex(meter, 1);
    const int q2Beat = mappedQuarterBeatIndex(meter, 2);
    const int q3Beat = mappedQuarterBeatIndex(meter, 3);
    const bool onQ1 = beatIndex == q1Beat;
    const bool onQ2 = beatIndex == q2Beat;
    const bool onQ3 = beatIndex == q3Beat;
    const bool backbeatStart = beatStart && isBackbeatBeat(meter, beatIndex);
    const float kick = controls.kickAmt;
    const float backbeat = controls.backbeatAmt;
    const float hat = controls.hatAmt;
    const float tom = controls.tomAmt;
    const float metal = controls.metalAmt;
    const float perc = controls.auxAmt;
    const float fill = controls.fill;

    switch (lane) {
    case LANE_KICK:
        switch (controls.genre) {
        case GENRE_BREAKBEAT:
        case GENRE_AMEN:
        case GENRE_JUNGLE:
            if (beatIndex == 0 && beatStart) return 0.98f;
            if (beatIndex == 1 && subIndex == std::max(1, stepsPerBeat / 2)) return 0.56f + 0.28f * kick;
            if (beatIndex == 2 && subIndex == std::max(1, stepsPerBeat / 2)) return 0.48f + 0.24f * kick;
            if (beatIndex == 3 && lateSub) return 0.18f + 0.20f * controls.variation;
            break;
        case GENRE_HIPHOP:
            if (beatIndex == 0 && beatStart) return 0.96f;
            if ((onQ1 || onQ2) && offbeat) return 0.22f + 0.22f * kick;
            if (onQ3 && lateSub) return 0.14f + 0.16f * controls.variation;
            break;
        case GENRE_JAZZ:
            if (beatStart) return 0.34f + 0.34f * kick;
            if (lateSub && (onQ1 || onQ3)) return 0.06f + 0.10f * controls.variation;
            break;
        case GENRE_DISCO:
        case GENRE_MOTORIK:
            if (beatStart) return 0.86f + 0.12f * kick;
            if (offbeat && onQ3) return 0.12f + 0.10f * controls.variation;
            break;
        case GENRE_SHUFFLE:
            if (beatIndex == 0 && beatStart) return 0.96f;
            if (onQ2 && beatStart) return 0.62f + 0.18f * kick;
            if (offbeat && (onQ1 || onQ3)) return 0.16f + 0.10f * controls.variation;
            break;
        case GENRE_ELECTRO:
            if (beatIndex == 0 && beatStart) return 0.94f;
            if (onQ2 && beatStart) return 0.40f + 0.22f * kick;
            if (lateSub) return 0.12f + 0.22f * controls.variation;
            break;
        case GENRE_DUB:
            if (beatIndex == 0 && beatStart) return 0.96f;
            if (onQ2 && beatStart) return 0.24f + 0.18f * kick;
            if (offbeat && onQ3) return 0.10f + 0.10f * controls.variation;
            break;
        case GENRE_BOSSA:
            if (beatIndex == 0 && beatStart) return 0.82f;
            if (onQ1 && lateSub) return 0.34f;
            if (onQ2 && beatStart) return 0.56f;
            if (onQ3 && offbeat) return 0.42f;
            break;
        case GENRE_AFRO:
            if (beatIndex == 0 && beatStart) return 0.84f;
            if (onQ1 && offbeat) return 0.34f;
            if (onQ2 && beatStart) return 0.58f;
            if (onQ3 && offbeat) return 0.38f;
            break;
        case GENRE_ROCK:
        default:
            if (beatIndex == 0 && beatStart) return 0.98f;
            if (onQ2 && beatStart) return 0.68f + 0.18f * kick;
            if (onQ3 && lateSub) return 0.10f + 0.18f * controls.variation;
            if (onQ1 && beatStart) return 0.12f + 0.10f * kick;
            break;
        }
        break;

    case LANE_SNARE:
        if (backbeatStart) {
            switch (controls.genre) {
            case GENRE_BREAKBEAT:
            case GENRE_AMEN:
            case GENRE_JUNGLE: return 0.88f + 0.10f * backbeat;
            case GENRE_HIPHOP: return 0.82f + 0.12f * backbeat;
            case GENRE_JAZZ: return 0.38f + 0.18f * backbeat;
            case GENRE_DISCO: return 0.76f + 0.18f * backbeat;
            case GENRE_ELECTRO: return 0.72f + 0.18f * backbeat;
            case GENRE_DUB: return 0.64f + 0.16f * backbeat;
            default: return 0.84f + 0.12f * backbeat;
            }
        }
        if (isBreakbeatFamily(controls.genre) && lateSub) return 0.16f + 0.20f * controls.variation * backbeat;
        if (controls.genre == GENRE_HIPHOP && lateSub && (onQ1 || onQ3)) return 0.08f + 0.08f * controls.variation;
        if (controls.genre == GENRE_JAZZ && (offbeat || lateSub)) return 0.10f + 0.18f * controls.variation * backbeat;
        if (fillBar && beatIndex >= q2Beat && lateSub) return 0.08f + 0.24f * fill * backbeat;
        if (controls.genre == GENRE_AFRO && offbeat) return 0.08f + 0.10f * controls.variation;
        if (controls.genre == GENRE_SHUFFLE && lateSub) return 0.06f + 0.10f * controls.variation;
        break;

    case LANE_CLAP:
        if (backbeatStart) {
            switch (controls.genre) {
            case GENRE_BREAKBEAT:
            case GENRE_AMEN:
            case GENRE_JUNGLE: return 0.10f + 0.14f * backbeat;
            case GENRE_HIPHOP: return 0.10f + 0.18f * backbeat;
            case GENRE_JAZZ: return 0.03f + 0.05f * backbeat;
            case GENRE_DISCO: return 0.78f + 0.16f * backbeat;
            case GENRE_ELECTRO: return 0.52f + 0.20f * backbeat;
            case GENRE_DUB: return 0.18f + 0.14f * backbeat;
            default: return 0.18f + 0.34f * backbeat;
            }
        }
        if (controls.genre == GENRE_DISCO && offbeat) return 0.08f + 0.14f * controls.variation;
        if (fillBar && onQ3 && !beatStart) return 0.06f + 0.18f * fill * backbeat;
        break;

    case LANE_CRASH:
        if (beatStart && beatIndex == 0) {
            return (barIndex == 0 ? 0.14f : 0.03f) + 0.06f * metal;
        }
        if (fillBar && beatStart && beatIndex >= q2Beat) {
            return 0.03f + 0.08f * fill * (0.55f + 0.45f * metal);
        }
        break;

    case LANE_CLOSED_HAT:
        if (controls.genre == GENRE_JAZZ) {
            if (stepsPerBeat == 3) {
                if (subIndex == 0) return 0.78f + 0.16f * hat;
                if (subIndex == 2) return 0.58f + 0.20f * hat;
                return 0.08f + 0.10f * controls.variation;
            }
            if (beatStart) return 0.74f + 0.14f * hat;
            if (offbeat) return 0.48f + 0.22f * hat;
            if (lateSub) return 0.14f + 0.12f * controls.variation;
            return 0.08f + 0.10f * controls.variation;
        }
        if (stepsPerBeat == 3) {
            if (subIndex == 0) return 0.70f + 0.18f * hat;
            if (subIndex == 2) return 0.54f + 0.20f * hat;
            return 0.18f + 0.16f * controls.variation;
        }
        if (isBreakbeatFamily(controls.genre)) {
            if (controls.genre == GENRE_JUNGLE) return 0.48f + 0.38f * hat;
            if (subIndex == 0 || subIndex == 2) return 0.78f + 0.16f * hat;
            return 0.30f + 0.24f * hat * controls.density;
        }
        if (controls.genre == GENRE_HIPHOP) {
            if (subIndex == 0 || subIndex == 2) return 0.66f + 0.16f * hat;
            return 0.08f + 0.14f * hat * controls.variation;
        }
        if (stepsPerBeat == 2) {
            return offbeat ? 0.66f + 0.20f * hat : 0.74f + 0.16f * hat;
        }
        if (subIndex == 0 || subIndex == 2) return 0.74f + 0.18f * hat;
        return 0.18f + 0.28f * hat * controls.density;

    case LANE_OPEN_HAT:
        if (offbeat) {
            switch (controls.genre) {
            case GENRE_BREAKBEAT:
            case GENRE_AMEN:
            case GENRE_JUNGLE: return 0.22f + 0.24f * hat;
            case GENRE_HIPHOP: return 0.10f + 0.12f * hat;
            case GENRE_JAZZ: return 0.12f + 0.12f * hat;
            case GENRE_DISCO:
            case GENRE_MOTORIK: return 0.34f + 0.32f * hat;
            case GENRE_DUB: return 0.14f + 0.18f * hat;
            default: return 0.18f + 0.20f * hat;
            }
        }
        if (controls.genre == GENRE_JAZZ && backbeatStart) return 0.18f + 0.20f * hat;
        if (fillBar && onQ3 && lateSub) return 0.16f + 0.14f * fill;
        break;

    case LANE_LOW_TOM:
        if (fillBar && beatIndex >= q2Beat && (offbeat || lateSub)) {
            return 0.08f + 0.28f * fill + 0.12f * tom;
        }
        break;

    case LANE_HIGH_TOM:
        if (fillBar && beatIndex >= q2Beat && !beatStart) {
            return 0.08f + 0.26f * fill + 0.12f * tom;
        }
        break;

    case LANE_BASH:
        switch (controls.genre) {
        case GENRE_BREAKBEAT:
        case GENRE_AMEN:
        case GENRE_JUNGLE:
            if (beatStart && beatIndex == 0) return 0.10f + 0.18f * metal;
            if (lateSub && (onQ1 || onQ3)) return 0.10f + 0.16f * metal;
            if (fillBar && beatIndex >= q2Beat && (offbeat || lateSub)) return 0.10f + 0.22f * fill * metal;
            break;
        case GENRE_ELECTRO:
            if (beatStart && beatIndex == 0) return 0.14f + 0.16f * metal;
            if (fillBar && beatIndex >= q2Beat && (beatStart || lateSub)) return 0.10f + 0.26f * fill * metal;
            if (lateSub && onQ3) return 0.08f + 0.14f * controls.variation;
            break;
        case GENRE_MOTORIK:
            if (beatStart && beatIndex == 0) return 0.12f + 0.18f * metal;
            if (fillBar && onQ3 && beatStart) return 0.10f + 0.22f * fill * metal;
            break;
        case GENRE_DUB:
            if ((offbeat && onQ3) || (lateSub && onQ2)) return 0.10f + 0.18f * metal;
            if (fillBar && onQ3) return 0.10f + 0.18f * fill * metal;
            break;
        default:
            if (fillBar && beatIndex >= q3Beat && (offbeat || lateSub)) return 0.06f + 0.18f * fill * metal;
            break;
        }
        break;

    case LANE_COWBELL:
        switch (controls.genre) {
        case GENRE_BREAKBEAT:
        case GENRE_AMEN:
        case GENRE_JUNGLE:
            if (lateSub && (onQ1 || onQ3)) return 0.08f + 0.12f * perc;
            break;
        case GENRE_DISCO:
            if (offbeat) return 0.16f + 0.24f * perc;
            if (beatStart && (onQ1 || onQ3)) return 0.08f + 0.14f * perc;
            break;
        case GENRE_MOTORIK:
            if (beatStart && (beatIndex == 0 || onQ2)) return 0.10f + 0.18f * perc;
            if (offbeat) return 0.10f + 0.16f * perc;
            break;
        case GENRE_BOSSA:
            if ((beatIndex == 0 || onQ2) && lateSub) return 0.16f + 0.18f * perc;
            if ((onQ1 || onQ3) && offbeat) return 0.16f + 0.20f * perc;
            break;
        case GENRE_AFRO:
            if (offbeat || lateSub) return 0.14f + 0.20f * perc;
            break;
        default:
            if (fillBar && beatIndex >= q2Beat && offbeat) return 0.06f + 0.16f * fill * perc;
            break;
        }
        break;

    case LANE_CLAVE:
        switch (controls.genre) {
        case GENRE_BOSSA:
            if (beatIndex == 0 && beatStart) return 0.26f + 0.16f * perc;
            if (onQ1 && offbeat) return 0.22f + 0.16f * perc;
            if (onQ2 && lateSub) return 0.22f + 0.16f * perc;
            if (onQ3 && beatStart) return 0.24f + 0.16f * perc;
            break;
        case GENRE_AFRO:
            if (beatIndex == 0 && beatStart) return 0.20f + 0.18f * perc;
            if (onQ1 && lateSub) return 0.18f + 0.18f * perc;
            if (onQ2 && offbeat) return 0.22f + 0.18f * perc;
            if (onQ3 && beatStart) return 0.18f + 0.16f * perc;
            break;
        case GENRE_SHUFFLE:
            if (onQ1 && lateSub) return 0.10f + 0.14f * perc;
            if (onQ3 && offbeat) return 0.10f + 0.14f * perc;
            break;
        default:
            if (fillBar && onQ3 && !beatStart) return 0.06f + 0.14f * fill * perc;
            break;
        }
        break;

    default:
        break;
    }

    return 0.0f;
}

[[nodiscard]] float styleEuclidBias(const Controls& controls, const int lane) {
    switch (controls.styleMode) {
    case StyleModeId::reel:
        switch (lane) {
        case LANE_KICK:
        case LANE_SNARE:
        case LANE_CLAP:
            return 0.82f;
        case LANE_CLOSED_HAT:
        case LANE_OPEN_HAT:
            return 1.04f;
        case LANE_COWBELL:
        case LANE_CLAVE:
            return 0.58f;
        default:
            return 0.90f;
        }
    case StyleModeId::waltz:
        switch (lane) {
        case LANE_KICK: return 0.74f;
        case LANE_SNARE:
        case LANE_CLAP: return 0.68f;
        case LANE_CLOSED_HAT:
        case LANE_OPEN_HAT: return 0.92f;
        default: return 0.88f;
        }
    case StyleModeId::jig:
        switch (lane) {
        case LANE_CLOSED_HAT:
        case LANE_OPEN_HAT: return 1.10f;
        case LANE_COWBELL:
        case LANE_CLAVE: return 0.92f;
        default: return 0.94f;
        }
    case StyleModeId::slipJig:
        switch (lane) {
        case LANE_CLOSED_HAT:
        case LANE_OPEN_HAT: return 1.14f;
        case LANE_COWBELL:
        case LANE_CLAVE: return 0.94f;
        default: return 0.92f;
        }
    case StyleModeId::diddley:
        switch (lane) {
        case LANE_KICK: return 0.84f;
        case LANE_SNARE:
        case LANE_CLAP: return 0.72f;
        case LANE_CLOSED_HAT:
        case LANE_OPEN_HAT: return 0.74f;
        case LANE_COWBELL:
        case LANE_CLAVE: return 1.34f;
        default: return 0.78f;
        }
    case StyleModeId::straight:
    case StyleModeId::autoMode:
    case StyleModeId::count:
    default:
        return 1.0f;
    }
}

[[nodiscard]] int euclidPulsesForLane(const Controls& controls,
                                      int lane,
                                      int stepsPerBar,
                                      bool fillBar,
                                      Rng& rng) {
    const float density = controls.density;
    const float variation = controls.variation;
    const float fill = controls.fill;
    const float macro = laneMacro(controls, lane);

    float desiredHits = 0.0f;
    switch (lane) {
    case LANE_KICK:
        desiredHits = 1.0f + ((controls.genre == GENRE_DISCO || controls.genre == GENRE_MOTORIK)
            ? 3.0f
            : (isBreakbeatFamily(controls.genre)
                ? 2.4f
                : (controls.genre == GENRE_HIPHOP ? 1.2f : (controls.genre == GENRE_JAZZ ? 2.2f : 1.6f)))) * density * macro;
        break;
    case LANE_CLAP:
        desiredHits = ((controls.genre == GENRE_DISCO || controls.genre == GENRE_ELECTRO)
            ? 1.6f
            : (isBreakbeatFamily(controls.genre) ? 0.45f : (controls.genre == GENRE_JAZZ ? 0.12f : 0.8f))) * density * macro;
        break;
    case LANE_SNARE:
        desiredHits = 1.0f + (isBreakbeatFamily(controls.genre)
            ? 1.8f
            : (controls.genre == GENRE_JAZZ ? 1.6f : 1.0f)) * density * macro * (0.4f + 0.6f * variation);
        break;
    case LANE_CRASH:
        desiredHits = fillBar ? (0.08f + 0.45f * fill * macro) : (0.02f + 0.10f * macro);
        break;
    case LANE_CLOSED_HAT:
        desiredHits = (stepsPerBar * (isBreakbeatFamily(controls.genre)
            ? (0.34f + 0.58f * density * macro)
            : (controls.genre == GENRE_HIPHOP
                ? (0.16f + 0.42f * density * macro)
                : (controls.genre == GENRE_JAZZ
                    ? (0.32f + 0.40f * density * macro)
                    : (0.20f + 0.55f * density * macro))))) +
            (stepsPerBar >= 16 ? 1.5f : 0.0f);
        break;
    case LANE_OPEN_HAT:
        desiredHits = (isBreakbeatFamily(controls.genre) ? 0.7f : (controls.genre == GENRE_JAZZ ? 0.6f : 0.4f)) +
            1.5f * density * macro;
        break;
    case LANE_LOW_TOM:
    case LANE_HIGH_TOM:
        desiredHits = fillBar ? (0.4f + 2.4f * fill * macro) : 0.0f;
        break;
    case LANE_BASH:
        desiredHits = fillBar
            ? (0.2f + 1.4f * fill * macro)
            : (((controls.genre == GENRE_ELECTRO) || (controls.genre == GENRE_DUB) || (controls.genre == GENRE_MOTORIK) ||
                isBreakbeatFamily(controls.genre))
                ? (0.15f + 0.75f * variation * macro)
                : (0.05f + 0.35f * variation * macro));
        break;
    case LANE_COWBELL:
        if (controls.genre == GENRE_DISCO || controls.genre == GENRE_MOTORIK) {
            desiredHits = 0.8f + 3.0f * density * macro;
        } else if (controls.genre == GENRE_BOSSA || controls.genre == GENRE_AFRO) {
            desiredHits = 0.8f + 2.4f * density * macro;
        } else {
            desiredHits = 0.2f + 1.0f * density * variation * macro;
        }
        break;
    case LANE_CLAVE:
        if (controls.genre == GENRE_BOSSA || controls.genre == GENRE_AFRO) {
            desiredHits = 0.8f + 2.0f * density * macro;
        } else if (controls.genre == GENRE_SHUFFLE) {
            desiredHits = 0.4f + 1.4f * density * macro;
        } else {
            desiredHits = 0.15f + 0.8f * variation * macro;
        }
        break;
    default:
        break;
    }

    desiredHits *= styleEuclidBias(controls, lane);
    if (lane != LANE_KICK && lane != LANE_SNARE && !fillBar) {
        desiredHits *= 0.22f + 0.78f * density;
    }
    const int jitter = static_cast<int>(std::lround((rng.nextFloat() * 2.0f - 1.0f) * (variation * 2.0f)));
    return clampi(static_cast<int>(std::lround(desiredHits)) + jitter, 0, stepsPerBar);
}

[[nodiscard]] float lowDensityAnchorScale(const Controls& controls,
                                          const int lane,
                                          const int beatIndex,
                                          const int subIndex,
                                          const int stepsPerBeat,
                                          const ::downspout::Meter& meter) {
    const float density = clampf(controls.density, 0.0f, 1.0f);
    const bool beatStart = subIndex == 0;
    const bool hardDownbeat = lane == LANE_KICK && beatIndex == 0 && beatStart;
    const bool backbeat = (lane == LANE_SNARE || lane == LANE_CLAP) && beatStart && isBackbeatBeat(meter, beatIndex);

    if (hardDownbeat || backbeat) {
        return 1.0f;
    }

    switch (lane) {
    case LANE_CLOSED_HAT:
    case LANE_OPEN_HAT:
        return 0.12f + 0.88f * density;
    case LANE_COWBELL:
    case LANE_CLAVE:
    case LANE_CRASH:
    case LANE_BASH:
        return 0.10f + 0.90f * density;
    case LANE_LOW_TOM:
    case LANE_HIGH_TOM:
        return 0.16f + 0.84f * density;
    case LANE_KICK:
        return 0.28f + 0.72f * density;
    case LANE_SNARE:
    case LANE_CLAP:
        return 0.22f + 0.78f * density;
    default:
        return 0.18f + 0.82f * density;
    }
}

[[nodiscard]] float euclidInfluenceForLane(const Controls& controls, int lane, bool fillBar) {
    switch (lane) {
    case LANE_KICK:
        return 0.10f + 0.35f * controls.variation * controls.kickAmt;
    case LANE_CLAP:
    case LANE_SNARE:
        return 0.10f + 0.32f * controls.variation * controls.backbeatAmt;
    case LANE_CRASH:
        return 0.03f + 0.10f * controls.variation * controls.metalAmt + (fillBar ? 0.06f : 0.0f);
    case LANE_CLOSED_HAT:
        return 0.24f + 0.52f * controls.variation * controls.hatAmt;
    case LANE_OPEN_HAT:
        return 0.16f + 0.42f * controls.variation * controls.hatAmt;
    case LANE_LOW_TOM:
    case LANE_HIGH_TOM:
        return 0.10f + 0.44f * controls.variation * controls.tomAmt + (fillBar ? 0.24f : 0.0f);
    case LANE_BASH:
        return 0.10f + 0.36f * controls.variation * controls.metalAmt + (fillBar ? 0.28f : 0.0f);
    case LANE_COWBELL:
        return 0.18f + 0.34f * controls.variation * controls.auxAmt;
    case LANE_CLAVE:
        return 0.16f + 0.32f * controls.variation * controls.auxAmt + (fillBar ? 0.10f : 0.0f);
    default:
        return 0.20f;
    }
}

[[nodiscard]] int stepVelocity(const Controls& controls,
                               const ::downspout::Meter& meter,
                               int lane,
                               int beatIndex,
                               int subIndex,
                               int stepsPerBeat,
                               bool fillBar,
                               Rng& rng,
                               std::uint8_t* flagsOut) {
    int velocity = 90;
    std::uint8_t flags = 0;
    const StepRole role = styleStepRole(controls.styleMode, meter, beatIndex, subIndex, stepsPerBeat);
    const StylePulseInfo pulse = stylePulseInfo(controls.styleMode, meter, beatIndex, subIndex, stepsPerBeat);

    switch (lane) {
    case LANE_KICK: velocity = 108; break;
    case LANE_CLAP: velocity = 104; break;
    case LANE_SNARE: velocity = 100; break;
    case LANE_CRASH: velocity = 96; break;
    case LANE_CLOSED_HAT: velocity = 82; break;
    case LANE_OPEN_HAT: velocity = 76; break;
    case LANE_LOW_TOM: velocity = 92; break;
    case LANE_HIGH_TOM: velocity = 94; break;
    case LANE_BASH: velocity = 110; break;
    case LANE_COWBELL: velocity = 88; break;
    case LANE_CLAVE: velocity = 90; break;
    default: break;
    }

    if (subIndex == 0) {
        velocity += 8;
    } else {
        velocity -= 4;
    }

    if (controls.styleMode != StyleModeId::autoMode) {
        if (role == StepRole::primary) {
            velocity += (lane == LANE_KICK || lane == LANE_SNARE || lane == LANE_CLAP) ? 9 : 6;
            flags |= STEP_FLAG_ACCENT;
        } else if (role == StepRole::secondary) {
            velocity += (lane == LANE_SNARE || lane == LANE_CLAP || lane == LANE_COWBELL) ? 5 : 2;
        } else if (role == StepRole::pickup) {
            velocity += 4;
        }

        if (controls.styleMode == StyleModeId::diddley && (lane == LANE_CLAVE || lane == LANE_COWBELL)) {
            velocity += 10;
            flags |= STEP_FLAG_ACCENT;
        }

        if ((controls.styleMode == StyleModeId::jig || controls.styleMode == StyleModeId::slipJig) &&
            pulse.pulseStart &&
            pulse.pulseIndex > 0 &&
            (lane == LANE_SNARE || lane == LANE_CLAP)) {
            velocity += 6;
        }
    } else {
        if ((lane == LANE_SNARE || lane == LANE_CLAP) && isBackbeatBeat(meter, beatIndex) && subIndex == 0) {
            velocity += 10;
            flags |= STEP_FLAG_ACCENT;
        }
        if (::downspout::meterHasCompoundFeel(meter) && subIndex == 0 && isPulseStartBeat(meter, beatIndex)) {
            velocity += (lane == LANE_KICK || lane == LANE_SNARE || lane == LANE_CLAP) ? 8 : 5;
            flags |= STEP_FLAG_ACCENT;
        }
        if (isTripleMeter(meter) && subIndex == 0) {
            if (beatIndex == 0) {
                velocity += 7;
                flags |= STEP_FLAG_ACCENT;
            } else if (beatIndex == 1 && (lane == LANE_SNARE || lane == LANE_CLAP || lane == LANE_COWBELL)) {
                velocity += 4;
            }
        }
    }
    if (lane == LANE_KICK && beatIndex == 0 && subIndex == 0) {
        velocity += 8;
        flags |= STEP_FLAG_ACCENT;
    }
    if ((lane == LANE_COWBELL || lane == LANE_CLAVE) && subIndex != 0) {
        velocity += 4;
    }
    if (lane == LANE_BASH && (beatIndex == 0 || fillBar)) {
        velocity += 6;
        flags |= STEP_FLAG_ACCENT;
    }
    if (fillBar && (lane == LANE_CRASH || lane == LANE_LOW_TOM || lane == LANE_HIGH_TOM)) {
        velocity += 6;
        flags |= STEP_FLAG_FILL;
    }
    if (fillBar && (lane == LANE_BASH || lane == LANE_COWBELL || lane == LANE_CLAVE)) {
        velocity += 4;
        flags |= STEP_FLAG_FILL;
    }

    velocity += rng.nextInt(-6, 6);
    velocity = clampi(velocity, 1, 127);

    if (flagsOut != nullptr) {
        *flagsOut = flags;
    }
    return velocity;
}

void clearStepHit(PatternState& pattern, int lane, int step) {
    if (lane < 0 || lane >= kLaneCount || step < 0 || step >= pattern.totalSteps) {
        return;
    }
    pattern.lanes[lane].steps[step].velocity = 0;
    pattern.lanes[lane].steps[step].flags = 0;
}

void setStepHit(PatternState& pattern, int lane, int step, int velocity, std::uint8_t flags) {
    if (lane < 0 || lane >= kLaneCount || step < 0 || step >= pattern.totalSteps) {
        return;
    }
    DrumStepCell& cell = pattern.lanes[lane].steps[step];
    if (velocity > cell.velocity) {
        cell.velocity = static_cast<std::uint8_t>(clampi(velocity, 1, 127));
    }
    cell.flags = static_cast<std::uint8_t>(cell.flags | flags);
}

[[nodiscard]] int stepForSixteenthSlot(const PatternState& pattern, const int barStart, const int slot) {
    if (pattern.stepsPerBar <= 0) {
        return barStart;
    }
    return barStart + clampi(static_cast<int>(std::lround(static_cast<float>(slot) *
                                                          static_cast<float>(pattern.stepsPerBar) / 16.0f)),
                             0,
                             pattern.stepsPerBar - 1);
}

void setSlotHit(PatternState& pattern,
                const int barStart,
                const int lane,
                const int slot,
                const int velocity,
                const std::uint8_t flags = 0) {
    setStepHit(pattern, lane, stepForSixteenthSlot(pattern, barStart, slot), velocity, flags);
}

void applyBackbeatBreakHats(PatternState& pattern,
                            const int barStart,
                            const Controls& controls,
                            const bool dense) {
    const int stride = dense ? 1 : 2;
    for (int slot = 0; slot < 16; slot += stride) {
        const int velocity = dense
            ? (62 + ((slot % 4) == 0 ? 8 : 0))
            : (78 + ((slot % 4) == 0 ? 7 : 0));
        setSlotHit(pattern, barStart, LANE_CLOSED_HAT, slot, velocity, 0);
    }

    if (controls.hatAmt > 0.20f) {
        const int openHatVelocity = controls.genre == GENRE_HIPHOP ? 62 : 70;
        setSlotHit(pattern, barStart, LANE_OPEN_HAT, 3, openHatVelocity, 0);
        setSlotHit(pattern, barStart, LANE_OPEN_HAT, 7, openHatVelocity + 2, 0);
        setSlotHit(pattern, barStart, LANE_OPEN_HAT, 11, openHatVelocity, 0);
        setSlotHit(pattern, barStart, LANE_OPEN_HAT, 15, openHatVelocity + 4, 0);
    }
}

void applyGenreSignatureToBar(PatternState& pattern, const Controls& controls, const int barIndex) {
    if (controls.styleMode != StyleModeId::autoMode ||
        pattern.stepsPerBar <= 0 ||
        barIndex < 0 ||
        barIndex >= pattern.bars) {
        return;
    }

    const int barStart = barIndex * pattern.stepsPerBar;

    switch (controls.genre) {
    case GENRE_ROCK:
        for (int slot = 0; slot < 16; slot += 2) {
            setSlotHit(pattern, barStart, LANE_CLOSED_HAT, slot, 82 + ((slot % 4) == 0 ? 10 : 0), 0);
        }
        setSlotHit(pattern, barStart, LANE_KICK, 0, 124, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_KICK, 8, 118, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_KICK, 14, 98, 0);
        setSlotHit(pattern, barStart, LANE_SNARE, 4, 122, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_SNARE, 12, 124, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_CRASH, 0, 94, STEP_FLAG_ACCENT);
        break;

    case GENRE_JAZZ:
        for (int slot = 0; slot < 16; slot += 4) {
            setSlotHit(pattern, barStart, LANE_KICK, slot, 66 + (slot == 0 ? 8 : 0), slot == 0 ? STEP_FLAG_ACCENT : 0);
            setSlotHit(pattern, barStart, LANE_CLOSED_HAT, slot, 82 + (slot == 0 ? 8 : 0), slot == 0 ? STEP_FLAG_ACCENT : 0);
            setSlotHit(pattern, barStart, LANE_CLOSED_HAT, slot + 3, 70, 0);
        }
        setSlotHit(pattern, barStart, LANE_OPEN_HAT, 4, 68, 0);
        setSlotHit(pattern, barStart, LANE_OPEN_HAT, 12, 72, 0);
        setSlotHit(pattern, barStart, LANE_SNARE, 6, 62, 0);
        setSlotHit(pattern, barStart, LANE_SNARE, 10, 58, 0);
        break;

    case GENRE_AMEN:
        applyBackbeatBreakHats(pattern, barStart, controls, false);
        setSlotHit(pattern, barStart, LANE_KICK, 0, 122, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_KICK, 6, 108, 0);
        setSlotHit(pattern, barStart, LANE_KICK, 10, 112, 0);
        setSlotHit(pattern, barStart, LANE_SNARE, 4, 119, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_SNARE, 12, 122, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_SNARE, 3, 58, 0);
        setSlotHit(pattern, barStart, LANE_SNARE, 7, 66, 0);
        setSlotHit(pattern, barStart, LANE_SNARE, 11, 58, 0);
        setSlotHit(pattern, barStart, LANE_SNARE, 15, 72, 0);
        setSlotHit(pattern, barStart, LANE_CRASH, 0, 86, STEP_FLAG_ACCENT);
        break;

    case GENRE_JUNGLE:
        applyBackbeatBreakHats(pattern, barStart, controls, true);
        setSlotHit(pattern, barStart, LANE_KICK, 0, 122, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_KICK, 6, 112, 0);
        setSlotHit(pattern, barStart, LANE_KICK, 10, 116, 0);
        setSlotHit(pattern, barStart, LANE_KICK, 14, 96, 0);
        setSlotHit(pattern, barStart, LANE_SNARE, 4, 121, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_SNARE, 12, 124, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_SNARE, 3, 54, 0);
        setSlotHit(pattern, barStart, LANE_SNARE, 7, 70, 0);
        setSlotHit(pattern, barStart, LANE_SNARE, 11, 62, 0);
        setSlotHit(pattern, barStart, LANE_SNARE, 15, 76, 0);
        setSlotHit(pattern, barStart, LANE_BASH, 15, 98, 0);
        break;

    case GENRE_BREAKBEAT:
        applyBackbeatBreakHats(pattern, barStart, controls, false);
        setSlotHit(pattern, barStart, LANE_KICK, 0, 120, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_KICK, 3, 96, 0);
        setSlotHit(pattern, barStart, LANE_KICK, 10, 110, 0);
        setSlotHit(pattern, barStart, LANE_SNARE, 4, 116, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_SNARE, 12, 118, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_SNARE, 7, 64, 0);
        setSlotHit(pattern, barStart, LANE_SNARE, 15, 70, 0);
        break;

    case GENRE_HIPHOP:
        for (int slot = 0; slot < 16; slot += 2) {
            setSlotHit(pattern, barStart, LANE_CLOSED_HAT, slot, 68 + ((slot % 4) == 0 ? 8 : 0), 0);
        }
        setSlotHit(pattern, barStart, LANE_KICK, 0, 120, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_KICK, 7, 94, 0);
        setSlotHit(pattern, barStart, LANE_KICK, 10, 108, 0);
        setSlotHit(pattern, barStart, LANE_SNARE, 4, 116, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_SNARE, 12, 118, STEP_FLAG_ACCENT);
        setSlotHit(pattern, barStart, LANE_SNARE, 15, 52, 0);
        setSlotHit(pattern, barStart, LANE_OPEN_HAT, 6, 58, 0);
        setSlotHit(pattern, barStart, LANE_OPEN_HAT, 14, 60, 0);
        break;

    default:
        break;
    }
}

[[nodiscard]] bool diddleyAccentSlot(const int phraseBar, const int slot) {
    if ((phraseBar % 2) == 0) {
        return slot == 0 || slot == 6 || slot == 12;
    }
    return slot == 4 || slot == 8;
}

void applyDiddleyStyleOverlayToBar(PatternState& pattern, const Controls& controls, const int bar) {
    if (controls.styleMode != StyleModeId::diddley ||
        pattern.stepsPerBar <= 0 ||
        pattern.totalSteps <= 0 ||
        bar < 0 ||
        bar >= pattern.bars) {
        return;
    }

    const int barStart = bar * pattern.stepsPerBar;
    const int phraseBar = bar % 2;
    for (int slot = 0; slot < 16; slot += 2) {
        setSlotHit(pattern,
                   barStart,
                   LANE_CLOSED_HAT,
                   slot,
                   60 + ((slot % 4) == 0 ? 8 : 0),
                   0);
    }

    for (int slot = 0; slot < 16; ++slot) {
        if (!diddleyAccentSlot(phraseBar, slot)) {
            continue;
        }

        const bool firstAccent = (phraseBar == 0 && slot == 0);
        const bool tailAccent = (phraseBar == 1 && slot == 8) || (phraseBar == 0 && slot == 12);
        const int accentVelocity = firstAccent ? 118 : (tailAccent ? 112 : 106);
        setSlotHit(pattern, barStart, LANE_CLAVE, slot, accentVelocity, STEP_FLAG_ACCENT);
        if (firstAccent || (phraseBar == 0 && slot == 12) || (phraseBar == 1 && slot == 8)) {
            setSlotHit(pattern, barStart, LANE_KICK, slot, firstAccent ? 116 : 102, firstAccent ? STEP_FLAG_ACCENT : 0);
        }
        if ((phraseBar == 0 && slot == 6) || (phraseBar == 1 && slot == 4)) {
            setSlotHit(pattern, barStart, LANE_SNARE, slot, 96, 0);
        }
    }
}

void applyDiddleyStyleOverlay(PatternState& pattern, const Controls& controls) {
    for (int bar = 0; bar < pattern.bars; ++bar) {
        applyDiddleyStyleOverlayToBar(pattern, controls, bar);
    }
}

void applyGenreSignature(PatternState& pattern, const Controls& controls) {
    if (controls.styleMode != StyleModeId::autoMode ||
        (!isBreakbeatFamily(controls.genre) &&
         controls.genre != GENRE_ROCK &&
         controls.genre != GENRE_HIPHOP &&
         controls.genre != GENRE_JAZZ)) {
        return;
    }

    for (int bar = 0; bar < pattern.bars; ++bar) {
        applyGenreSignatureToBar(pattern, controls, bar);
    }
}

void clearBarHits(PatternState& pattern, int barIndex) {
    if (barIndex < 0 || barIndex >= pattern.bars) {
        return;
    }

    const int barStart = barIndex * pattern.stepsPerBar;
    const int barEnd = clampi(barStart + pattern.stepsPerBar, 0, pattern.totalSteps);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (int step = barStart; step < barEnd; ++step) {
            pattern.lanes[lane].steps[step].velocity = 0;
            pattern.lanes[lane].steps[step].flags = 0;
        }
    }
}

void clearPattern(PatternState& pattern, const Controls& controls) {
    std::memset(&pattern, 0, sizeof(PatternState));
    pattern.version = kPatternStateVersion;
    pattern.bars = controls.bars;
    pattern.stepsPerBeat = stepsPerBeatForResolution(controls.resolution);
    pattern.meter = ::downspout::Meter {};
    pattern.stepsPerBar = ::downspout::meterStepsPerBar(pattern.meter, pattern.stepsPerBeat);
    pattern.totalSteps = clampi(pattern.bars * pattern.stepsPerBar, 1, kMaxPatternSteps);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        pattern.lanes[lane].midiNote = noteForLane(controls.kitMap, lane);
    }
}

void clearPattern(PatternState& pattern, const Controls& controls, const ::downspout::Meter& meter) {
    clearPattern(pattern, controls);
    pattern.meter = ::downspout::sanitizeMeter(meter);
    pattern.stepsPerBar = ::downspout::meterStepsPerBar(pattern.meter, pattern.stepsPerBeat);
    pattern.totalSteps = clampi(pattern.bars * pattern.stepsPerBar, 1, kMaxPatternSteps);
}

void copyPatternPrefix(PatternState& target, const PatternState& source, int prefixSteps) {
    if (prefixSteps <= 0) {
        return;
    }
    for (int lane = 0; lane < kLaneCount; ++lane) {
        std::memcpy(target.lanes[lane].steps.data(),
                    source.lanes[lane].steps.data(),
                    static_cast<std::size_t>(prefixSteps) * sizeof(DrumStepCell));
    }
}

void applyFillOverlayToBar(PatternState& pattern,
                           const Controls& controls,
                           std::uint32_t seedValue,
                           int barIndex) {
    if (pattern.bars <= 0 || pattern.totalSteps <= 0 || controls.fill < 0.08f) {
        return;
    }

    Rng rng;
    const int stepsPerBar = pattern.stepsPerBar;
    const int stepsPerBeat = pattern.stepsPerBeat;
    const int targetBar = clampi(barIndex, 0, pattern.bars - 1);
    const int barStart = targetBar * stepsPerBar;
    const int barEnd = clampi(barStart + stepsPerBar, 0, pattern.totalSteps);
    const int fillBeats = preferredFillBeats(pattern.meter, controls.fill);
    const int fillSteps = clampi(fillBeats * stepsPerBeat, stepsPerBeat, stepsPerBar);
    const int zoneStart = clampi(barEnd - fillSteps, barStart, barEnd);
    int motif = 0;

    rng.seed(seedValue ^ 0xC001D00Du);

    switch (controls.genre) {
    case GENRE_ROCK:
    case GENRE_SHUFFLE:
    case GENRE_HIPHOP:
    case GENRE_JAZZ:
        motif = rng.nextInt(0, 1);
        break;
    case GENRE_DISCO:
    case GENRE_MOTORIK:
        motif = 2 + rng.nextInt(0, 1);
        break;
    case GENRE_ELECTRO:
    case GENRE_DUB:
    case GENRE_BREAKBEAT:
    case GENRE_AMEN:
    case GENRE_JUNGLE:
        motif = 1 + rng.nextInt(0, 2);
        break;
    case GENRE_BOSSA:
    case GENRE_AFRO:
        motif = (rng.nextFloat() < 0.65f) ? 2 : 3;
        break;
    default:
        motif = rng.nextInt(0, 3);
        break;
    }

    for (int step = zoneStart; step < barEnd; ++step) {
        clearStepHit(pattern, LANE_OPEN_HAT, step);
        clearStepHit(pattern, LANE_CRASH, step);
        clearStepHit(pattern, LANE_BASH, step);
        if (controls.fill > 0.45f) {
            if ((step - zoneStart) % 2 == 1) {
                clearStepHit(pattern, LANE_CLOSED_HAT, step);
            }
            if ((step % stepsPerBeat) != 0 && controls.fill > 0.60f) {
                clearStepHit(pattern, LANE_KICK, step);
            }
        }
    }

    switch (motif) {
    case 0: {
        int index = 0;
        const int stride = controls.fill > 0.65f ? 1 : 2;
        for (int step = zoneStart; step < barEnd; step += stride) {
            const int lane = (index % 2 == 0) ? LANE_LOW_TOM : LANE_HIGH_TOM;
            setStepHit(pattern,
                       lane,
                       step,
                       92 + index * 5,
                       static_cast<std::uint8_t>(STEP_FLAG_FILL | (index > 1 ? STEP_FLAG_ACCENT : 0)));
            index += 1;
        }
        setStepHit(pattern,
                   (controls.genre == GENRE_ELECTRO || controls.genre == GENRE_MOTORIK) ? LANE_BASH : LANE_CRASH,
                   barEnd - 1,
                   114,
                   static_cast<std::uint8_t>(STEP_FLAG_FILL | STEP_FLAG_ACCENT));
        break;
    }

    case 1: {
        int rise = 0;
        for (int step = zoneStart; step < barEnd; ++step) {
            setStepHit(pattern, LANE_SNARE, step, 88 + rise * 4, STEP_FLAG_FILL);
            if ((step - zoneStart) % 2 == 1) {
                setStepHit(pattern, LANE_CLAP, step, 84 + rise * 3, STEP_FLAG_FILL);
            }
            rise += 1;
        }
        setStepHit(pattern,
                   LANE_HIGH_TOM,
                   clampi(barEnd - 2, zoneStart, barEnd - 1),
                   108,
                   static_cast<std::uint8_t>(STEP_FLAG_FILL | STEP_FLAG_ACCENT));
        if (controls.metalAmt > 0.20f) {
            setStepHit(pattern,
                       LANE_BASH,
                       barEnd - 1,
                       116,
                       static_cast<std::uint8_t>(STEP_FLAG_FILL | STEP_FLAG_ACCENT));
        }
        break;
    }

    case 2: {
        int toggle = 0;
        for (int step = zoneStart; step < barEnd; ++step) {
            const int stepInBar = step - barStart;
            if (isOffbeatStep(stepInBar, stepsPerBeat) || ((step - zoneStart) % 2 == 0)) {
                setStepHit(pattern, toggle % 2 == 0 ? LANE_COWBELL : LANE_CLAVE, step, 90 + toggle * 2, STEP_FLAG_FILL);
                toggle += 1;
            }
        }
        setStepHit(pattern, LANE_LOW_TOM, zoneStart, 96, STEP_FLAG_FILL);
        setStepHit(pattern,
                   LANE_HIGH_TOM,
                   clampi(barEnd - 2, zoneStart, barEnd - 1),
                   104,
                   static_cast<std::uint8_t>(STEP_FLAG_FILL | STEP_FLAG_ACCENT));
        break;
    }

    case 3:
    default: {
        const int bashStep = clampi(zoneStart + fillSteps / 2, zoneStart, barEnd - 1);
        int index = 0;
        setStepHit(pattern, LANE_BASH, bashStep, 114, static_cast<std::uint8_t>(STEP_FLAG_FILL | STEP_FLAG_ACCENT));
        for (int step = zoneStart; step < barEnd; step += 2) {
            const int lane = (index % 3 == 0) ? LANE_HIGH_TOM : ((index % 3 == 1) ? LANE_COWBELL : LANE_CLAVE);
            setStepHit(pattern, lane, step, 92 + index * 4, STEP_FLAG_FILL);
            index += 1;
        }
        setStepHit(pattern, LANE_CLAVE, clampi(barEnd - 2, zoneStart, barEnd - 1), 98, STEP_FLAG_FILL);
        setStepHit(pattern,
                   LANE_HIGH_TOM,
                   barEnd - 1,
                   108,
                   static_cast<std::uint8_t>(STEP_FLAG_FILL | STEP_FLAG_ACCENT));
        break;
    }
    }
}

void applyFillOverlay(PatternState& pattern, const Controls& controls, std::uint32_t seedValue) {
    applyFillOverlayToBar(pattern, controls, seedValue, pattern.bars - 1);
}

void buildBar(PatternState& pattern,
              const Controls& controls,
              int barIndex,
              bool fillBar,
              std::uint32_t seedValue) {
    const int stepsPerBar = pattern.stepsPerBar;
    const int stepsPerBeat = pattern.stepsPerBeat;
    Rng rng;
    rng.seed(seedValue);

    int pulses[kLaneCount];
    int offsets[kLaneCount];
    for (int lane = 0; lane < kLaneCount; ++lane) {
        pulses[lane] = euclidPulsesForLane(controls, lane, stepsPerBar, fillBar, rng);
        offsets[lane] = pulses[lane] > 0 ? rng.nextInt(0, stepsPerBar - 1) : 0;
    }

    for (int step = 0; step < stepsPerBar; ++step) {
        const int globalStep = barIndex * stepsPerBar + step;
        if (globalStep >= pattern.totalSteps) {
            break;
        }

        const int beatIndex = step / stepsPerBeat;
        const int subIndex = step % stepsPerBeat;

        for (int lane = 0; lane < kLaneCount; ++lane) {
            float anchorProbabilityValue =
                anchorProbability(controls, pattern.meter, lane, barIndex, beatIndex, subIndex, stepsPerBeat, fillBar);
            if (!fillBar) {
                anchorProbabilityValue *= lowDensityAnchorScale(controls, lane, beatIndex, subIndex, stepsPerBeat, pattern.meter);
            }
            const bool anchor =
                anchorProbabilityValue > 0.0f && rng.nextFloat() < clampf(anchorProbabilityValue, 0.0f, 1.0f);
            const bool euclid = euclidHit(step, pulses[lane], offsets[lane], stepsPerBar);
            bool hit = anchor;

            if (!hit && euclid) {
                const float chance = clampf(euclidInfluenceForLane(controls, lane, fillBar), 0.0f, 1.0f);
                hit = rng.nextFloat() < chance;
            }

            if (!hit) {
                continue;
            }

            std::uint8_t flags = 0;
            const int velocity =
                stepVelocity(controls, pattern.meter, lane, beatIndex, subIndex, stepsPerBeat, fillBar, rng, &flags);
            pattern.lanes[lane].steps[globalStep].velocity = static_cast<std::uint8_t>(velocity);
            pattern.lanes[lane].steps[globalStep].flags = flags;
        }
    }
}

void applyFuguePulseOverlay(PatternState& pattern, const Controls& controls) {
    if (controls.genre != GENRE_FUGUE ||
        pattern.totalSteps <= 0 ||
        pattern.stepsPerBeat <= 0 ||
        pattern.stepsPerBar <= 0) {
        return;
    }

    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (int step = 0; step < pattern.totalSteps; ++step) {
            clearStepHit(pattern, lane, step);
        }
    }

    for (int bar = 0; bar < pattern.bars; ++bar) {
        const int barStart = bar * pattern.stepsPerBar;
        for (int beat = 0; beat < pattern.meter.numerator; ++beat) {
            const int step = barStart + beat * pattern.stepsPerBeat;
            if (step >= pattern.totalSteps) {
                break;
            }

            const bool downbeat = beat == 0;
            setStepHit(pattern, LANE_CLOSED_HAT, step, downbeat ? 82 : 66, downbeat ? STEP_FLAG_ACCENT : 0);
            if (downbeat) {
                setStepHit(pattern, LANE_KICK, step, 88, STEP_FLAG_ACCENT);
            } else if (beat == pattern.meter.numerator / 2 && controls.auxAmt > 0.18f) {
                setStepHit(pattern, LANE_CLAVE, step, 56, 0);
            }
        }
    }
}

void cleanupPattern(PatternState& pattern, const Controls& controls) {
    if (pattern.totalSteps <= 0) {
        return;
    }

    applyFuguePulseOverlay(pattern, controls);
    if (controls.genre == GENRE_FUGUE) {
        return;
    }

    const int stepsPerBar = pattern.stepsPerBar;
    const int stepsPerBeat = pattern.stepsPerBeat;

    for (int step = 0; step < pattern.totalSteps; ++step) {
        DrumStepCell& closed = pattern.lanes[LANE_CLOSED_HAT].steps[step];
        DrumStepCell& open = pattern.lanes[LANE_OPEN_HAT].steps[step];
        DrumStepCell& cowbell = pattern.lanes[LANE_COWBELL].steps[step];
        DrumStepCell& clave = pattern.lanes[LANE_CLAVE].steps[step];
        DrumStepCell& clap = pattern.lanes[LANE_CLAP].steps[step];
        DrumStepCell& snare = pattern.lanes[LANE_SNARE].steps[step];

        if (closed.velocity > 0 && open.velocity > 0) {
            if (isOffbeatStep(step % stepsPerBar, stepsPerBeat)) {
                closed.velocity = 0;
                closed.flags = 0;
            } else {
                open.velocity = 0;
                open.flags = 0;
            }
        }
        if (cowbell.velocity > 0 && clave.velocity > 0) {
            if (controls.genre == GENRE_BOSSA || controls.genre == GENRE_AFRO) {
                cowbell.velocity = 0;
                cowbell.flags = 0;
            } else {
                clave.velocity = 0;
                clave.flags = 0;
            }
        }
        if (snare.velocity > 0 && clap.velocity > 0 &&
            controls.genre != GENRE_DISCO && controls.genre != GENRE_ELECTRO) {
            clap.velocity = 0;
            clap.flags = 0;
        }
    }

    for (int bar = 0; bar < pattern.bars; ++bar) {
        const int barStart = bar * stepsPerBar;
        const bool sectionStart = bar == 0;
        const bool fillResolution = bar == pattern.bars - 1;
        const int maxCrashHits = (sectionStart || fillResolution || controls.metalAmt > 0.70f) ? 1 : 0;
        int crashHits = 0;
        int bashHits = 0;
        for (int step = 0; step < stepsPerBar && (barStart + step) < pattern.totalSteps; ++step) {
            DrumStepCell& crash = pattern.lanes[LANE_CRASH].steps[barStart + step];
            DrumStepCell& bash = pattern.lanes[LANE_BASH].steps[barStart + step];
            if (crash.velocity > 0) {
                crashHits += 1;
                if (crashHits > maxCrashHits) {
                    crash.velocity = 0;
                    crash.flags = 0;
                }
            }
            if (bash.velocity > 0) {
                bashHits += 1;
                if (bashHits > 2) {
                    bash.velocity = 0;
                    bash.flags = 0;
                }
            }

            if (isFillZoneStep(step, stepsPerBar, stepsPerBeat, pattern.meter, controls.fill)) {
                const bool fillActivity =
                    pattern.lanes[LANE_LOW_TOM].steps[barStart + step].velocity > 0 ||
                    pattern.lanes[LANE_HIGH_TOM].steps[barStart + step].velocity > 0 ||
                    pattern.lanes[LANE_BASH].steps[barStart + step].velocity > 0 ||
                    pattern.lanes[LANE_COWBELL].steps[barStart + step].velocity > 0 ||
                    pattern.lanes[LANE_CLAVE].steps[barStart + step].velocity > 0;

                if (fillActivity) {
                    clearStepHit(pattern, LANE_OPEN_HAT, barStart + step);
                    if ((step - (stepsPerBar - stepsPerBeat)) % 2 != 0) {
                        clearStepHit(pattern, LANE_CLOSED_HAT, barStart + step);
                    }
                }
            }
        }

        const bool needsBackbeat = controls.backbeatAmt > 0.30f && controls.styleMode != StyleModeId::diddley;
        if (needsBackbeat) {
            std::array<int, ::downspout::kMaxMeterGroups> backbeatBeats {};
            const int backbeatCount = collectAccentBeatsForStyle(controls.styleMode, pattern.meter, backbeatBeats);
            for (int index = 0; index < backbeatCount; ++index) {
                const int backbeatStep = barStart + (backbeatBeats[static_cast<std::size_t>(index)] * stepsPerBeat);
                if (backbeatStep < pattern.totalSteps &&
                    pattern.lanes[LANE_SNARE].steps[backbeatStep].velocity == 0 &&
                    pattern.lanes[LANE_CLAP].steps[backbeatStep].velocity == 0) {
                    pattern.lanes[LANE_SNARE].steps[backbeatStep].velocity = static_cast<std::uint8_t>(102 + (index * 2));
                    pattern.lanes[LANE_SNARE].steps[backbeatStep].flags = STEP_FLAG_ACCENT;
                }
            }
        }
    }
}

}  // namespace

Controls clampControls(const Controls& raw) {
    Controls controls = raw;
    controls.genre = static_cast<GenreId>(clampi(static_cast<int>(controls.genre), 0, static_cast<int>(GenreId::count) - 1));
    controls.styleMode = static_cast<StyleModeId>(clampi(static_cast<int>(controls.styleMode), 0, static_cast<int>(StyleModeId::count) - 1));
    controls.channel = clampi(controls.channel, 1, 16);
    controls.kitMap = static_cast<KitMapId>(clampi(static_cast<int>(controls.kitMap), 0, static_cast<int>(KitMapId::count) - 1));
    controls.bars = clampi(controls.bars, kMinBars, kMaxBars);
    controls.resolution = static_cast<ResolutionId>(clampi(static_cast<int>(controls.resolution), 0, static_cast<int>(ResolutionId::count) - 1));
    controls.density = clampf(controls.density, 0.0f, 1.0f);
    controls.variation = clampf(controls.variation, 0.0f, 1.0f);
    controls.fill = clampf(controls.fill, 0.0f, 1.0f);
    controls.vary = clampf(controls.vary, 0.0f, 1.0f);
    controls.kickAmt = clampf(controls.kickAmt, 0.0f, 1.0f);
    controls.backbeatAmt = clampf(controls.backbeatAmt, 0.0f, 1.0f);
    controls.hatAmt = clampf(controls.hatAmt, 0.0f, 1.0f);
    controls.auxAmt = clampf(controls.auxAmt, 0.0f, 1.0f);
    controls.tomAmt = clampf(controls.tomAmt, 0.0f, 1.0f);
    controls.metalAmt = clampf(controls.metalAmt, 0.0f, 1.0f);
    controls.actionNew = clampi(controls.actionNew, 0, 1048576);
    controls.actionMutate = clampi(controls.actionMutate, 0, 1048576);
    controls.actionFill = clampi(controls.actionFill, 0, 1048576);
    return controls;
}

bool structuralControlsChanged(const Controls& a, const Controls& b) {
    return a.genre != b.genre ||
           a.styleMode != b.styleMode ||
           a.channel != b.channel ||
           a.kitMap != b.kitMap ||
           a.bars != b.bars ||
           a.resolution != b.resolution ||
           std::fabs(a.density - b.density) >= 0.0001f ||
           std::fabs(a.variation - b.variation) >= 0.0001f ||
           std::fabs(a.fill - b.fill) >= 0.0001f ||
           a.seed != b.seed ||
           std::fabs(a.kickAmt - b.kickAmt) >= 0.0001f ||
           std::fabs(a.backbeatAmt - b.backbeatAmt) >= 0.0001f ||
           std::fabs(a.hatAmt - b.hatAmt) >= 0.0001f ||
           std::fabs(a.auxAmt - b.auxAmt) >= 0.0001f ||
           std::fabs(a.tomAmt - b.tomAmt) >= 0.0001f ||
           std::fabs(a.metalAmt - b.metalAmt) >= 0.0001f;
}

int stepsPerBeatForResolution(ResolutionId resolution) {
    switch (resolution) {
    case RESOLUTION_4TH:
        return 1;
    case RESOLUTION_8TH:
        return 2;
    case RESOLUTION_16T:
        return 3;
    case RESOLUTION_16TH:
    default:
        return 4;
    }
}

void regeneratePattern(PatternState& pattern,
                       const Controls& rawControls,
                       const ::downspout::Meter& meter,
                       bool fillOnlyRefresh) {
    const Controls controls = clampControls(rawControls);
    const PatternState previousPattern = pattern;
    const bool hadPreviousPattern = previousPattern.totalSteps > 0;
    PatternState nextPattern {};
    clearPattern(nextPattern, controls, meter);

    const std::int32_t nextSerial = nextGenerationSerial(previousPattern.generationSerial);
    nextPattern.generationSerial = nextSerial;

    const bool compatibleShape = hadPreviousPattern &&
                                 previousPattern.bars == nextPattern.bars &&
                                 ::downspout::metersEqual(previousPattern.meter, nextPattern.meter) &&
                                 previousPattern.stepsPerBar == nextPattern.stepsPerBar &&
                                 previousPattern.totalSteps == nextPattern.totalSteps;

    if (fillOnlyRefresh && compatibleShape && nextPattern.totalSteps > nextPattern.stepsPerBar) {
        const int prefixSteps = nextPattern.totalSteps - nextPattern.stepsPerBar;
        copyPatternPrefix(nextPattern, previousPattern, prefixSteps);
        const int lastBar = nextPattern.bars - 1;
        buildBar(nextPattern,
                 controls,
                 lastBar,
                 true,
                 barSeedForSerial(controls, nextSerial, lastBar, true));
    } else {
        for (int bar = 0; bar < nextPattern.bars; ++bar) {
            const bool fillBar = (bar == nextPattern.bars - 1);
            buildBar(nextPattern,
                     controls,
                     bar,
                     fillBar,
                     barSeedForSerial(controls, nextSerial, bar, fillBar));
        }
    }

    applyFillOverlay(nextPattern,
                     controls,
                     baseSeedForSerial(controls, nextSerial) ^
                         fillSeedForSerial(controls, nextSerial) ^
                         0x6D2B79F5u);
    applyGenreSignature(nextPattern, controls);
    applyDiddleyStyleOverlay(nextPattern, controls);
    cleanupPattern(nextPattern, controls);

    nextPattern.version = kPatternStateVersion;
    pattern = nextPattern;
}

void refreshBar(PatternState& pattern,
                const Controls& rawControls,
                const ::downspout::Meter& meter,
                int barIndex) {
    const Controls controls = clampControls(rawControls);
    if (pattern.bars <= 0 ||
        pattern.totalSteps <= 0 ||
        pattern.bars != controls.bars ||
        !::downspout::metersEqual(pattern.meter, meter) ||
        pattern.stepsPerBeat != stepsPerBeatForResolution(controls.resolution)) {
        regeneratePattern(pattern, controls, meter, false);
        return;
    }

    const int clampedBar = clampi(barIndex, 0, pattern.bars - 1);
    const std::int32_t nextSerial = nextGenerationSerial(pattern.generationSerial);
    PatternState nextPattern = pattern;
    nextPattern.generationSerial = nextSerial;
    nextPattern.version = kPatternStateVersion;

    for (int lane = 0; lane < kLaneCount; ++lane) {
        nextPattern.lanes[lane].midiNote = noteForLane(controls.kitMap, lane);
    }

    clearBarHits(nextPattern, clampedBar);
    buildBar(nextPattern,
             controls,
             clampedBar,
             clampedBar == nextPattern.bars - 1,
             barSeedForSerial(controls, nextSerial, clampedBar, clampedBar == nextPattern.bars - 1));

    if (clampedBar == nextPattern.bars - 1) {
        applyFillOverlay(nextPattern,
                         controls,
                         baseSeedForSerial(controls, nextSerial) ^
                             fillSeedForSerial(controls, nextSerial) ^
                             0x6D2B79F5u);
    }

    applyGenreSignatureToBar(nextPattern, controls, clampedBar);
    applyDiddleyStyleOverlay(nextPattern, controls);
    cleanupPattern(nextPattern, controls);
    pattern = nextPattern;
}

void refreshFillBar(PatternState& pattern,
                    const Controls& rawControls,
                    const ::downspout::Meter& meter,
                    int barIndex) {
    const Controls controls = controlsForManualFill(rawControls);
    if (pattern.bars <= 0 ||
        pattern.totalSteps <= 0 ||
        pattern.bars != controls.bars ||
        !::downspout::metersEqual(pattern.meter, meter) ||
        pattern.stepsPerBeat != stepsPerBeatForResolution(controls.resolution)) {
        regeneratePattern(pattern, controls, meter, false);
        return;
    }

    const int clampedBar = clampi(barIndex, 0, pattern.bars - 1);
    const std::int32_t nextSerial = nextGenerationSerial(pattern.generationSerial);
    PatternState nextPattern = pattern;
    nextPattern.generationSerial = nextSerial;
    nextPattern.version = kPatternStateVersion;

    for (int lane = 0; lane < kLaneCount; ++lane) {
        nextPattern.lanes[lane].midiNote = noteForLane(controls.kitMap, lane);
    }

    clearBarHits(nextPattern, clampedBar);
    buildBar(nextPattern,
             controls,
             clampedBar,
             true,
             barSeedForSerial(controls, nextSerial, clampedBar, true));
    applyFillOverlayToBar(nextPattern,
                          controls,
                          baseSeedForSerial(controls, nextSerial) ^
                              fillSeedForSerial(controls, nextSerial) ^
                              0x6D2B79F5u ^
                              (static_cast<std::uint32_t>(clampedBar + 1) * 0xA24BAED5u),
                          clampedBar);
    applyGenreSignatureToBar(nextPattern, controls, clampedBar);
    applyDiddleyStyleOverlayToBar(nextPattern, controls, clampedBar);
    PatternState cleanedPattern = nextPattern;
    cleanupPattern(cleanedPattern, controls);

    const int barStart = clampedBar * nextPattern.stepsPerBar;
    const int barEnd = clampi(barStart + nextPattern.stepsPerBar, 0, nextPattern.totalSteps);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        for (int step = barStart; step < barEnd; ++step) {
            nextPattern.lanes[lane].steps[step] = cleanedPattern.lanes[lane].steps[step];
        }
    }

    pattern = nextPattern;
}

}  // namespace downspout::drumgen
