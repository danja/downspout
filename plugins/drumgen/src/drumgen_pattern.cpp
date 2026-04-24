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

constexpr auto RESOLUTION_8TH = ResolutionId::eighth;
constexpr auto RESOLUTION_16TH = ResolutionId::sixteenth;
constexpr auto RESOLUTION_16T = ResolutionId::sixteenthTriplet;

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

[[nodiscard]] bool isFillZoneStep(int stepInBar, int stepsPerBar, int stepsPerBeat, float fill) {
    const int fillBeats = fill > 0.62f ? 2 : 1;
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

[[nodiscard]] float anchorProbability(const Controls& controls,
                                      int lane,
                                      int barIndex,
                                      int beatIndex,
                                      int subIndex,
                                      int stepsPerBeat,
                                      bool fillBar) {
    const bool beatStart = subIndex == 0;
    const bool offbeat = isOffbeatStep(beatIndex * stepsPerBeat + subIndex, stepsPerBeat);
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
        switch (controls.genre) {
        case GENRE_DISCO:
        case GENRE_MOTORIK:
            if (beatStart) return 0.86f + 0.12f * kick;
            if (offbeat && beatIndex == 3) return 0.12f + 0.10f * controls.variation;
            break;
        case GENRE_SHUFFLE:
            if (beatIndex == 0 && beatStart) return 0.96f;
            if (beatIndex == 2 && beatStart) return 0.62f + 0.18f * kick;
            if (offbeat && (beatIndex == 1 || beatIndex == 3)) return 0.16f + 0.10f * controls.variation;
            break;
        case GENRE_ELECTRO:
            if (beatIndex == 0 && beatStart) return 0.94f;
            if (beatIndex == 2 && beatStart) return 0.40f + 0.22f * kick;
            if (lateSub) return 0.12f + 0.22f * controls.variation;
            break;
        case GENRE_DUB:
            if (beatIndex == 0 && beatStart) return 0.96f;
            if (beatIndex == 2 && beatStart) return 0.24f + 0.18f * kick;
            if (offbeat && beatIndex == 3) return 0.10f + 0.10f * controls.variation;
            break;
        case GENRE_BOSSA:
            if (beatIndex == 0 && beatStart) return 0.82f;
            if (beatIndex == 1 && lateSub) return 0.34f;
            if (beatIndex == 2 && beatStart) return 0.56f;
            if (beatIndex == 3 && offbeat) return 0.42f;
            break;
        case GENRE_AFRO:
            if (beatIndex == 0 && beatStart) return 0.84f;
            if (beatIndex == 1 && offbeat) return 0.34f;
            if (beatIndex == 2 && beatStart) return 0.58f;
            if (beatIndex == 3 && offbeat) return 0.38f;
            break;
        case GENRE_ROCK:
        default:
            if (beatIndex == 0 && beatStart) return 0.98f;
            if (beatIndex == 2 && beatStart) return 0.68f + 0.18f * kick;
            if (beatIndex == 3 && lateSub) return 0.10f + 0.18f * controls.variation;
            if (beatIndex == 1 && beatStart) return 0.12f + 0.10f * kick;
            break;
        }
        break;

    case LANE_SNARE:
        if ((beatIndex == 1 || beatIndex == 3) && beatStart) {
            switch (controls.genre) {
            case GENRE_DISCO: return 0.76f + 0.18f * backbeat;
            case GENRE_ELECTRO: return 0.72f + 0.18f * backbeat;
            case GENRE_DUB: return 0.64f + 0.16f * backbeat;
            default: return 0.84f + 0.12f * backbeat;
            }
        }
        if (fillBar && beatIndex >= 2 && lateSub) return 0.08f + 0.24f * fill * backbeat;
        if (controls.genre == GENRE_AFRO && offbeat) return 0.08f + 0.10f * controls.variation;
        if (controls.genre == GENRE_SHUFFLE && lateSub) return 0.06f + 0.10f * controls.variation;
        break;

    case LANE_CLAP:
        if ((beatIndex == 1 || beatIndex == 3) && beatStart) {
            switch (controls.genre) {
            case GENRE_DISCO: return 0.78f + 0.16f * backbeat;
            case GENRE_ELECTRO: return 0.52f + 0.20f * backbeat;
            case GENRE_DUB: return 0.18f + 0.14f * backbeat;
            default: return 0.18f + 0.34f * backbeat;
            }
        }
        if (controls.genre == GENRE_DISCO && offbeat) return 0.08f + 0.14f * controls.variation;
        if (fillBar && beatIndex == 3 && !beatStart) return 0.06f + 0.18f * fill * backbeat;
        break;

    case LANE_CRASH:
        if (beatStart && beatIndex == 0) {
            return (barIndex == 0 ? 0.34f : 0.16f) + 0.18f * metal;
        }
        if (fillBar && beatStart && beatIndex >= 2) {
            return 0.08f + 0.18f * fill * (0.55f + 0.45f * metal);
        }
        break;

    case LANE_CLOSED_HAT:
        if (stepsPerBeat == 3) {
            if (subIndex == 0) return 0.70f + 0.18f * hat;
            if (subIndex == 2) return 0.54f + 0.20f * hat;
            return 0.18f + 0.16f * controls.variation;
        }
        if (stepsPerBeat == 2) {
            return offbeat ? 0.66f + 0.20f * hat : 0.74f + 0.16f * hat;
        }
        if (subIndex == 0 || subIndex == 2) return 0.74f + 0.18f * hat;
        return 0.18f + 0.28f * hat * controls.density;

    case LANE_OPEN_HAT:
        if (offbeat) {
            switch (controls.genre) {
            case GENRE_DISCO:
            case GENRE_MOTORIK: return 0.34f + 0.32f * hat;
            case GENRE_DUB: return 0.14f + 0.18f * hat;
            default: return 0.18f + 0.20f * hat;
            }
        }
        if (fillBar && beatIndex == 3 && lateSub) return 0.16f + 0.14f * fill;
        break;

    case LANE_LOW_TOM:
        if (fillBar && beatIndex >= 2 && (offbeat || lateSub)) {
            return 0.08f + 0.28f * fill + 0.12f * tom;
        }
        break;

    case LANE_HIGH_TOM:
        if (fillBar && beatIndex >= 2 && !beatStart) {
            return 0.08f + 0.26f * fill + 0.12f * tom;
        }
        break;

    case LANE_BASH:
        switch (controls.genre) {
        case GENRE_ELECTRO:
            if (beatStart && beatIndex == 0) return 0.14f + 0.16f * metal;
            if (fillBar && beatIndex >= 2 && (beatStart || lateSub)) return 0.10f + 0.26f * fill * metal;
            if (lateSub && beatIndex == 3) return 0.08f + 0.14f * controls.variation;
            break;
        case GENRE_MOTORIK:
            if (beatStart && beatIndex == 0) return 0.12f + 0.18f * metal;
            if (fillBar && beatIndex == 3 && beatStart) return 0.10f + 0.22f * fill * metal;
            break;
        case GENRE_DUB:
            if ((offbeat && beatIndex == 3) || (lateSub && beatIndex == 2)) return 0.10f + 0.18f * metal;
            if (fillBar && beatIndex == 3) return 0.10f + 0.18f * fill * metal;
            break;
        default:
            if (fillBar && beatIndex >= 3 && (offbeat || lateSub)) return 0.06f + 0.18f * fill * metal;
            break;
        }
        break;

    case LANE_COWBELL:
        switch (controls.genre) {
        case GENRE_DISCO:
            if (offbeat) return 0.16f + 0.24f * perc;
            if (beatStart && (beatIndex == 1 || beatIndex == 3)) return 0.08f + 0.14f * perc;
            break;
        case GENRE_MOTORIK:
            if (beatStart && (beatIndex == 0 || beatIndex == 2)) return 0.10f + 0.18f * perc;
            if (offbeat) return 0.10f + 0.16f * perc;
            break;
        case GENRE_BOSSA:
            if ((beatIndex == 0 || beatIndex == 2) && lateSub) return 0.16f + 0.18f * perc;
            if ((beatIndex == 1 || beatIndex == 3) && offbeat) return 0.16f + 0.20f * perc;
            break;
        case GENRE_AFRO:
            if (offbeat || lateSub) return 0.14f + 0.20f * perc;
            break;
        default:
            if (fillBar && beatIndex >= 2 && offbeat) return 0.06f + 0.16f * fill * perc;
            break;
        }
        break;

    case LANE_CLAVE:
        switch (controls.genre) {
        case GENRE_BOSSA:
            if (beatIndex == 0 && beatStart) return 0.26f + 0.16f * perc;
            if (beatIndex == 1 && offbeat) return 0.22f + 0.16f * perc;
            if (beatIndex == 2 && lateSub) return 0.22f + 0.16f * perc;
            if (beatIndex == 3 && beatStart) return 0.24f + 0.16f * perc;
            break;
        case GENRE_AFRO:
            if (beatIndex == 0 && beatStart) return 0.20f + 0.18f * perc;
            if (beatIndex == 1 && lateSub) return 0.18f + 0.18f * perc;
            if (beatIndex == 2 && offbeat) return 0.22f + 0.18f * perc;
            if (beatIndex == 3 && beatStart) return 0.18f + 0.16f * perc;
            break;
        case GENRE_SHUFFLE:
            if (beatIndex == 1 && lateSub) return 0.10f + 0.14f * perc;
            if (beatIndex == 3 && offbeat) return 0.10f + 0.14f * perc;
            break;
        default:
            if (fillBar && beatIndex == 3 && !beatStart) return 0.06f + 0.14f * fill * perc;
            break;
        }
        break;

    default:
        break;
    }

    return 0.0f;
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
        desiredHits = 1.0f + ((controls.genre == GENRE_DISCO || controls.genre == GENRE_MOTORIK) ? 3.0f : 1.6f) * density * macro;
        break;
    case LANE_CLAP:
        desiredHits = ((controls.genre == GENRE_DISCO || controls.genre == GENRE_ELECTRO) ? 1.6f : 0.8f) * density * macro;
        break;
    case LANE_SNARE:
        desiredHits = 1.0f + 1.0f * density * macro * (0.4f + 0.6f * variation);
        break;
    case LANE_CRASH:
        desiredHits = fillBar ? (0.3f + 1.2f * fill * macro) : (0.15f + 0.35f * macro);
        break;
    case LANE_CLOSED_HAT:
        desiredHits = (stepsPerBar * (0.20f + 0.55f * density * macro)) + (stepsPerBar >= 16 ? 1.5f : 0.0f);
        break;
    case LANE_OPEN_HAT:
        desiredHits = 0.4f + 1.5f * density * macro;
        break;
    case LANE_LOW_TOM:
    case LANE_HIGH_TOM:
        desiredHits = fillBar ? (0.4f + 2.4f * fill * macro) : 0.0f;
        break;
    case LANE_BASH:
        desiredHits = fillBar
            ? (0.2f + 1.4f * fill * macro)
            : (((controls.genre == GENRE_ELECTRO) || (controls.genre == GENRE_DUB) || (controls.genre == GENRE_MOTORIK))
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

    const int jitter = static_cast<int>(std::lround((rng.nextFloat() * 2.0f - 1.0f) * (variation * 2.0f)));
    return clampi(static_cast<int>(std::lround(desiredHits)) + jitter, 0, stepsPerBar);
}

[[nodiscard]] float euclidInfluenceForLane(const Controls& controls, int lane, bool fillBar) {
    switch (lane) {
    case LANE_KICK:
        return 0.10f + 0.35f * controls.variation * controls.kickAmt;
    case LANE_CLAP:
    case LANE_SNARE:
        return 0.10f + 0.32f * controls.variation * controls.backbeatAmt;
    case LANE_CRASH:
        return 0.10f + 0.28f * controls.variation * controls.metalAmt + (fillBar ? 0.16f : 0.0f);
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
                               int lane,
                               int beatIndex,
                               int subIndex,
                               int stepsPerBeat,
                               bool fillBar,
                               Rng& rng,
                               std::uint8_t* flagsOut) {
    int velocity = 90;
    std::uint8_t flags = 0;

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

    if ((lane == LANE_SNARE || lane == LANE_CLAP) && (beatIndex == 1 || beatIndex == 3) && subIndex == 0) {
        velocity += 10;
        flags |= STEP_FLAG_ACCENT;
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
    pattern.stepsPerBar = pattern.stepsPerBeat * 4;
    pattern.totalSteps = clampi(pattern.bars * pattern.stepsPerBar, 1, kMaxPatternSteps);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        pattern.lanes[lane].midiNote = noteForLane(controls.kitMap, lane);
    }
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
    const int fillBeats = controls.fill > 0.62f ? 2 : 1;
    const int fillSteps = clampi(fillBeats * stepsPerBeat, stepsPerBeat, stepsPerBar);
    const int zoneStart = clampi(barEnd - fillSteps, barStart, barEnd);
    int motif = 0;

    rng.seed(seedValue ^ 0xC001D00Du);

    switch (controls.genre) {
    case GENRE_ROCK:
    case GENRE_SHUFFLE:
        motif = rng.nextInt(0, 1);
        break;
    case GENRE_DISCO:
    case GENRE_MOTORIK:
        motif = 2 + rng.nextInt(0, 1);
        break;
    case GENRE_ELECTRO:
    case GENRE_DUB:
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
            const float anchorProb = anchorProbability(controls, lane, barIndex, beatIndex, subIndex, stepsPerBeat, fillBar);
            const bool anchor = anchorProb > 0.0f && rng.nextFloat() < clampf(anchorProb, 0.0f, 1.0f);
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
            const int velocity = stepVelocity(controls, lane, beatIndex, subIndex, stepsPerBeat, fillBar, rng, &flags);
            pattern.lanes[lane].steps[globalStep].velocity = static_cast<std::uint8_t>(velocity);
            pattern.lanes[lane].steps[globalStep].flags = flags;
        }
    }
}

void cleanupPattern(PatternState& pattern, const Controls& controls) {
    if (pattern.totalSteps <= 0) {
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
        int crashHits = 0;
        int bashHits = 0;
        for (int step = 0; step < stepsPerBar && (barStart + step) < pattern.totalSteps; ++step) {
            DrumStepCell& crash = pattern.lanes[LANE_CRASH].steps[barStart + step];
            DrumStepCell& bash = pattern.lanes[LANE_BASH].steps[barStart + step];
            if (crash.velocity > 0) {
                crashHits += 1;
                if (crashHits > 2) {
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

            if (isFillZoneStep(step, stepsPerBar, stepsPerBeat, controls.fill)) {
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

        const int backbeat1 = barStart + stepsPerBeat;
        const int backbeat2 = barStart + stepsPerBeat * 3;
        const bool needsBackbeat = controls.backbeatAmt > 0.30f;
        if (needsBackbeat) {
            if (backbeat1 < pattern.totalSteps &&
                pattern.lanes[LANE_SNARE].steps[backbeat1].velocity == 0 &&
                pattern.lanes[LANE_CLAP].steps[backbeat1].velocity == 0) {
                pattern.lanes[LANE_SNARE].steps[backbeat1].velocity = 102;
                pattern.lanes[LANE_SNARE].steps[backbeat1].flags = STEP_FLAG_ACCENT;
            }
            if (backbeat2 < pattern.totalSteps &&
                pattern.lanes[LANE_SNARE].steps[backbeat2].velocity == 0 &&
                pattern.lanes[LANE_CLAP].steps[backbeat2].velocity == 0) {
                pattern.lanes[LANE_SNARE].steps[backbeat2].velocity = 104;
                pattern.lanes[LANE_SNARE].steps[backbeat2].flags = STEP_FLAG_ACCENT;
            }
        }
    }
}

}  // namespace

Controls clampControls(const Controls& raw) {
    Controls controls = raw;
    controls.genre = static_cast<GenreId>(clampi(static_cast<int>(controls.genre), 0, static_cast<int>(GenreId::count) - 1));
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
                       bool fillOnlyRefresh) {
    const Controls controls = clampControls(rawControls);
    const PatternState previousPattern = pattern;
    const bool hadPreviousPattern = previousPattern.totalSteps > 0;
    PatternState nextPattern {};
    clearPattern(nextPattern, controls);

    const std::int32_t nextSerial = nextGenerationSerial(previousPattern.generationSerial);
    nextPattern.generationSerial = nextSerial;

    const bool compatibleShape = hadPreviousPattern &&
                                 previousPattern.bars == nextPattern.bars &&
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
    cleanupPattern(nextPattern, controls);

    nextPattern.version = kPatternStateVersion;
    pattern = nextPattern;
}

void refreshBar(PatternState& pattern,
                const Controls& rawControls,
                int barIndex) {
    const Controls controls = clampControls(rawControls);
    if (pattern.bars <= 0 ||
        pattern.totalSteps <= 0 ||
        pattern.bars != controls.bars ||
        pattern.stepsPerBeat != stepsPerBeatForResolution(controls.resolution)) {
        regeneratePattern(pattern, controls, false);
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

    cleanupPattern(nextPattern, controls);
    pattern = nextPattern;
}

void refreshFillBar(PatternState& pattern,
                    const Controls& rawControls,
                    int barIndex) {
    const Controls controls = controlsForManualFill(rawControls);
    if (pattern.bars <= 0 ||
        pattern.totalSteps <= 0 ||
        pattern.bars != controls.bars ||
        pattern.stepsPerBeat != stepsPerBeatForResolution(controls.resolution)) {
        regeneratePattern(pattern, controls, false);
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

    cleanupPattern(nextPattern, controls);
    pattern = nextPattern;
}

}  // namespace downspout::drumgen
