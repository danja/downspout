#pragma once

#include <array>
#include <cstdint>

// Shared definition of Conductor's editable block form and control messages.
// The core, the panel and every receiver that decodes scene CC 20 use this
// table, so adding or renumbering a block type cannot silently desynchronise
// them. Scene values deliberately retain the original five section values and
// insert Theme and Null between them.
namespace downspout::conductor_protocol {

inline constexpr int kMaxBlocks = 16;
inline constexpr int kBlockTypeCount = 7;

enum class BlockType : int {
    Null = 0,
    Intro = 1,
    Theme = 2,
    Develop = 3,
    Break = 4,
    Reprise = 5,
    Coda = 6,
};

struct BlockControl {
    int scene = 0;
    int density = 0;
    int energy = 0;
    int mutation = 0;
    int reset = 0;
};

inline constexpr std::array<const char*, kBlockTypeCount> kBlockTypeNames {{
    "Null", "Intro", "Theme", "Develop", "Break", "Reprise", "Coda",
}};

// Short labels for the compact block strip in the panel.
inline constexpr std::array<const char*, kBlockTypeCount> kBlockTypeShortNames {{
    "Null", "Intro", "Theme", "Devel", "Break", "Reprise", "Coda",
}};

// Scene values keep the legacy Intro/Develop/Break/Reprise/Coda numbers so
// older routing still behaves the same way.
inline constexpr std::array<int, kBlockTypeCount> kSceneValues {{
    1, 0, 16, 32, 64, 96, 127,
}};

inline constexpr std::array<int, kBlockTypeCount> kDensityValues {{
    0, 30, 48, 74, 18, 62, 20,
}};

inline constexpr std::array<int, kBlockTypeCount> kEnergyValues {{
    0, 28, 44, 68, 20, 58, 42,
}};

inline constexpr std::array<int, kBlockTypeCount> kMutationValues {{
    0, 12, 24, 54, 38, 32, 8,
}};

inline constexpr std::array<int, kBlockTypeCount> kResetValues {{
    0, 127, 0, 0, 0, 0, 127,
}};

// Legacy section-note offsets are retained for Intro, Develop, Break, Reprise
// and Coda. Theme shares Develop's offset; Null emits no note.
inline constexpr std::array<int, kBlockTypeCount> kNoteOffsets {{
    -1, 0, 1, 1, 2, 3, 4,
}};

[[nodiscard]] constexpr int toInt(const BlockType type) noexcept
{
    return static_cast<int>(type);
}

[[nodiscard]] constexpr bool isValid(const int type) noexcept
{
    return type >= 0 && type < kBlockTypeCount;
}

[[nodiscard]] constexpr BlockType fromInt(const int type) noexcept
{
    if (!isValid(type))
        return BlockType::Null;
    return static_cast<BlockType>(type);
}

[[nodiscard]] constexpr const char* name(const BlockType type) noexcept
{
    return kBlockTypeNames[static_cast<std::size_t>(toInt(type))];
}

[[nodiscard]] constexpr const char* shortName(const BlockType type) noexcept
{
    return kBlockTypeShortNames[static_cast<std::size_t>(toInt(type))];
}

[[nodiscard]] constexpr int sceneCc(const BlockType type) noexcept
{
    return kSceneValues[static_cast<std::size_t>(toInt(type))];
}

[[nodiscard]] constexpr BlockControl controlValues(const BlockType type) noexcept
{
    const auto i = static_cast<std::size_t>(toInt(type));
    return {kSceneValues[i], kDensityValues[i], kEnergyValues[i],
            kMutationValues[i], kResetValues[i]};
}

[[nodiscard]] constexpr int noteOffset(const BlockType type) noexcept
{
    return kNoteOffsets[static_cast<std::size_t>(toInt(type))];
}

[[nodiscard]] constexpr bool emitsNote(const BlockType type) noexcept
{
    return type != BlockType::Null;
}

// Decode a received scene value. Exact protocol values win; other values snap
// to the nearest one so a host passing CC 20 through a curve stays predictable.
[[nodiscard]] constexpr BlockType blockTypeFromSceneCc(const int value) noexcept
{
    const int clamped = value < 0 ? 0 : (value > 127 ? 127 : value);
    int best = 0;
    int bestDistance = clamped > kSceneValues[0] ? clamped - kSceneValues[0]
                                                 : kSceneValues[0] - clamped;
    for (int i = 1; i < kBlockTypeCount; ++i) {
        const int v = kSceneValues[i];
        const int distance = clamped > v ? clamped - v : v - clamped;
        if (distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return fromInt(best);
}

// Ground encodes phrase-role overrides as 1..7; 0 means no override.
[[nodiscard]] constexpr int groundPhraseRoleOverride(const BlockType type) noexcept
{
    switch (type) {
    case BlockType::Null:    return 0;
    case BlockType::Intro:   return 1; // Statement
    case BlockType::Theme:   return 4; // Pedal
    case BlockType::Develop: return 3; // Climb
    case BlockType::Break:   return 5; // Breakdown
    case BlockType::Reprise: return 2; // Answer
    case BlockType::Coda:    return 6; // Cadence
    }
    return 0;
}

[[nodiscard]] constexpr int groundPhraseRoleOverrideFromSceneCc(const int value) noexcept
{
    return groundPhraseRoleOverride(blockTypeFromSceneCc(value));
}

// Harmonic Atlas movement: 0 Tonal, 1 Modal, 2 Chromatic, 3 Neo-Riemannian.
[[nodiscard]] constexpr int harmonicAtlasStyle(const BlockType type) noexcept
{
    switch (type) {
    case BlockType::Null:
    case BlockType::Intro:   return 0;
    case BlockType::Theme:
    case BlockType::Develop: return 1;
    case BlockType::Break:   return 2;
    case BlockType::Reprise:
    case BlockType::Coda:    return 3;
    }
    return 0;
}

[[nodiscard]] constexpr int harmonicAtlasStyleFromSceneCc(const int value) noexcept
{
    return harmonicAtlasStyle(blockTypeFromSceneCc(value));
}

// Floozy body types are numbered 0..6 in its parameter table.
[[nodiscard]] constexpr int floozyBodyType(const BlockType type) noexcept
{
    switch (type) {
    case BlockType::Null:
    case BlockType::Intro:   return 0;
    case BlockType::Theme:   return 1;
    case BlockType::Develop: return 3;
    case BlockType::Break:   return 4;
    case BlockType::Reprise: return 5;
    case BlockType::Coda:    return 6;
    }
    return 0;
}

[[nodiscard]] constexpr int floozyBodyTypeFromSceneCc(const int value) noexcept
{
    return floozyBodyType(blockTypeFromSceneCc(value));
}

// Melgen contours are numbered 0..5 in its core types.
[[nodiscard]] constexpr int melgenContour(const BlockType type) noexcept
{
    switch (type) {
    case BlockType::Null:
    case BlockType::Intro:   return 0;
    case BlockType::Theme:   return 1;
    case BlockType::Develop: return 2;
    case BlockType::Break:   return 3;
    case BlockType::Reprise: return 4;
    case BlockType::Coda:    return 5;
    }
    return 0;
}

[[nodiscard]] constexpr int melgenContourFromSceneCc(const int value) noexcept
{
    return melgenContour(blockTypeFromSceneCc(value));
}

} // namespace downspout::conductor_protocol
