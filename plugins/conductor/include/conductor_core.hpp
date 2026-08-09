#pragma once
#include "generative_common.hpp"
#include <array>

namespace downspout::conductor {
using downspout::generative::MidiBlock;
using downspout::generative::ParamSpec;
using downspout::generative::Transport;

enum Param : std::uint32_t {
    kMode, kMinBars, kMaxBars, kIntroWeight, kDevelopWeight, kBreakWeight,
    kRepriseWeight, kCodaWeight, kBaseNote, kChannel, kSceneCc, kDensityCc,
    kEnergyCc, kMutationCc, kResetCc, kSeed, kStatusSection, kStatusBarsLeft,
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
    {"status_section","Current section",0,4,0,true,true},
    {"status_bars_left","Bars remaining",0,64,0,true,true},
}};

struct State {
    bool wasPlaying = false;
    bool havePosition = false;
    double previousEnd = 0.0;
    int section = -1;
    int sectionStartBar = 0;
    int nextSectionBar = 0;
    int currentBar = 0;
    std::uint64_t sectionSerial = 0;
    int activeNote = -1;
};
void reset(State&) noexcept;
MidiBlock process(State&, const std::array<float,kParameterCount>&, const Transport&,
                  std::uint32_t, double) noexcept;
} // namespace downspout::conductor
