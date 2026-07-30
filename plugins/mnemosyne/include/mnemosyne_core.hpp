#pragma once
#include "generative_common.hpp"
#include <array>
#include <optional>
#include <string>

namespace downspout::mnemosyne {
using downspout::generative::MidiBlock;
using downspout::generative::MidiEvent;
using downspout::generative::ParamSpec;
using downspout::generative::Transport;
inline constexpr int kPhraseCapacity=8;
inline constexpr int kEventCapacity=64;
enum Param:std::uint32_t{kMode,kPhraseBars,kNovelty,kContinuity,kRegister,kRhythmFidelity,
 kTransform,kSeed,kChannel,kPassInput,kStatusPhrases,kStatusEvents,kParameterCount};
inline constexpr std::array<ParamSpec,kParameterCount>kParameterSpecs{{
{"mode","Mode",0,2,1,true},{"phrase_bars","Phrase bars",1,8,2,true},{"novelty","Novelty",0,1,0.35f},
{"continuity","Continuity",0,1,0.7f},{"register","Register",24,96,60,true},{"rhythm_fidelity","Rhythm fidelity",0,1,0.8f},
{"transform","Transform",0,5,0,true},{"seed","Seed",1,65535,5573,true},{"channel","Output channel",1,16,1,true},
{"pass_input","Pass input",0,1,1,true},{"status_phrases","Stored phrases",0,kPhraseCapacity,0,true,true},
{"status_events","Captured events",0,kEventCapacity,0,true,true}
}};
struct Note{std::uint8_t note=60,velocity=96,step=0,duration=1;};
struct Phrase{std::array<Note,kEventCapacity>notes{};std::uint8_t count=0;};
struct State{
 std::array<Phrase,kPhraseCapacity>reservoir{};int phraseCount=0;int writePhrase=0;
 Phrase capture{};std::array<int,128>captureIndex{};std::int64_t phraseSerial=-1;
 std::array<int,32>activeNotes{};int activeCount=0;bool havePosition=false;double previousEnd=0;
};
void resetRuntime(State&)noexcept;
MidiBlock process(State&,const std::array<float,kParameterCount>&,const Transport&,std::uint32_t,double,
 const MidiEvent*,std::uint32_t)noexcept;
std::string serializeReservoir(const State&);
bool deserializeReservoir(const std::string&,State&);
} // namespace downspout::mnemosyne
