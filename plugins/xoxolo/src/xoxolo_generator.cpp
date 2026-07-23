#include "xoxolo_generator.hpp"

#include "xoxolo_engine.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace downspout::xoxolo {
namespace {

enum class Voice : std::size_t {
    kick = 0,
    snare,
    clap,
    closedHat,
    openHat,
    tom,
    cymbal,
    percussion,
    count
};

struct VoiceTemplate {
    const char* anchors;
    const char* tension;
    const char* fills;
};

using StyleTemplate = std::array<VoiceTemplate, static_cast<std::size_t>(Voice::count)>;

// Each string is one 16-slot phrase. Anchors define the recognizable groove,
// tension marks syncopations, and fills provide density-dependent decoration.
constexpr std::array<StyleTemplate, static_cast<std::size_t>(GenerationStyle::count)> kTemplates {{
    // Jazz
    StyleTemplate {{
        {"x.......x.......", "...x......x...x.", "......x........x"},
        {"....x.......x...", ".......x.......x", "..x.......x....."},
        {"................", "..........x.....", "......x........."},
        {"x...x...x...x...", "..x...x...x...x.", ".x.x.x.x.x.x.x.x"},
        {"......x.......x.", "...x.......x....", "...........x...."},
        {"................", "..............x.", ".......x........"},
        {"x.....x...x.....", "..............x.", "...x.......x...."},
        {"................", "..x.......x.....", "......x.......x."},
    }},
    // Drum & Bass
    StyleTemplate {{
        {"x.........x.....", "...x....x....x..", "......x........x"},
        {"....x.......x...", ".......x......x.", "..x.......x....."},
        {"............x...", ".......x........", "................"},
        {"x.x.x.x.x.x.x.x.", ".x.x.x.x.x.x.x.x", "xxxxxxxxxxxxxxxx"},
        {".......x.......x", "...x......x.....", ".............x.."},
        {"................", "..............x.", "......x........x"},
        {"x...............", "...............x", "................"},
        {"................", "..x.......x.....", "......x.......x."},
    }},
    // House
    StyleTemplate {{
        {"x...x...x...x...", "..............x.", "..x.......x....."},
        {"....x.......x...", "...............x", "................"},
        {"....x.......x...", "..........x.....", "......x........."},
        {"..x...x...x...x.", ".x.x.x.x.x.x.x.x", "x.x.x.x.x.x.x.x."},
        {"......x.......x.", "...............x", "..........x....."},
        {"................", "..............x.", ".......x........"},
        {"x...............", "............x...", "................"},
        {"................", "...x.......x....", "......x.......x."},
    }},
    // Funk
    StyleTemplate {{
        {"x......x..x.....", "...x........x...", "......x........x"},
        {"....x.......x...", ".......x.......x", "..x.......x....."},
        {"............x...", "......x.........", "..........x....."},
        {"x.x.x.x.x.x.x.x.", ".x...x...x...x..", "xxxxxxxxxxxxxxxx"},
        {"......x.......x.", "...x.......x....", "..........x....."},
        {"................", "..............x.", ".......x........"},
        {"x...............", "...........x....", "................"},
        {"..x.......x.....", "......x.......x.", "...x.......x...."},
    }},
    // Rock
    StyleTemplate {{
        {"x.......x.......", "..........x.....", "......x........x"},
        {"....x.......x...", "..............x.", "..........x....."},
        {"................", ".......x........", "................"},
        {"x.x.x.x.x.x.x.x.", ".x.x.x.x.x.x.x.x", "xxxxxxxxxxxxxxxx"},
        {"................", "......x.......x.", "..........x....."},
        {"................", "..............x.", "............x..x"},
        {"x...............", "............x...", "................"},
        {"................", "...............x", "................"},
    }},
    // Latin
    StyleTemplate {{
        {"x.....x...x.....", "...x.......x....", "..............x."},
        {"....x.......x...", ".......x.......x", "..........x....."},
        {"................", "..x.......x.....", "......x........."},
        {"x.x.x.x.x.x.x.x.", ".x...x...x...x..", "xxxxxxxxxxxxxxxx"},
        {"......x.......x.", "...x.......x....", ".............x.."},
        {"................", "..............x.", ".......x........"},
        {"x...............", "............x...", "................"},
        {"..x...x...x...x.", "...x.......x....", ".x...x...x...x.."},
    }},
}};

[[nodiscard]] float clamp01(const float value)
{
    return std::max(0.0f, std::min(value, 1.0f));
}

class Random {
public:
    explicit Random(const std::uint32_t seed)
        : state_(seed != 0 ? seed : 0x9e3779b9u) {}

