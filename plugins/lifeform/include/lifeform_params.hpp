#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::lifeform {

inline constexpr std::size_t kGridWidth = 8;
inline constexpr std::size_t kGridHeight = 8;
inline constexpr std::size_t kCellCount = kGridWidth * kGridHeight;

enum class ScaleId : std::uint32_t {
    chromatic = 0,
    major,
    ionian,
    minor,
    dorian,
    mixolydian,
    pentatonic,
    blues,
    neapolitanMajor,
    neapolitanMinor,
    altered,
    halfWholeDiminished,
    wholeHalfDiminished,
    bebopDominant,
    bebopMajor,
    bebopMinor,
    count,
};

enum class ClockMode : std::uint32_t {
    autoClock = 0,
    host,
    free,
    count,
};

enum class OutputMode : std::uint32_t {
    voices = 0,
    single,
    drums,
    count,
};

enum class EmitMode : std::uint32_t {
    lean = 0,
    full,
    count,
};

inline constexpr std::uint32_t kParamCellStart = 0;
inline constexpr std::uint32_t kParamRootNote = 64;
inline constexpr std::uint32_t kParamScale = 65;
inline constexpr std::uint32_t kParamGate = 66;
inline constexpr std::uint32_t kParamVelocity = 67;
inline constexpr std::uint32_t kParamMutation = 68;
inline constexpr std::uint32_t kParamDensity = 69;
inline constexpr std::uint32_t kParamClockMode = 70;
inline constexpr std::uint32_t kParamOutputMode = 71;
inline constexpr std::uint32_t kParamBaseChannel = 72;
inline constexpr std::uint32_t kParamLedFeedback = 73;
inline constexpr std::uint32_t kParamRunning = 74;
inline constexpr std::uint32_t kParamRandomize = 75;
inline constexpr std::uint32_t kParamClear = 76;
inline constexpr std::uint32_t kParamSeed = 77;
inline constexpr std::uint32_t kParamStep = 78;
inline constexpr std::uint32_t kParamPanic = 79;
inline constexpr std::uint32_t kParamEmitMode = 80;
inline constexpr std::uint32_t kParamStatusActive = 81;
inline constexpr std::uint32_t kParamStatusGeneration = 82;
inline constexpr std::uint32_t kParamStatusCellStart = 83;
inline constexpr std::uint32_t kParamPassInput = kParamStatusCellStart + static_cast<std::uint32_t>(kCellCount);
inline constexpr std::uint32_t kParameterCount = kParamPassInput + 1;

inline constexpr std::array<const char*, static_cast<std::size_t>(ScaleId::count)> kScaleNames = {{
    "Chromatic",
    "Major",
    "Ionian",
    "Minor",
    "Dorian",
    "Mixolydian",
    "Pentatonic",
    "Blues",
    "Neo Major",
    "Neo Minor",
    "Altered",
    "Half-Whole Dim",
    "Whole-Half Dim",
    "Bebop Dom",
    "Bebop Major",
    "Bebop Minor",
}};

inline constexpr std::array<const char*, static_cast<std::size_t>(ClockMode::count)> kClockModeNames = {{
    "Auto",
    "Host",
    "Free",
}};

inline constexpr std::array<const char*, static_cast<std::size_t>(OutputMode::count)> kOutputModeNames = {{
    "Voices",
    "Single",
    "Drums",
}};

inline constexpr std::array<const char*, static_cast<std::size_t>(EmitMode::count)> kEmitModeNames = {{
    "Lean",
    "Full",
}};

inline constexpr std::array<std::uint8_t, 9> kTopButtonCCs = {{
    91, 92, 93, 94, 95, 96, 97, 98, 99,
}};

inline constexpr std::uint8_t kLogoCC = 99;

inline constexpr std::array<std::uint8_t, 8> kSideButtonCCs = {{
    19, 29, 39, 49, 59, 69, 79, 89,
}};

inline constexpr std::array<std::uint8_t, 8> kDrumNotes = {{
    36, 38, 42, 46, 45, 50, 39, 51,
}};

inline constexpr std::uint8_t kLedOff = 0;
inline constexpr std::uint8_t kLedDim = 2;
inline constexpr std::uint8_t kLedWhite = 3;
inline constexpr std::uint8_t kLedRed = 5;
inline constexpr std::uint8_t kLedOrange = 9;
inline constexpr std::uint8_t kLedYellow = 14;
inline constexpr std::uint8_t kLedGreen = 21;
inline constexpr std::uint8_t kLedCyan = 37;
inline constexpr std::uint8_t kLedBlue = 45;
inline constexpr std::uint8_t kLedPurple = 53;
inline constexpr std::uint8_t kLedPink = 57;

[[nodiscard]] constexpr std::uint8_t gridToNote(const std::size_t row, const std::size_t col) noexcept
{
    return row < kGridHeight && col < kGridWidth
        ? static_cast<std::uint8_t>((row + 1u) * 10u + (col + 1u))
        : 0u;
}

[[nodiscard]] constexpr bool noteToGrid(const std::uint8_t note, std::size_t& row, std::size_t& col) noexcept
{
    if (note < 11u || note > 88u)
        return false;

    const std::uint8_t tens = static_cast<std::uint8_t>(note / 10u);
    const std::uint8_t ones = static_cast<std::uint8_t>(note % 10u);
    if (tens < 1u || tens > 8u || ones < 1u || ones > 8u)
        return false;

    row = static_cast<std::size_t>(tens - 1u);
    col = static_cast<std::size_t>(ones - 1u);
    return true;
}

[[nodiscard]] constexpr std::size_t cellIndex(const std::size_t row, const std::size_t col) noexcept
{
    return row * kGridWidth + col;
}

} // namespace downspout::lifeform
