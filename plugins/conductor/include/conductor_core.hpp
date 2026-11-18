#pragma once

#include "downspout/conductor_protocol.hpp"
#include "generative_common.hpp"

#include <array>

namespace downspout::conductor {
using downspout::generative::MidiBlock;
using downspout::generative::ParamSpec;
using downspout::generative::Transport;
using downspout::conductor_protocol::BlockType;

enum Param : std::uint32_t {
    kMode, kMinBars, kMaxBars, kIntroWeight, kDevelopWeight, kBreakWeight,
    kRepriseWeight, kCodaWeight, kBaseNote, kChannel, kSceneCc, kDensityCc,
    kEnergyCc, kMutationCc, kResetCc, kSeed, kStatusSection, kStatusBarsLeft,
    kPendingSection,
    // Appended so existing host parameter indices stay valid.
    kBlockCount,
    kBlock1Type, kBlock2Type, kBlock3Type, kBlock4Type,
    kBlock5Type, kBlock6Type, kBlock7Type, kBlock8Type,
    kNullWeight, kThemeWeight,
    kStatusBlock,
    kParameterCount
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs {{
    {"mode","Form mode",0,1,0,true}, {"min_bars","Minimum bars",1,32,4,true},
    {"max_bars","Maximum bars",1,64,12,true}, {"intro_weight","Intro weight",0,1,0.35f},
    {"develop_weight","Development weight",0,1,0.9f}, {"break_weight","Break weight",0,1,0.4f},
    {"reprise_weight","Reprise weight",0,1,0.65f}, {"coda_weight","Coda weight",0,1,0.2f},
    {"base_note","Section base note",0,119,24,true}, {"channel","MIDI channel",1,16,16,true},
    {"scene_cc","Scene CC",0,127,20,true}, {"density_cc","Density CC",0,127,21,true},
    {"energy_cc","Energy CC",0,127,22,true}, {"mutation_cc","Mutation CC",0,127,23,true},
    {"reset_cc","Reset CC",0,127,24,true}, {"seed","Seed",1,65535,2909,true},
    {"status_section","Current block type",-1,6,-1,true,true},
    {"status_bars_left","Bars remaining",0,64,0,true,true},
    {"pending_section","Pending block jump",0,8,0,true},
    {"block_count","Block count",1,8,8,true},
    {"block_1_type","Block 1 type",0,6,1,true},
    {"block_2_type","Block 2 type",0,6,3,true},
    {"block_3_type","Block 3 type",0,6,3,true},
    {"block_4_type","Block 4 type",0,6,4,true},
    {"block_5_type","Block 5 type",0,6,3,true},
    {"block_6_type","Block 6 type",0,6,5,true},
    {"block_7_type","Block 7 type",0,6,3,true},
    {"block_8_type","Block 8 type",0,6,6,true},
    {"null_weight","Null weight",0,1,0.0f},
    {"theme_weight","Theme weight",0,1,0.0f},
    {"status_block","Current block",-1,7,-1,true,true},
}};

[[nodiscard]] constexpr Param blockTypeParam(const int blockIndex) noexcept
{
    return static_cast<Param>(static_cast<std::uint32_t>(kBlock1Type)
                              + static_cast<std::uint32_t>(blockIndex));
}

struct State {
    bool wasPlaying = false;
    bool havePosition = false;
    double previousEnd = 0.0;
    int section = -1;       // current block type, kept for the legacy status output
    int blockIndex = -1;    // position in the editable block list
    int sectionStartBar = 0;
    int nextSectionBar = 0;
    int currentBar = 0;
    std::uint64_t sectionSerial = 0;
    int activeNote = -1;
    int activeChannel = -1;
    int pendingSection = -1; // block index, or -1 for none
    int pendingBar = -1;     // 0-based bar at which the jump takes effect
};

void reset(State&) noexcept;
MidiBlock process(State&, const std::array<float,kParameterCount>&, const Transport&,
                  std::uint32_t, double) noexcept;
} // namespace downspout::conductor
