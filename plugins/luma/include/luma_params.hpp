#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::luma {

inline constexpr std::size_t kGridWidth = 8;
inline constexpr std::size_t kGridHeight = 8;
inline constexpr std::size_t kCellCount = kGridWidth * kGridHeight;

enum class ScaleId : std::uint32_t {
    major = 0,
    ionian,
    minor,
    dorian,
    mixolydian,
    pentatonic,
    blues,
    phrygianDominant,
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
    split = 0,
    single,
    count,
};

struct ParamSpec {
    const char* symbol;
    const char* name;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool boolean = false;
    bool trigger = false;
    bool output = false;
};

inline constexpr std::uint32_t kParamCellStart = 0;
inline constexpr std::uint32_t kParamRootNote = 64;
inline constexpr std::uint32_t kParamScale = 65;
inline constexpr std::uint32_t kParamDensity = 66;
inline constexpr std::uint32_t kParamEnergy = 67;
inline constexpr std::uint32_t kParamGate = 68;
inline constexpr std::uint32_t kParamSwing = 69;
inline constexpr std::uint32_t kParamClockMode = 70;
inline constexpr std::uint32_t kParamOutputMode = 71;
inline constexpr std::uint32_t kParamBaseChannel = 72;
inline constexpr std::uint32_t kParamLedFeedback = 73;
inline constexpr std::uint32_t kParamRandomize = 74;
inline constexpr std::uint32_t kParamClear = 75;
inline constexpr std::uint32_t kParamStatusActive = 76;
inline constexpr std::uint32_t kParamStatusStep = 77;
inline constexpr std::uint32_t kParamStatusCellStart = 78;
inline constexpr std::uint32_t kParamPassInput = kParamStatusCellStart + static_cast<std::uint32_t>(kCellCount);
inline constexpr std::uint32_t kParameterCount = kParamPassInput + 1;

inline constexpr std::array<const char*, static_cast<std::size_t>(ScaleId::count)> kScaleNames = {{
    "Major",
    "Ionian",
    "Minor",
    "Dorian",
    "Mixolydian",
    "Pentatonic",
    "Blues",
    "Phryg Dom",
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
    "Split",
    "Single",
}};

inline constexpr std::array<std::uint8_t, 9> kTopButtonCCs = {{
    91, 92, 93, 94, 95, 96, 97, 98, 99,
}};

inline constexpr std::array<std::uint8_t, 8> kSideButtonCCs = {{
    19, 29, 39, 49, 59, 69, 79, 89,
}};

inline constexpr std::array<std::uint8_t, 8> kDrumNotes = {{
    36, 38, 42, 46, 45, 50, 39, 51,
}};

inline constexpr std::uint8_t kLedOff = 0;
inline constexpr std::uint8_t kLedWhite = 3;
inline constexpr std::uint8_t kLedRed = 5;
inline constexpr std::uint8_t kLedAmber = 9;
inline constexpr std::uint8_t kLedYellow = 14;
inline constexpr std::uint8_t kLedGreen = 21;
inline constexpr std::uint8_t kLedCyan = 37;
inline constexpr std::uint8_t kLedBlue = 45;
inline constexpr std::uint8_t kLedPurple = 53;

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

} // namespace downspout::luma