    [[nodiscard]] float unit()
    {
        std::uint32_t x = state_;
        x ^= x << 13u;
        x ^= x >> 17u;
        x ^= x << 5u;
        state_ = x;
        return static_cast<float>(x & 0x00ffffffu) / static_cast<float>(0x01000000u);
    }

private:
    std::uint32_t state_;
};

[[nodiscard]] int laneForVoice(const NotePresetId preset, const Voice voice)
{
    if (clampNotePreset(preset) == NotePresetId::avlDrumkits) {
        switch (voice) {
        case Voice::kick: return 0;
        case Voice::snare: return 2;
        case Voice::clap: return 3;
        case Voice::closedHat: return 6;
        case Voice::openHat: return 10;
        case Voice::tom: return 9;
        case Voice::cymbal: return 15;
        case Voice::percussion: return 20;
        case Voice::count: break;
        }
    } else {
        switch (voice) {
        case Voice::kick: return 0;
        case Voice::snare: return 2;
        case Voice::clap: return 1;
        case Voice::closedHat: return 4;
        case Voice::openHat: return 6;
        case Voice::tom: return 7;
        case Voice::cymbal: return 3;
        case Voice::percussion: return 9;
        case Voice::count: break;
        }
    }
    return -1;
}

[[nodiscard]] bool marked(const char* pattern, const int step)
{
    return pattern[step % 16] == 'x';
}

}  // namespace

GenerationSettings clampGenerationSettings(const GenerationSettings& settings)
{
    GenerationSettings result = settings;
    const int style = static_cast<int>(result.style);
    if (style < 0 || style >= static_cast<int>(GenerationStyle::count))
        result.style = GenerationStyle::jazz;
    result.density = clamp01(result.density);
    result.tension = clamp01(result.tension);
    return result;
}

const char* generationStyleName(const GenerationStyle style)
{
    switch (clampGenerationSettings({style, 0.0f, 0.0f}).style) {
    case GenerationStyle::jazz: return "Jazz";
    case GenerationStyle::drumAndBass: return "Drum & Bass";
    case GenerationStyle::house: return "House";
    case GenerationStyle::funk: return "Funk";
    case GenerationStyle::rock: return "Rock";
    case GenerationStyle::latin: return "Latin";
    case GenerationStyle::count: break;
    }
    return "Jazz";
}

void generatePattern(PatternState& pattern,
                     const GenerationSettings& rawSettings,
                     const std::uint32_t seed)
{
    sanitizePattern(pattern);
    clearPattern(pattern);

    const GenerationSettings settings = clampGenerationSettings(rawSettings);
    const StyleTemplate& style = kTemplates[static_cast<std::size_t>(settings.style)];
    Random random(seed);

    const float anchorChance = 0.52f + settings.density * 0.46f;
    const float tensionChance = settings.density * (0.08f + settings.tension * 0.72f);
    const float fillChance = settings.density * settings.density * (0.06f + settings.tension * 0.34f);

    for (std::size_t voiceIndex = 0; voiceIndex < style.size(); ++voiceIndex) {
        const int lane = laneForVoice(pattern.notePreset, static_cast<Voice>(voiceIndex));
        if (lane < 0)
            continue;

        const VoiceTemplate& voice = style[voiceIndex];
        for (int step = 0; step < pattern.totalSteps; ++step) {
            const float roll = random.unit();
            float chance = 0.0f;
            if (marked(voice.anchors, step))
                chance = anchorChance;
            else if (marked(voice.tension, step))
                chance = tensionChance;
            else if (marked(voice.fills, step))
                chance = fillChance;

            // Later phrases get a small fill lift so 17-32 step patterns evolve.
            if (step >= 16 && (step % 16) >= 12)
                chance = std::min(1.0f, chance + 0.08f * settings.tension * settings.density);
            if (roll < chance)
                setCell(pattern, lane, step, true);
        }
    }
}

}  // namespace downspout::xoxolo
