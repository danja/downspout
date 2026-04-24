#pragma once

#include "GremlinDriverEngine.hpp"

#include <array>
#include <cstdint>

namespace downspout::gremlin_driver {

using flues::gremlindriver::ClockMode;
using flues::gremlindriver::LaneConfig;
using flues::gremlindriver::LaneShape;
using flues::gremlindriver::LaneTarget;
using flues::gremlindriver::TriggerAction;
using flues::gremlindriver::TriggerConfig;

inline constexpr std::size_t kLaneCount = flues::gremlindriver::kLaneCount;
inline constexpr std::size_t kTriggerCount = flues::gremlindriver::kTriggerCount;
inline constexpr std::size_t kTargetCount = flues::gremlindriver::kTargetCount;

struct ParamSpec {
    const char* symbol;
    const char* name;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool boolean = false;
};

inline constexpr std::array<const char*, 2> kClockModeNames = {{
    "Free",
    "Transport",
}};

inline constexpr std::array<const char*, 10> kTargetNames = {{
    "Off",
    "Macro 1",
    "Macro 2",
    "Macro 3",
    "Macro 4",
    "Macro 5",
    "Macro 6",
    "Macro 7",
    "Macro 8",
    "Master",
}};

inline constexpr std::array<const char*, 6> kShapeNames = {{
    "Sine",
    "Triangle",
    "Ramp",
    "SampleHold",
    "RandomWalk",
    "Logistic",
}};

inline constexpr std::array<const char*, 11> kActionNames = {{
    "Off",
    "Reseed",
    "Burst",
    "Rand Source",
    "Rand Delay",
    "Rand All",
    "Scene Down",
    "Scene Up",
    "Panic",
    "Mode Down",
    "Mode Up",
}};

inline constexpr std::array<ParamSpec, 2> kGlobalParamSpecs = {{
    {"clock_mode", "Clock Mode", 0.0f, 1.0f, 1.0f, true, false},
    {"bpm", "BPM", 40.0f, 220.0f, 120.0f, false, false},
}};

inline constexpr std::array<ParamSpec, kLaneCount * 5> kLaneParamSpecs = {{
    {"lane1_target", "Lane 1 Target", 0.0f, 9.0f, 1.0f, true, false},
    {"lane1_shape", "Lane 1 Shape", 0.0f, 5.0f, 1.0f, true, false},
    {"lane1_rate", "Lane 1 Rate", 0.0f, 1.0f, 0.22f, false, false},
    {"lane1_depth", "Lane 1 Depth", 0.0f, 1.0f, 0.34f, false, false},
    {"lane1_center", "Lane 1 Center", 0.0f, 1.0f, 0.60f, false, false},
    {"lane2_target", "Lane 2 Target", 0.0f, 9.0f, 2.0f, true, false},
    {"lane2_shape", "Lane 2 Shape", 0.0f, 5.0f, 4.0f, true, false},
    {"lane2_rate", "Lane 2 Rate", 0.0f, 1.0f, 0.18f, false, false},
    {"lane2_depth", "Lane 2 Depth", 0.0f, 1.0f, 0.42f, false, false},
    {"lane2_center", "Lane 2 Center", 0.0f, 1.0f, 0.46f, false, false},
    {"lane3_target", "Lane 3 Target", 0.0f, 9.0f, 4.0f, true, false},
    {"lane3_shape", "Lane 3 Shape", 0.0f, 5.0f, 3.0f, true, false},
    {"lane3_rate", "Lane 3 Rate", 0.0f, 1.0f, 0.12f, false, false},
    {"lane3_depth", "Lane 3 Depth", 0.0f, 1.0f, 0.44f, false, false},
    {"lane3_center", "Lane 3 Center", 0.0f, 1.0f, 0.36f, false, false},
    {"lane4_target", "Lane 4 Target", 0.0f, 9.0f, 6.0f, true, false},
    {"lane4_shape", "Lane 4 Shape", 0.0f, 5.0f, 5.0f, true, false},
    {"lane4_rate", "Lane 4 Rate", 0.0f, 1.0f, 0.16f, false, false},
    {"lane4_depth", "Lane 4 Depth", 0.0f, 1.0f, 0.24f, false, false},
    {"lane4_center", "Lane 4 Center", 0.0f, 1.0f, 0.18f, false, false},
}};

inline constexpr std::array<ParamSpec, kTriggerCount * 3> kTriggerParamSpecs = {{
    {"trigger1_action", "Trigger 1 Action", 0.0f, 10.0f, 2.0f, true, false},
    {"trigger1_rate", "Trigger 1 Rate", 0.0f, 1.0f, 0.28f, false, false},
    {"trigger1_chance", "Trigger 1 Chance", 0.0f, 1.0f, 0.55f, false, false},
    {"trigger2_action", "Trigger 2 Action", 0.0f, 10.0f, 4.0f, true, false},
    {"trigger2_rate", "Trigger 2 Rate", 0.0f, 1.0f, 0.10f, false, false},
    {"trigger2_chance", "Trigger 2 Chance", 0.0f, 1.0f, 0.18f, false, false},
}};

inline constexpr std::array<const char*, kLaneCount> kLaneStatusNames = {{
    "Lane 1 Status",
    "Lane 2 Status",
    "Lane 3 Status",
    "Lane 4 Status",
}};

inline constexpr std::array<const char*, kTriggerCount> kTriggerStatusNames = {{
    "Trigger 1 Status",
    "Trigger 2 Status",
}};

inline constexpr std::array<std::uint8_t, 16> kPrimaryKnobCCs = {{
    16, 20, 24, 28, 46, 50, 54, 58,
    17, 21, 25, 29, 47, 51, 55, 59,
}};

inline constexpr std::array<std::uint8_t, 8> kHiddenKnobCCs = {{
    18, 22, 26, 30, 48, 52, 56, 60,
}};

inline constexpr std::array<std::uint8_t, 8> kMacroFaderCCs = {{
    19, 23, 27, 31, 49, 53, 57, 61,
}};

inline constexpr std::uint8_t kMasterFaderCC = 62;

inline constexpr std::array<std::uint8_t, 11> kActionNotes = {{
    0,
    2,
    5,
    8,
    11,
    14,
    17,
    20,
    23,
    25,
    26,
}};

}  // namespace downspout::gremlin_driver
